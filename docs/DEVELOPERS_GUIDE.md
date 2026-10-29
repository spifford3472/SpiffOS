# SpiffOS Developer's Guide — C and Kernel Concepts

Status: educational foundation, not an implemented-kernel reference. Last updated: 2026-10-26.

## Who this is for
A programmer who understands algorithms, variables and functions, but may not have used C in decades. SpiffOS favors straightforward C and explains the machine-level decisions. Examples below are illustrative and **not** compiled SpiffOS code.

## From C source to a bootable kernel
C source (`.c`) is compiled to object files; headers (`.h`) declare shared interfaces. A linker places objects into a kernel image using an explicit memory layout. A **freestanding** kernel does not automatically have the host operating system's C library, heap, files or `printf`. The build must specify target ABI, startup code, linker script and supported runtime functions. Limine loads the kernel; SpiffOS must initialize its own memory, interrupts and user processes.

## Pointers and ownership
A pointer holds an address; `*pointer` accesses the pointed-to object, while `&value` obtains an address. An invalid pointer can corrupt memory in kernel mode, so every API must document who owns an allocation, how long it lives, and who frees it.

```c
/*
 * Purpose: Read a byte without modifying it.
 * byte_address: non-NULL pointer to a valid readable byte.
 * Returns: the byte stored at that address.
 * Ownership: does not take ownership or allocate memory.
 * Caller must ensure the address is mapped and accessible.
 */
unsigned char read_byte(const unsigned char *byte_address)
{
    return *byte_address; /* Dereference: read the byte at this address. */
}
```

A `const` pointer target cannot be modified through that pointer. `NULL` means no valid target. A pointer to memory that has been freed is **dangling** and must never be dereferenced.

## Structures, headers and visibility
A `struct` groups fields; `typedef` can give a type a convenient name. Headers expose contracts; `static` at file scope keeps implementation details private to that source file. Include guards or `#pragma once` prevent duplicate declarations. Prefer fixed-width integer types such as `uint64_t` for registers and page-table entries; use `size_t` for object sizes. Document units (bytes, pages, ticks) in names and comments.

## Bits and hardware registers
`&` tests bits, `|` sets bits, `~` inverts bits, and `<<` shifts. Use named masks, e.g. `PAGE_PRESENT`, rather than unexplained hexadecimal literals. Hardware registers may require `volatile` access, but **volatile is not a synchronization primitive**. CPU memory ordering, atomic operations, locks and interrupt masking are distinct topics and require careful review.

## Virtual memory and paging
Physical RAM is not the same as a process's virtual address space. Page tables map virtual pages to physical frames and carry access permissions. Ring 0 code must protect kernel mappings from ring 3 writes. A page fault may indicate missing mapping, invalid access or a protection violation. The allocator must preserve emergency resources for the kernel and supervisor even when user memory is exhausted. The exact SpiffOS paging layout and allocation algorithms remain open.

## Interrupts, concurrency and system calls
An interrupt can interrupt ordinary execution at an inconvenient point. Interrupt handlers must respect context restrictions: avoid sleeping, unbounded work and unsafe locks. User applications request privileged operations through a documented syscall interface rather than touching hardware or kernel pointers directly. A system-call boundary validates every user-provided pointer, length and permission. The actual syscall ABI is not yet decided.

## Assembly and x86-64
Minimal assembly may be necessary for entry, context switching, interrupt stubs and privileged instructions. Each assembly block must explain registers, stack alignment, calling convention, clobbers, privilege assumptions and why C cannot safely perform the operation.

## Driver interfaces
A modular monolithic kernel has drivers in kernel space behind small versioned interfaces. The interface should document initialization, shutdown, error handling, ownership, locking, device capability discovery and recovery. A device's PCI ID does not imply a driver already exists.

