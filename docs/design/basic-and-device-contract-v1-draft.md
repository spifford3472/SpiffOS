# SpiffOS BASIC and Device Contract — v1 Design Gate

**Status:** DRAFT FOR REVIEW. This is a planning baseline, not an approved grammar, working runtime, or driver. Keep repository private and public Pages disabled until release approval.

## Why this gate precedes kernel implementation
The current BASIC reference is an outline and the USB/audio requirements cover only part of the hardware story. We must specify stable language semantics and generic OS device contracts before freezing kernel/user-space boundaries. A minimal QEMU boot proof of concept can proceed in parallel without locking unapproved APIs.

## Deliverables and definition of done
1. **Language grammar:** lexical rules, comments, identifiers, namespaces, case, literals, operators/precedence, statement separators, multiline blocks, functions/subs, imports, scope, error handling, execution lifecycle, interpreter/compiler parity and a normative grammar.
2. **Type semantics:** INT64/FLOAT64/BOOLEAN/BYTE/INT32/STRING/BYTES/TYPE, conversions, overflow, Unicode indexing, arrays (zero-based inclusive upper bounds; eight dimensions), REDIM/PRESERVE and memory ownership.
3. **Command registry:** every public BASIC command/function gets a stable name, category, status, grammar, parameters, return type, side effects, blocking/async rules, capability/permission, errors, one simple example, one edge case, and at least one conformance test. Explicitly distinguish supported, deferred and omitted C64 commands.
4. **Hardware abstraction:** discovery, device identity, driver binding, capabilities, access permissions, exclusive/shared ownership, removal/reconnection, async events, timeouts and failure behavior. No application should depend on a vendor-specific device path for normal operation.
5. **Drivers:** baseline virtual QEMU devices and physical laptop targets; USB host (xHCI prioritized), hubs, HID keyboard/mouse/gamepad, CDC ACM and USB-UART serial, USB Audio Class, and wired network. Record which require emulation or actual hardware to test.
6. **Conformance:** one independent test per command, sample programs, device mocks, QEMU boot tests, negative tests, disconnection tests, safety gates and interpreter/compiler result equivalence.
7. **Release gate:** boot into BASIC; edit/save/load/run programs; persistent storage; sample graphics/audio/gamepad; publish only after privacy, reproducibility and documentation review.

## BASIC command catalog — coverage checklist (not final signatures)

| Domain | Required candidate surface |
| --- | --- |
| Console/editor | PRINT, INPUT, CLS, LOCATE, COLOR, EDIT, LIST, NEW, RUN, STOP, CONTINUE, LOAD, SAVE, HELP |
| Core language | assignment, CONST, DIM, REDIM, IF/ELSEIF/ELSE, SELECT CASE, FOR/NEXT, WHILE/WEND, DO/LOOP, EXIT, SUB, FUNCTION, RETURN, TYPE, TRY/CATCH/FINALLY, IMPORT |
| Numeric | ABS, SGN, INT, FIX, ROUND, MIN, MAX, SQR, SIN, COS, TAN, ATN, LOG, EXP, RND, RANDOMIZE, conversion and formatting |
| Text/binary | LEN, LEFT$, RIGHT$, MID$, INSTR, TRIM$, UCASE$, LCASE$, CHR$, ASC, STR$, VAL, TEXT.ENCODE/DECODE, BYTES allocation and endian reads/writes |
| Memory | PEEK/POKE and 16/32/64-bit variants, virtual register constants, capability checks |
| Filesystem | FILE.OPEN/CLOSE/READ/WRITE/READLINE/WRITELINE/SEEK, FILE.EXISTS/RENAME/DELETE, DIR.LIST/CREATE/REMOVE, atomic SAVE |
| Graphics | SCREEN, PLOT, POINT, LINE, RECT, CIRCLE, PAINT, DRAW.TEXT, SPRITE.CREATE/MOVE/SHOW/HIDE/COLLISION, TILE.SET, FRAME.WAIT |
| Sound | SOUND.PLAY/STOP, MUSIC.PLAY/STOP, AUDIO.VOLUME/MUTE, AUDIO.INPUT/OUTPUT stream operations |
| Input | KEY, KEY.DOWN, MOUSE.POSITION/BUTTON, GAMEPAD.LIST/OPEN/STATE/ON EVENT, INPUT device discovery |
| USB/serial | DEVICE.LIST/INFO/ON EVENT, USB.INFO, SERIAL.LIST/OPEN/CONFIGURE/READ/WRITE/CLOSE |
| Network | NET.STATUS, DNS.RESOLVE, TCP.CONNECT/LISTEN/ACCEPT/SEND/RECEIVE/CLOSE, UDP.OPEN/SEND/RECEIVE/CLOSE |
| OS/task/events | TASK.START/STATUS/WAIT/STOP, MESSAGE.SEND/RECEIVE/REQUEST/REPLY/SUBSCRIBE/PUBLISH, WORKSPACE.SELECT/INFO, TIMER, SLEEP, DATE/TIME, ERROR.INFO |
| Build/debug | COMPILE, TRACE, BREAK, STEP, ASSERT |

