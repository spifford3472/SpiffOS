/* Purpose: bounded screen-position editor over the boot text workspace.
 * Dependencies: logical-line and text-console; caller owns one static state.
 * Invariants: no heap/BASIC/interrupts; at most 64 rows and 65 line records.
 * References: docs/decisions/ADR-screen-editor-logical-lines.md. */
#ifndef SPIFFOS_SCREEN_EDITOR_H
#define SPIFFOS_SCREEN_EDITOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "logical-line.h"

enum {
    SCREEN_EDITOR_MAX_ROWS = 64,
    SCREEN_EDITOR_MAX_LINES = 65,
    SCREEN_EDITOR_LINE_CAPACITY = 4096
};

enum screen_editor_action {
    SCREEN_EDITOR_CHARACTER,
    SCREEN_EDITOR_ENTER,
    SCREEN_EDITOR_BACKSPACE,
    SCREEN_EDITOR_LEFT,
    SCREEN_EDITOR_RIGHT,
    SCREEN_EDITOR_UP,
    SCREEN_EDITOR_DOWN,
    SCREEN_EDITOR_DELETE,
    SCREEN_EDITOR_TOGGLE_INSERT
};

enum screen_editor_result {
    SCREEN_EDITOR_IGNORED,
    SCREEN_EDITOR_ACCEPTED,
    SCREEN_EDITOR_SUBMITTED,
    SCREEN_EDITOR_FULL,
    SCREEN_EDITOR_ERROR
};

struct screen_editor_line {
    struct logical_line text;
    int64_t start_row;
    bool used;
};

struct screen_editor {
    struct text_console *console;
    struct screen_editor_line lines[SCREEN_EDITOR_MAX_LINES];
    unsigned char storage[SCREEN_EDITOR_MAX_LINES][SCREEN_EDITOR_LINE_CAPACITY];
    uint64_t cursor_row;
    uint64_t cursor_column;
    size_t cursor_offset;
    int cursor_line;
    logical_line_submit_fn submit;
    void *submit_context;
    bool insert_mode;
    bool cursor_visible;
    bool capacity_full;
    bool initialized;
};

/* Initialize static caller-owned editor state after existing boot output.
 * Shows the overwrite block cursor on a new blank row. False may leave the
 * separating newline but never submits or allocates. */
bool screen_editor_initialize(struct screen_editor *editor,
    struct text_console *console, logical_line_submit_fn submit,
    void *submit_context);

/* Register already-rendered counted text as one logical line without changing
 * any pixels. This lets earlier boot output participate in arrows and Enter.
 * Rejects overlap, unsupported rows/capacity and partial copies atomically. */
bool screen_editor_seed_line(struct screen_editor *editor, uint64_t start_row,
    const unsigned char *bytes, size_t length);

/* Apply one abstract editor action. Character requires a supported printable
 * byte. Named navigation clamps to the visible screen. Insert toggles on make
 * events supplied by the decoder. Every return restores a visible cursor. */
enum screen_editor_result screen_editor_accept(struct screen_editor *editor,
    enum screen_editor_action action, unsigned char character);

#endif
