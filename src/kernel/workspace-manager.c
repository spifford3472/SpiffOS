/* Purpose: implement bounded M7 diagnostic workspace state and screen images.
 * Dependencies: Limine framebuffer, boot-logo, memory-boot and screen-editor.
 * Invariants: exclusive boot CPU, IRQs disabled, fixed BSS; no heap/BASIC.
 * Reference: docs/decisions/ADR-m7-text-workspace-switching.md. */
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include "workspace-manager.h"
#include "boot-logo.h"
#include "memory-boot.h"

/* Each workspace owns exactly one snapshot; raw pixels preserve the original
 * boot logo, scrollback view and XOR cursor shape without reverse rendering.
 * This bounded static storage is not the one-page M4 heap. */
static uint32_t workspace_pixels[WORKSPACE_COUNT]
    [WORKSPACE_MAX_HEIGHT][WORKSPACE_MAX_WIDTH];

/* Verify physical row bounds before touching mapped device memory. A rejected
 * large video mode cannot partially overwrite a snapshot. */
static bool snapshot_supported(const struct limine_framebuffer *fb)
{
    return fb != NULL && fb->address != NULL && fb->bpp == 32 &&
        fb->width != 0 && fb->height != 0 &&
        fb->width <= WORKSPACE_MAX_WIDTH && fb->height <= WORKSPACE_MAX_HEIGHT &&
        fb->pitch >= fb->width * 4 && fb->pitch % 4 == 0;
}

/* Copy only visible 32-bit words, excluding pitch padding. Both endpoints are
 * owned by this manager; framebuffer memory is volatile and loader-mapped. */
static void snapshot_transfer(const struct limine_framebuffer *fb,
                              size_t index, bool capture)
{
    volatile uint8_t *base = fb->address;
    for (size_t y = 0; y < fb->height; ++y) {
        volatile uint32_t *physical =
            (volatile uint32_t *)(base + y * fb->pitch);
        for (size_t x = 0; x < fb->width; ++x) {
            if (capture) workspace_pixels[index][y][x] = physical[x];
            else physical[x] = workspace_pixels[index][y][x];
        }
    }
}

/* Establish workspace 1 using the already initialized console and pixels.
 * The editor owns its internal line storage and never moves afterward. */
bool workspace_manager_initialize(struct workspace_manager *manager,
    const struct text_console *boot_console,
    logical_line_submit_fn submit, void *submit_context)
{
    if (manager == NULL || boot_console == NULL || !boot_console->initialized ||
        submit == NULL || !snapshot_supported(boot_console->framebuffer))
        return false;
    manager->framebuffer = boot_console->framebuffer;
    manager->consoles[0] = *boot_console;
    manager->submit = submit;
    manager->submit_context = submit_context;
    manager->active_id = 1;
    static const unsigned char first[] = "SPIFFOS WORKSPACE";
    static const unsigned char second[] = "OUTPUT SCROLLS THE LOGO.";
    if (!screen_editor_initialize(&manager->editors[0], &manager->consoles[0],
                                  submit, submit_context) ||
        !screen_editor_seed_line(&manager->editors[0], 13, first, sizeof first - 1) ||
        !screen_editor_seed_line(&manager->editors[0], 14, second, sizeof second - 1))
        return false;
    manager->ready[0] = true;
    return true;
}

/* Prepare a new editor only after verifying snapshot bounds and target ID.
 * Existing targets require no initialization and keep every logical line. */
bool workspace_manager_switch(struct workspace_manager *manager, size_t id)
{
    if (manager == NULL || id < 1 || id > WORKSPACE_COUNT ||
        !snapshot_supported(manager->framebuffer) || manager->active_id < 1 ||
        manager->active_id > WORKSPACE_COUNT || !manager->ready[manager->active_id - 1])
        return false;
    if (id == manager->active_id) return true;
    size_t previous = manager->active_id - 1;
    size_t target = id - 1;
    snapshot_transfer(manager->framebuffer, previous, true);
    manager->snapshot_ready[previous] = true;
    bool newly_initialized = !manager->ready[target];
    if (newly_initialized) {
        if (!boot_workspace_reset(&manager->consoles[target], manager->framebuffer) ||
            !screen_editor_initialize(&manager->editors[target],
                &manager->consoles[target], manager->submit, manager->submit_context)) {
            snapshot_transfer(manager->framebuffer, previous, false);
            return false;
        }
        manager->ready[target] = true;
    } else if (manager->snapshot_ready[target]) {
        snapshot_transfer(manager->framebuffer, target, false);
    } else {
        snapshot_transfer(manager->framebuffer, previous, false);
        return false;
    }
    if (!memory_boot_select_workspace(id)) {
        if (newly_initialized) manager->ready[target] = false;
        snapshot_transfer(manager->framebuffer, previous, false);
        return false;
    }
    manager->active_id = id;
    return true;
}

/* Borrow current initialized editor; never copy its self-referential pointers. */
struct screen_editor *workspace_manager_active(struct workspace_manager *manager)
{
    if (manager == NULL || manager->active_id < 1 ||
        manager->active_id > WORKSPACE_COUNT ||
        !manager->ready[manager->active_id - 1]) return NULL;
    return &manager->editors[manager->active_id - 1];
}

/* Recreate one workspace's blank boot view without modifying any other owner.
 * The retained memory service performs its own validated owner reset first. */
bool workspace_manager_reset(struct workspace_manager *manager, size_t id)
{
    if (manager == NULL || id < 1 || id > WORKSPACE_COUNT ||
        !snapshot_supported(manager->framebuffer) ||
        !memory_boot_reset_workspace(id)) return false;
    size_t target = id - 1;
    manager->ready[target] = false;
    manager->snapshot_ready[target] = false;
    if (manager->active_id != id) return true;
    if (!boot_workspace_reset(&manager->consoles[target], manager->framebuffer) ||
        !screen_editor_initialize(&manager->editors[target],
            &manager->consoles[target], manager->submit, manager->submit_context))
        return false;
    manager->ready[target] = true;
    return true;
}
