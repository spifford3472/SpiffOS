/*
 * Purpose: check fixed-label bounds, scaling and preservation of surrounding RAM.
 * Dependencies: host C library and the actual kernel framebuffer module.
 * Invariants: host RAM only; no kernel entry or privileged instructions.
 * References: ADR-011; docs/operations/text-2026-10-28.md.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limine.h>
#include "framebuffer.h"

enum { ROWS = 97, STRIDE = 264, GUARD_WORDS = 8 };
#define SENTINEL UINT32_C(0xa5a5a5a5)
struct guarded_screen {
    uint32_t before[GUARD_WORDS];
    uint32_t pixels[ROWS][STRIDE];
    uint32_t after[GUARD_WORDS];
};

/* Exercise exact-fit and odd-sized layouts, then reject invalid inputs.
 * No arguments, zero means success; assertions abort on failure.
 * Static host-owned buffers; no hardware access or concurrent use. */
int main(void)
{
    static struct guarded_screen screen;
    static struct guarded_screen unchanged;
    struct limine_framebuffer framebuffer = {
        .address = screen.pixels, .width = 256, .height = 96,
        .pitch = STRIDE * sizeof(uint32_t), .bpp = 32,
        .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8, .red_mask_shift = 16,
        .green_mask_size = 8, .green_mask_shift = 8,
        .blue_mask_size = 8, .blue_mask_shift = 0
    };
    for (unsigned int extra = 0; extra <= 1; ++extra) {
        framebuffer.width = 256 + extra;
        framebuffer.height = 96 + extra;
        memset(&screen, 0xa5, sizeof(screen));
        assert(framebuffer_draw_boot_title(&framebuffer));
        for (unsigned int i = 0; i < GUARD_WORDS; ++i) {
            assert(screen.before[i] == SENTINEL);
            assert(screen.after[i] == SENTINEL);
        }
        /* Both dimensions center the label at x=16. Every other pixel,
         * including row padding and any extra row, must retain its sentinel. */
        for (unsigned int y = 0; y < ROWS; ++y) {
            for (unsigned int x = 0; x < STRIDE; ++x) {
                if (y >= 48 && y < 80 && x >= 16 && x < 240) {
                    assert(screen.pixels[y][x] == 0 ||
                           screen.pixels[y][x] == UINT32_C(0xffffff));
                } else {
                    assert(screen.pixels[y][x] == SENTINEL);
                }
            }
        }
        /* S begins with 00111100. The first set bit must become a 4x4 block,
         * with the two preceding source columns remaining black. */
        for (unsigned int y = 48; y < 52; ++y) {
            assert(screen.pixels[y][16] == 0);
            assert(screen.pixels[y][23] == 0);
            for (unsigned int x = 24; x < 28; ++x) {
                assert(screen.pixels[y][x] == UINT32_C(0xffffff));
            }
        }
        for (unsigned int y = 48; y < 80; ++y) {
            assert(memcmp(&screen.pixels[y][112], &screen.pixels[y][144], 32 * sizeof(uint32_t)) == 0);
            assert(memcmp(&screen.pixels[y][16], &screen.pixels[y][208], 32 * sizeof(uint32_t)) == 0);
        }
        for (unsigned int x = 16; x < 240; ++x) {
            assert(screen.pixels[79][x] == 0); /* blank final glyph row */
        }
    }
    unchanged = screen;
    assert(!framebuffer_draw_boot_title(NULL));
    struct limine_framebuffer invalid = framebuffer;
    invalid.width = 255;
    assert(!framebuffer_draw_boot_title(&invalid));
    invalid = framebuffer; invalid.height = 95;
    assert(!framebuffer_draw_boot_title(&invalid));
    invalid = framebuffer; invalid.address = NULL;
    assert(!framebuffer_draw_boot_title(&invalid));
    invalid = framebuffer; invalid.bpp = 24;
    assert(!framebuffer_draw_boot_title(&invalid));
    assert(memcmp(&screen, &unchanged, sizeof(screen)) == 0);
    puts("PASS: title exact-fit/odd-size layout, guards, padding, scale and no-write rejection");
    return 0;
}
