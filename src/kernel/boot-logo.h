/* Purpose: draw the original SpiffOS power-up logo, using embedded RGB data.
 * Dependencies: borrowed Limine framebuffer and validated framebuffer helpers.
 * Invariant: no runtime SVG/fonts/decoder/heap. References: ADR-011 and
 * docs/operations/scroll-logo-2026-10-28.md. */
#ifndef SPIFFOS_BOOT_LOGO_H
#define SPIFFOS_BOOT_LOGO_H
#include <stdbool.h>
struct limine_framebuffer;
struct text_console;
/* Clear navy display and draw centered original logo at y=24. Require 432x264
 * or larger. Borrow stable descriptor/mappings; no allocation. Startup-only.
 * False for missing/unsupported screen. The background covers the whole display. */
bool boot_logo_draw(const struct limine_framebuffer *framebuffer);
/* Explicit boot/workspace reset: clear the full display, draw initial logo,
 * position text below it. No permanently reserved rows. Borrow mappings and
 * caller-owned console; false disables state. Startup-only, no allocation.
 * Scrolling never invokes this function; no user reset key is wired yet. */
bool boot_workspace_reset(struct text_console *console,
    const struct limine_framebuffer *framebuffer);
#endif
