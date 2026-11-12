/* Purpose: define a minimal dual 8259A interrupt controller interface.
 * Dependencies: fixed-width integers and caller-supplied port IO callbacks.
 * Invariants: IF=0 during boot setup; all IRQs masked until a handler exists;
 * no allocator, hardware assumptions in hosted tests, or stable driver ABI.
 * Reference: docs/decisions/ADR-m9-3-pic-foundation.md. */
#ifndef X86_PIC_H
#define X86_PIC_H
#include <stdbool.h>
#include <stdint.h>

/* Only the caller owns the context and callback lifetime; functions never
 * store an IO context beyond the state object lifetime. All IO is byte sized. */
struct x86_pic_io {
    uint8_t (*read)(void *context, uint16_t port);
    void (*write)(void *context, uint16_t port, uint8_t byte);
    void *context;
};

struct x86_pic {
    struct x86_pic_io io;
    uint16_t masked_irqs; /* bit one means IRQ blocked; starts at 0xffff */
    bool ready;
};

/* Mask both chips, remap 0x20/0x28, leave all masked. Requires IF=0
 * enforced by caller. On invalid pointers no hardware writes occur. */
bool x86_pic_initialize(struct x86_pic *pic, const struct x86_pic_io *io);

/* Only one caller/CPU may modify PIC state. IRQ2 is reserved for cascade;
 * slave unmask automatically permits cascade, and the last slave mask
 * restores it. Rejected inputs leave state and hardware untouched. */
bool x86_pic_mask_irq(struct x86_pic *pic, uint8_t irq);
bool x86_pic_unmask_irq(struct x86_pic *pic, uint8_t irq);

/* Acknowledge an IRQ after bounded handling. Spurious IRQ7 receives no
 * EOI; spurious IRQ15 receives only master cascade EOI. Other slave IRQs
 * receive slave then master EOIs. No EOI on invalid IRQ. */
bool x86_pic_acknowledge(struct x86_pic *pic, uint8_t irq);

/* Boot-only hardware adapter using IN/OUT and bounded IO waits. Must only
 * be called in ring 0 with CPU IF already cleared. */
bool x86_pic_initialize_hardware(void);
bool x86_pic_hardware_ready(void);
bool x86_pic_mask_hardware_irq(uint8_t irq);
bool x86_pic_unmask_hardware_irq(uint8_t irq);
bool x86_pic_acknowledge_hardware(uint8_t irq);
#endif
