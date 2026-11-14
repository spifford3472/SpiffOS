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

## Implemented M9-3D5 alias/MTRR gate
The same page table can have a present mapping below an ancestor whose U/S bit permits user access; a *mapping-presence* audit must therefore not reject that ancestor as UNKNOWN before inspecting whether the device physical address is actually mapped. The separate MMIO authorization walker still requires supervisor-only paths. `x86_page_probe_read_only` distinguishes a demonstrably absent PTE from uninspectable page-table frames; an unknown result blocks all APIC device-register reads. The kernel reads CPUID MTRR support, `IA32_MTRRCAP` and `IA32_MTRR_DEF_TYPE` without mutation, verifies PAT index3 UC and scans the two known higher-half direct-map aliases. It then uses only M9-3D4's validated kernel-private UC MMIO windows. LAPIC ID is read from offset 0x20; I/O APIC ID/version from IOREGSEL/IOWIN, with the index selector restored after each read. Invalid topology, a mismatched ID, a bad version or an out-of-range IRQ0 GSI causes unavailability, not STI or controller programming. Supported evidence is QEMU single boot CPU; these controls are not a general laptop driver ABI.

## First APIC IRQ0 (M9-3D6) planning contract
QEMU q35 PIT channel0 IRQ0 may be delivered through IOAPIC GSI2 rather than legacy PIC IRQ0: resolve actual GSI via MADT source override and validate it against the hardware IOAPIC version's redirection entry range. Unlike IRQ injection through the masked 8259, route a fixed-delivery vector 0x30 to the enabled boot local APIC ID. Hardware external interrupt delivery via an x86-64 *interrupt gate* clears IF; an assembly ISR must save interrupted general-purpose registers, align RSP to the C calling convention, increment the monotonic tick counter and write the LAPIC EOI register, restore registers and use IRETQ. The spurious LAPIC vector 0xFF also needs a present harmless gate before software enabling. PIT0 mode2 reload 11932 yields ~100Hz; do not touch PIT2 used by the one-second F12 overlay. No serial I/O, allocator or scheduler in an ISR.

## Implemented boot-CPU IRQ0 (M9-3D6)
An x86 hardware interrupt gate automatically saves RIP/CS/RFLAGS and clears IF, but it does *not* preserve general-purpose registers or guarantee a 16-byte aligned interrupted stack. The M9-3D6 assembly saves RAX/RBX/RCX/RDX/RSI/RDI/RBP/R8–R15, saves RSP in callee-preserved R12 **after** saving the original R12, aligns down to 16 for the C handler, and restores exactly before IRETQ. The handler increments `kernel_timer`'s 64-bit atomic tick and sends local APIC EOI at offset 0xb0; it never talks to the masked legacy PIC. An IOAPIC indirect entry occupies two 32-bit registers: low contains vector, trigger/polarity and mask, high contains destination APIC ID in bits31:24. Setup writes the masked entry, validates readback, then unmasks only the actual IRQ0 GSI after PIT channel0 reload=11932. A disabled-route QEMU test must continue showing an unavailable clock. Spurious vector 0xff has its own IRETQ-only gate and no EOI. These implementation details are internal to the QEMU-first boot CPU and are not a published driver ABI.


## M10-A kernel task policy — initial hosted implementation, 2026-11-12

The kernel's **task policy** is a pure C state machine in
`src/kernel/kernel-task.h` and `src/kernel/kernel-task.c`. No instruction
pointer or stack is switched in M10-A. A task descriptor's `state` controls
whether the scheduler can select it. The FIFO queue stores numeric indices
into a fixed 16-entry table, not raw pointers into temporary memory.
Exactly one slot (15) is reserved for a future system supervisor.

A task handle contains both its slot and a generation. When a terminated
descriptor is reused, the generation advances; an old handle must fail
validation rather than control a different task. If generation reaches
`UINT32_MAX`, that slot cannot be reused. Because the task table is bounded,
simple shifting of the small FIFO on removal is deliberate: it is easier
to reason about than lock-free queues or pointer-linked structures.

