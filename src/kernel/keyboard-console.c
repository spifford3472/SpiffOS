/* Purpose: store a complete logical command while rendering soft-wrapped rows.
 * Dependencies: logical-line and text-console; no hardware, heap or BASIC.
 * Invariants: append-only cursor at line end in this checkpoint; one caller.
 * References: docs/decisions/ADR-screen-editor-logical-lines.md. */
#include <stddef.h>
#include <stdint.h>
#include "text-console.h"
#include "keyboard-console.h"

/* Return whether the current diagnostic font/input milestone accepts a byte.
 * Pure classification; lowercase/full punctuation remain later keyboard work. */
static bool supported_printable(unsigned char character)
{
    return (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == ' ' ||
        character == '.' || character == ':' || character == '?';
}

/* Borrow initialized console and bounded storage, advance past existing output,
 * and remember the start as a row in the monotonically scrolled workspace.
 * False disables state; screen may contain the attempted separating newline. */
bool keyboard_console_initialize(struct keyboard_console *input,
    struct text_console *console, unsigned char *storage, size_t capacity,
    logical_line_submit_fn submit, void *submit_context)
{
    if (input == NULL) {
        return false;
    }
    input->console = NULL;
    input->start_row_absolute = 0;
    input->submit = NULL;
    input->submit_context = NULL;
    if (console == NULL || !console->initialized || console->columns == 0 ||
        submit == NULL ||
        !logical_line_initialize(&input->line, storage, capacity) ||
        !text_console_putc(console, '\n') ||
        console->scroll_count > UINT64_MAX - console->row) {
        return false;
    }
    input->console = console;
    input->start_row_absolute = console->scroll_count + console->row;
    input->submit = submit;
    input->submit_context = submit_context;
    return true;
}

/* Erase the final stored character, including across a soft-wrap boundary.
 * Absolute row arithmetic retains the mapping when earlier rows scrolled away.
 * False means inconsistent mapping/drawing; logical bytes remain unchanged. */
static bool backspace_last(struct keyboard_console *input)
{
    struct text_console *console = input->console;
    if (input->line.length == 0) {
        return false;
    }
    uint64_t index = (uint64_t)(input->line.length - 1);
    uint64_t row_offset = index / console->columns;
    if (input->start_row_absolute > UINT64_MAX - row_offset) {
        return false;
    }
    uint64_t absolute_row = input->start_row_absolute + row_offset;
    if (absolute_row < console->scroll_count) {
        return false;
    }
    uint64_t screen_row = absolute_row - console->scroll_count;
    uint64_t column = index % console->columns;
    if (screen_row >= console->rows ||
        !text_console_position(console, column, screen_row) ||
        !text_console_putc(console, ' ') ||
        !text_console_position(console, column, screen_row)) {
        return false;
    }
    return logical_line_backspace(&input->line, NULL) == LOGICAL_LINE_OK;
}

/* Append/erase/submit one event. Soft wrapping comes only from display width;
 * it never inserts a stored terminator or calls the submission callback.
 * Enter preserves text if the synchronous callback rejects it. */
enum keyboard_console_result keyboard_console_accept(
    struct keyboard_console *input, unsigned char character)
{
    if (input == NULL || input->console == NULL) {
        return KEYBOARD_CONSOLE_ERROR;
    }
    struct text_console *console = input->console;
    if (character == '\b') {
        if (input->line.length == 0) {
            return KEYBOARD_CONSOLE_IGNORED;
        }
        return backspace_last(input) ?
            KEYBOARD_CONSOLE_ACCEPTED : KEYBOARD_CONSOLE_ERROR;
    }
    if (character == '\n') {
        size_t submitted_length = input->line.length;
        if (!logical_line_submit(&input->line,
                                 input->submit, input->submit_context)) {
            return KEYBOARD_CONSOLE_ERROR;
        }
        /* Once the callback accepts the complete command, clear it before any
         * later display failure so retrying Enter cannot submit it twice. */
        logical_line_reset(&input->line);
        /* An exact-width line already leaves the display cursor at column zero
         * on its next row. Another newline would create an unintended blank row. */
        if (submitted_length == 0 ||
            submitted_length % console->columns != 0) {
            if (!text_console_putc(console, '\n')) {
                return KEYBOARD_CONSOLE_ERROR;
            }
        }
        if (console->scroll_count > UINT64_MAX - console->row) {
            return KEYBOARD_CONSOLE_ERROR;
        }
        input->start_row_absolute = console->scroll_count + console->row;
        return KEYBOARD_CONSOLE_SUBMITTED;
    }
    if (!supported_printable(character)) {
        return KEYBOARD_CONSOLE_IGNORED;
    }
    if (input->line.length == input->line.capacity) {
        return KEYBOARD_CONSOLE_FULL;
    }
    if (!text_console_putc(console, character)) {
        return KEYBOARD_CONSOLE_ERROR;
    }
    if (logical_line_append(&input->line, character) != LOGICAL_LINE_OK) {
        return KEYBOARD_CONSOLE_ERROR;
    }
    return KEYBOARD_CONSOLE_ACCEPTED;
}
