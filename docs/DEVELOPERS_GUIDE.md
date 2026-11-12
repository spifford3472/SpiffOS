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

## Screen-position editor and visible cursor — 2026-10-29

The earlier append-only keyboard console remains as a regression fixture.
Normal keyboard input now uses screen-editor.c and named decoder events.
Arrow keys select a visible row and column. Each stored logical record retains
its bytes independently of those coordinates; moving away never submits it.

Overwrite replaces a byte at the selected offset; Insert shifts the suffix
right before storing the new byte. Space padding turns a position past text
into an explicit counted offset. Buffer capacity is checked before mutation.
Deleting before the cursor shifts the suffix left. Rows gained or lost by
these edits move following framebuffer pixels and logical records together.

Downward row copying walks bottom to top so each source row is read before
its destination can overwrite it. Upward copies walk top to bottom. Both
operate on visible pixels and honor reported pitch and untouched padding.

The cursor is an overlay that inverts pixels, then inverts them again before
drawing or moving content. Applying the operation twice restores the exact
prior bits, including logo artwork. Overwrite covers the full 16x16 cell;
Insert covers the bottom two pixel rows. Capacity rejection adds a distinct
top band/notch. A steady cursor needs no timer in the polling diagnostic.

The embedded 65-by-4096-byte text storage lives in static kernel BSS. It is too
large for Limine's entry stack. A line with a hidden prefix keeps all bytes
while visible fragments remain. Backspace at the top-left visible fragment
clamps the resulting cursor to the screen while preserving the complete line.

Run make test-keyboard for sanitizer checks and real QEMU injection, including
independent pixel expectations for both cursor shapes and push-down reflow.
See [screen-editor validation](operations/screen-editor-2026-10-29.md).

## Memory foundation: inventory before allocation — 2026-10-29

A physical frame is a 4096-byte unit of RAM. A virtual address is a process or
kernel address translated through page tables; it is not automatically a
physical address. M1 reads only Limine's metadata about physical intervals.
It never reads or writes the RAM described by those intervals.

An interval is half-open: base is included, base+length is excluded. Check that
addition before using the end. Only complete frames entirely within USABLE
regions qualify. Divide base by 4096 and add one frame if there is a remainder;
divide the exclusive end by 4096 to obtain the last eligible boundary. Working
in frame numbers avoids an overflowing address+4095 alignment expression.
Physical frame zero is deliberately excluded in this initial policy.

The boot adapter copies at most 512 metadata entries into static BSS and calls
the pure analyzer. All interval ends are checked before overlap comparisons.
An overlap involving usable memory is an error; two reserved entries may
legitimately overlap. Neither unknown types nor bootloader-reclaimable data are
made available. Limine's current stack, page tables and responses still matter.

Raw usable bytes, candidate full-frame bytes and excluded usable bytes obey:
usable = candidate + excluded; candidate = frame_count * 4096.
Reserved/MMIO holes are not counted as installed RAM. Counters are published
only after full validation; failure preserves the caller's previous summary.

The future memory manager will use one shared pool with OS, system-session,
workspace and process ownership. Ownership tags support accounting and cleanup;
hardware page permissions will enforce process isolation later. A protected
supervisor reserve, no swap and recoverable allocation errors remain required.
This inventory does not implement those later allocation/protection mechanisms.

Run make test-memory for sanitizer tests plus independently recomputed real
QEMU inventories at 128, 256 and 512MiB. This is a diagnostic test matrix, not a
minimum-RAM product specification. Existing editor frames stay unchanged.
See decisions/ADR-memory-foundation.md and operations/memory-inventory-2026-10-29.md.

## M2 frame ownership — 2026-10-29

An allocator ordinal is an index through complete usable frames, skipping holes.
It is not a physical address. The translator walks the borrowed immutable map
and converts the ordinal to a numeric physical address. Nothing dereferences
that number in M2. A uint16_t slot records FREE or an internal owner group;
allocation changes that slot and counters, and release reverses those changes.
The metadata table itself is kernel BSS, excluded from usable RAM by Limine.

Reserve ordinals form a separate pool. Ordinary exhaustion returns FRAME_OOM
without consuming reserve or changing the output. Only an explicit supervisor
reserve request uses reserve pages. Wrong-owner and double frees are rejected.
These tags provide bookkeeping, not process isolation or stale-handle safety.
The internal descriptor, immutable map, slots and nonaliasing output must stay
valid for the allocator lifetime; one CPU with interrupts disabled owns them.

Run make test-frame-allocator for sanitizer checks and make test-memory for
three real guest sizes, including all kernel/session/workspace ownership groups
and reserve/free restoration. See decisions/ADR-frame-allocator.md and
operations/frame-allocator-2026-10-29.md for diagnostic limits and evidence.

## Bounded heap and owner lifetimes — 2026-10-30
The frame allocator manages complete 4096-byte physical pages. M3 authorizes a
live exact-owner page through the loader's validated direct map. M4 divides a
caller-supplied accessible arena into smaller blocks rounded to 16 bytes. The
current boot test supplies one ordinary kernel-owned page, then returns it.