The scheduler owns **metadata only**. The caller must ensure no function is
called concurrently or from IRQ context. M10-B must separately own stacks,
saved registers, bootstrap/idle execution and deferred stack reclamation.
M10-C may add tick accounting but MUST NOT move execution from within IRQ0.
See [the governing ADR](decisions/ADR-m10-task-scheduler-foundation.md).

Run `make HOST_CC=gcc test-kernel-task` for the ASan/UBSan hosted tests.
The normal `make test-memory` now includes them. On 2026-11-12 an isolated
Forge checkout successfully compiled with freestanding Clang, ran sanitized
GCC tests, and passed QEMU 128/256/512MiB exact-memory/framebuffer tests.
The original working owner's checkout and ISO were not modified.


## M10-B cooperative switching — isolated guest proof, 2026-11-12

The initial M10-B private assembly switch (`x86-task-switch.S`) saves RBX,
RBP and R12–R15 and stores the current RSP in the caller-owned task context.
The saved return address already present on each task's stack supplies RIP
when that task resumes. A never-before-run task gets an explicitly prepared
stack frame with those six saved-register slots and the trampoline return
address. The trampoline begins with RSP modulo 16 equal to 8, as a normal
SysV x86-64 C function does after a call.

This is **cooperative**, not preemptive: an explicit call transfers control
only when task code yields. IRQ0's existing complete register trap frame and
IRETQ implementation are unchanged. IF must be enabled on entry to task
switching; scheduler metadata transitions use short saved-IF critical
sections, never scheduling directly from the IRQ handler.

The first diagnostic kernel test requests four contiguous, independently
owned M2/M3 ordinary KERNEL pages for each of two stacks. A checked direct
map supplies a continuous 16 KiB stack window. Failed/fragmented acquisition
rolls back all frames; the supervisor pool remains reserved. The root
dispatcher uses M10-A's FIFO selection for 512 yields per task, checks task
termination and bottom-of-stack canaries, and releases the pages from its
own surviving boot stack. The retained-memory service and polled keyboard
are revalidated after release. The normal kernel does not link these
diagnostic switch objects; the guest proof is compiled only with
`SPIFFOS_M10_B_SWITCH_TEST`.

Run `make HOST_CC=gcc test-m10-b-host` for the native stack-switch smoke
test and `make test-m10-b-guest` for isolated 128/256/512 MiB QEMU runs.
Both passed on 2026-11-12. The guest proof does **not** claim a general
task-stack manager, hardware guard pages, ring-3 processes, asynchronous
IRQ-frame switching, or fault-injected stack-allocation failure coverage.
The six callee-saved registers RBX/RBP/R12–R15 are now independently tested
with six unique assembly sentinels in `tests/x86-task-register-check.S`.
The hosted probe passed on 2026-11-12. Guard pages and production task
stack management remain separate hardening gates.


## M10-C monotonic scheduler accounting and safe-point request — 2026-11-12

M10-C adds `kernel-scheduler-clock.c`, a small independent IRQ0
accounting service. The existing M9 clock still owns elapsed time, PIT0
routing, IRQ vector 0x30 and LAPIC EOI. On each existing hardware tick,
the new service increments a saturating 64-bit count and, every ten
ticks, sets an atomic **coalescing Boolean** request. It does not select
a task, alter any task stack, or execute an IRQ-driven context switch.
A future cooperative dispatcher can atomically consume the request
only at a separately proven safe point.

A test-only branch in the existing polled keyboard loop consumes the
pending bit and prints `SPIFFOS_M10_C_REQUEST_OK` after at least ten
scheduler ticks. The default build does **not** consume or act on this
flag; a pending request does not imply a CPU time-slice or privilege.
No priority or real-time latency guarantee is established by M10-C.

