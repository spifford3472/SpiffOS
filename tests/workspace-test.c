/* Purpose: prove logo scrolls as content, stays gone and returns only on reset.
 * Dependencies: actual console, logo, font and framebuffer modules in host RAM.
 * Invariants: guarded padded buffers, no hardware/heap. References: ADR-011
 * and docs/operations/workspace-logo-2026-10-28.md. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limine.h>
#include "framebuffer.h"
#include "boot-logo.h"
#include "text-console.h"
#define NAVY UINT32_C(0x091b35)
#define SENTINEL UINT32_C(0xa5a5a5a5)
struct guarded { uint32_t before[8], pixels[288][456], after[8]; };

/* Check reset, exact first-scroll copy, disappearance, reuse and reset restore.
 * Zero returns success; assertions abort. Static host RAM only, single thread. */
int main(void)
{
    static struct guarded screen, initial;
    memset(&screen, 0xa5, sizeof(screen));
    struct limine_framebuffer fb = {
        .address = screen.pixels, .width = 448, .height = 288, .pitch = 456*4,
        .bpp = 32, .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8, .red_mask_shift = 16,
        .green_mask_size = 8, .green_mask_shift = 8,
        .blue_mask_size = 8, .blue_mask_shift = 0
    };
    struct text_console console;
    assert(boot_workspace_reset(&console, &fb));
    assert(console.left == 0 && console.top == 0);
    assert(console.columns == 28 && console.rows == 18);
    assert(console.row == 13 && console.column == 0);
    assert(screen.pixels[0][0] == NAVY && screen.pixels[287][447] == NAVY);
    initial = screen;
    assert(!text_console_position(&console, 28, 0));
    assert(!text_console_position(&console, 0, 18));
    assert(console.row == 13 && console.column == 0);
    assert(text_console_position(&console, 0, console.rows - 1));
    assert(text_console_putc(&console, '\n'));
    assert(text_console_putc(&console, '\n')); /* exactly one scroll, no drawing */
    for (unsigned int y = 0; y < 288; ++y) {
        for (unsigned int x = 0; x < 448; ++x) {
            assert(screen.pixels[y][x] == (y < 272 ? initial.pixels[y+16][x] : NAVY));
        }
    }
    /* Repeated scrolling clears the whole bitmap, then leaves it absent. */
    for (unsigned int pass = 0; pass < 2; ++pass) {
        for (uint64_t row = 0; row < console.rows * 2; ++row) {
            assert(text_console_putc(&console, '\n'));
        }
        for (unsigned int y = 0; y < 288; ++y) {
            for (unsigned int x = 0; x < 448; ++x) {
                assert(screen.pixels[y][x] == NAVY);
            }
        }
    }
    /* The former header and screen edges are ordinary usable character cells. */
    assert(text_console_position(&console, 0, 0));
    assert(text_console_putc(&console, 'A'));
    assert(screen.pixels[0][4] == 0xa0a0ff);
    assert(text_console_position(&console, 27, 17));
    assert(text_console_putc(&console, 'Z'));
    assert(screen.pixels[272][436] != NAVY);
    for (unsigned int y = 0; y < 288; ++y) {
        for (unsigned int x = 448; x < 456; ++x) {
            assert(screen.pixels[y][x] == SENTINEL);
        }
    }
    for (unsigned int i = 0; i < 8; ++i) {
        assert(screen.before[i] == SENTINEL && screen.after[i] == SENTINEL);
    }
    assert(boot_workspace_reset(&console, &fb));
    assert(memcmp(&screen, &initial, sizeof(screen)) == 0);
    assert(console.row == 13 && console.column == 0);
    puts("PASS: full-screen uniform workspace, scrolling logo, no reappearance, cell reuse and explicit reset");
    return 0;
}
