/* Purpose: mask, remap, and acknowledge legacy dual 8259 PIC interrupts.
 * Dependencies: x86-pic.h; x86 I/O ports for the optional hardware adapter.
 * Invariants: single boot CPU, no allocation; IF stays cleared throughout
 * Phase B; IO writes are bounded and explicit. IRQ2 is cascade-only.
 * Reference: docs/decisions/ADR-m9-3-pic-foundation.md. */
#include "x86-pic.h"
#include <stddef.h>

enum {
    MASTER_COMMAND=0x20, MASTER_DATA=0x21,
    SLAVE_COMMAND=0xa0, SLAVE_DATA=0xa1,
    IO_WAIT_PORT=0x80, ICW1_INIT_ICW4=0x11,
    ICW4_8086=0x01, MASTER_VECTOR_START=0x20,
    SLAVE_VECTOR_START=0x28, MASTER_CASCADE_MASK=0x04,
    SLAVE_CASCADE_ID=0x02, EOI=0x20, READ_ISR=0x0b,
    IRQ_CASCADE=2, IRQ_MASTER_SPURIOUS=7, IRQ_SLAVE_SPURIOUS=15
};

/* One register write followed by a short PC-compatibility bus delay.
 * No retries, heap operations or hidden IO; host tests observe both writes. */
static void write_delayed(struct x86_pic *pic, uint16_t port, uint8_t byte)
{
    pic->io.write(pic->io.context, port, byte);
    pic->io.write(pic->io.context, IO_WAIT_PORT, 0);
}

/* Initialize a caller-owned state and the dual controllers. The port
 * callback contract is synchronous; the IO context remains borrowed.
 * Invalid input changes no state and issues no IO. PIC presence cannot be
 * reliably established by writes; IRQ0 delivery requires a later QEMU test. */
bool x86_pic_initialize(struct x86_pic *pic, const struct x86_pic_io *io)
{
    if (pic == NULL || io == NULL || io->read == NULL || io->write == NULL)
        return false;
    if (pic->ready) return false;
    pic->io = *io;
    pic->ready = false;
    /* First block inherited firmware IRQ enables. ICW setup has no
     * opportunity to release a live IRQ because the caller holds IF=0. */
    write_delayed(pic, MASTER_DATA, 0xff);
    write_delayed(pic, SLAVE_DATA, 0xff);
    write_delayed(pic, MASTER_COMMAND, ICW1_INIT_ICW4);
    write_delayed(pic, SLAVE_COMMAND, ICW1_INIT_ICW4);
    write_delayed(pic, MASTER_DATA, MASTER_VECTOR_START);
    write_delayed(pic, SLAVE_DATA, SLAVE_VECTOR_START);
    write_delayed(pic, MASTER_DATA, MASTER_CASCADE_MASK);
    write_delayed(pic, SLAVE_DATA, SLAVE_CASCADE_ID);
    write_delayed(pic, MASTER_DATA, ICW4_8086);
    write_delayed(pic, SLAVE_DATA, ICW4_8086);
    write_delayed(pic, MASTER_DATA, 0xff);
    write_delayed(pic, SLAVE_DATA, 0xff);
    pic->masked_irqs = 0xffffu;
    pic->ready = true;
    return true;
}

/* Change one IRQ's mask via state shadow, not a possibly stale firmware
 * register. IRQ2 is reserved for slave cascade and never directly exposed. */
bool x86_pic_mask_irq(struct x86_pic *pic, uint8_t irq)
{
    if (pic == NULL || !pic->ready || irq >= 16 || irq == IRQ_CASCADE)
        return false;
    uint16_t masks = (uint16_t)(pic->masked_irqs | (uint16_t)(1u << irq));
    if (irq >= 8 && (masks & 0xff00u) == 0xff00u)
        masks |= (uint16_t)(1u << IRQ_CASCADE);
    if (masks == pic->masked_irqs) return true;
    uint16_t before = pic->masked_irqs;
    pic->masked_irqs = masks;
    if ((uint8_t)before != (uint8_t)masks)
        write_delayed(pic, MASTER_DATA, (uint8_t)masks);
    if ((uint8_t)(before >> 8) != (uint8_t)(masks >> 8))
        write_delayed(pic, SLAVE_DATA, (uint8_t)(masks >> 8));
    return true;
}

/* Unmask exactly one IRQ. A slave IRQ also unmasks master IRQ2 after
 * updating the slave mask; no software caller can independently own IRQ2. */