Run `make HOST_CC=gcc test-scheduler-clock` for ASan/UBSan hosted tests
of interval boundaries, coalescing, invalid operations and saturation.
Run `make test-m10-c-guest` for disposable QEMU guests at 128, 256 and
512 MiB. Both passed on 2026-11-12. In the same isolated source checkout,
M9 memory, keyboard, #UD, #DF and hardware IRQ0 regressions passed; IRQ0
delivered 100 real ticks in 0.994, 0.994 and 0.995 seconds, respectively.
The disabled-route negative test continued to leave IF=0 and the clock
unavailable. The normal kernel ISO built without any M10-B diagnostic
objects. No owner's installed ISO was changed.

M10-D remains separately subject to architecture review and owner
acceptance. Never interpret a pending request as permission to perform
an asynchronous context switch.


## M10-D1/D2: proof before timer-driven task switching — 2026-11-13

The owner approved staged M10-D implementation after the M10-A/B/C
acceptance; merge and deployment remain separate gates. M10-D1 is
only a **pure host-tested priority selector**: four bands, accumulated
wait-age preference, deterministic tie-breaks, and a reserved
supervisor dispatch opportunity every fourth selection. It does not
manage the task queues or guarantee a wall-clock CPU budget.

M10-D2 is a **test-only real-interrupt frame check**. To interrupt
arbitrary C code safely we must preserve more than a normal function
call: all 15 general-purpose CPU registers, the saved RIP, code
segment CS, flags (especially IF), the original stack pointer RSP,
and stack segment SS. The existing M9 assembly already pushes all
15 registers and returns with `IRETQ`. D2 adds a gated checker that
reads that saved stack without changing it. It is called only in a
special disposable QEMU build after the normal tick handler/EOI.

In 64-bit mode, a subtle processor rule matters: **SS:RSP is pushed
on every interrupt, even without a privilege change**. This differs
from legacy x86. The handler's aligned RSP is NOT the interrupted
function's original RSP. Use the hardware-saved RSP at offset 18
(8-byte slots after the 15 saved GPRs), followed by SS at offset 19.
See Intel SDM Volume 3A section 7.14.2 and
`docs/operations/m10-d2-irq-frame-20261113.md` for the full layout.

The probe's assembly routine fills all 15 GPRs with different
64-bit sentinels and spins without yielding while three real PIT0
interrupts arrive. The read-only IRQ checker verifies every register,
saved original stack, RIP within the spin loop, CS, SS and the exact full RFLAGS word. The assembly establishes a known comparison-derived flags value and snapshots it with PUSHFQ before entering the interruptible spin. The
assembly routine checks callee-saved registers again after `IRETQ`.
An intentionally wrong R15 in a separate test ISO must produce
`SPIFFOS_M10_D2_FRAME_ERROR`; otherwise the test is invalid.
No task handoff or preemption is implemented by D2.

Run `make HOST_CC=gcc test-m10-d2-guest` for D2 positive/negative
QEMU tests at 128, 256 and 512 MiB. For the broad evening suite,
run `make HOST_CC=gcc test-m10-d2-acceptance` in the disposable
source checkout. The latter runs D1/D2, memory/keyboard/IRQ0,
#UD/#DF, M10-B/C and APIC/MMIO regression gates. Do not run the
test with the installed Forge owner ISO path; all test boot media
are disposable and use isolated QEMU firmware variables.


## M10-D3.1: prepared workspaces and read-only F10 monitor

The nine diagnostic workspace identities are prepared at boot; only
workspace 1 is painted and its editor is initially active. The eight
inactive `consoles[]` structures borrow the framebuffer descriptor but
**do not draw** until the first workspace selection. The separate
`prepared[]` and `ready[]` flags prevent a prepared identity from being
mistaken for an initialized, input-capable editor. No offscreen
framebuffer snapshot is allocated at boot; a real F2–F9 switch still
allocates bounded pages through the existing retained memory service.

