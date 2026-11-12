/* Purpose: define the fixed x86-64 IDT and IDTR wire layouts.
 * Dependencies: C11 fixed-width integers; no allocation or hardware IO.
 * Invariants: packed sizes are exact; only vectors 0..31 may be present in
 * M9-3A, with IST1 for vector 8 only. Do not enable interrupts until
 * IRQ gates are independently designed.
 * Reference: docs/decisions/ADR-m9-exception-idt.md. */
#ifndef X86_IDT_H
#define X86_IDT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * IDT gate structure matching x86-64 specification.
 * Size: 16 bytes (must be exactly 16 bytes for proper IDT layout)
 */
struct x86_idt_gate {
    uint16_t offset_low;   /**< Lower 16 bits of interrupt handler address */
    uint16_t selector;     /**< Code segment selector in GDT */
    uint8_t ist;           /**< Interrupt Stack Table index (0-7) */
    uint8_t flags;         /**< Gate type and attributes */
    uint16_t offset_mid;   /**< Middle 16 bits of interrupt handler address */
    uint32_t offset_high;  /**< Upper 32 bits of interrupt handler address */
    uint32_t reserved;     /**< Reserved bits (must be zero) */
} __attribute__((packed));

/**
 * IDT descriptor register structure.
 * Size: 10 bytes (must be exactly 10 bytes for proper IDTR layout)
 */
struct x86_idtr {
    uint16_t limit;        /**< Limit of IDT (size in bytes - 1) */
    uint64_t base;         /**< Base address of IDT */
} __attribute__((packed));

/**
 * Number of exception gates (0-31)
 */
#define IDT_EXCEPTION_COUNT 32

/**
 * Total number of IDT entries
 */
#define IDT_TOTAL_COUNT 256

/**
 * Creates a single IDT gate entry.
 * 
 * @param out Pointer to output gate structure (must be valid, not NULL)
 * @param handler Address of interrupt handler function (must be valid)
 * @param selector Code segment selector in GDT (must be valid)
 * @return true if successful, false if invalid parameters
 * 
 * Ownership: Caller owns out; handler is a borrowed code address, not storage
 * Units: handler address is 64-bit, selector is 16-bit. No IRQ or allocation.
 * Failure: Returns false without writing for invalid pointer, zero handler or non-ring-zero selector
 */
bool x86_idt_make_gate(struct x86_idt_gate *out, uint64_t handler, uint16_t selector);

/**
 * Constructs multiple IDT gate entries from array of handler addresses.
 * 
 * @param entries Pointer to array of IDT gate structures (must be valid, not NULL)
 * @param count Number of entries to construct (must equal IDT_TOTAL_COUNT)
 * @param handler_addresses Array of interrupt handler addresses (must be valid, not NULL)
 * @param selector Code segment selector in GDT (must be valid)
 * @return true if successful, false if invalid parameters
 * 
 * Ownership: Caller owns entries; handler_addresses is borrowed synchronously
 * Units: count is entries (exactly 256), handler_addresses has 32 64-bit addresses. No IRQ.
 * Failure: Returns false without writing if any input is invalid, count differs from IDT_TOTAL_COUNT, or handler is zero
 */
bool x86_idt_construct(struct x86_idt_gate *entries, size_t count,
                       const uint64_t *handler_addresses, uint16_t selector);

/* Install the 32 exception gates using current ring-0 CS and LIDT. True means
 * ready, false keeps old IDTR. Boot CPU only with CPU interrupts disabled. */
bool x86_idt_initialize(void);

/* Add only the boot CPU's timer and spurious interrupt gates, preserving
 * all exception/IST1 gates and existing PIC vectors absent. Requires IF=0,
 * x86_idt_initialize() success, no concurrent IRQ or handler rebinding. */
bool x86_idt_enable_apic_timer_gates(void);

/* Static assertions for structure sizes */
_Static_assert(sizeof(struct x86_idt_gate) == 16, "IDT gate must be exactly 16 bytes");
_Static_assert(sizeof(struct x86_idtr) == 10, "IDTR must be exactly 10 bytes");

#endif /* X86_IDT_H */
