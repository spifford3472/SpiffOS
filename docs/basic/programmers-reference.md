# SpiffOS BASIC Programmer's Reference — Outline v0.1
Status: OUTLINE ONLY; commands shown are candidate names, not approved syntax. Date: 2026-10-20.

## Reference template for every command
Name; category; status (ACCEPTED CONCEPT / PROPOSED SYNTAX / OPEN); purpose; syntax/grammar; parameters and types; return value; state and side effects; blocking and concurrency behavior; errors and recovery; permissions; immediate vs program use; version; executable examples; conformance tests.

## Part I — Language fundamentals
Interactive BASIC screen; source files and EDIT; identifiers and literals; types and coercion; expressions/operators; local/global scope; arrays and collections; SUB, FUNCTION, parameters and returns; IF/ELSE, SELECT, FOR, WHILE, DO; exceptions/errors; modules/libraries; comments; execution and debugging. Unnumbered structured BASIC is the accepted direction; detailed grammar is OPEN.

## Part II — Core BASIC command catalog (candidate families)
PRINT, INPUT; LET/assignment; IF, THEN, ELSE; FOR, NEXT; GOTO/GOSUB (legacy status OPEN); DIM; DATA, READ, RESTORE (legacy status OPEN); SUB, END SUB, FUNCTION, END FUNCTION, RETURN; LOAD, SAVE, RUN, STOP, LIST, NEW, EDIT, EXIT; string/numeric functions; time/date; error handling.

## Part III — System, tasks and workspaces
TASK START/STOP/STATUS (syntax OPEN); workspace select/query; task priority/state; safe memory introspection; supervisor-only commands distinguished from user calls.

## Part IV — Named messages and events
SUBSCRIBE channel; SEND channel, payload; RECEIVE channel, variable; ON MESSAGE channel CALL handler. All syntax illustrative only. Define subscription timing, queue overflow, event handler safety and error conditions.

## Part V — Graphics and text
Screen mode, color, text cursor, pixel, line, shape, sprite, animation and frame sync; logical coordinates; virtual graphics register PEEK/POKE contract (OPEN).

## Part VI — Sound and music
Music start/stop, notes, instruments, sound effects, channels/voices, playback position, mixing and mute rules; foreground workspace ownership.

## Part VII — Files and directories
Open/close/read/write/seek, list, create/remove directories, rename, atomic save, storage status and error reporting; filesystem syntax OPEN.

## Part VIII — TCP/IP networking
Network status/configuration; DNS resolve; TCP connect/listen/accept/send/receive/close; UDP send/receive; timeouts, nonblocking operations and message-driven notifications; illustrative HTTP client and server. Exact commands OPEN.

## Part IX — Devices and input
Keyboard, mouse, USB controller, timers, device capabilities, safe access and event channels.

## Part X — Learning and migration appendices
A. Original Commodore BASIC commands retained or modernized (to classify individually).
B. Original Commodore BASIC commands intentionally omitted (to classify individually; hardware-specific PEEK/POKE behavior cannot be copied directly).
C. New SpiffOS BASIC commands (to enumerate once syntax is approved).
D. Full runnable tutorials: hello world, editor, sprites, music plus effects, IPC, files, TCP client/server.
E. Error codes and reference index.

## Illustrative event example — NOT finalized syntax
```basic
SUBSCRIBE "SYS.KEYBOARD"
ON MESSAGE "SYS.KEYBOARD" CALL HandleKey
SUB HandleKey(key)
    PRINT key
END SUB
```

## Acceptance gate
Before implementation, approve grammar, command signatures, scope, errors, safe-point behavior and minimum conformance examples. Every final command requires a complete reference entry, not merely this outline.

## Approved BASIC direction — design supplement (2026-10-20)

This section records **accepted capabilities**, not a finalized grammar. All example command spellings remain **PROPOSED SYNTAX** until the command-by-command reference and tests are approved. See ADR-005 and ADR-006.

