/* Purpose: advance a logical text cursor and render bounded character cells.
 * Dependencies: boot framebuffer primitives and original console font.
 * Invariants: state and framebuffer metadata/mappings remain valid and owned
 * by their callers; startup CPU only; no allocation or interrupts.
 * References: ADR-011 and docs/operations/console-2026-10-28.md. */
#include <stddef.h>
#include <stdint.h>
#include "framebuffer.h"
#include "console-font.h"
#include "text-console.h"

enum { CONSOLE_BACKGROUND = 0x091b35, CONSOLE_FOREGROUND = 0xa0a0ff };

/* Initialize and clear a viewport, with dimensions in cells and origin in pixels.
 * False disables this state without screen writes; successful initialization
 * borrows framebuffer metadata and mappings. No allocation; startup CPU only. */
bool text_console_initialize(struct text_console *console,
    const struct limine_framebuffer *framebuffer, uint64_t x, uint64_t y,
    uint64_t columns, uint64_t rows)
{
    if (console == NULL) {
        return false;
    }
    console->initialized = false;
    /* Validate multiplication before converting cell counts to screen pixels. */
    if (columns == 0 || rows == 0 || columns > UINT64_MAX / TEXT_CELL_PIXELS ||
        rows > UINT64_MAX / TEXT_CELL_PIXELS ||
        !framebuffer_fill_rectangle(framebuffer, x, y, columns * TEXT_CELL_PIXELS,
                                    rows * TEXT_CELL_PIXELS, CONSOLE_BACKGROUND)) {
        return false;
    }
    console->framebuffer = framebuffer;
    console->left = x;
    console->top = y;
    console->columns = columns;
    console->rows = rows;
    console->column = 0;
    console->row = 0;
    console->initialized = true;
    return true;
}

/* Consume one byte, scrolling a pending bottom row before output if needed.
 * A final cell/newline may set row == rows; next input makes space. False
 * indicates invalid state or a drawing failure; a string can leave a prefix.
 * No ownership transfer, allocations or concurrent/interrupt use. */
bool text_console_putc(struct text_console *console, unsigned char character)
{
    if (console == NULL || !console->initialized || console->row > console->rows) {
        return false;
    }
    if (console->row == console->rows) {
        /* Defer moving the last line until another byte actually needs room.
         * This keeps an exact-fill final character visible on the screen. */
        if (!framebuffer_scroll_rectangle(console->framebuffer,
            console->left, console->top, console->columns * TEXT_CELL_PIXELS,
            console->rows * TEXT_CELL_PIXELS, TEXT_CELL_PIXELS,
            CONSOLE_BACKGROUND)) {
            return false;
        }
        console->row = console->rows - 1;
        console->column = 0;
    }
    if (character == '\n') {
        console->column = 0;
        ++console->row;
        return true;
    }
    uint8_t glyph[8];
    for (unsigned int row = 0; row < 8; ++row) {
        glyph[row] = console_font_row(character, row);
    }
    if (!framebuffer_draw_cell(console->framebuffer,
        console->left + console->column * TEXT_CELL_PIXELS,
        console->top + console->row * TEXT_CELL_PIXELS,
        glyph, CONSOLE_FOREGROUND, CONSOLE_BACKGROUND)) {
        return false;
    }
    ++console->column;
    if (console->column == console->columns) {
        console->column = 0;
        ++console->row;
    }
    return true;
}

/* Print borrowed NUL-terminated text; false may leave a successfully printed
 * prefix. No allocations or ownership transfer. Startup CPU only. */
bool text_console_write(struct text_console *console, const char *text)
{
    if (console == NULL || !console->initialized || text == NULL) {
        return false;
    }
    for (; *text != '\0'; ++text) {
        if (!text_console_putc(console, (unsigned char)*text)) {
            return false;
        }
    }
    return true;
}

/* Move the logical cursor in cell units; never draw pixels or a logo.
 * False rejects invalid state/coordinates without changes. Startup-only,
 * caller-owned state, no allocation or ownership transfer. */
bool text_console_position(struct text_console *console, uint64_t column, uint64_t row)
{
    if (console == NULL || !console->initialized ||
        column >= console->columns || row >= console->rows) {
        return false;
    }
    console->column = column;
    console->row = row;
    return true;
}
