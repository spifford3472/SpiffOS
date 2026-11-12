/* Purpose: provide one static x86-64 TSS and a dedicated double-fault IST.
 * Dependencies: x86-tss.h, SGDT/LGDT/LTR/STR instructions, active Limine GDT.
 * Invariants: boot CPU only, IF off; no allocation, no inter-CPU sharing;
 * preserve old descriptors/selectors by copying the active GDT exactly.
 * Reference: docs/decisions/ADR-m9-3-exception-stacks.md. */
#include "x86-tss.h"

/* Reserving 8 KiB accommodates ordinary early-boot GDTs; oversized tables
 * fail before LGDT. The TSS descriptor must start on an eight-byte slot. */
enum { BOOT_GDT_CAPACITY = 8192, EMERGENCY_STACK_BYTES = 16384 };
struct x86_gdtr { uint16_t limit; uint64_t base; } __attribute__((packed));
static uint8_t owned_gdt[BOOT_GDT_CAPACITY] __attribute__((aligned(16)));
static uint8_t emergency_stack[EMERGENCY_STACK_BYTES]
    __attribute__((aligned(16)));
static struct x86_tss64 boot_tss __attribute__((aligned(16)));
static bool tss_ready;

/* Build the two-word available 64-bit TSS descriptor. A zero base or
 * nonsensical limit is rejected without touching the caller's words.
 * Long-mode system descriptors use present + type 9 and a 20-bit limit. */
bool x86_tss_make_descriptor(uint64_t *out_words, uint64_t base,
                             uint32_t limit)
{
    if (out_words == (void *)0 || base == 0 || limit < 103 ||
        limit > 0xfffffu) return false;
    uint64_t low = ((uint64_t)(limit & 0xffffu)) |
        ((base & 0xffffffu) << 16) |
        (UINT64_C(0x89) << 40) |
        (((uint64_t)limit & 0xf0000u) << 32) |
        (((base >> 24) & 0xffu) << 56);
    uint64_t high = base >> 32;
    out_words[0] = low;
    out_words[1] = high;
    return true;
}

/* Read existing GDTR and reproduce all current selector positions before
 * loading TR. No descriptor selectors are rewritten; failing GDT validation
 * changes no CPU registers. Assumes accessible, mapped loader GDT metadata.
 * The eventual SMP/guard-page milestone must replace this bootstrap policy. */
bool x86_tss_initialize(void)
{
    struct x86_gdtr current;
    uint16_t cs;
    uint16_t tr;
    __asm__ volatile ("sgdt %0" : "=m"(current));
    __asm__ volatile ("movw %%cs, %0" : "=r"(cs));
    size_t original_bytes = (size_t)current.limit + 1u;
    if (tss_ready || current.base == 0 ||
        original_bytes < 8 || original_bytes > BOOT_GDT_CAPACITY - 16 ||
        (original_bytes & 7u) != 0 ||
        (cs & 7u) != 0 || (size_t)(cs & ~7u) >= original_bytes)
        return false;
    /* LTR needs an unused slot; never clobber a loaded descriptor. */
    size_t descriptor_offset = original_bytes;
    if (descriptor_offset > UINT16_MAX) return false;
    const uint8_t *source = (const uint8_t *)(uintptr_t)current.base;
    for (size_t i = 0; i < original_bytes; ++i)
        owned_gdt[i] = source[i];
    for (size_t i = original_bytes; i < original_bytes + 16; ++i)
        owned_gdt[i] = 0;

    boot_tss = (struct x86_tss64){0};
    boot_tss.ist1 = (uint64_t)(uintptr_t)
        (emergency_stack + EMERGENCY_STACK_BYTES);
    boot_tss.io_map_base = sizeof boot_tss;
    uint64_t descriptor_words[2];
    if (!x86_tss_make_descriptor(descriptor_words,
            (uint64_t)(uintptr_t)&boot_tss, sizeof boot_tss - 1u))
        return false;
    for (size_t i = 0; i < 16; ++i)
        owned_gdt[descriptor_offset + i] =
            (uint8_t)(descriptor_words[i / 8u] >> ((i % 8u) * 8u));
    struct x86_gdtr updated = {
        .limit = (uint16_t)(original_bytes + 16u - 1u),
        .base = (uint64_t)(uintptr_t)owned_gdt
    };
    /* The old CS still indexes a byte-identical code descriptor in our GDT.
     * No far return is required because CS's cached attributes are unchanged. */
    __asm__ volatile ("lgdt %0" : : "m"(updated) : "memory");
    uint16_t selector = (uint16_t)descriptor_offset;
    __asm__ volatile ("ltr %0" : : "rm"(selector) : "memory");
    __asm__ volatile ("str %0" : "=rm"(tr));
    tss_ready = (tr == selector);
    return tss_ready;
}

/* Single-CPU read of the persistent initialization flag; no shared access
 * or locking until SMP support is designed. */
bool x86_tss_emergency_ready(void)
{
    return tss_ready;
}
