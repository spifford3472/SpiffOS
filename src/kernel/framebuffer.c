/*
 * Purpose: draw a boot pattern and fixed bitmap title into screen memory.
 * Dependencies: official Limine framebuffer metadata; no host C library.
 * Invariants: borrowed bootloader mappings stay valid, single startup CPU,
 * no concurrent drawing, no allocation or interrupts. Only 32-bit RGB with
 * disjoint 8-bit channels is supported. Padding is never written.
 * References: ADR-011; docs/operations/framebuffer-2026-10-28.md.
 */
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include "framebuffer.h"

enum { BORDER_PIXELS = 16, BYTES_PER_PIXEL = 4, CHANNEL_BITS = 8 };

/* Check metadata before any screen writes; false means unsupported/unsafe.
 * Borrows the descriptor, owns nothing, has no side effects or allocations.
 * Caller guarantees the descriptor itself is readable; boot CPU only.
 * This checks arithmetic/layout, not whether arbitrary addresses are mapped. */
static bool framebuffer_supported(const struct limine_framebuffer *framebuffer)
{
    if (framebuffer == NULL || framebuffer->address == NULL ||
        framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        framebuffer->bpp != 32 ||
        framebuffer->width <= 2 * BORDER_PIXELS ||
        framebuffer->height <= 2 * BORDER_PIXELS) {
        return false;
    }
    /* Pitch is measured in bytes, and may include padding after visible pixels.
     * Check multiplication before calculating row length or total span. */
    if (framebuffer->width > UINT64_MAX / BYTES_PER_PIXEL ||
        framebuffer->pitch < framebuffer->width * BYTES_PER_PIXEL ||
        framebuffer->pitch % BYTES_PER_PIXEL != 0 ||
        (uintptr_t)framebuffer->address % BYTES_PER_PIXEL != 0 ||
        framebuffer->height > UINTPTR_MAX / framebuffer->pitch ||
        (uintptr_t)framebuffer->address >
            UINTPTR_MAX - framebuffer->height * framebuffer->pitch) {
        return false;
    }
    /* Restrict shifts before evaluating them: shifting a 32-bit value by 32
     * or more is undefined in C. Each 8-bit channel must fit within the word. */
    if (framebuffer->red_mask_size != CHANNEL_BITS ||
        framebuffer->green_mask_size != CHANNEL_BITS ||
        framebuffer->blue_mask_size != CHANNEL_BITS ||
        framebuffer->red_mask_shift > 24 ||
        framebuffer->green_mask_shift > 24 ||
        framebuffer->blue_mask_shift > 24) {
        return false;
    }
    uint32_t red_mask = UINT32_C(255) << framebuffer->red_mask_shift;
    uint32_t green_mask = UINT32_C(255) << framebuffer->green_mask_shift;
    uint32_t blue_mask = UINT32_C(255) << framebuffer->blue_mask_shift;
    return (red_mask & green_mask) == 0 && (red_mask & blue_mask) == 0 &&
           (green_mask & blue_mask) == 0;
}

/* Pack three 0..255 channel values according to previously checked metadata.
 * Returns the pixel word with unused bits zero. Pure calculation; no ownership,
 * allocation, error path or hardware access; any context after validation. */
static uint32_t pixel_color(const struct limine_framebuffer *framebuffer,
                            uint8_t red, uint8_t green, uint8_t blue)
{
    return ((uint32_t)red << framebuffer->red_mask_shift) |
           ((uint32_t)green << framebuffer->green_mask_shift) |
           ((uint32_t)blue << framebuffer->blue_mask_shift);
}

/* Draw a purple border enclosing red, green and blue vertical bars.
 * Argument: borrowed descriptor with dimensions in pixels, pitch in bytes.
 * Returns false without writing if metadata is unsupported; true after drawing.
 * Owns no memory; performs volatile screen stores. Startup CPU only, no locks
 * or interrupts; descriptor and full screen mapping must remain valid. */
bool framebuffer_draw_boot_pattern(const struct limine_framebuffer *framebuffer)
{
    if (!framebuffer_supported(framebuffer)) {
        return false;
    }
    uint32_t border = pixel_color(framebuffer, 160, 160, 255);
    uint32_t red = pixel_color(framebuffer, 255, 0, 0);
    uint32_t green = pixel_color(framebuffer, 0, 255, 0);
    uint32_t blue = pixel_color(framebuffer, 0, 0, 255);
    uint64_t inner_width = framebuffer->width - 2 * BORDER_PIXELS;
    uint64_t first_boundary = BORDER_PIXELS + inner_width / 3;
    uint64_t second_boundary = BORDER_PIXELS + inner_width * 2 / 3;
    volatile uint8_t *pixels = framebuffer->address;

    for (uint64_t y = 0; y < framebuffer->height; ++y) {
        /* Byte arithmetic advances by the reported pitch, including padding.
         * The checked alignment lets us then address this row as 32-bit pixels.
         * Volatile keeps the compiler from discarding hardware-visible stores. */
        volatile uint32_t *row =
            (volatile uint32_t *)(pixels + y * framebuffer->pitch);
        for (uint64_t x = 0; x < framebuffer->width; ++x) {
            uint32_t color;
            if (x < BORDER_PIXELS || y < BORDER_PIXELS ||
                x >= framebuffer->width - BORDER_PIXELS ||
                y >= framebuffer->height - BORDER_PIXELS) {
                color = border;
            } else if (x < first_boundary) {
                color = red;
            } else if (x < second_boundary) {
                color = green;
            } else {
                color = blue;
            }
            row[x] = color;
        }
    }
    return true;
}

