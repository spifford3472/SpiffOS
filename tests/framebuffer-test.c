/*
 * Purpose: exercise framebuffer memory bounds and metadata rejection on Forge.
 * Dependencies: host C library and the same drawing module used by the kernel.
 * Invariants: ordinary RAM only; no port I/O or privileged kernel entry.
 * References: ADR-011; docs/operations/framebuffer-2026-10-28.md.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limine.h>
#include "framebuffer.h"

enum { WIDTH = 40, HEIGHT = 36, STRIDE = 48, GUARD_WORDS = 8 };
#define SENTINEL UINT32_C(0xa5a5a5a5)
struct guarded_screen {
    uint32_t before[GUARD_WORDS];
    uint32_t pixels[HEIGHT][STRIDE];
    uint32_t after[GUARD_WORDS];
};

/* Verify guard words and all row padding; abort on corruption.
 * Borrows host RAM; read-only, single thread, no ownership transfer. */
static void check_guards(const struct guarded_screen *screen)
{
    for (unsigned int i = 0; i < GUARD_WORDS; ++i) {
        assert(screen->before[i] == SENTINEL);
        assert(screen->after[i] == SENTINEL);
    }
    for (unsigned int y = 0; y < HEIGHT; ++y) {
        for (unsigned int x = WIDTH; x < STRIDE; ++x) {
            assert(screen->pixels[y][x] == SENTINEL);
        }
    }
}

/* Test padded rows, two color layouts and rejection before writes.
 * No arguments; returns zero on success, assertion aborts on failure.
 * Host process only; stack-owned memory, no kernel/hardware calls. */
int main(void)
{
    struct guarded_screen screen;
    memset(&screen, 0xa5, sizeof(screen));
    struct limine_framebuffer framebuffer = {
        .address = screen.pixels, .width = WIDTH, .height = HEIGHT,
        .pitch = STRIDE * sizeof(uint32_t), .bpp = 32,
        .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8, .red_mask_shift = 16,
        .green_mask_size = 8, .green_mask_shift = 8,
        .blue_mask_size = 8, .blue_mask_shift = 0
    };
    assert(framebuffer_draw_boot_pattern(&framebuffer));
    check_guards(&screen);
    assert(screen.pixels[0][0] == UINT32_C(0xa0a0ff));
    assert(screen.pixels[18][16] == UINT32_C(0xff0000));
    assert(screen.pixels[18][19] == UINT32_C(0x00ff00));
    assert(screen.pixels[18][23] == UINT32_C(0x0000ff));
    assert(screen.pixels[HEIGHT - 1][WIDTH - 1] == UINT32_C(0xa0a0ff));

    framebuffer.red_mask_shift = 0;
    framebuffer.blue_mask_shift = 16;
    assert(framebuffer_draw_boot_pattern(&framebuffer));
    check_guards(&screen);
    assert(screen.pixels[0][0] == UINT32_C(0xffa0a0));
    assert(screen.pixels[18][16] == UINT32_C(0x0000ff));
    assert(screen.pixels[18][23] == UINT32_C(0xff0000));

    struct guarded_screen unchanged = screen;
    struct limine_framebuffer invalid;
    assert(!framebuffer_draw_boot_pattern(NULL));
    /* Each rejection must leave every byte of our simulated screen untouched. */
#define REJECT(field, value) do { \
        invalid = framebuffer; invalid.field = (value); \
        assert(!framebuffer_draw_boot_pattern(&invalid)); \
        assert(memcmp(&screen, &unchanged, sizeof(screen)) == 0); \
    } while (0)
    REJECT(address, NULL);
    REJECT(address, (uint8_t *)screen.pixels + 1);
    REJECT(bpp, 24);
    REJECT(memory_model, 0);
    REJECT(width, 0);
    REJECT(height, 32);
    REJECT(pitch, 0);
    REJECT(pitch, WIDTH * 4 - 1);
    REJECT(pitch, WIDTH * 4 + 1);
    REJECT(red_mask_size, 7);
    REJECT(red_mask_shift, 25);
    REJECT(red_mask_shift, 8); /* overlaps the green field */
    REJECT(width, UINT64_MAX);
    REJECT(height, UINT64_MAX);
    REJECT(address, (void *)(uintptr_t)(UINTPTR_MAX - 3));
#undef REJECT
    puts("PASS: framebuffer guards, row padding, RGB/BGR layouts and metadata rejection");
    return 0;
}
