/* Purpose: preserve nine independent diagnostic text editors and viewports.
 * Dependencies: screen editor, framebuffer, boot logo, retained memory service.
 * Invariants: one boot CPU, interrupts disabled, no heap or user processes.
 * Reference: docs/decisions/ADR-m7-text-workspace-switching.md. */
#ifndef SPIFFOS_WORKSPACE_MANAGER_H
#define SPIFFOS_WORKSPACE_MANAGER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
    bool system_visible;
    bool indicator_visible;
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
/* Immutable diagnostic snapshot assembled by the owning keyboard loop.
 * No clock or scheduler reads occur in the graphics manager/IRQ context.
 * Data is informational, not a scheduler command or control interface. */
struct workspace_system_diagnostics {
    bool clock_available;
    uint64_t uptime_ms;
    bool scheduler_available;
    uint64_t scheduler_ticks;
    bool diagnostic_preemption_build;
};

/* Toggle read-only system view, preserving current user pixels and editor. */
bool workspace_manager_system_with_diagnostics(struct workspace_manager *manager,
    const struct workspace_system_diagnostics *diagnostics);

bool workspace_manager_system(struct workspace_manager *manager);
/* Draw a large centered ID, then restore its exact pixels on hide. */
bool workspace_manager_identify(struct workspace_manager *manager);
void workspace_manager_hide_indicator(struct workspace_manager *manager);
/* Blend transient indicator toward saved pixels: step 1 or 2. */
void workspace_manager_fade_indicator(struct workspace_manager *manager, unsigned int step);
bool workspace_manager_switch(struct workspace_manager *manager, size_t id);
/* Return current editor borrowed until manager teardown. */
struct screen_editor *workspace_manager_active(struct workspace_manager *manager);
/* Clear only the requested editor, pixels and memory-service owner. The active
 * workspace is repainted; inactive targets reinitialize on next switch. */
bool workspace_manager_reset(struct workspace_manager *manager, size_t id);
#endif
