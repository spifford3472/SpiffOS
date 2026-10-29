/* Purpose: small bounded boot console with a logical row/column cursor.
 * Dependencies: borrowed Limine framebuffer, framebuffer primitives and font.
 * Invariants: startup CPU only, no allocation/interrupts/concurrent use. The
 * caller owns state; never modify its fields except through this API.
 * References: ADR-011 and docs/operations/console-2026-10-28.md. */
#ifndef SPIFFOS_TEXT_CONSOLE_H
#define SPIFFOS_TEXT_CONSOLE_H
#include <stdbool.h>
#include <stdint.h>
struct limine_framebuffer;
enum { TEXT_CELL_PIXELS = 16 };
struct text_console {
    const struct limine_framebuffer *framebuffer;
    uint64_t left, top, columns, rows, column, row;
    /* Total physical text rows removed from the top since initialization.
     * Monotonic count lets stored logical lines survive pixel scrolling. */
    uint64_t scroll_count;
    bool initialized;
};
/* Initialize caller-owned state and clear a viewport. x/y in pixels; size in
 * character cells. False leaves screen untouched and state uninitialized.
 * Borrow framebuffer/mappings until use ends. Startup-only; no allocation. */
bool text_console_initialize(struct text_console *console,
    const struct limine_framebuffer *framebuffer, uint64_t x, uint64_t y,
    uint64_t columns, uint64_t rows);
/* Consume a byte; newline advances row/reset column, others draw and wrap.
 * True means consumed; row==rows means next input scrolls up one cell row.
 * False on invalid state or drawing failure. No scrollback/history buffer.
 * Unsupported bytes paint '?'. Startup-only, borrowed state, no allocation. */
bool text_console_putc(struct text_console *console, unsigned char character);
/* Print borrowed NUL-terminated text. False may leave a printed prefix.
 * NULL rejects without writes. Startup-only, no ownership transfer/allocation. */
bool text_console_write(struct text_console *console, const char *text);
/* Position the next character in a valid cell without clearing or redrawing.
 * Cell units; false leaves state/screen unchanged. Borrow initialized state;
 * startup-only, no allocation/ownership transfer. Does not restore any logo. */
bool text_console_position(struct text_console *console, uint64_t column, uint64_t row);
#endif