F10 samples the M10-C monotonic tick count outside IRQ context and
shows actual prepared/editor/snapshot counts. Before the first real
tick the source may be unavailable, which is displayed explicitly
instead of inventing an uptime of zero. Task CPU percentage is also
UNAVAILABLE until D4 provides verified scheduler accounting. This
monitor is read-only and restores the previous workspace screen on
F1–F9. See `docs/decisions/ADR-m10-d31-workspaces-debug-hub.md`.


## M10-D4 experimental priority handoff and supervisor reservation

D4 tests exercise the exact M10-D3 real IRQ0 handoff with the **pure
D1 priority selector** linked only under the special
`SPIFFOS_M10_D4_PRIORITY_TEST` build flag. The 100 Hz IRQ path still
accounts ticks and emits exactly one LAPIC EOI before the selector runs.
A bounded ready snapshot maps the permanent boot-stack supervisor to
reserved slot 15, CPU-hog worker A to Time-sensitive slot 0, and
CPU-hog worker B to Background slot 1. The pure selector uses
`waiting_dispatches + priority*8` with saturation and a supervisor
reservation every fourth dispatch when runnable.

The hosted `make HOST_CC=gcc test-m10-d4-host` runs all four
priority classes, readiness churn, no-starvation checks and bad
snapshot rejection. The live `make test-m10-d4-guest` uses QEMU
128/256/512MiB to check both workers, supervisor, 16KiB independently
owned stacks, clean boot-stack cleanup and fails closed when IRQ0
is absent or a worker stack guard is corrupt. The test scheduler
also temporarily marks Background blocked, then wakes it: its
*ready-time* waiting credit is retained across blocks but does not
increase while blocked. Resetting this credit at every wake caused
a real QEMU starvation regression under repeated short runnable
windows; retaining it fixes the test without changing the normal
runtime path.

This is a **diagnostic-only stepping stone** to a real task subsystem:
we still need to integrate actual task runnable/blocked state,
Interactive-class live dispatch, priority inversion constraints,
supervisor error recovery and verified CPU budgets. The ordinary
SpiffOS editor and F10 are never preempted by D4 diagnostic flags.
See `docs/decisions/ADR-m10-d4-priority-supervisor-proof.md`.


## M10-D4 completion: actual task blocking and bounded priority inheritance

The earlier D4 checkpoint could choose between two CPU-bound workers,
but its blocked/woken pattern was just a synthetic ready-list filter.
The **separate D4 completion test** replaces that model with a real,
bounded state owner in `m10-d4-task-control.[ch]`. Four worker states
can be READY, BLOCKED or QUARANTINED, and an always-reserved supervisor
on slot 15 is not allowed to block. Each worker is assigned a distinct
priority class: Background, Normal, Interactive, Time-sensitive.
Blocked tasks retain previously earned *runnable-time* wait credit
but do not accrue new credit while blocked.

The live guest in `m10-d4-complete-guest.c` has four independently
owned 16KiB frame-backed stacks, real 100Hz PIT0 IRQ saves and 20-word
IRETQ returns. Background worker blocks itself with interrupts masked,
and the original boot-stack supervisor wakes it; worker3 (Time-sensitive)
also blocks on a real one-waiter lock owned by Background. On contention,
Background temporarily inherits Time-sensitive priority. Releasing the
lock restores its base priority and transfers ownership to exactly the
blocked waiter. Only one owner and one waiter are supported; arbitrary
mutex graphs and ring3 lock authorization are future features.

The supervisor can quarantine an **inactive** worker, but cannot wake
a quarantined task without independently checking the saved frame's
ownership, bounds, CS, SS and flags. To prove this, the guest deliberately
corrupts a saved frame, refuses unsafe recovery, then restores and
validates the existing owned frame before allowing that task to resume.
Fatal hardware CPU exceptions still halt; this is NOT a general #PF
recovery system.

