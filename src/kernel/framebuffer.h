/*
 * Purpose: expose one boot-only framebuffer diagnostic.
 * Dependencies: Limine metadata supplied by the startup CPU; no allocation.
 * Invariant: this is not a general display or workspace interface.
 * References: ADR-011; docs/operations/framebuffer-2026-10-28.md.
 */
#ifndef SPIFFOS_FRAMEBUFFER_H
#define SPIFFOS_FRAMEBUFFER_H
#include <stdbool.h>
struct limine_framebuffer;

/* Draw into a borrowed, mapped Limine framebuffer; never takes ownership.
 * Returns true after drawing, false for unsupported metadata without writes.
 * Startup CPU only, interrupts disabled; caller guarantees metadata/mapping
 * remain valid throughout. Uses no allocator, locks or serial hardware. */
bool framebuffer_draw_boot_pattern(const struct limine_framebuffer *framebuffer);
/* Draw the fixed bitmap SPIFFOS label on a black panel over the boot pattern.
 * Borrows the mapped descriptor; returns false without writes if unsupported
 * or smaller than 256x96 pixels. Returns true after drawing. Startup CPU only;
 * no allocation, ownership transfer, interrupts or concurrent access. */
bool framebuffer_draw_boot_title(const struct limine_framebuffer *framebuffer);
#endif
