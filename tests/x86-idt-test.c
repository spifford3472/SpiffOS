/* Purpose: test M9-2 IDT encoding and rejection without privileged CPU IO.
 * Dependencies: x86-idt pure constructor and C11 assertions.
 * Invariants: never load an IDTR or execute an exception in hosted tests.
 * Reference: docs/decisions/ADR-m9-exception-idt.md. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "x86-idt.h"

/* Link-only stub table; hosted tests never call x86_idt_initialize or LIDT. */
const uintptr_t x86_exception_stub_table[IDT_EXCEPTION_COUNT] = {0};
const uintptr_t x86_irq_stub_table[IDT_PIC_IRQ_COUNT] = {0};

/* Check both zero-on-invalid and all 32 explicit valid entries. No ownership
 * transfer, allocation, kernel instructions or concurrency occurs. */
int main(void)
{
    struct x86_idt_gate gate = {0};
    const uint64_t addr = UINT64_C(0xffffffff80001234);
    assert(x86_idt_make_gate(&gate, addr, 0x28));
    assert(gate.offset_low == 0x1234);
    assert(gate.offset_mid == 0x8000);
    assert(gate.offset_high == 0xffffffffu);
    assert(gate.selector == 0x28 && gate.flags == 0x8e);
    assert(gate.ist == 0 && gate.reserved == 0);
    struct x86_idt_gate saved = gate;
    assert(!x86_idt_make_gate(&gate, 0, 0x28));
    assert(!x86_idt_make_gate(&gate, addr, 0x2b));
    assert(!x86_idt_make_gate(NULL, addr, 0x28));
    assert(memcmp(&gate, &saved, sizeof gate) == 0);
    uint64_t handlers[IDT_EXCEPTION_COUNT];
    for (size_t i = 0; i < IDT_EXCEPTION_COUNT; ++i)
        handlers[i] = addr + i * 16;
    struct x86_idt_gate entries[IDT_TOTAL_COUNT];
    memset(entries, 0xa5, sizeof entries);
    struct x86_idt_gate old = entries[0];
    assert(!x86_idt_construct(entries, IDT_TOTAL_COUNT - 1, handlers, 0x28));
    assert(memcmp(&entries[0], &old, sizeof old) == 0);
    handlers[6] = 0;
    assert(!x86_idt_construct(entries, IDT_TOTAL_COUNT, handlers, 0x28));
    assert(memcmp(&entries[0], &old, sizeof old) == 0);
    handlers[6] = addr + 96;
    assert(x86_idt_construct(entries, IDT_TOTAL_COUNT, handlers, 0x28));
    for (size_t i = 0; i < IDT_EXCEPTION_COUNT; ++i)
        assert(entries[i].flags == 0x8e && entries[i].selector == 0x28);
    for (size_t i = IDT_EXCEPTION_COUNT; i < IDT_TOTAL_COUNT; ++i) {
        const struct x86_idt_gate zero = {0};
        assert(memcmp(&entries[i], &zero, sizeof zero) == 0);
    }
    uint64_t irq_handlers[IDT_PIC_IRQ_COUNT];
    for (size_t irq = 0; irq < IDT_PIC_IRQ_COUNT; ++irq)
        irq_handlers[irq] = addr + 4096 + irq * 32u;
    struct x86_idt_gate previous = entries[32];
    irq_handlers[4] = 0;
    assert(!x86_idt_add_pic_gates(entries, IDT_TOTAL_COUNT, irq_handlers, 0x28));
    assert(memcmp(&entries[32], &previous, sizeof previous) == 0);
    irq_handlers[4] = addr + 4096 + 4u * 32u;
    assert(x86_idt_add_pic_gates(entries, IDT_TOTAL_COUNT, irq_handlers, 0x28));
    for (size_t irq = 0; irq < IDT_PIC_IRQ_COUNT; ++irq) {
        assert(entries[32 + irq].flags == 0x8e);
        assert(entries[32 + irq].ist == 0);
        assert(entries[32 + irq].selector == 0x28);
    }
    for (size_t vector = 48; vector < IDT_TOTAL_COUNT; ++vector) {
        const struct x86_idt_gate zero = {0};
        assert(memcmp(&entries[vector], &zero, sizeof zero) == 0);
    }
    return 0;
}
