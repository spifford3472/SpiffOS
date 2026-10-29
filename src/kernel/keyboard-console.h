/* Purpose: stored logical-line input and soft wrapping on the boot console.
 * Dependencies: logical-line and text-console. Caller owns all storage/state.
 * Invariants: one active append-only line, no heap/BASIC/concurrent use.
 * See docs/decisions/ADR-screen-editor-logical-lines.md. */
#ifndef SPIFFOS_KEYBOARD_CONSOLE_H
#define SPIFFOS_KEYBOARD_CONSOLE_H
#include <stddef.h>
#include <stdint.h>
#include "logical-line.h"

struct text_console;

enum keyboard_console_result {
    KEYBOARD_CONSOLE_IGNORED,
    KEYBOARD_CONSOLE_ACCEPTED,
    KEYBOARD_CONSOLE_SUBMITTED,
    KEYBOARD_CONSOLE_FULL,
    KEYBOARD_CONSOLE_ERROR
};

struct keyboard_console {
    struct text_console *console;
    struct logical_line line;
    uint64_t start_row_absolute;
    logical_line_submit_fn submit;
    void *submit_context;
};

/* Start a logical line on the row after existing output. Borrow console and
 * storage until editing ends. Callback receives counted complete text on Enter.
 * False leaves input unusable; no heap, submission or ownership transfer. */
bool keyboard_console_initialize(struct keyboard_console *input,
    struct text_console *console, unsigned char *storage, size_t capacity,
    logical_line_submit_fn submit, void *submit_context);

/* Apply supported printable ASCII, Backspace or Enter. Printable text appends
 * through soft wraps; only Enter calls submit. FULL preserves complete text.
 * Result distinguishes ignored boundaries, capacity and rendering errors. */
enum keyboard_console_result keyboard_console_accept(
    struct keyboard_console *input, unsigned char character);

#endif
