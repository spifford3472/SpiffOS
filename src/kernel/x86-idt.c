/* Purpose: build and activate a static CPU-exception IDT for the boot CPU.
 * Dependencies: x86-idt.h, x86-64 long-mode instructions, static stub table.
 * Invariants: 32 exception gates, vector 8 uses a dedicated IST1 from the
 * boot-CPU TSS; PIC IRQ gates 32..47 installed but kept masked until
 * validated startup; interrupts disabled during IDT setup, no dynamic
 * allocation or CPU interrupt enable in the initializer.
 * References: ADR-m9-exception-idt.md and ADR-m9-kernel-foundation.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "x86-idt.h"
#include "x86-tss.h"

enum { IDT_GATE_PRESENT_INTERRUPT = 0x8e };
static struct x86_idt_gate exception_idt[IDT_TOTAL_COUNT];

/* The assembly stubs include the CPU vector and a normalized error code.
 * Each address is borrowed from the linked immutable kernel text segment. */
extern const uintptr_t x86_exception_stub_table[IDT_EXCEPTION_COUNT];
extern const uintptr_t x86_irq_stub_table[IDT_PIC_IRQ_COUNT];

/* Encode one ring-0 interrupt gate into caller-owned storage, validating its
 * pointer, nonzero handler and ring-0 CS. Invalid inputs leave out untouched.
 * Pure computation: no hardware accesses, allocations, or concurrency. */
bool x86_idt_make_gate(struct x86_idt_gate *out, uint64_t handler,
                       uint16_t selector)
{
    if (out == NULL || handler == 0 || selector == 0 ||
        (selector & 3u) != 0) return false;
    struct x86_idt_gate gate = {
        .offset_low = (uint16_t)(handler & UINT16_MAX),
        .selector = selector,
        .ist = 0,
        .flags = IDT_GATE_PRESENT_INTERRUPT,
        .offset_mid = (uint16_t)((handler >> 16) & UINT16_MAX),
        .offset_high = (uint32_t)(handler >> 32),
        .reserved = 0
    };
    *out = gate;
    return true;
}

/* Prepare an entire 256-entry table using exactly 32 ordered exception
 * handlers. Missing handlers or invalid selector reject without writing any
 * destination bytes. Hardware vectors 32..255 remain explicitly absent.
 * Caller owns all arrays; interrupts disabled if used during live setup. */
bool x86_idt_construct(struct x86_idt_gate *entries, size_t count,
                       const uint64_t *handler_addresses, uint16_t selector)
{
    if (entries == NULL || count != IDT_TOTAL_COUNT ||
        handler_addresses == NULL || selector == 0 || (selector & 3u) != 0)
        return false;
    for (size_t vector = 0; vector < IDT_EXCEPTION_COUNT; ++vector)
        if (handler_addresses[vector] == 0) return false;
    for (size_t vector = 0; vector < IDT_TOTAL_COUNT; ++vector) {
        if (vector < IDT_EXCEPTION_COUNT) {
            (void)x86_idt_make_gate(&entries[vector],
                                   handler_addresses[vector], selector);
        } else {
            entries[vector] = (struct x86_idt_gate){0};
        }
    }
    return true;
}

/* Preflight every IRQ handler address before mutating the 16 controller
 * gates. Leave vectors 48..255 absent, and CPU vector gates unchanged. */
bool x86_idt_add_pic_gates(struct x86_idt_gate *entries, size_t count,
                           const uint64_t *irq_handlers, uint16_t selector)
{
    if (entries == NULL || count != IDT_TOTAL_COUNT ||
        irq_handlers == NULL || selector == 0 || (selector & 3u) != 0)
        return false;
    for (size_t irq = 0; irq < IDT_PIC_IRQ_COUNT; ++irq)
        if (irq_handlers[irq] == 0) return false;
    for (size_t irq = 0; irq < IDT_PIC_IRQ_COUNT; ++irq)
        (void)x86_idt_make_gate(&entries[IDT_PIC_VECTOR_FIRST + irq],
                                irq_handlers[irq], selector);
    return true;
}

/* Install the static table on the boot CPU. Requires ring 0 and a valid
 * current CS from the active GDT, with CPU IF already cleared. This loads only
 * an IDTR; it does not modify the PIC/APIC, enable IRQs, or return an error.
 * Failure keeps the loader's previous IDTR and stops the caller's boot. */
bool x86_idt_initialize(void)
{
    uint16_t cs;
    __asm__ volatile ("movw %%cs, %0" : "=r"(cs));
    uint64_t addresses[IDT_EXCEPTION_COUNT];
    for (size_t i = 0; i < IDT_EXCEPTION_COUNT; ++i)
        addresses[i] = (uint64_t)x86_exception_stub_table[i];
    if (!x86_tss_initialize()) return false;
    if (!x86_idt_construct(exception_idt, IDT_TOTAL_COUNT, addresses, cs))
        return false;
    /* Double faults always enter on the dedicated IST1 emergency stack.
     * The TR was loaded before this gate becomes visible to the CPU. */
    exception_idt[8].ist = 1;
    uint64_t irq_addresses[IDT_PIC_IRQ_COUNT];
    for (size_t irq = 0; irq < IDT_PIC_IRQ_COUNT; ++irq)
        irq_addresses[irq] = (uint64_t)x86_irq_stub_table[irq];
    if (!x86_idt_add_pic_gates(exception_idt, IDT_TOTAL_COUNT,
                                irq_addresses, cs)) return false;
    struct x86_idtr descriptor = {
        .limit = (uint16_t)(sizeof exception_idt - 1),
        .base = (uint64_t)(uintptr_t)exception_idt
    };
    __asm__ volatile ("lidt %0" : : "m"(descriptor) : "memory");
    return true;
}
