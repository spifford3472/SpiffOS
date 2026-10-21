# USB Communications and Audio I/O — Hardware Requirements Draft

Status: **requirements proposal**, not a driver implementation or ratified API. 2026-10-20.

## Motivation
SpiffOS must support programs that interact with external hardware, including amateur-radio equipment and other serial/audio devices, without becoming an amateur-radio-specific operating system. Keep the public OS interfaces generic and reusable.

## USB communications
- Enumerate USB host controllers, hubs, devices and interfaces; expose stable, permission-checked device identities/capabilities.
- Prioritize USB serial CDC ACM and common USB-to-UART adapters. Vendor-specific adapters require explicit driver support.
- BASIC should support enumerate/open/configure/read/write/close; baud rate, parity, data bits, stop bits, timeouts, and line state where the device supports them.
- USB unplug/replug must not hang the BASIC process or the OS. Surface disconnection as an error/event, allow explicit reopen, and avoid silently binding to the wrong replacement device.
- Serial control lines (DTR/RTS) and PTT-related operations must be explicit and safe. Never key an attached transmitter as an incidental effect of enumeration, open, restart or recovery.
- Device permissions must respect process isolation and Supervisor recovery.

## Audio capture and playback
- Prioritize class-compliant USB audio devices and practical built-in audio hardware where supported by drivers.
- Full-duplex PCM capture/playback, mono/stereo, sample rate, sample format, input/output gain, device discovery and selection.
- Provide low-latency buffered streams with explicit buffer sizes, underrun/overrun reporting, timestamps where feasible, and device-loss recovery.
- Audio capture and output must coexist with the 32-voice-per-workspace virtual sound architecture without competing ownership of physical devices. The Audio Manager is the authority for routing/mixing; capture APIs must be designed consistently.
- The architecture must permit an application to capture a USB audio stream while using USB serial control simultaneously, without one blocking the other.
- Timing guarantees and actual supported sample rates are hardware/driver dependent; define acceptance tests rather than assuming universal performance.

## Proposed BASIC learning examples (syntax not finalized)
```basic
' Enumerate available serial devices
FOR EACH device IN USB.SERIAL.LIST()
    PRINT device.NAME
NEXT

' Open serial link with explicit settings
port = SERIAL.OPEN("DEVICE-ID", 38400, TIMEOUT=1000)
SERIAL.WRITE port, "STATUS"
reply = SERIAL.READ(port, 128)
SERIAL.CLOSE port
```

```basic
' Capture one buffer of PCM audio; exact format/API pending
mic = AUDIO.INPUT.OPEN("DEVICE-ID", RATE=48000, CHANNELS=1)
samples = AUDIO.INPUT.READ(mic, 1024)
AUDIO.INPUT.CLOSE mic
```

## Open design gates
1. USB host controller baseline for target laptop and QEMU/OVMF development.
2. Initial supported USB serial chipsets and USB audio-class versions.
3. Device ownership/permissions and safe PTT/CAT handling.
4. API grammar and buffer type; blocking vs event-driven I/O.
5. Audio routing and capture permissions across workspaces.
6. Loopback and hardware-in-the-loop tests for disconnects, timing, latency, underruns and long-duration stability.

No code or production radio-control authorization is implied by this document.