Each name is a **candidate**, not an implementation promise. Resolve synonyms, overloads and compatibility before approval. An authoritative machine-readable registry should eventually generate the reference index and test inventory.

## Generic input and USB device model

### Driver layering
Hardware controller → bus enumeration → interface/class driver → OS Device Manager → capability-checked BASIC handle/event. A physical USB composite device may expose HID, audio and serial interfaces at once. Drivers own USB transfers; BASIC does not directly access controller registers.

### HID game controllers
- Support standard USB HID gamepads/joysticks first, independent of brand. Map physical buttons, hats, axes and triggers to a normalized capability descriptor.
- Use zero-based controller handles and standardized button/axis indices **only after explicit approval**; do not assume every controller has the same buttons or ranges.
- Provide both polling (`GAMEPAD.STATE`) and events (connect, disconnect, button, axis, hat); avoid duplicate independent event buses by routing through Message Manager.
- Normalize axes to a documented range and expose raw values optionally. Support dead zones, calibration, trigger semantics, multiple simultaneous controllers and button debouncing policy.
- Disconnects invalidate handles and deliver events; a reconnect is not silently treated as the same device.
- HID keyboards and mice must use the same underlying class framework but keep separate higher-level BASIC APIs.
- USB HID is not universal compatibility: proprietary controllers and Bluetooth require additional drivers and testing.

### External devices
- Enumerate by stable OS-generated identity, USB vendor/product/interface and capabilities. A descriptive label is not a guaranteed unique ID.
- Serial abstraction covers built-in UART, CDC ACM and USB-to-UART adapters; explicitly configurable baud, parity, stop bits, data bits, timeouts, flow control and control lines.
- Support generic USB HID devices via a carefully scoped, permissioned report API; vendor-specific protocols are device-specific application logic.
- Bulk/interrupt endpoints and raw USB transfers should be a separately permissioned advanced API, not the default beginner interface.
- No implicit DTR/RTS transitions or transmission keying on discovery, opening, restart or recovery; define hardware-specific safe behavior and test it.

### Audio
- Device Manager enumerates capture/playback endpoints; Audio Manager arbitrates routing/mixing.
- Virtual 32-voice music/SFX output and raw PCM capture/playback coexist without device ownership races.
- Full-duplex streams require documented format negotiation, buffering, timestamps when available, under/overrun reporting, permission and unplug behavior.
- Timing and latency requirements are measured on supported hardware, not assumed.

## Illustrative BASIC examples — not runnable yet

```basic
' Controller-driven sprite: proposed syntax only
pads = GAMEPAD.LIST()
IF LEN(pads) > 0 THEN
    pad = GAMEPAD.OPEN(pads(0).ID)
    DO
        state = GAMEPAD.STATE(pad)
        x = x + state.AXIS(0) * 4
        y = y + state.AXIS(1) * 4
        SPRITE.MOVE 0, x, y
        FRAME.WAIT
    LOOP UNTIL state.BUTTON(0)
    GAMEPAD.CLOSE pad
END IF
```

```basic
' USB serial: proposed syntax only; open must not assert PTT
ports = SERIAL.LIST()
FOR EACH p IN ports
    PRINT p.NAME, p.ID
NEXT
port = SERIAL.OPEN(ports(0).ID, 38400, TIMEOUT=1000)
SERIAL.WRITE port, "STATUS"
reply = SERIAL.READ(port, 128)
SERIAL.CLOSE port
```

```basic
' Sound-card input: proposed syntax only
devices = AUDIO.INPUT.LIST()
mic = AUDIO.INPUT.OPEN(devices(0).ID, RATE=48000, CHANNELS=1)
samples = AUDIO.INPUT.READ(mic, 1024)
AUDIO.INPUT.CLOSE mic
```

## Decisions requiring explicit review
- Final grammar and compatibility policy: GOTO/GOSUB/DATA/READ/RESTORE and implicit typing.
- Sprite IDs and controller IDs: 0-based vs 1-based; consistency with arrays.
- Standard gamepad axes/button naming and normalization; HID report API scope.
- Event callbacks vs queued messages, timeouts and interrupt-safe BASIC scheduling.
- Device permissions, USB raw-access policy and DTR/RTS default handling.
- Audio sample formats and shared/exclusive capture behavior.
- Which QEMU virtual devices are mandatory for the first bootable preview.
- Which commands must work for preview 0.1 vs be fully specified but deferred.

## Proposed implementation order
1. Ratify grammar, type semantics, command registry and device model.
2. Build QEMU/OVMF UEFI boot-to-BASIC skeleton and automated smoke test.
3. Implement minimal language subset and console/editor.
4. Implement storage and basic graphics/sound/input abstractions; test with virtual devices.
5. Implement HID gamepad, USB serial and USB audio with hardware-in-the-loop testing.
6. Add conformance tests and runnable example collection; review public-release readiness.

No public website activation, repository visibility change, hardware transmission or merge is authorized by this draft.
