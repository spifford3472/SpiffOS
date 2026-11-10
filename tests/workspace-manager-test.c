/* Purpose: hosted M7 independent editor and framebuffer snapshot regression.
 * Dependencies: workspace-manager, console, framebuffer and boot-logo sources.
 * Invariants: test stubs model validated memory-service owner changes only.
 * Reference: docs/decisions/ADR-m7-text-workspace-switching.md. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limine.h>
#include "workspace-manager.h"
#include "boot-logo.h"

static size_t selected = 1;
static size_t resets[9];
static bool deny_switch;
/* Simulate memory service selection and injected failure without display writes. */
bool memory_boot_select_workspace(size_t id)
{
    if (deny_switch || id < 1 || id > 9) return false;
    selected = id;
    return true;
}
/* Track owner-isolated resets, rejecting invalid IDs. */
bool memory_boot_reset_workspace(size_t id)
{
    if (id < 1 || id > 9) return false;
    ++resets[id - 1];
    return true;
}
/* A diagnostic submit callback never changes workspace ownership. */
static bool submit(void *ctx, const unsigned char *text, size_t length)
{
    (void)ctx; (void)text; (void)length;
    return true;
}
/* Compare the full framebuffer, not only editor metadata. */
static uint64_t checksum(const uint32_t *pixels, size_t count)
{
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) value = value * 33 + pixels[i];
    return value;
}
int main(void)
{
    enum { WIDTH = 480, HEIGHT = 272 };
    uint32_t *pixels = calloc(WIDTH * HEIGHT, sizeof *pixels);
    assert(pixels != NULL);
    struct limine_framebuffer fb = {
        .address = pixels, .width = WIDTH, .height = HEIGHT,
        .pitch = WIDTH * 4, .bpp = 32, .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8, .green_mask_size = 8, .blue_mask_size = 8,
        .red_mask_shift = 16, .green_mask_shift = 8, .blue_mask_shift = 0
    };
    struct text_console console = {0};
    assert(boot_workspace_reset(&console, &fb));
    assert(text_console_write(&console, "SPIFFOS WORKSPACE\nOUTPUT SCROLLS THE LOGO."));
    struct workspace_manager *manager = calloc(1, sizeof *manager);
    assert(manager != NULL);
    assert(workspace_manager_initialize(manager, &console, submit, NULL));
    /* Unsupported larger modes must retain the original editor rather than
     * turning an otherwise bootable display into KEYBOARD_UNAVAILABLE. */
    fb.width = WORKSPACE_MAX_WIDTH + 1;
    assert(!workspace_manager_switch(manager, 2));
    assert(workspace_manager_active(manager) != NULL);
    fb.width = WIDTH;
    assert(screen_editor_accept(workspace_manager_active(manager), SCREEN_EDITOR_CHARACTER, 'A') == SCREEN_EDITOR_ACCEPTED);
    assert(screen_editor_accept(workspace_manager_active(manager), SCREEN_EDITOR_TOGGLE_INSERT, 0) == SCREEN_EDITOR_ACCEPTED);
    uint64_t first = checksum(pixels, WIDTH * HEIGHT);
    assert(!workspace_manager_switch(manager, 0));
    assert(!workspace_manager_switch(manager, 10));
    assert(selected == 1);
    deny_switch = true;
    assert(!workspace_manager_switch(manager, 2));
    assert(selected == 1 && checksum(pixels, WIDTH * HEIGHT) == first);
    deny_switch = false;
    assert(workspace_manager_switch(manager, 2));
    assert(selected == 2 && !workspace_manager_active(manager)->insert_mode);
    assert(screen_editor_accept(workspace_manager_active(manager), SCREEN_EDITOR_CHARACTER, 'B') == SCREEN_EDITOR_ACCEPTED);
    uint64_t second = checksum(pixels, WIDTH * HEIGHT);
    assert(first != second);
    assert(workspace_manager_switch(manager, 1));
    assert(checksum(pixels, WIDTH * HEIGHT) == first);
    assert(workspace_manager_active(manager)->insert_mode);
    assert(workspace_manager_switch(manager, 2));
    assert(checksum(pixels, WIDTH * HEIGHT) == second);
    for (size_t id = 3; id <= 9; ++id) {
        assert(workspace_manager_switch(manager, id));
        assert(screen_editor_accept(workspace_manager_active(manager), SCREEN_EDITOR_CHARACTER,
            (unsigned char)('A' + id)) == SCREEN_EDITOR_ACCEPTED);
    }
    assert(workspace_manager_reset(manager, 2));
    assert(resets[1] == 1 && resets[0] == 0 && resets[8] == 0);
    assert(workspace_manager_switch(manager, 1));
    assert(checksum(pixels, WIDTH * HEIGHT) == first);
    assert(workspace_manager_switch(manager, 2));
    assert(!workspace_manager_active(manager)->insert_mode);
    assert(checksum(pixels, WIDTH * HEIGHT) != second);
    assert(workspace_manager_reset(manager, 2));
    assert(resets[1] == 2);
    assert(workspace_manager_switch(manager, 1));
    assert(checksum(pixels, WIDTH * HEIGHT) == first);
    free(manager);
    free(pixels);
    return 0;
}
