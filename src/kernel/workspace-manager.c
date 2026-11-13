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
#include "framebuffer.h"
#include "console-font.h"

/* Borrow per-page HHDM snapshots, not a 35MiB contiguous boot-time BSS.
 * Page backing is obtained atomically before any pixel mutation. */
enum { SNAPSHOT_PIXELS_PER_PAGE = 1024,
       SNAPSHOT_PAGE_COUNT = WORKSPACE_MAX_HEIGHT * WORKSPACE_MAX_WIDTH /
                             SNAPSHOT_PIXELS_PER_PAGE };
static uint32_t *workspace_pages[WORKSPACE_COUNT][SNAPSHOT_PAGE_COUNT];

/* Save only the indicator's center rectangle, never editor backing storage.
 * At 8x8 font pixels scaled 16x, its 128x128 region is bounded. */
enum { INDICATOR_SIZE = 128 };
static uint32_t indicator_pixels[INDICATOR_SIZE][INDICATOR_SIZE];

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
static bool snapshot_transfer(const struct limine_framebuffer *fb,
                              size_t index, bool capture)
{
    if (!snapshot_supported(fb) || index >= WORKSPACE_COUNT) return false;
    size_t last = ((size_t)fb->height - 1) * WORKSPACE_MAX_WIDTH +
                  (size_t)fb->width;
    size_t pages = (last + SNAPSHOT_PIXELS_PER_PAGE - 1) /
                   SNAPSHOT_PIXELS_PER_PAGE;
    if (pages == 0 || pages > SNAPSHOT_PAGE_COUNT ||
        !memory_boot_snapshot_prepare(index + 1, pages,
                                      workspace_pages[index])) return false;
    volatile uint8_t *base = fb->address;
    for (size_t y = 0; y < fb->height; ++y) {
        volatile uint32_t *physical =
            (volatile uint32_t *)(base + y * fb->pitch);
        size_t offset = y * WORKSPACE_MAX_WIDTH;
        for (size_t x = 0; x < fb->width; ++x) {
            size_t pixel = offset + x;
            uint32_t *backing =
                &workspace_pages[index][pixel / SNAPSHOT_PIXELS_PER_PAGE]
                                      [pixel % SNAPSHOT_PIXELS_PER_PAGE];
            if (capture) *backing = physical[x];
            else physical[x] = *backing;
        }
    }
    return true;
}

/* Establish workspace 1 using the already initialized console and pixels.
 * The editor owns its internal line storage and never moves afterward. */
bool workspace_manager_initialize(struct workspace_manager *manager,
    const struct text_console *boot_console,
    logical_line_submit_fn submit, void *submit_context)
{
    if (manager == NULL || boot_console == NULL || !boot_console->initialized ||
        submit == NULL || boot_console->framebuffer == NULL)
        return false;
    manager->framebuffer = boot_console->framebuffer;
    manager->consoles[0] = *boot_console;
    manager->submit = submit;
    manager->submit_context = submit_context;
    manager->active_id = 1;
    manager->system_visible = false;
    manager->indicator_visible = false;
    static const unsigned char first[] = "SPIFFOS WORKSPACE";
    static const unsigned char second[] = "OUTPUT SCROLLS THE LOGO.";
    if (!screen_editor_initialize(&manager->editors[0], &manager->consoles[0],
                                  submit, submit_context) ||
        !screen_editor_seed_line(&manager->editors[0], 13, first, sizeof first - 1) ||
        !screen_editor_seed_line(&manager->editors[0], 14, second, sizeof second - 1))
        return false;
    manager->ready[0] = true;
    /* Prepare every bounded workspace's logical identity at boot. Only
     * workspace 1 has a painted editor; offscreen consoles are descriptive
     * metadata and MUST NOT draw into the physical framebuffer here.
     * A first F2–F9 activation still initializes its own editor and pixels
     * transactionally after successful snapshot allocation. */
    for (size_t i = 0; i < WORKSPACE_COUNT; ++i) {
        if (i != 0) {
            manager->consoles[i] = *boot_console;
            manager->editors[i].initialized = false;
            manager->ready[i] = false;
            manager->snapshot_ready[i] = false;
        }
        manager->prepared[i] = true;
    }
    return true;
}

/* Restore the exact original 128x128 region, including any cursor overlay.
 * This operation is idempotent and requires no allocation or interrupts. */