### Language and compiler
- Unnumbered structured BASIC; included interpreter and native x86-64 compiler with shared semantics and runtime APIs.
- Proposed: `COMPILE "HELLO.SPX"` compiles the current BASIC program into a source-independent native executable, preserving source; `RUN "HELLO.SPX"` launches it. Proposed editor shortcuts F5 interpreted run, F9 compile.
- Compilation errors should identify source line/column in the editor. Optimization is incremental; compute-heavy compiled code should be faster but no fixed speedup is promised.
- `TRY/CATCH` handles recoverable runtime errors. Example:
```basic
TRY
    data = FILE.READ("MISSING.TXT")
CATCH err
    PRINT err.MESSAGE
END TRY
```

### Types and examples
- Default `INT64` and `FLOAT64`, UTF-8 `STRING`, `BOOLEAN`, `BYTE`, `INT32`, structures, arrays, `BYTES`.
- Automatic variables and integer-to-float promotion; optional explicit declarations with strict lossy conversion rules; catchable overflow.
```basic
score = 100
score = score + 0.5
DIM exact AS INT64
TYPE Player
    name AS STRING
    score AS INT64
END TYPE
```
- Arrays: zero-based, inclusive upper bounds, maximum eight dimensions, row-major, initialized values, bounds checking, `REDIM`/`REDIM PRESERVE`. E.g. `DIM pixels(319,199) AS BYTE` has 320 × 200 elements.
- `BYTES` is a dynamic binary buffer; text encoding/decoding is explicit; big/little-endian access required. Proposed:
```basic
packet = BYTES(1024)
text$ = TEXT.DECODE(packet, "UTF-8")
```
- Unicode character-counting text length distinct from UTF-8 byte length; precise Unicode definition open.

### Files and native programs
- Simple automatic open/close file functions **and** advanced file handles:
```basic
FILE.WRITE "HELLO.TXT", "Hello"
text$ = FILE.READ("HELLO.TXT")
file = FILE.OPEN("DATA.TXT", "WRITE")
FILE.WRITELINE file, "record"
FILE.CLOSE file
```
- Binary file I/O uses `BYTES`; errors are catchable; SpiffFS atomic save/recovery and workspace isolation apply.
- Distinguish SPX launch from raw `BLOAD` into protected logical memory and controlled Ring 3 `SYS`. Proposed examples:
```basic
RUN "GAME.SPX"
BLOAD "ROUTINE.BIN", &H20000
```
- Raw `BLOAD` does not authorize execution. Native executable ABI and safe `SYS` entry remain open.

### Networking
- Binary-first TCP/UDP operations with explicit text conversion, blocking timeouts and nonblocking readiness; blocking only suspends the caller. TCP reads may be partial.
```basic
socket = TCP.CONNECT("example.com", 80, TIMEOUT=5000)
data = TCP.RECEIVE(socket, 4096)
TCP.CLOSE socket
```

### Processes and messaging
- Independent Ring 3 BASIC processes in v1; shared-memory threads deferred. Process lifecycle includes start, status, wait, graceful stop, exit status and protected Supervisor-only forced termination.
- IPC messages support numeric, Boolean, string and `BYTES` data; immutable delivery, per-sender FIFO, atomic queueing, single consumption, bounded queues, timeouts, request/reply IDs, opaque handles and process-exit errors.
```basic
worker = TASK.START("WORKER.SPX")
result = TASK.REQUEST(worker, "CALCULATE", 100, TIMEOUT=5000)
TASK.STOP worker
```
- Kernel-enforced workspace permissions; exclusive file locks and automatic release on process termination. Deadlock detection for straightforward synchronous waits where practical.

### Remaining reference work
Before implementing these APIs, document each command's complete syntax, types, return values, blocking behavior, permissions, errors, and conformance tests. Resolve compiler ABI, `REDIM PRESERVE` multidimensional semantics, Unicode indexing, TCP partial sends, IPC queue-full policy, file-lock semantics and native code execution safety.