/* Draw the fixed word SPIFFOS; this is intentionally not a general text API.
 * Borrows a validated, mapped framebuffer. Returns false before any writes for
 * unsupported metadata or insufficient space; true after painting the title.
 * Owns/allocates nothing. Startup CPU only, no concurrent drawing/interrupts.
 * The caller keeps Limine's descriptor and screen mappings valid throughout.
 * See docs/operations/text-2026-10-28.md for layout and validation.
 */
bool framebuffer_draw_boot_title(const struct limine_framebuffer *framebuffer)
{
    enum {
        GLYPH_WIDTH = 8, GLYPH_HEIGHT = 8, SCALE = 4, LETTER_COUNT = 7,
        TITLE_WIDTH = LETTER_COUNT * GLYPH_WIDTH * SCALE,
        TITLE_HEIGHT = GLYPH_HEIGHT * SCALE, TITLE_TOP = 48
    };
    /* Original hand-defined glyphs: each byte is one row, bit 7 at the left.
     * Repeated letters are explicit to make the fixed label easy to follow.
     * For example, 0x3c is 00111100: four white pixels between black margins.
     * The final zero row supplies spacing beneath each letter. */
    static const uint8_t glyphs[LETTER_COUNT][GLYPH_HEIGHT] = {
        {0x3c, 0x66, 0x60, 0x3c, 0x06, 0x66, 0x3c, 0x00}, /* S */
        {0x7c, 0x66, 0x66, 0x7c, 0x60, 0x60, 0x60, 0x00}, /* P */
        {0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00}, /* I */
        {0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0x00}, /* F */
        {0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0x00}, /* F */
        {0x3c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00}, /* O */
        {0x3c, 0x66, 0x60, 0x3c, 0x06, 0x66, 0x3c, 0x00}  /* S */
    };
    if (!framebuffer_supported(framebuffer) ||
        framebuffer->width < TITLE_WIDTH + 2 * BORDER_PIXELS ||
        framebuffer->height < TITLE_TOP + TITLE_HEIGHT + BORDER_PIXELS) {
        return false;
    }
    uint64_t left = (framebuffer->width - TITLE_WIDTH) / 2;
    uint32_t white = pixel_color(framebuffer, 255, 255, 255);
    uint32_t black = pixel_color(framebuffer, 0, 0, 0);
    volatile uint8_t *pixels = framebuffer->address;
    for (unsigned int y = 0; y < TITLE_HEIGHT; ++y) {
        volatile uint32_t *row = (volatile uint32_t *)(
            pixels + (TITLE_TOP + y) * framebuffer->pitch);
        for (unsigned int x = 0; x < TITLE_WIDTH; ++x) {
            /* Integer division maps each 4x4 screen square to one font bit.
             * A character occupies 32 screen columns; its source row has 8 bits.
             * The mask walks from the highest (leftmost) bit toward the lowest. */
            unsigned int letter = x / (GLYPH_WIDTH * SCALE);
            unsigned int column = (x / SCALE) % GLYPH_WIDTH;
            uint8_t bits = glyphs[letter][y / SCALE];
            bool illuminated = (bits & (UINT8_C(0x80) >> column)) != 0;
            row[left + x] = illuminated ? white : black;
        }
    }
    return true;
}

/* Fill a nonempty rectangle after complete metadata/bounds validation.
 * Origin and size in pixels; rgb is logical 0xRRGGBB. False causes no writes.
 * Borrows mappings, no allocation, startup CPU only/no concurrent drawing. */
bool framebuffer_fill_rectangle(const struct limine_framebuffer *framebuffer,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height, uint32_t rgb)
{
    /* Subtraction-based bounds checks avoid overflowing x+width or y+height. */
    if (!framebuffer_supported(framebuffer) || width == 0 || height == 0 ||
        x > framebuffer->width || width > framebuffer->width - x ||
        y > framebuffer->height || height > framebuffer->height - y) {
        return false;
    }
    uint32_t color = pixel_color(framebuffer, (uint8_t)(rgb >> 16),
                                 (uint8_t)(rgb >> 8), (uint8_t)rgb);
    volatile uint8_t *pixels = framebuffer->address;
    for (uint64_t row_index = 0; row_index < height; ++row_index) {
        volatile uint32_t *row = (volatile uint32_t *)(
            pixels + (y + row_index) * framebuffer->pitch);
        for (uint64_t column = 0; column < width; ++column) {
            row[x + column] = color;
        }
    }
    return true;
}

