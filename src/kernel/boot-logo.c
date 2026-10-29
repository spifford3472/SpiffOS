/* Purpose: display bitmap derived from assets/branding/spiffos-logo.svg.
 * Dependencies: generated immutable RGB bytes and framebuffer helpers.
 * Invariants: boot CPU only, borrowed mappings, no decoder/font library/heap.
 * References: ADR-011 and docs/operations/scroll-logo-2026-10-28.md. */
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include "framebuffer.h"
#include "boot-logo.h"
#include "text-console.h"
#include "boot-logo-data.h"

/* Clear the power-up interior and draw the original fixed-size logo.
 * Borrow descriptor/mapping, return false if unavailable/too small. No heap,
 * ownership transfer or concurrent/interrupt use. No intentional boot delay. */
bool boot_logo_draw(const struct limine_framebuffer *framebuffer)
{
    if (framebuffer == NULL || framebuffer->width < BOOT_LOGO_WIDTH + 32 ||
        framebuffer->height < 264) {
        return false;
    }
    return framebuffer_fill_rectangle(framebuffer, 0, 0,
               framebuffer->width, framebuffer->height, 0x091b35) &&
           framebuffer_draw_rgb_image(framebuffer,
               (framebuffer->width - BOOT_LOGO_WIDTH) / 2, 24,
               BOOT_LOGO_WIDTH, BOOT_LOGO_HEIGHT, boot_logo_rgb, sizeof(boot_logo_rgb));
}

/* Reset one boot workspace, making the logo ordinary initial pixel content.
 * Caller owns console, borrows descriptor/mappings. False disables state;
 * full display uses whole cells with no header reservation. No allocation,
 * delay, concurrent use or user input handling. Only explicit reset/boot calls. */
bool boot_workspace_reset(struct text_console *console,
    const struct limine_framebuffer *framebuffer)
{
    if (console == NULL) {
        return false;
    }
    console->initialized = false;
    if (framebuffer == NULL || framebuffer->width < BOOT_LOGO_WIDTH + 32 ||
        framebuffer->height < 264) {
        return false;
    }
    /* The original logo ends at y=184. Row 13 begins at 13*16=208.
     * Initial cursor placement skips the image without excluding those rows
     * from the viewport: future scrolls and output can use every cell. */
    if (!text_console_initialize(console, framebuffer, 0, 0,
            framebuffer->width / TEXT_CELL_PIXELS,
            framebuffer->height / TEXT_CELL_PIXELS) ||
        !boot_logo_draw(framebuffer) || !text_console_position(console, 0, 13)) {
        console->initialized = false;
        return false;
    }
    return true;
}