For regression testing, run
`make HOST_CC=gcc test-m10-d4-complete-acceptance` on an isolated
D4 completion checkout. This includes GCC ASan/UBSan state and
inheritance tests, 12 four-worker QEMU cases (three positive plus
three each IRQ-disabled, corrupt-frame and corrupt-canary negatives),
the previous D4 test fixture, D3/D2/D1 and M9/M10 functional regression
suite. The diagnostic serial output includes genuine worker slice
counts, supervisor dispatch count and maximum gap in **PIT ticks**
(approximately 10ms each in QEMU), total elapsed timer time and
block/wakeup/validated-recovery counts. These are experimentally
observed scheduling opportunities, not hard realtime CPU quotas.

Only the `SPIFFOS_M10_D4_COMPLETE_TEST` image includes these objects.
No default SpiffOS image preempts editor tasks. See
`docs/operations/m10-d4-completion-20261113.md` and the D4 ADR.


## M10-D5.1 — Why a terminated task descriptor is not free

The M10-A task table identifies tasks by a slot and a generation. A task can
be marked TERMINATED while its former stack, saved interrupt frame, owned
synchronization object, or wait registration is still retained. The table's
`reclaimed` flag is therefore not an execution-context check: it is an
**explicit acknowledgement from a separate verified reaper** that reclamation
has already completed on a surviving stack.

`kernel_task_terminate()` marks logical termination and leaves the slot
reserved. `kernel_task_reap()` validates the handle and state and marks
the terminated descriptor reusable, but does **not** free memory or verify
stack detachment. The production dispatcher/reaper must independently prove
those properties before calling it. `kernel_task_create()` skips terminated
slots without that acknowledgement, then increments the generation on reuse.
A stale handle is rejected. If the 32-bit generation reaches its maximum,
the slot is permanently exhausted rather than wrapping.

This initial D5.1 code is hosted-tested metadata only, not yet a production
interrupt-safe scheduler or a stack-lifecycle implementation. The ordinary
SpiffOS ISO remains without general task preemption. Run
`make HOST_CC=gcc test-m10-d5-reap` for the 20,000-cycle sanitizer test
and `make HOST_CC=gcc test-m10-d5-acceptance` for inherited D4 regressions.
See [D5.1 task-model ADR](decisions/ADR-m10-d5-production-task-model.md).


## M10-D5.2a — Releasing a task's stack from a surviving stack

Marking a task TERMINATED removes it from scheduling, but the CPU may still be executing instructions on that task's 16 KiB stack. Calling a memory-owner release there would let the CPU overwrite already-freed backing. The M10-B diagnostic now deliberately attempts a self-reap and requires that it **fail without freeing anything**. It then returns via the existing cooperative switch to the permanent boot stack; the dispatcher reads its actual RSP, validates the retired task's saved RSP and stack bounds, and calls `kernel_task_reap_owned` under interrupt exclusion. Only a successful exact-owner backing release permits `kernel_task_reap` to make the descriptor reusable.

The range checker guards integer overflow before adding stack length and rejects malformed or overlapping ranges. The release callback belongs to trusted kernel memory code, not a user task. Tests cover 20,000 retirements, release refusal, stale generations, rejected self-free, and ordinary-task privilege confusion. Run `make HOST_CC=gcc test-m10-d5-execution-acceptance` in an isolated checkout. This still does not prove general production lock/wait resource cleanup or enable ordinary editor preemption. See [execution-reaper ADR](decisions/ADR-m10-d5-execution-aware-reaper.md).


## M10-D5.2b — The task resource ledger and quarantine state

The M10-A task handle is `(slot,generation)`. A slot can be reused, but
its generation must advance. A stack-owner record must therefore be
registered against the **whole handle**, not just its numeric slot.
`kernel-task-resources.[ch]` implements exactly one bounded (16-element)
ledger indexed by the existing task table; it does not schedule or switch
tasks. Before first dispatch the trusted memory owner records the exact
16 KiB stack base, range, saved stack pointer and memory-owner token.
Attempting to use the same backing or owner token for another live
generation fails. A later dispatcher-side saved-RSP refresh may update
the suspended context value, but cannot redirect the stack's backing or
owner index.