Each block has an external metadata record: offset, length, owner and previous/
next record indices. Metadata is outside payload bytes so an invalid pointer
cannot trick free into reading a fake header immediately before that pointer.
The active list covers the arena exactly. Every mutation first checks bounds,
reciprocal links, coverage and independently recomputed per-owner counters.
Allocation splits a free block when needed, zeroes the complete rounded payload
and only then publishes the pointer. Free merges adjacent free ranges, returning
retired records to the inactive pool. Payload is cleared on reuse, not on free.

Workspace cleanup accepts only workspace tokens 1–9. Session teardown has its
own fixed-owner call. Neither operation changes the kernel-owned backing page
or reserve pool; other groups retain both contents and accounting. Bad pointers,
wrong owners, exhaustion, metadata exhaustion and corrupt state return explicit
errors. A heap reported corrupt must be retired by its trusted caller.
Owner tags provide bookkeeping, not hardware isolation or stale-handle safety.

Run make test-kernel-heap for focused and deterministic model-based sanitizer
tests, or make test-memory for all memory suites and real guest/pixel checks
at 128/256/512 MiB. Persistent multi-page backing, scheduler synchronization,
real session/workspace services and ring-3 exposure are separate milestones.
See decisions/ADR-memory-services.md and the memory-services night record.

## Retained boot memory — M6
After reversible M1-M5 probes, boot acquires one ordinary 4096-byte frame through
M3. M6 initializes ten protected heap roots and keeps that frame allocated.
Physical ownership remains kernel; heap owner tags separate session/workspace
lifetimes inside the page. Ordinary clients cannot free a root. Retire clears
every root/descriptor pointer before failed startup returns physical backing.
The initial summary reports one retained frame; checks after console and actual
editor initialization prove the ten roots remain valid. This is identity and
lifetime state, not the full workspace scheduler or migrated screen storage.
Run make test-memory for M1-M6 sanitizer/three-size QEMU/pixel verification.

## Complete US keyboard path
A key needs three coordinated pieces: its scan-code mapping, editor acceptance
and a visible glyph. US punctuation now passes all three while alphabet input
retains uppercase behavior. Shift selects number-row/punctuation symbols.
NumLock starts on; its make latches until release so repeat cannot toggle it
repeatedly. Shift temporarily reverses keypad digit/navigation mode. Keypad
operators and Enter do not depend on NumLock. LED synchronization and additional
keypad navigation beyond arrows/Insert/Delete remain deferred.

Delete erases at the cursor without changing its counted offset; Backspace
erases before it and moves left. Both use the logical-line storage and the same
row-shrink/redraw path, preserving following records when wrapping changes.
At the end of a line Delete is harmless. Tests inject both extended Delete and
keypad Delete and compare complete framebuffer pixels for symbols/deletion.

## M9-2: the CPU exception table
An IDT entry is sixteen bytes in x86-64: the handler's 64-bit virtual address
is split into offset_low (16 bits), offset_mid (16 bits), and offset_high
(32 bits). The entry also contains the current ring-0 code-segment selector,
interrupt-stack-table index and gate flags. A packed ten-byte IDTR contains
the table's byte length minus one and its address. LIDT changes only the
boot CPU's exception lookup; it does not itself enable external IRQs.

