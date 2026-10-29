/*
 * Purpose: expose one boot-only framebuffer diagnostic.
 * Dependencies: Limine metadata supplied by the startup CPU; no allocation.
 * Invariant: this is not a general display or workspace interface.
 * References: ADR-011; docs/operations/framebuffer-2026-10-28.md.
 */
#ifndef SPIFFOS_FRAMEBUFFER_H
#define SPIFFOS_FRAMEBUFFER_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
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
/* Fill a bounded nonempty pixel rectangle with logical 0xRRGGBB color.
 * Borrow mapped metadata; false means unsupported/out-of-bounds, no writes.
 * Startup CPU only, no ownership/allocation or concurrent access. */
bool framebuffer_fill_rectangle(const struct limine_framebuffer *framebuffer,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height, uint32_t rgb);
/* Draw one opaque 8x8 glyph at scale 2 (16x16 screen pixels), bit 7 at left.
 * Colors use logical 0xRRGGBB; false means no writes. Borrow 8 readable rows
 * and mapped framebuffer. Startup-only, no allocation/ownership transfer. */
bool framebuffer_draw_cell(const struct limine_framebuffer *framebuffer,
    uint64_t x, uint64_t y, const uint8_t glyph[8], uint32_t foreground,
    uint32_t background);
/* Scroll a bounded pixel rectangle upward; clear exposed rows with 0xRRGGBB.
 * amount in pixels, zero is a no-op, amount==height clears all. Invalid bounds
 * or amount>height return false before writes. Borrow valid mappings;
 * startup-only, no allocation/ownership transfer or concurrent drawing. */
bool framebuffer_scroll_rectangle(const struct limine_framebuffer *framebuffer,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint64_t amount, uint32_t background);
/* Move a bounded rectangle downward by amount pixels within itself and clear
 * the exposed top. Copying bottom-to-top preserves overlapping source rows.
 * Invalid bounds or amount>height return false before writes. */
bool framebuffer_move_rectangle_down(const struct limine_framebuffer *framebuffer,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint64_t amount, uint32_t background);
/* Bitwise-invert every stored pixel word in a bounded rectangle. Applying the
 * same rectangle twice restores its exact prior pixels, including logo art.
 * False means invalid bounds and no writes. */
bool framebuffer_invert_rectangle(const struct limine_framebuffer *framebuffer,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height);
/* Draw packed RGB24 rows, converting logical RGB to the framebuffer layout.
 * Validate byte count and all bounds before writes; false means no writes.
 * Borrow readable data and mapped framebuffer. Startup-only, no allocations. */
bool framebuffer_draw_rgb_image(const struct limine_framebuffer *framebuffer,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    const uint8_t *rgb, size_t byte_count);
#endif