The resource ledger counts lock ownership and waiter registrations only
as **interlocks**. These are not yet wired to a production mutex manager.
A task with nonzero counts can be logically TERMINATED, but its stack
remains reserved and it cannot be reaped; the supervisor must resolve
the outstanding references through a separate, future cleanup protocol.
A terminated task may not decrement its own counts afterward, because
that would invent unverified cleanup.

The trusted surviving dispatcher reads its actual RSP with assembly,
then calls `kernel_task_resources_retire`. It checks the complete handle,
registered backing, saved RSP, outstanding references and supervisor or
external-dispatcher authority before invoking the D5.2a off-stack
reaper. Failed physical release **quarantines** the binding and blocks
retries; the existing test allocator can poison itself after partial
release and does not guarantee atomic rollback. Quarantined tasks are
unavailable for descriptor reuse. Only a future explicit supervisor
recovery design may resolve them.

The M10-B QEMU diagnostic exercises the registered-owner path at
128/256/512 MiB. Hosted GCC ASan/UBSan tests include 20,000 recycling
cycles, collision/stale handle checks, overflow/underflow, blocked
termination, self-release denial, ordinary-task denial and quarantine
without retry. Run `make HOST_CC=gcc test-m10-d5-resource-ownership`
for the fast gate and `make HOST_CC=gcc test-m10-d5-resource-acceptance`
for the inherited D5.2a/D4/M9 suite.

This is a **development-only execution boundary**, not a production
reaper queue, resource graph or general editor preemption. Existing
mutexes, wait queues and drivers do not yet register references in the
ledger; general task termination must remain disabled until they do.
See [D5.2b ownership ADR](decisions/ADR-m10-d5-resource-ownership.md).


## M10-D5.2c — A bounded supervisor cleanup FIFO

A terminated kernel task may still own other kernel resources. Calling `kernel_task_reap` directly would permit reusing its task slot before its stack, locks and waiter registrations are verified. The D5.2c `kernel-task-cleanup.[ch]` module is a small static FIFO holding complete `(slot,generation)` task identities, not pointers into freed memory. At most the 15 ordinary task slots can be queued; slot 15 is reserved permanently for the supervisor. Queue validation rejects duplicate slots and invalid generations and checks the **entire queue** before calling a memory-owner release function.

On the surviving boot stack, a service visit examines at most the count recorded at entry. If a task still has lock or waiter references, its entry rotates to the tail. An eligible task's registered backing is retired through the D5.2b resource ledger. A refused release quarantines the ledger entry and removes only that request from the automatic queue: backing is never blindly retried or marked reusable. The queue itself never frees or fabricates outstanding synchronization ownership. All calls require IF=0; none run in timer IRQ context.

`kernel-task-sync-proof.[ch]` separately models one registered lock owner and one registered waiter using exact generation-tagged handles. It calls the D5.2b reference API for each acquisition, wait registration, release and ownership transfer, with checks that prohibit stale or terminated owners. This **does not yet** block or wake real CPU tasks, implement priority donation, or connect the distinct D4 diagnostic lock controller to production M10-A state. The point is to prove reference transfer and fail-closed termination before designing the actual generalized mutex implementation.

Fast sanitizer gates: `make HOST_CC=gcc test-m10-d5-supervisor-queue` and `make HOST_CC=gcc test-m10-d5-sync-ledger`. Complete inherited QEMU/host gate: `make HOST_CC=gcc test-m10-d5-supervisor-acceptance`. The M10-B QEMU fixture uses the queue only after returning from the outgoing worker stack to the permanent boot dispatcher. No ordinary editor/preemption ISO change is authorized. See [ADR-m10-d5-supervisor-cleanup](decisions/ADR-m10-d5-supervisor-cleanup.md).


