/* Purpose: verify RGB image bounds, logo placement and neighboring memory.
 * Dependencies: host RAM, actual framebuffer/logo modules and generated asset.
 * Invariant: no hardware access or allocation. References: ADR-011 and
 * docs/operations/scroll-logo-2026-10-28.md. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limine.h>
#include "framebuffer.h"
#include "boot-logo.h"
#include "boot-logo-data.h"
#define SENTINEL UINT32_C(0xa5a5a5a5)
struct guarded { uint32_t before[8], pixels[288][456], after[8]; };

/* Check every logo/interior pixel plus guards/padding and rejected RGB input.
 * Returns zero on success; assertions abort. Static host-owned buffers only. */
int main(void)
{
    static struct guarded screen, saved;
    struct limine_framebuffer fb = {
        .address = screen.pixels, .width = 448, .height = 288, .pitch = 456*4,
        .bpp = 32, .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8, .red_mask_shift = 16,
        .green_mask_size = 8, .green_mask_shift = 8,
        .blue_mask_size = 8, .blue_mask_shift = 0
    };
    memset(&screen, 0xa5, sizeof(screen));
    assert(boot_logo_draw(&fb));
    for (unsigned int y = 0; y < 288; ++y) {
        for (unsigned int x = 0; x < 456; ++x) {
            uint32_t expected = SENTINEL;
            if (x < 448) {
                expected = 0x091b35;
            }
            if (y >= 24 && y < 184 && x >= 24 && x < 424) {
                unsigned int offset = ((y-24)*400 + x-24)*3;
                expected = ((uint32_t)boot_logo_rgb[offset] << 16) |
                           ((uint32_t)boot_logo_rgb[offset+1] << 8) |
                           boot_logo_rgb[offset+2];
            }
            assert(screen.pixels[y][x] == expected);
        }
    }
    for (unsigned int i = 0; i < 8; ++i) {
        assert(screen.before[i] == SENTINEL && screen.after[i] == SENTINEL);
    }
    saved = screen;
    const uint8_t tiny[12] = {255,0,0, 0,255,0, 0,0,255, 17,34,51};
    assert(!framebuffer_draw_rgb_image(&fb, 16, 16, 2, 2, tiny, 11));
    assert(!framebuffer_draw_rgb_image(&fb, UINT64_MAX, 0, 2, 2, tiny, 12));
    assert(!framebuffer_draw_rgb_image(&fb, 0, 0, UINT64_MAX, 2, tiny, 12));
    assert(!framebuffer_draw_rgb_image(&fb, 0, 0, 2, 2, NULL, 12));
    assert(!boot_logo_draw(NULL));
    fb.width = 431; assert(!boot_logo_draw(&fb)); fb.width = 448;
    fb.height = 263; assert(!boot_logo_draw(&fb)); fb.height = 288;
    assert(memcmp(&screen, &saved, sizeof(screen)) == 0);
    assert(framebuffer_draw_rgb_image(&fb, 16, 16, 2, 2, tiny, 12));
    assert(screen.pixels[16][16] == 0xff0000);
    assert(screen.pixels[16][17] == 0x00ff00);
    assert(screen.pixels[17][16] == 0x0000ff);
    assert(screen.pixels[17][17] == 0x112233);
    fb.red_mask_shift = 0; fb.blue_mask_shift = 16;
    assert(framebuffer_draw_rgb_image(&fb, 16, 16, 2, 2, tiny, 12));
    assert(screen.pixels[16][16] == 0x0000ff);
    assert(screen.pixels[17][17] == 0x332211);
    puts("PASS: logo every pixel/placement, RGB conversion, truncation, overflow and guards");
    return 0;
}