/* Draw borrowed glyph rows into one 16x16 cell, with logical RGB colors.
 * False leaves screen untouched for invalid metadata/coordinates/NULL glyph.
 * Full cell/mapping must remain valid. No allocation; startup CPU only. */
bool framebuffer_draw_cell(const struct limine_framebuffer *framebuffer,
    uint64_t x, uint64_t y, const uint8_t glyph[8], uint32_t foreground,
    uint32_t background)
{
    enum { CELL_PIXELS = 16, GLYPH_SCALE = 2 };
    if (glyph == NULL || !framebuffer_supported(framebuffer) ||
        x > framebuffer->width || CELL_PIXELS > framebuffer->width - x ||
        y > framebuffer->height || CELL_PIXELS > framebuffer->height - y) {
        return false;
    }
    uint32_t ink = pixel_color(framebuffer, (uint8_t)(foreground >> 16),
                               (uint8_t)(foreground >> 8), (uint8_t)foreground);
    uint32_t paper = pixel_color(framebuffer, (uint8_t)(background >> 16),
                                 (uint8_t)(background >> 8), (uint8_t)background);
    volatile uint8_t *pixels = framebuffer->address;
    for (unsigned int py = 0; py < CELL_PIXELS; ++py) {
        volatile uint32_t *row = (volatile uint32_t *)(
            pixels + (y + py) * framebuffer->pitch);
        for (unsigned int px = 0; px < CELL_PIXELS; ++px) {
            bool set = (glyph[py / GLYPH_SCALE] & (UINT8_C(0x80) >> (px / GLYPH_SCALE))) != 0;
            row[x + px] = set ? ink : paper;
        }
    }
    return true;
}

/* Move only visible pixels upward within a rectangle; clear exposed bottom.
 * Pixel units, logical 0xRRGGBB background. False means no writes; zero amount
 * succeeds unchanged, height amount clears everything. Borrows mappings;
 * no heap, startup CPU only, no concurrent drawing or interrupt use. */
bool framebuffer_scroll_rectangle(const struct limine_framebuffer *framebuffer,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    uint64_t amount, uint32_t background)
{
    if (!framebuffer_supported(framebuffer) || width == 0 || height == 0 ||
        amount > height || x > framebuffer->width ||
        width > framebuffer->width - x || y > framebuffer->height ||
        height > framebuffer->height - y) {
        return false;
    }
    if (amount == 0) {
        return true;
    }
    volatile uint8_t *pixels = framebuffer->address;
    /* Source rows lie below destination rows. Copy top-to-bottom so a write
     * never overwrites pixels we still need to read (the regions overlap).
     * Respect pitch and viewport width; neither padding nor neighbors move. */
    for (uint64_t row_index = 0; row_index < height - amount; ++row_index) {
        volatile uint32_t *destination = (volatile uint32_t *)(
            pixels + (y + row_index) * framebuffer->pitch);
        const volatile uint32_t *source = (const volatile uint32_t *)(
            pixels + (y + row_index + amount) * framebuffer->pitch);
        for (uint64_t column = 0; column < width; ++column) {
            destination[x + column] = source[x + column];
        }
    }
    return framebuffer_fill_rectangle(framebuffer, x, y + height - amount,
                                       width, amount, background);
}

/* Copy a packed RGB24 image into a validated screen rectangle.
 * Pixel dimensions, byte_count bytes readable at borrowed rgb. False before
 * any writes on truncated data or invalid bounds. No ownership/allocation;
 * startup CPU only with stable descriptor, mapping and source data. */
bool framebuffer_draw_rgb_image(const struct limine_framebuffer *framebuffer,
    uint64_t x, uint64_t y, uint64_t width, uint64_t height,
    const uint8_t *rgb, size_t byte_count)
{
    if (rgb == NULL || !framebuffer_supported(framebuffer) ||
        width == 0 || height == 0 || width > SIZE_MAX / 3 ||
        height > SIZE_MAX / (width * 3) || byte_count < width * height * 3 ||
        x > framebuffer->width || width > framebuffer->width - x ||
        y > framebuffer->height || height > framebuffer->height - y) {
        return false;
    }
    volatile uint8_t *pixels = framebuffer->address;
    for (uint64_t py = 0; py < height; ++py) {
        volatile uint32_t *row = (volatile uint32_t *)(pixels + (y + py) * framebuffer->pitch);
        for (uint64_t px = 0; px < width; ++px) {
            /* Source rows have three bytes per pixel, destination rows may
             * include padding and use a different red/green/blue bit order. */
            size_t offset = (py * width + px) * 3;
            row[x + px] = pixel_color(framebuffer, rgb[offset], rgb[offset+1], rgb[offset+2]);
        }
    }
    return true;
}
