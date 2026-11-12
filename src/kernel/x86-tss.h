/* Purpose: define the boot-CPU x86-64 task-state and emergency IST service.
 * Dependencies: C11 integers, x86 long-mode descriptor format.
 * Invariants: single CPU; interrupts remain disabled; no heap use or hardware
 * interrupt handling; the emergency stack is not a guarded page mapping.
 * Reference: docs/decisions/ADR-m9-3-exception-stacks.md. */
#ifndef X86_TSS_H
#define X86_TSS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Packed 64-bit TSS hardware layout (104 bytes); never accessed via an
 * unaligned u64 lvalue outside this structure. All addresses are virtual. */
struct x86_tss64 {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3, io_map_base;
} __attribute__((packed));

/* Pure descriptor encoder. Writes both eight-byte words only on success.
 * base is a valid mapped TSS address, limit is sizeof(TSS)-1, and out has
 * space for exactly 16 bytes. No CPU effects; caller owns destination. */
bool x86_tss_make_descriptor(uint64_t *out_words, uint64_t base,
                             uint32_t limit);

/* Install a kernel-owned copy of the current GDT, add a 16-byte TSS
 * descriptor, load GDTR and TR, and verify the task register selector.
 * Requires CPL0 and IF=0. Existing CS/DS selectors retain their entries;
 * on rejection before LGDT, the old GDT remains active. No heap or IRQs. */
bool x86_tss_initialize(void);

/* True only after successful boot-CPU TSS registration. The IDT may then
 * assign IST index 1 to vector 8; other vectors retain normal stacks. */
bool x86_tss_emergency_ready(void);

_Static_assert(sizeof(struct x86_tss64) == 104, "64-bit TSS layout");
#endif