void workspace_manager_hide_indicator(struct workspace_manager *manager)
{
    if (manager == NULL || !manager->indicator_visible) return;
    const struct limine_framebuffer *fb = manager->framebuffer;
    size_t x0 = (fb->width - INDICATOR_SIZE) / 2;
    size_t y0 = (fb->height - INDICATOR_SIZE) / 2;
    volatile uint8_t *base = fb->address;
    for (size_t y = 0; y < INDICATOR_SIZE; ++y) {
        volatile uint32_t *row = (volatile uint32_t *)(base + (y0+y)*fb->pitch);
        for (size_t x = 0; x < INDICATOR_SIZE; ++x)
            row[x0+x] = indicator_pixels[y][x];
    }
    manager->indicator_visible = false;
}

/* Render an opaque centered 8x8 bitmap digit at 16x pixel scale.
 * The saved rectangle is restored before any editor or system mutation. */
bool workspace_manager_identify(struct workspace_manager *manager)
{
    if (manager == NULL || manager->indicator_visible ||
        !snapshot_supported(manager->framebuffer) ||
        manager->framebuffer->width < INDICATOR_SIZE ||
        manager->framebuffer->height < INDICATOR_SIZE) return false;
    const struct limine_framebuffer *fb = manager->framebuffer;
    size_t x0 = (fb->width - INDICATOR_SIZE) / 2;
    size_t y0 = (fb->height - INDICATOR_SIZE) / 2;
    volatile uint8_t *base = fb->address;
    for (size_t y = 0; y < INDICATOR_SIZE; ++y) {
        volatile uint32_t *row = (volatile uint32_t *)(base + (y0+y)*fb->pitch);
        for (size_t x = 0; x < INDICATOR_SIZE; ++x)
            indicator_pixels[y][x] = row[x0+x];
    }
    unsigned char digit = (unsigned char)('0' + manager->active_id);
    for (size_t y = 0; y < INDICATOR_SIZE; ++y) {
        uint8_t bits = console_font_row(digit, (unsigned int)(y/16));
        volatile uint32_t *row = (volatile uint32_t *)(base + (y0+y)*fb->pitch);
        for (size_t x = 0; x < INDICATOR_SIZE; ++x)
            row[x0+x] = (bits & (0x80u >> (x/16))) ? 0x00ffffffu : 0x00112244u;
    }
    manager->indicator_visible = true;
    return true;
}

/* Fade toward the exact saved framebuffer without altering editor state.
 * step 1 retains 2/3 overlay; step 2 retains 1/3 overlay. Full restoration
 * is performed by hide_indicator at the deadline. RGB channel blending uses
 * integer arithmetic bounded to 0..255, with no heap or interrupts. */
void workspace_manager_fade_indicator(struct workspace_manager *manager,
                                      unsigned int step)
{
    if (manager == NULL || !manager->indicator_visible || step < 1 || step > 2)
        return;
    const struct limine_framebuffer *fb = manager->framebuffer;
    size_t x0 = (fb->width - INDICATOR_SIZE) / 2;
    size_t y0 = (fb->height - INDICATOR_SIZE) / 2;
    volatile uint8_t *base = fb->address;
    unsigned int opacity = 3 - step;
    unsigned char digit = (unsigned char)('0' + manager->active_id);
    for (size_t y = 0; y < INDICATOR_SIZE; ++y) {
        uint8_t bits = console_font_row(digit, (unsigned int)(y/16));
        volatile uint32_t *row = (volatile uint32_t *)(base + (y0+y)*fb->pitch);
        for (size_t x = 0; x < INDICATOR_SIZE; ++x) {
            uint32_t original = indicator_pixels[y][x];
            uint32_t overlay = (bits & (0x80u >> (x/16))) ?
                0x00ffffffu : 0x00112244u;
            uint32_t blended = 0;
            for (unsigned int shift = 0; shift <= 16; shift += 8) {
                unsigned int from = (original >> shift) & 255u;
                unsigned int to = (overlay >> shift) & 255u;
                blended |= ((from * (3 - opacity) + to * opacity) / 3u) << shift;
            }
            row[x0+x] = blended;
        }
    }
}

/* Render a decimal count without libc/sprintf, heap allocation or any
 * writes outside the borrowed initialized system-view console. Only the
 * foreground boot CPU calls this outside IRQ context. */
