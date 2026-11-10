/* Purpose: preserve nine independent diagnostic text editors and viewports.
 * Dependencies: screen editor, framebuffer, boot logo, retained memory service.
 * Invariants: one boot CPU, interrupts disabled, no heap or user processes.
 * Reference: docs/decisions/ADR-m7-text-workspace-switching.md. */
#ifndef SPIFFOS_WORKSPACE_MANAGER_H
#define SPIFFOS_WORKSPACE_MANAGER_H
#include <stdbool.h>
#include <stddef.h>
#include "screen-editor.h"
#include "text-console.h"
struct limine_framebuffer;
/* Bound snapshots to 1280x800 32-bit pixels. Larger modes continue with
 * workspace 1, but reject switching rather than corrupting unrelated RAM. */
enum { WORKSPACE_COUNT = 9, WORKSPACE_MAX_WIDTH = 1280,
       WORKSPACE_MAX_HEIGHT = 800 };
struct workspace_manager {
    struct screen_editor editors[WORKSPACE_COUNT];
    struct text_console consoles[WORKSPACE_COUNT];
    bool ready[WORKSPACE_COUNT];
    bool snapshot_ready[WORKSPACE_COUNT];
    size_t active_id;
    const struct limine_framebuffer *framebuffer;
    logical_line_submit_fn submit;
    void *submit_context;
};
/* Initialize first editor over already drawn boot screen; no display reset. */
bool workspace_manager_initialize(struct workspace_manager *manager,
    const struct text_console *boot_console,
    logical_line_submit_fn submit, void *submit_context);
/* Switch to 1..9, preserving all visible pixels and editor state.
 * On unsupported framebuffer dimensions or bad ID, leaves active view intact. */
bool workspace_manager_switch(struct workspace_manager *manager, size_t id);
/* Return current editor borrowed until manager teardown. */
struct screen_editor *workspace_manager_active(struct workspace_manager *manager);
/* Clear only the requested editor, pixels and memory-service owner. The active
 * workspace is repainted; inactive targets reinitialize on next switch. */
bool workspace_manager_reset(struct workspace_manager *manager, size_t id);
#endif