bool x86_pic_unmask_irq(struct x86_pic *pic, uint8_t irq)
{
    if (pic == NULL || !pic->ready || irq >= 16 || irq == IRQ_CASCADE)
        return false;
    uint16_t masks = (uint16_t)(pic->masked_irqs & ~(uint16_t)(1u << irq));
    if (irq >= 8) masks &= (uint16_t)~(1u << IRQ_CASCADE);
    if (masks == pic->masked_irqs) return true;
    uint16_t before = pic->masked_irqs;
    pic->masked_irqs = masks;
    /* When activating slave IRQ, update slave before opening cascade. */
    if ((uint8_t)(before >> 8) != (uint8_t)(masks >> 8))
        write_delayed(pic, SLAVE_DATA, (uint8_t)(masks >> 8));
    if ((uint8_t)before != (uint8_t)masks)
        write_delayed(pic, MASTER_DATA, (uint8_t)masks);
    return true;
}

/* Read PIC's in-service register, not its pending-request register.
 * OCW3 0x0b selects ISR on command-port reads. This is bounded two-IO
 * operation and makes spurious IRQ7/IRQ15 testable in hosted tests. */
static uint8_t read_isr(struct x86_pic *pic, uint16_t command_port)
{
    pic->io.write(pic->io.context, command_port, READ_ISR);
    return pic->io.read(pic->io.context, command_port);
}

/* Ack source with correct cascade order. The 8259 generates phantom IRQ7/15
 * vectors when the original edge disappears during CPU acknowledgment:
 * phantom IRQ7 never acquired master ISR bit7, phantom IRQ15 still acquired
 * master's cascade ISR bit. Send no slave EOI for that phantom IRQ15. */
bool x86_pic_acknowledge(struct x86_pic *pic, uint8_t irq)
{
    if (pic == NULL || !pic->ready || irq >= 16) return false;
    if (irq == IRQ_MASTER_SPURIOUS &&
        (read_isr(pic, MASTER_COMMAND) & 0x80u) == 0)
        return true;
    if (irq == IRQ_SLAVE_SPURIOUS &&
        (read_isr(pic, SLAVE_COMMAND) & 0x80u) == 0) {
        pic->io.write(pic->io.context, MASTER_COMMAND, EOI);
        return true;
    }
    if (irq >= 8)
        pic->io.write(pic->io.context, SLAVE_COMMAND, EOI);
    pic->io.write(pic->io.context, MASTER_COMMAND, EOI);
    return true;
}

/* Privileged raw byte-port IO. x86 IN/OUT are synchronous and require CPL0
 * (or IOPL); callback interface keeps these out of hosted unit tests. */
static uint8_t hardware_read(void *context, uint16_t port)
{
    (void)context;
    uint8_t byte;
    __asm__ volatile ("inb %1, %0" : "=a"(byte) : "Nd"(port) : "memory");
    return byte;
}

/* Privileged raw port write; never allocates, sleeps, or waits for a flag. */
static void hardware_write(void *context, uint16_t port, uint8_t byte)
{
    (void)context;
    __asm__ volatile ("outb %0, %1" : : "a"(byte), "Nd"(port) : "memory");
}

static struct x86_pic boot_pic;

/* Refuse hardware setup unless IF is already 0. Caller must have installed
 * IDT exception gates and retain IF=0 until IRQ handlers are ready. */
bool x86_pic_initialize_hardware(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    if ((flags & (1u << 9)) != 0) return false;
    const struct x86_pic_io io = { hardware_read, hardware_write, NULL };
    return x86_pic_initialize(&boot_pic, &io);
}

/* Read-only single-CPU controller status; does not imply IRQ availability. */
bool x86_pic_hardware_ready(void) { return boot_pic.ready; }

/* Hardware-side IRQ mask adapter; must only be called with serialized
 * controller ownership. Never modify IRQ masks from an unrelated CPU. */
bool x86_pic_mask_hardware_irq(uint8_t irq)
{ return x86_pic_mask_irq(&boot_pic, irq); }

/* Hardware-side unmask adapter; only after a valid handler exists. */
bool x86_pic_unmask_hardware_irq(uint8_t irq)
{ return x86_pic_unmask_irq(&boot_pic, irq); }

/* Hardware EOI adapter; appropriate for IRQ-entry context once installed. */
bool x86_pic_acknowledge_hardware(uint8_t irq)
{ return x86_pic_acknowledge(&boot_pic, irq); }
