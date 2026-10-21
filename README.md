<p align="center">
  <img src="assets/branding/spiffos-logo.svg" alt="SpiffOS — mountain, winding path and sunset; Explore, Create, Connect" width="760">
</p>

<h1 align="center">SpiffOS</h1>
<p align="center"><strong>The wonder of 8-bit computing. The possibilities of modern hardware.</strong></p>
<p align="center"><em>Explore. Create. Connect.</em></p>

---

## Remember when the computer belonged to you?

There was something magical about turning on an 8-bit computer.

No maze of windows. No accounts to configure. No waiting for a desktop full of services to settle down. Just a screen, a blinking cursor, and an invitation to make something.

You could type a few lines of BASIC and the machine would respond. You could draw on the screen, make a sound, move a sprite, save your work, and gradually learn how the whole system fit together. A computer wasn't only something you *used*. It was something you could **understand, explore, and program**.

That feeling is what SpiffOS sets out to bring back.

Not by pretending today's computers are still 8-bit machines. Not by emulating a Commodore 64. And not by putting a retro-looking shell on Linux.

**SpiffOS is an independent, native x86-64 operating system, designed around a modern BASIC language and the idea that the computer should invite you to create.**

We want the simplicity and immediacy of early personal computing—with the memory, graphics, sound, storage, networking, and processing power available today.

## Our idea

Imagine switching on a laptop and arriving directly at:

```text
SpiffOS BASIC
READY.
█
```

You can immediately write a program:

```basic
PRINT "HELLO, WORLD!"
```

Then go further: animate sprites, compose music, work with files, communicate over TCP/IP, read a USB device, or build an application that runs as its own protected process.

Beginners should be able to create something interesting in minutes. Experienced programmers should be able to explore documented virtual hardware, use `PEEK` and `POKE`, and compile BASIC into native applications.

The machine should be approachable without being artificially limited.

## What makes SpiffOS different?

| Classic spirit | Modern capability |
| --- | --- |
| Boot directly to a programming environment | Native x86-64 operating system; no Linux base or C64 emulation |
| BASIC as the first-class language | Structured, line-number-free BASIC with a built-in native compiler |
| Learn by experimenting with memory | Documented, protected virtual registers and byte-addressed `PEEK`/`POKE` |
| Easy sprites, tiles and sound | Modern full-color graphics, animation, collisions and music/effects |
| Save a program and turn the machine off | Crash-consistent SpiffFS design and recoverable A/B system updates |
| Understand what the computer is doing | Documented subsystem boundaries, process isolation and an accessible Programmer's Reference |
| Connect devices and other computers | USB, wired TCP/IP, serial communications and audio I/O as design priorities |

## Create anything

SpiffOS is designed to be a place for experimentation: games, educational programs, graphics, music, utilities, network services, instrument control, and projects we haven't imagined yet.

A special priority is **real-world hardware interfacing**. We want SpiffOS to be useful not only for software demonstrations but for applications that talk to devices on a desk, a workbench, or in the field.

That means our hardware roadmap explicitly includes:

- **USB communications:** host-controller support, enumeration, device discovery, USB serial adapters (including CDC ACM and common USB-to-UART families), dependable read/write APIs, and safe device disconnect/reconnect handling.
- **Audio input and output:** USB audio-class devices and practical sound-card capture/playback, with sample-rate and channel configuration, buffering, timing, and full-duplex operation.
- **Applications that combine both:** for example, a BASIC program can control an external device over USB serial while simultaneously capturing or playing audio.
- **Networking:** wired Ethernet and TCP/IP first; Wi-Fi later.

These are **requirements and development targets**, not claims that drivers or applications already exist. Hardware-specific support will be added behind stable interfaces and documented capability discovery. Low-latency audio and device timing need explicit tests, not just successful enumeration.

## Built to learn, built to last

SpiffOS aims to be small, understandable, and responsive. It uses a modular monolithic kernel, protected user processes, a workspace model, and a single authoritative service for each major subsystem.

Its design favors:

- Fast boot into BASIC rather than a conventional desktop startup.
- Safe recovery after sudden power loss, without pretending all storage hardware can guarantee durability.
- Nine workspaces, each with independent programs and virtual graphics/audio state.
- A per-workspace 32-voice virtual sound chip, mixed through one Audio Manager.
- Stable, documented APIs and virtual device addresses for programs that outlive any one laptop.
- Simple commands for beginners and precise lower-level interfaces for advanced developers.
- Documentation and examples that explain *why* something works, not merely which command to type.

## Project status

**Early architecture and specification stage — not yet a bootable, usable operating system.**

The repository currently records design principles, architectural decisions, a draft BASIC reference, and example-driven tutorials. Syntax shown in draft references is provisional until approved and implemented. No hardware driver, audio engine, filesystem, compiler, or BASIC command should be assumed functional merely because it is documented.

Development is planned around virtual machines first, then real x86-64 laptop hardware. Changes are reviewed through pull requests. Architecture decisions and implementation are deliberately kept separate.

## Read the design

- [System overview](docs/architecture/system-overview.md)
- [Design principles](docs/architecture/design-principles.md)
- [Accepted decisions](docs/decisions/accepted.md)
- [Open decisions](docs/decisions/open.md)
- [BASIC Programmer's Reference](docs/basic/programmers-reference.md)
- [Graphics and sound architecture](docs/architecture/ADR-009-virtual-graphics.md)
- [Virtual sound architecture](docs/architecture/ADR-010-virtual-audio.md)
- [Branding assets](assets/branding/spiffos-logo.svg)

## The spirit behind the project

The best part of those early computers wasn't their limitations. It was the sense that **anything was possible if you were curious enough to try**.

SpiffOS is an attempt to keep that feeling alive—without giving up what decades of computing have taught us.

**Simple enough to understand. Powerful enough to create.**

*SpiffOS is an independent project, inspired by the era of 8-bit personal computing. It is not affiliated with Commodore or its successors.*
