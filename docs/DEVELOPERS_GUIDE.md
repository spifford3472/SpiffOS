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