## Debugging and testing
Start in QEMU with serial logging and deterministic smoke tests; test fault paths and memory exhaustion deliberately. Build artifacts and logs must distinguish verified outcomes from expectations. Physical tests use a dedicated USB boot medium, not the Linux Mint installation disk. Never assume that power-loss resilience is solved until storage recovery has been designed and tested.

## How to read SpiffOS code
1. Read `AGENTS.md`, the accepted/open registers and the relevant ADR.
2. Read the file header and public header API.
3. Follow the function contract: input, ownership, error path and side effects.
4. Check which code executes in user space, kernel space or interrupt context.
5. Consult the build and test instructions before changing behavior.

As the kernel is implemented, replace illustrative examples with verified excerpts and add a walkthrough of each real subsystem.

## Verified first-boot implementation — 2026-10-28

The repository now has a freestanding Clang/LLD build and a QEMU UEFI serial
smoke test. Run `make test` from the repository root. The kernel prints
`SPIFFOS_BOOT_OK` and halts; it does not yet provide BASIC or user processes.
Read [the recovery and boot record](operations/first-boot-2026-10-28.md) for
prerequisites, protocol pins, source walkthrough, VS Code debugging and actual
validation. The educational examples above remain illustrative.

## Writing pixels — verified 2026-10-28

A framebuffer is mapped memory whose contents become the display. Limine gives
us the starting address, dimensions, bytes per row (pitch), and color-bit layout.
The kernel requests that description, then borrows it while Limine's mappings
remain valid. It does not allocate or free screen memory in this diagnostic.

`framebuffer.c` advances to a row using byte arithmetic:
`row_address = address + y * pitch`. It then treats that row as an array of
32-bit pixels: `row[x] = color`. One pixel occupies four bytes; pitch may include
extra bytes after the visible pixels. Using width instead of pitch to find the
next row would put writes in the wrong place when padding exists.

The volatile pixel pointer makes each assignment an actual store. Color masks
describe where red, green and blue bits belong in that stored word. The code
checks those masks, address alignment and arithmetic before drawing.

Run `make test-framebuffer` to check memory guards and QEMU's actual display,
or `make run` to see the pattern. See the
[framebuffer validation record](operations/framebuffer-2026-10-28.md).

### QEMU window prerequisite on Forge

Interactive `make run` needs QEMU's graphical backend. On Linux Mint/Ubuntu,
install `qemu-system-gui` in a local terminal with
`sudo apt install qemu-system-gui`. Run from a graphical desktop session.
The headless framebuffer test can pass even when this package is absent.
If no window appears, inspect `build/run-qemu.log`: QEMU may have started a
VNC server instead. The owner installed the package and confirmed the pattern.

## Turning font bits into letters — verified 2026-10-28

The boot label SPIFFOS uses seven fixed 8x8 bitmaps in framebuffer.c.
Each bitmap row occupies one byte. For example, 0x3c has binary value 00111100:
the middle four source pixels are white and the other four are black.

The renderer tests one bit with `bits & (0x80 >> column)`. Shifting the mask
moves from the leftmost bit toward the rightmost. A nonzero result paints white.
Dividing screen coordinates by four maps a 4x4 block back to one bitmap pixel.
The result is larger lettering drawn using the same pitch-aware framebuffer
stores as the color pattern, with no font library or graphics service.

This deliberately draws one fixed label. A full character set, arbitrary text,
cursor, newlines and scrolling are future steps. Run `make test-text` or
`make run`; see [the text validation record](operations/text-2026-10-28.md).

## A console is state plus pixel drawing — verified 2026-10-28

`struct text_console` records where the next character belongs: a row and a
column. The framebuffer origin is measured in pixels; cursor positions are
measured in character cells. One cell is 16x16 pixels at the current 2x scale.
A cell at (column,row) starts at `(left + column*16, top + row*16)`.