## M10-D5.2d — Why blocking a mutex must change task readiness

An outstanding waiter reference alone is not enough to suspend execution. A task that stays RUNNABLE could be dispatched even while its lock is held. The internal `kernel-task-mutex.[ch]` adapter connects the D5.2b registered reference ledger with M10-A's actual `kernel_task_block()` and `kernel_task_wake()` transitions. Only the current RUNNING task may acquire, and only the exact RUNNING owner may release. If lock ownership is contested by a second RUNNING task, one waiter reference is registered and that task becomes BLOCKED; it is no longer in the FIFO. Unlock transfers the registered owner count, removes the waiter reference and wakes the exact generation-tagged task once into the FIFO. Repeated/forged operations, dying owners, overflow and invalid waiters are rejected.

**Important execution boundary:** M10-A changes scheduling policy metadata but cannot save the CPU registers of a blocked thread. The calling worker must actually switch to a surviving dispatcher stack before any new task executes. The hosted GCC ASan/UBSan checkpoint validates reference and metadata invariants. The extended M10-B test-only real-QEMU fixture additionally verifies the execution handoff: a blocked worker switches off its actual stack, its owner releases and wakes it, and the worker resumes the saved instruction stream after its context switch to release its newly transferred lock. This is a **cooperative** execution proof, not general IRQ-preemptive mutex safety. D4's test-only priority inheritance is not reused as a second production scheduler. Real lock execution, nested/multiple waiters, owner-death cancellation and generalized preemption remain future work. Run `make HOST_CC=gcc test-m10-d5-mutex-block-wake` for the new 20,000-cycle test or `make HOST_CC=gcc test-m10-d5-mutex-acceptance` for inherited regression.


## M10-D5.3 — Ranked mutexes and bounded priority donation

The experimental `kernel-sync-d53.[ch]` sidecar is deliberately **not another task scheduler**: every owner and waiter is a full M10-A `(slot,generation)` handle and every held lock or registered waiter must appear in the D5.2b resource ledger. There are four fixed locks, ranks 0–3, and fifteen ordinary task slots; supervisor slot 15 cannot own or wait. Acquiring another lock is allowed only when its rank is strictly greater than all locks the task already holds, preventing circular dependency if all participants comply. A contender registers a waiter reference, changes RUNNING to BLOCKED, and must switch to the surviving dispatcher stack. Unlock chooses the highest effective-priority waiter, with FIFO tie-breaking; ownership transfers before that exact task is made RUNNABLE.

Base priority is recorded separately for each registered generation. Effective priority propagates in at most fifteen bounded passes along wait-for edges, so when a high-priority task waits for a lock held by another task, that owner inherits its effective priority, including through a chain of strictly ascending ranks. This is **priority metadata only**: the current M10-A FIFO dispatcher does not use the donated value. Do not claim that scheduler-level priority inversion is solved until the actual dispatch path consumes it and retains the reserved supervisor service guarantee.

A terminated owner does not automatically lose its mutex; the owner remains registered and `d53_orphan` reports the lock unavailable. Its stack cannot be reclaimed while references remain, and no automatic retry or unsafe reset is allowed. The trusted dispatcher can explicitly cancel an *exact BLOCKED waiter* while it is still live, drop that one reference and wake it before termination; it cannot erase references from a terminated task. Host GCC ASan/UBSan gate: `make HOST_CC=gcc test-m10-d53-sync`; inherited full test gate: `make HOST_CC=gcc test-m10-d53-acceptance`. See `decisions/ADR-m10-d53-bounded-synchronization.md`.

**Integration boundary:** The D5.3 sidecar currently reconciles all D5.2b `owned_locks` and `registered_waiters` counters for its registered tasks. Do not run this sidecar and the earlier D5.2d one-waiter fixture concurrently against the same task registry; they do not yet share a global object-identity ledger. A future migration must unify the actual lock/wait object owners before enabling generalized production synchronization.