A CPU fault can push an error code (for example #PF) or no error code
(for example #UD). The assembly entry normalizes both shapes to vector,error
and aligns the stack before calling the C fail-stop reporter. Because the
reporter never returns, there is no IRETQ path in this checkpoint. All
hardware IRQ entries remain not-present; STI must remain forbidden until
controller masking, acknowledgments and safe stacks have separate coverage.
See docs/decisions/ADR-m9-exception-idt.md.

## Long-mode TSS and emergency exception stacks (M9-3A)

An x86-64 Task State Segment (TSS) still matters in long mode: the processor reads its Interrupt Stack Table (IST) when an IDT gate specifies a nonzero IST index. The TSS must be described by a 16-byte available-system descriptor in the *active* GDT and loaded using LTR. The descriptor base is a kernel virtual address; the emergency stack pointer points just beyond its last byte because the CPU stack grows downward. Do not confuse dedicated static stack storage with a guard page; a guard page requires deliberate page-table mappings and negative tests.

## Legacy PIC port and mask basics (M9-3B)

The x86-compatible pair of 8259 interrupt controllers has master ports 0x20/0x21 and slave ports 0xA0/0xA1. Initialization control words (ICWs) assign new vector offsets; PIC data ports subsequently contain eight mask bits each, with **one meaning masked**. Master IRQ2 connects the slave controller. An end-of-interrupt (EOI) must be issued slave-first then master for legitimate slave IRQs; a phantom IRQ7 requires no EOI, while phantom slave IRQ15 requires only master EOI after checking the ISR. Hosted tests use injected IO callbacks instead of privileged IN/OUT instructions.

## Monotonic time without a wall clock (M9-3C)

A monotonic timer measures intervals, not calendar dates. The kernel timer begins unavailable even though its state is initialized, and becomes readable after the first validated interrupt increments its count. The counter saturates at the maximum 64-bit value rather than rolling backward. Millisecond conversion divides first to avoid overflow in `ticks * 1000`; caller-visible output remains untouched on invalid inputs. An IRQ-updated tick must be loaded atomically because normal code can be interrupted between instructions.

## APIC and ACPI discovery contract (M9-3D)
ACPI tables are firmware-owned byte sequences. Validate signature, table length, checksum and every variable-length record before reading fields. The MADT describes LAPIC, IOAPIC and source overrides, but table addresses are physical and must not be dereferenced until mapped and validated. LAPIC and IOAPIC registers are MMIO, not legacy PIC I/O ports; their cache attributes and interrupt routing must be initialized before enabling IF. An I/O APIC global system interrupt (GSI) can differ from an ISA IRQ number because of an override. See ADR-m9-3-apic-routing.md.

## Local APIC CPU preflight

The CPUID **leaf 1 EDX bit 9** reports the presence of local APIC hardware (not ECX). Register IA32_APIC_BASE MSR 0x1B contains enable (bit 11), x2APIC mode (bit 10), BSP flag (bit 8), and the physical APIC base address in bits 12–51. Confirm the base agrees with a validated ACPI MADT LAPIC address before considering mappings. This does not imply the page is mapped or safe for MMIO.

## Read-only page-table walking for APIC (M9-3D3)

A processor's CR3 points to a physical page-table root. A conservative x86-64 walker must resolve that root and every next-level page through a validated RAM mapping, reject missing/not-present or user-accessible entries, and handle 4 KiB, 2 MiB and 1 GiB leaves. A physical LAPIC address supplied by firmware is **not** a virtual pointer. Even when virtual translation matches physical MMIO, caching properties must be validated against PAT/MTRRs before any device-register read/write. An unavailable mapping proof is not the same as proof of an unmapped device.

## M9-3D4 mapping design: APIC MMIO versus ordinary RAM
LAPIC and IOAPIC firmware physical addresses are **device register windows**; they are not M2 ordinary RAM frames and must not be allocated or freed as RAM. Only newly allocated *page-table frames* belong to the M2 kernel owner. A mapping is created in a dedicated kernel-only virtual aperture, never by blindly adding the HHDM offset. A leaf PTE must be present, supervisor-only (U/S=0 throughout ancestor chain), writable, NX, and uncached. When PAT bit 7 for a 4-KiB leaf is zero, setting PCD and PWT selects IA32_PAT index 3. Read IA32_PAT MSR 0x277 to verify that index contains the UC type before enabling device access; blindly setting PCD/PWT does not itself establish a portable effective cache type. Any missing proof means no MMIO access or interrupts. An operation that starts linking page tables must maintain ownership and be able to reverse partial changes without freeing still-linked pages.

## Implemented MMIO aperture transaction (M9-3D4)
The kernel uses two private high-half virtual 4KiB slots at `0xffffc80000000000` and `0xffffc80000001000`, not the HHDM, for ACPI-provided LAPIC and IOAPIC physical pages. A pure policy function validates CPUID physical width, NX capability, enabled EFER.NXE and IA32_PAT index 3 UC, then builds non-user, RW, NX PTEs with PWT/PCD=1 and PAT=0. Existing nonzero slots are never overwritten. `x86_mmio_map_create` allocates and zeros new tables via the exact-owner M2/M3 kernel interface, builds leaf and intermediate entries while unreachable, and publishes through one root entry only after staging. `x86_mmio_map_destroy` unlinks first and invalidates both TLB entries before releasing frames; any release error poisons the transaction. A re-walk of the active CR3 verifies the two translations before reporting success. This is **only an MMIO address-translation service**: device register reads, controller programming and IRQ enabling require separate authorization and tests. The older HHDM mapping-unverified diagnostic remains independent.

## M9-3D5 APIC identification and indirect IOAPIC reads
The xAPIC local ID appears in LAPIC register offset 0x20, bits 31:24; cross-check CPUID leaf 1 EBX bits 31:24 and enabled MADT local APIC entries before trusting it. An I/O APIC does not provide a flat readable version register: software writes the requested index to IOREGSEL (MMIO offset 0), then reads IOWIN (MMIO offset 0x10). Selecting register 0 (IOAPICID) or 1 (IOAPICVER) is a hardware *index-latch write* but does not configure redirection or change interrupt enable state. IOAPICVER bits 23:16 report the largest redirection-entry index, so capacity is that value plus 1, not simply the field value. MMIO reads must use volatile registers through prevalidated virtual mappings with correct uncached effective type, not physical addresses or HHDM guesses. No IRR, EOI, LVT, redirection or STI changes in M9-3D5.