After successfully painting a character, the console advances its column.
The last column wraps to column zero on the next row. A newline also selects
column zero on the next row. Reaching `row == rows` means full; further output
fails without writing outside the panel. There is no scrolling or blinking
cursor yet. A string can print a prefix before reporting that the panel filled.

The console owns no allocated memory: the caller supplies state and keeps the
borrowed Limine framebuffer valid. Font lookup returns row bits; framebuffer
functions pack colors and touch screen memory. Unsupported characters display
'?'. This separation keeps cursor rules independent of the hardware layout.

Run `make test-console` for guarded memory and actual QEMU screen verification.
Close an old QEMU window before `make run` to load the new kernel. See the
[console record](operations/console-2026-10-28.md) for exact scope and results.

## Scrolling pixels and embedding the logo — verified 2026-10-28

A pending bottom row now makes space on the next byte instead of refusing it.
The framebuffer helper copies the console pixels up one cell row (16 pixels),
then clears the exposed bottom row. Copy top-to-bottom: the lower source pixels
must be read before a later destination write could overwrite them. Pitch and
viewport width keep the logo, neighboring pixels and padding untouched.

The power-up logo comes from the original assets/branding/spiffos-logo.svg.
Development tools render it once into checked RGB24 bytes. Ordinary builds
verify the source/bitmap hashes and embed those bytes as a constant array.
SpiffOS converts each RGB triple to the screen's reported color-bit layout.
It needs no SVG/image/font library at runtime and adds no boot delay.

The header contains the fixed original logo; the console below it prints enough
numbered lines to force scrolling. Run `make test-powerup`, or close an old QEMU
window and run `make run`. See [scroll/logo validation](operations/scroll-logo-2026-10-28.md)
for regeneration prerequisites, exact tests and limitations. This supersedes the
earlier console guide's full-panel-error policy; historical records are retained.

## Logo is initial content, not a fixed header — owner correction 2026-10-28

The whole screen now shares RGB(9,27,53) navy and belongs to the text workspace.
The boot-logo derivative uses this same flat backing; original SVG art remains
unchanged. No border, header or separate-color panel stays reserved.

`boot_workspace_reset()` clears the grid, draws the logo and starts the cursor
on row 13. All rows still belong to the viewport. Scrolling physically moves
those initial logo pixels out, and subsequent output can use their former
positions. Only an explicit workspace reset or boot draws the logo again.
At 1280x800 this provides 80 columns by 50 rows. A reset key/command awaits input.

Normal boot leaves the logo visible. Use `make test-workspace-scroll` for the
isolated stress variant; it does not replace the normal ISO. See
[workspace/logo validation](operations/workspace-logo-2026-10-28.md). This
supersedes the earlier guide's statement that the logo stays outside scrolling.

## Following a keyboard press through SpiffOS

The QEMU keyboard milestone is deliberately split into three small layers.
This is an interactive kernel diagnostic, not the BASIC interpreter.

1. The emulated keyboard sends a scan code for a physical key. The PS/2
   transport in `src/kernel/keyboard-ps2.c` first configures the controller
   explicitly. A keyboard set-2 stream is translated by the controller into
   set 1. Ports 0x64/0x60 carry status/data; they are not memory addresses.
2. For the A key, the decoder in `src/kernel/keyboard.c` receives make byte
   0x1e and returns uppercase ASCII 0x41. Release byte 0x9e returns no text.
   These two numbers belong to different systems: key position versus text.
   Prefixes/modifiers are remembered in caller-owned decoder state. Ctrl/Alt
   combinations and unsupported keys are ignored rather than accidentally typed.
3. `keyboard-console.c` checks the current line length and echoes A through
   `text_console_putc`. The font maps that character to bitmap rows;
   `framebuffer.c` expands each bitmap pixel and respects the display pitch.
   A logical column is measured in cells; framebuffer positions are pixels.
4. The same accepted character is logged as `KEY:41` on serial. This lets us
   distinguish transport/decoding failures from display failures.