## M10-D5.4 — Selection policy is not a second task scheduler

The D5.4 adapter `kernel-dispatch-d54.[ch]` computes effective priorities from D5.3 lock ownership and the exact RUNNABLE identities in M10-A's ready queue. It maintains bounded *age counters only*. It has no task-state table or stack owner. The actual selected RUNNABLE→RUNNING transition occurs exclusively inside `kernel_task_dispatch_selected()` in `kernel-task.c`, with full-generation validation and the same queue invariants as FIFO dispatch. Invalid profiles, stale identities and broken resource ledgers fail before any task or age mutation. A runnable reserved supervisor is selected at least every fourth dispatch opportunity, using the existing pure `kernel_priority_choose` policy, but this does **not** guarantee wall-clock service under arbitrary non-yielding tasks.

The new `SPIFFOS_M10_D54_GUEST_TEST` build flag enables a **separate**, three-worker QEMU diagnostic. A background owner first takes rank-0 lock and BLOCKS on an external event, making it legitimate for a high-priority and an interactive waiter to both block on the mutex. A surviving boot dispatcher wakes the owner after both waiters are suspended. The owner is selected at high effective priority, releases to the high waiter, which resumes its *real x86-64 saved C continuation* and passes ownership to the medium waiter, whose independent continuation releases the lock. The owner then exits; the boot dispatcher verifies canaries and registered refs and releases each physical stack exactly once. This is a **cooperative test-only** proof; IRQ preemption and production editor concurrency remain disabled.

Run `make HOST_CC=gcc test-m10-d54-dispatch` (20,000 supervisor selection decisions and priority/aging negative tests), `make HOST_CC=gcc test-m10-d54-guest` (three memory sizes), or `make HOST_CC=gcc test-m10-d54-acceptance` (complete inherited regression). See `decisions/ADR-m10-d54-priority-dispatch.md`.


## M10-D5.5 — Fail-closed preemption admission is not hardware proof

`kernel-preempt-safety-d55.[ch]` is a **diagnostic-only** pure policy. It does not own tasks, contexts, locks, stacks, interrupts or IRQ entry state; the sole scheduler remains M10-A. A future low-level x86 entry adapter must capture the *real* RFLAGS/IF, IRQ nesting, authoritative running task identity and observed RSP. The gate cannot prevent a malicious caller from forging its `d55_cpu_evidence` snapshot; that is deliberately an open integration gate, not a completed security guarantee. Its use today is testing invariants before any actual IRQ-driven switching is authorized.

Cooperative worker handoff can be admitted outside IRQ with no active critical section, even if a worker has a registered lock. IRQ-return switching requires IF=0, exactly one IRQ entry, zero critical depth, exact generation/task registration, RSP within its owned stack, and **no registered lock or waiter refs**. Synchronous lock edits require the exact RUNNING ordinary worker with IF=0 and no IRQ entry. Cancellation, reaping and verified supervisor service require an independent, physically observed RSP outside *all* registered task stacks, IF=0 and no IRQ/critical nesting; an ordinary RUNNING task cannot impersonate a privileged dispatcher. The gate does not modify authoritative state.

The supervisor watchdog accepts a **provided monotonic clock sample** and a nonzero deadline. Only an explicitly verified service completion on a surviving dispatcher/supervisor stack resets the service timer. A mere dispatch selection does not. Missing clock, time rollback or elapsed time **strictly greater** than the configured limit sets a permanent diagnostic fault; no reset/retry method exists. This detects failures; it is **not** an independent timer, forced CPU interrupt or proof of an absolute realtime budget for non-yielding code. Run `make HOST_CC=gcc test-m10-d55-safety` for GCC ASan/UBSan exclusion, timeout, stale task and 20,000 service cycles, or `make HOST_CC=gcc test-m10-d55-acceptance` to include all existing QEMU/keyboard/IRQ0 regression gates.
