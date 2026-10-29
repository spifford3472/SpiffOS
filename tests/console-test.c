/* Purpose: verify bounded console state and framebuffer writes in guarded RAM.
 * Dependencies: host C library and actual console/font/framebuffer modules.
 * Invariant: host-only, no kernel entry, port I/O or interrupts.
 * References: ADR-011 and docs/operations/console-2026-10-28.md. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limine.h>
#include "framebuffer.h"
#include "console-font.h"
#include "text-console.h"

enum { HEIGHT = 80, STRIDE = 88, WIDTH = 80 };
#define SENTINEL UINT32_C(0xa5a5a5a5)
struct guarded_screen { uint32_t before[8], pixels[HEIGHT][STRIDE], after[8]; };

/* Assert the 3x2 viewport is the sole modified region, including padding/guards.
 * Borrows screen; assertion aborts on failure. Pure host reads, single thread. */
static void check_bounds(const struct guarded_screen *screen)
{
    for (unsigned int i = 0; i < 8; ++i) {
        assert(screen->before[i] == SENTINEL && screen->after[i] == SENTINEL);
    }
    for (unsigned int y = 0; y < HEIGHT; ++y) {
        for (unsigned int x = 0; x < STRIDE; ++x) {
            if (x < 8 || x >= 56 || y < 8 || y >= 40) {
                assert(screen->pixels[y][x] == SENTINEL);
            }
        }
    }
}

/* Check all byte glyph lookups, then exercise cursor and guarded memory.
 * Returns zero on success; assertions abort. Static buffers owned by test,
 * no hardware access or concurrent use. */
int main(void)
{
    static struct guarded_screen screen, previous;
    struct limine_framebuffer fb = {
        .address = screen.pixels, .width = WIDTH, .height = HEIGHT,
        .pitch = STRIDE * sizeof(uint32_t), .bpp = 32,
        .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8, .red_mask_shift = 16,
        .green_mask_size = 8, .green_mask_shift = 8,
        .blue_mask_size = 8, .blue_mask_shift = 0
    };
    for (unsigned int c = 0; c < 256; ++c) {
        for (unsigned int row = 0; row < 8; ++row) {
            assert((console_font_row((unsigned char)c, row) & ~0x7c) == 0);
        }
        assert(console_font_row((unsigned char)c, 7) == 0);
        assert(console_font_row((unsigned char)c, 100) == 0);
    }
    assert(console_font_row('A', 0) == 0x38);
    assert(console_font_row(' ', 0) == 0);
    assert(console_font_row('a', 0) == console_font_row('?', 0));
    memset(&screen, 0xa5, sizeof(screen));
    struct text_console console;
    assert(text_console_initialize(&console, &fb, 8, 8, 3, 2));
    assert(console.column == 0 && console.row == 0);
    assert(screen.pixels[8][8] == UINT32_C(0x091b35));
    assert(text_console_putc(&console, 'A'));
    assert(console.column == 1 && console.row == 0);
    assert(screen.pixels[8][12] == UINT32_C(0xa0a0ff));
    assert(screen.pixels[9][13] == UINT32_C(0xa0a0ff)); /* 2x2 scale */
    assert(text_console_write(&console, "BC"));
    assert(console.column == 0 && console.row == 1); /* immediate wrap */
    previous = screen;
    assert(text_console_putc(&console, '\n'));
    assert(console.column == 0 && console.row == 2);
    assert(memcmp(&screen, &previous, sizeof(screen)) == 0);
    assert(text_console_putc(&console, 'Z'));
    assert(console.column == 1 && console.row == 1); /* pending row scrolled */

    assert(text_console_initialize(&console, &fb, 8, 8, 3, 2));
    assert(text_console_write(&console, "A\nB"));
    assert(console.column == 1 && console.row == 1);
    assert(text_console_initialize(&console, &fb, 8, 8, 3, 2));
    assert(text_console_write(&console, "ABCDEF"));
    assert(console.column == 0 && console.row == 2); /* exact final cell */
    assert(text_console_initialize(&console, &fb, 8, 8, 3, 2));
    assert(text_console_write(&console, "ABCDEFG")); /* output continues by scrolling */
    assert(console.column == 1 && console.row == 1);
    assert(text_console_initialize(&console, &fb, 8, 8, 3, 2));
    assert(text_console_write(&console, "ABC\n")); /* wrapped row then newline */
    assert(console.row == 2 && console.column == 0);

    assert(text_console_initialize(&console, &fb, 8, 8, 3, 2));
    assert(text_console_putc(&console, '?'));
    assert(text_console_putc(&console, 'a'));
    assert(text_console_putc(&console, 255));
    for (unsigned int y = 8; y < 24; ++y) {
        assert(memcmp(&screen.pixels[y][8], &screen.pixels[y][24], 16 * sizeof(uint32_t)) == 0);
        assert(memcmp(&screen.pixels[y][8], &screen.pixels[y][40], 16 * sizeof(uint32_t)) == 0);
    }
    previous = screen;
    assert(!text_console_write(&console, NULL));
    assert(!text_console_write(NULL, "A"));
    assert(!text_console_putc(NULL, 'A'));
    assert(!text_console_initialize(NULL, &fb, 8, 8, 3, 2));
    assert(!text_console_initialize(&console, &fb, 8, 8, 0, 2));
    assert(!text_console_putc(&console, 'A'));
    assert(!text_console_initialize(&console, &fb, 8, 8, UINT64_MAX, 2));
    assert(!text_console_initialize(&console, &fb, UINT64_MAX, 8, 3, 2));
    assert(!text_console_initialize(&console, &fb, 8, 70, 3, 2));
    assert(!text_console_initialize(&console, NULL, 8, 8, 3, 2));
    assert(!framebuffer_fill_rectangle(&fb, 0, 0, UINT64_MAX, 1, 0));
    uint8_t glyph[8] = {0};
    assert(!framebuffer_draw_cell(&fb, WIDTH - 15, 0, glyph, 0, 0));
    assert(!framebuffer_draw_cell(&fb, 0, HEIGHT - 15, glyph, 0, 0));
    assert(!framebuffer_draw_cell(&fb, 0, 0, NULL, 0, 0));
    assert(memcmp(&screen, &previous, sizeof(screen)) == 0);
    check_bounds(&screen);

    /* The same logical colors must pack correctly when red/blue are reversed. */
    fb.red_mask_shift = 0; fb.blue_mask_shift = 16;
    assert(text_console_initialize(&console, &fb, 8, 8, 3, 2));
    assert(screen.pixels[8][8] == UINT32_C(0x351b09));
    assert(text_console_putc(&console, 'A'));
    assert(screen.pixels[8][12] == UINT32_C(0xffa0a0));
    check_bounds(&screen);
    puts("PASS: console cursor, wrap, newline, pending scroll, replacement, bounds, padding and colors");
    return 0;
}
