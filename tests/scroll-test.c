/* Purpose: verify overlapping viewport scrolling, clearing and guards.
 * Dependencies: host RAM and actual framebuffer/console implementation.
 * Invariant: no privileged instructions or allocation. References: ADR-011
 * and docs/operations/scroll-logo-2026-10-28.md. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limine.h>
#include "framebuffer.h"
#include "text-console.h"
#define SENTINEL UINT32_C(0xa5a5a5a5)
struct guarded { uint32_t before[8], pixels[80][88], after[8]; };

/* Exercise top-down overlap, pending wrap, one-row clear and invalid inputs.
 * Returns zero on success, assertions abort on failure. Static host-owned RAM;
 * no allocations, hardware operations or concurrency. */
int main(void)
{
    static struct guarded screen, saved;
    struct limine_framebuffer fb = {
        .address = screen.pixels, .width = 80, .height = 80, .pitch = 88*4,
        .bpp = 32, .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8, .red_mask_shift = 16,
        .green_mask_size = 8, .green_mask_shift = 8,
        .blue_mask_size = 8, .blue_mask_shift = 0
    };
    memset(&screen, 0xa5, sizeof(screen));
    for (unsigned int y = 8; y < 56; ++y) {
        for (unsigned int x = 8; x < 56; ++x) {
            screen.pixels[y][x] = y * 100 + x;
        }
    }
    saved = screen;
    assert(framebuffer_scroll_rectangle(&fb, 8, 8, 48, 48, 16, 0x202060));
    for (unsigned int y = 0; y < 80; ++y) {
        for (unsigned int x = 0; x < 88; ++x) {
            uint32_t expected = saved.pixels[y][x];
            if (x >= 8 && x < 56 && y >= 8 && y < 56) {
                expected = y < 40 ? saved.pixels[y+16][x] : 0x202060;
            }
            assert(screen.pixels[y][x] == expected);
        }
    }
    assert(memcmp(screen.before, saved.before, sizeof(screen.before)) == 0);
    assert(memcmp(screen.after, saved.after, sizeof(screen.after)) == 0);
    saved = screen;
    assert(framebuffer_scroll_rectangle(&fb, 8, 8, 48, 48, 0, 0));
    assert(!framebuffer_scroll_rectangle(&fb, 8, 8, 48, 48, 49, 0));
    assert(!framebuffer_scroll_rectangle(&fb, UINT64_MAX, 8, 48, 48, 16, 0));
    assert(memcmp(&screen, &saved, sizeof(screen)) == 0);

    struct text_console console;
    assert(text_console_initialize(&console, &fb, 8, 8, 3, 2));
    assert(text_console_write(&console, "ABCDEF"));
    saved = screen;
    assert(console.row == 2 && console.column == 0);
    assert(text_console_putc(&console, 'G'));
    assert(console.row == 1 && console.column == 1);
    for (unsigned int y = 8; y < 24; ++y) {
        assert(memcmp(&screen.pixels[y][8], &saved.pixels[y+16][8], 48*4) == 0);
    }
    for (unsigned int y = 24; y < 40; ++y) {
        for (unsigned int x = 24; x < 56; ++x) {
            assert(screen.pixels[y][x] == 0x091b35);
        }
    }
    assert(text_console_write(&console, "\n\nABCDEF\nG"));
    assert(console.row <= console.rows && console.column < console.columns);
    assert(text_console_initialize(&console, &fb, 8, 8, 3, 1));
    assert(text_console_write(&console, "ABC"));
    saved = screen;
    assert(text_console_putc(&console, '\n'));
    for (unsigned int y = 8; y < 24; ++y) {
        for (unsigned int x = 8; x < 56; ++x) {
            assert(screen.pixels[y][x] == 0x091b35);
        }
    }
    assert(text_console_putc(&console, 'Z'));
    for (unsigned int y = 0; y < 80; ++y) {
        for (unsigned int x = 0; x < 88; ++x) {
            if (x < 8 || x >= 56 || y < 8 || y >= 40) {
                assert(screen.pixels[y][x] == saved.pixels[y][x]);
            }
        }
    }
    puts("PASS: overlapping scroll, bottom clear, pending wrap/newline, one-row console and guards");
    return 0;
}