static bool system_write_unsigned(struct text_console *console, uint64_t value)
{
    char reversed[21];
    char digits[21];
    size_t count=0;
    do {
        reversed[count++]=(char)('0'+(value%10u));
        value/=10u;
    } while(value!=0 && count<sizeof reversed-1);
    for(size_t i=0;i<count;++i) digits[i]=reversed[count-1-i];
    digits[count]='\0';
    return text_console_write(console,digits);
}

/* F10 renders a bounded read-only diagnostic snapshot. Report only actual
 * workspace metadata and timer observations supplied by the boot caller.
 * A missing timer source is NOT zero ticks; it is shown as unavailable.
 * Existing F10 calls remain supported through the compatibility wrapper.
 * The view's drawing occurs only after the foreground pixels are preserved. */
bool workspace_manager_system_diagnostics(struct workspace_manager *manager,
    bool clock_present, uint64_t ticks)
{
    if (manager == NULL || !snapshot_supported(manager->framebuffer)) return false;
    workspace_manager_hide_indicator(manager);
    if (manager->system_visible) return true;
    size_t index = manager->active_id - 1;
    if (!snapshot_transfer(manager->framebuffer, index, true)) return false;
    manager->snapshot_ready[index] = true;
    struct text_console system_console = {0};
    const struct limine_framebuffer *fb = manager->framebuffer;
    size_t prepared=0,ready=0,snapshots=0;
    for (size_t i=0;i<WORKSPACE_COUNT;++i) {
        if(manager->prepared[i]) ++prepared;
        if(manager->ready[i]) ++ready;
        if(manager->snapshot_ready[i]) ++snapshots;
    }
    bool displayed =
        text_console_initialize(&system_console, fb, 0, 0,
            fb->width / TEXT_CELL_PIXELS, fb->height / TEXT_CELL_PIXELS) &&
        text_console_write(&system_console,
            "SPIFFOS SYSTEM MONITOR (READ ONLY)\n\nACTIVE WORKSPACE: ") &&
        system_write_unsigned(&system_console,manager->active_id) &&
        text_console_write(&system_console," / 9\nPREPARED WORKSPACES: ") &&
        system_write_unsigned(&system_console,prepared) &&
        text_console_write(&system_console," / 9\nINITIALIZED EDITORS: ") &&
        system_write_unsigned(&system_console,ready) &&
        text_console_write(&system_console," / 9\nFRAMEBUFFER SNAPSHOTS: ") &&
        system_write_unsigned(&system_console,snapshots) &&
        text_console_write(&system_console," / 9\nIRQ0 TICKS: ") &&
        (!clock_present ||
            system_write_unsigned(&system_console,ticks)) &&
        (clock_present ||
            text_console_write(&system_console,"UNAVAILABLE")) &&
        text_console_write(&system_console,
            "\nTASK CPU LOAD: UNAVAILABLE\n"
            "D3 PREEMPTION: DIAGNOSTIC TEST ONLY\n\n"
            "F1-F9: WORKSPACES\nF10: SYSTEM SCREEN\n"
            "F12: SHOW WORKSPACE NUMBER");
    if (!displayed) {
        (void)snapshot_transfer(fb,index,false);
        return false;
    }
    manager->system_visible = true;
    return true;
}

/* Retain the established API for older hosted tests; no live IRQ source
 * is implied, so its tick field is explicitly unavailable. */
bool workspace_manager_system(struct workspace_manager *manager)
{
    return workspace_manager_system_diagnostics(manager,false,0);
}

/* Prepare a new editor only after verifying snapshot bounds and target ID.
 * Existing targets require no initialization and keep every logical line. */
bool workspace_manager_switch(struct workspace_manager *manager, size_t id)
{
    if (manager == NULL || id < 1 || id > WORKSPACE_COUNT ||
        !snapshot_supported(manager->framebuffer) || manager->active_id < 1 ||
        manager->active_id > WORKSPACE_COUNT || !manager->ready[manager->active_id - 1])
        return false;
    workspace_manager_hide_indicator(manager);
    if (manager->system_visible) {
        if (!snapshot_transfer(manager->framebuffer, manager->active_id - 1, false))
            return false;
        manager->system_visible = false;
    }
    if (id == manager->active_id) return true;
    size_t previous = manager->active_id - 1;
    size_t target = id - 1;
    if (!snapshot_transfer(manager->framebuffer, previous, true)) return false;
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
        if (!snapshot_transfer(manager->framebuffer, target, false))
            return false;
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
    workspace_manager_hide_indicator(manager);
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