5. Backspace moves one cell left, draws a blank cell, and restores that
   position. It cannot cross the current input line's start. Enter advances
   the console row and resets the editable length; it executes nothing.
6. At the bottom, the console's existing pending-scroll rule moves pixels
   upward before more output. The logo is just those initial pixels. No
   keyboard or scrolling code calls the logo-reset helper, so it stays gone.

No dynamic memory is needed. The line editor remembers a length, not a command
buffer yet. Each line is capped at 63 characters or columns-1, whichever is
smaller; excess printable keys are ignored until Backspace/Enter. Uppercase
A-Z, digits, space, period, Shift+semicolon (colon), Shift+slash (question mark),
Enter and Backspace work. Letters are uppercase with or without Shift.
This font/input subset precedes lowercase, full punctuation, line wrapping,
a visible cursor, command storage/execution and keyboard layout selection.

The main CPU repeatedly polls for bytes with interrupts disabled. The x86
PAUSE instruction is a polling hint, not a sleep or scheduler. This consumes
CPU and cannot support the planned multitasking design; interrupts/timers
and a final input service belong to subsequent milestones.

Run `make test-keyboard` for host sanitizer tests and actual QMP-injected keys.
Run `make test-workspace test-workspace-scroll` for earlier screen regressions.
Run `make run`, focus the QEMU window and type. Use Enter to create new rows.
There is no keyboard workspace-reset command yet.

## Safe interactive launching — 2026-10-29

Run `make run` from this checkout in a graphical desktop session. The runner
explicitly chooses GTK, starts windowed, scales the framebuffer with the window,
and keeps the host pointer visible. Normal keyboard focus accepts typing;
exclusive Grab Input is unnecessary. Do not enable View > Grab Input or hover
grabbing for this diagnostic. **Ctrl+Alt+G releases an accidental input grab**
(the observed recovery shortcut, also verified in QEMU's installed GTK source
and manual). Close the window to exit QEMU; this runner then removes its private
firmware variables and control socket. Closing the emulator loses RAM-only work;
this is host process shutdown, not a guest filesystem shutdown implementation.

The run command selects an emulated absolute pointer through local QMP before
execution and again after the boot marker. This avoids GTK's relative-pointer
click capture without a guest mouse driver. It changes no physical USB devices.
The fixed guest framebuffer scales; window resizing does not change guest modes.
`make test` and `make debug` retain headless behavior. Debug GDB remains local-only.
Run `python3 scripts/test-safe-launcher.py` after `make iso` to exercise real
headless guest boot, pointer selection, input and runner cleanup. Desktop focus,
resizing, capture release and window close remain manual acceptance checks.
See [launcher evidence](operations/safe-launcher-2026-10-29.md).

## Stored logical input and soft wrapping — verified 2026-10-29

Screen pixels show text but do not own command text. The logical-line module
stores a counted byte sequence in caller-provided bounded memory. The keyboard
console renders those bytes across every available column. Reaching the right
edge selects the next physical display row; it does not store Enter, call the
submission callback or divide the command.

The text console counts rows removed by scrolling. The input line remembers its
absolute starting row, so it can erase its final character across a wrap while
retaining a prefix that has already left the screen. Enter borrows the entire
counted sequence for one synchronous diagnostic callback. The current callback
writes SUBMIT: evidence to serial and executes no BASIC.

The 4096-byte buffer is static kernel diagnostic storage, keeping it off Limine's
entry stack. Filling it rejects the new character and preserves the complete
existing line; it never truncates or submits a prefix. Backspace can free space.
This capacity is not a BASIC language limit or final user interface contract.

Run `make test-keyboard` for sanitizer checks plus real QEMU key injection,
including a three-row logical line and exact serial submission. Read
[the validation record](operations/logical-lines-2026-10-29.md). Arrows, visible
cursor, Insert editing, multiple screen lines and owner-approved push-down reflow
remain the next checkpoint.
