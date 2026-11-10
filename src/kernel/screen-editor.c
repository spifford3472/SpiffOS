/* Purpose: edit bounded logical lines at visible screen positions.
 * Dependencies: logical-line, text-console and framebuffer primitives.
 * Invariants: static caller-owned state; no heap, BASIC, IRQs or concurrency.
 * References: docs/decisions/ADR-screen-editor-logical-lines.md. */
#include <stddef.h>
#include <stdint.h>
#include "framebuffer.h"
#include "text-console.h"
#include "screen-editor.h"

/* Return whether the diagnostic font/input milestone can render this byte.
 * Full text encoding and keyboard layouts remain outside this checkpoint. */
static bool supported_printable(unsigned char character)
{
    return character >= 32 && character <= 126 &&
        !(character >= 'a' && character <= 'z');
}

/* Count physical rows occupied by stored bytes. Empty lines own one row so
 * blank screen positions retain a stable logical-line identity. */
static uint64_t line_span(const struct screen_editor *editor, int line_index)
{
    size_t length = editor->lines[line_index].text.length;
    if (length == 0) {
        return 1;
    }
    return (uint64_t)((length - 1) / editor->console->columns + 1);
}

/* XOR the owner-approved cursor shape. Repeating this function restores the
 * exact underlying text or logo pixels. A full buffer adds a top notch/band:
 * overwrite cancels the top two rows of its block; insert adds a top band. */
static bool toggle_cursor(struct screen_editor *editor)
{
    if (editor->cursor_row >= editor->console->rows ||
        editor->cursor_column >= editor->console->columns) {
        return false;
    }
    uint64_t x = editor->console->left +
        editor->cursor_column * TEXT_CELL_PIXELS;
    uint64_t y = editor->console->top +
        editor->cursor_row * TEXT_CELL_PIXELS;
    bool ok;
    if (editor->insert_mode) {
        ok = framebuffer_invert_rectangle(editor->console->framebuffer,
            x, y + TEXT_CELL_PIXELS - 2, TEXT_CELL_PIXELS, 2);
    } else {
        ok = framebuffer_invert_rectangle(editor->console->framebuffer,
            x, y, TEXT_CELL_PIXELS, TEXT_CELL_PIXELS);
    }
    if (ok && editor->capacity_full) {
        ok = framebuffer_invert_rectangle(editor->console->framebuffer,
            x, y, TEXT_CELL_PIXELS, 2);
    }
    if (ok) {
        editor->cursor_visible = !editor->cursor_visible;
    }
    return ok;
}

/* Hide a shown cursor before any pixel movement or redraw. */
static bool hide_cursor(struct screen_editor *editor)
{
    return !editor->cursor_visible || toggle_cursor(editor);
}

/* Find the logical line owning a visible physical row. A line owns its entire
 * final row, including blank cells after its stored byte count. */
static int line_at_row(const struct screen_editor *editor, uint64_t row)
{
    for (int index = 0; index < SCREEN_EDITOR_MAX_LINES; ++index) {
        if (!editor->lines[index].used) {
            continue;
        }
        int64_t start = editor->lines[index].start_row;
        int64_t end = start + (int64_t)line_span(editor, index);
        if ((int64_t)row >= start && (int64_t)row < end) {
            return index;
        }
    }
    return -1;
}

/* Reclaim records that no longer have any visible row. The selected line is
 * retained because a long command may have a scrolled-off prefix. */
static void reclaim_invisible_lines(struct screen_editor *editor)
{
    for (int index = 0; index < SCREEN_EDITOR_MAX_LINES; ++index) {
        if (!editor->lines[index].used || index == editor->cursor_line) {
            continue;
        }
        int64_t start = editor->lines[index].start_row;
        int64_t end = start + (int64_t)line_span(editor, index);
        if (end <= 0 || start >= (int64_t)editor->console->rows) {
            editor->lines[index].used = false;
        }
    }
}

/* Allocate one empty record at a physical row. Storage is embedded in the
 * caller-owned editor and reinitialized only after the prior record is unused. */
static int allocate_line(struct screen_editor *editor, int64_t start_row)
{
    reclaim_invisible_lines(editor);
    for (int index = 0; index < SCREEN_EDITOR_MAX_LINES; ++index) {
        if (editor->lines[index].used) {
            continue;
        }
        if (!logical_line_initialize(&editor->lines[index].text,
            editor->storage[index], SCREEN_EDITOR_LINE_CAPACITY)) {
            return -1;
        }
        editor->lines[index].start_row = start_row;
        editor->lines[index].used = true;
        return index;
    }
    return -1;
}

/* Draw one whole logical line from memory, never by reading framebuffer text.
 * Clearing its owned visible rows removes stale shifted characters while rows
 * outside the line, including untouched logo art, remain byte-for-byte intact. */
static bool redraw_line(struct screen_editor *editor, int line_index)
{
    const struct screen_editor_line *line = &editor->lines[line_index];
    uint64_t span = line_span(editor, line_index);
    for (uint64_t relative = 0; relative < span; ++relative) {
        int64_t screen_row = line->start_row + (int64_t)relative;
        if (screen_row < 0 || screen_row >= (int64_t)editor->console->rows) {
            continue;
        }
        if (!framebuffer_fill_rectangle(editor->console->framebuffer,
            editor->console->left,
            editor->console->top + (uint64_t)screen_row * TEXT_CELL_PIXELS,
            editor->console->columns * TEXT_CELL_PIXELS, TEXT_CELL_PIXELS,
            TEXT_CONSOLE_BACKGROUND_RGB)) {
            return false;
        }
    }
    for (size_t offset = 0; offset < line->text.length; ++offset) {
        int64_t screen_row = line->start_row +
            (int64_t)(offset / editor->console->columns);
        if (screen_row < 0 || screen_row >= (int64_t)editor->console->rows) {
            continue;
        }
        uint64_t column = offset % editor->console->columns;
        if (!text_console_draw_at(editor->console, column,
            (uint64_t)screen_row, line->text.bytes[offset])) {
            return false;
        }
    }
    return true;
}

/* Insert cleared physical rows at a boundary and move following pixels and
 * line identities down together. Rows falling below the viewport are dropped. */
static bool insert_rows(struct screen_editor *editor, int64_t boundary,
    uint64_t count, int excluded_line)
{
    if (count == 0) {
        return true;
    }
    int64_t visible_boundary = boundary < 0 ? 0 : boundary;
    if (visible_boundary < (int64_t)editor->console->rows) {
        uint64_t row = (uint64_t)visible_boundary;
        uint64_t height_rows = editor->console->rows - row;
        if (count > height_rows ||
            !framebuffer_move_rectangle_down(editor->console->framebuffer,
                editor->console->left,
                editor->console->top + row * TEXT_CELL_PIXELS,
                editor->console->columns * TEXT_CELL_PIXELS,
                height_rows * TEXT_CELL_PIXELS,
                count * TEXT_CELL_PIXELS, TEXT_CONSOLE_BACKGROUND_RGB)) {
            return false;
        }
    }
    for (int index = 0; index < SCREEN_EDITOR_MAX_LINES; ++index) {
        if (editor->lines[index].used && index != excluded_line &&
            editor->lines[index].start_row >= boundary) {
            editor->lines[index].start_row += (int64_t)count;
        }
    }
    reclaim_invisible_lines(editor);
    return true;
}

/* Remove physical rows at a boundary, pulling all following pixels/identities
 * upward and exposing clean rows at the bottom. */
static bool remove_rows(struct screen_editor *editor, int64_t boundary,
    uint64_t count, int excluded_line)
{
    if (count == 0) {
        return true;
    }
    int64_t visible_boundary = boundary < 0 ? 0 : boundary;
    if (visible_boundary < (int64_t)editor->console->rows) {
        uint64_t row = (uint64_t)visible_boundary;
        uint64_t height_rows = editor->console->rows - row;
        if (count > height_rows ||
            !framebuffer_scroll_rectangle(editor->console->framebuffer,
                editor->console->left,
                editor->console->top + row * TEXT_CELL_PIXELS,
                editor->console->columns * TEXT_CELL_PIXELS,
                height_rows * TEXT_CELL_PIXELS,
                count * TEXT_CELL_PIXELS, TEXT_CONSOLE_BACKGROUND_RGB)) {
            return false;
        }
    }
    int64_t source_boundary = boundary + (int64_t)count;
    for (int index = 0; index < SCREEN_EDITOR_MAX_LINES; ++index) {
        if (editor->lines[index].used && index != excluded_line &&
            editor->lines[index].start_row >= source_boundary) {
            editor->lines[index].start_row -= (int64_t)count;
        }
    }
    reclaim_invisible_lines(editor);
    return true;
}

/* Scroll the complete workspace upward one row, including arbitrary logo
 * pixels, and keep every line's screen coordinate synchronized. */
static bool scroll_one_row(struct screen_editor *editor)
{
    if (!text_console_scroll_up_one_row(editor->console)) {
        return false;
    }
    for (int index = 0; index < SCREEN_EDITOR_MAX_LINES; ++index) {
        if (editor->lines[index].used) {
            --editor->lines[index].start_row;
        }
    }
    if (editor->cursor_row > 0) {
        --editor->cursor_row;
    }
    reclaim_invisible_lines(editor);
    return true;
}

/* Convert the selected line/offset to a visible cursor cell, scrolling only
 * when editing grows below the viewport. */
static bool position_from_offset(struct screen_editor *editor)
{
    if (editor->cursor_line < 0 ||
        !editor->lines[editor->cursor_line].used) {
        return false;
    }
    int64_t row = editor->lines[editor->cursor_line].start_row +
        (int64_t)(editor->cursor_offset / editor->console->columns);
    uint64_t column = editor->cursor_offset % editor->console->columns;
    if (row < 0) {
        /* Deleting just before the first visible fragment may put the logical
         * cursor in the hidden prefix. This checkpoint has no history view:
         * clamp to the first visible cell while retaining the complete text. */
        editor->cursor_offset = (size_t)(
            -editor->lines[editor->cursor_line].start_row) *
            editor->console->columns;
        row = 0;
        column = 0;
    }
    editor->cursor_row = (uint64_t)row;
    editor->cursor_column = column;
    while (editor->cursor_row >= editor->console->rows) {
        if (!scroll_one_row(editor)) {
            return false;
        }
    }
    (void)text_console_position(editor->console, editor->cursor_column,
                                editor->cursor_row);
    return true;
}

/* Select the line/byte offset corresponding to the current visible cell. */
static void select_at_cursor(struct screen_editor *editor)
{
    int line_index = line_at_row(editor, editor->cursor_row);
    editor->cursor_line = line_index;
    if (line_index < 0) {
        editor->cursor_offset = 0;
    } else {
        editor->cursor_offset =
            (size_t)((int64_t)editor->cursor_row -
                     editor->lines[line_index].start_row) *
                editor->console->columns +
            editor->cursor_column;
    }
    (void)text_console_position(editor->console, editor->cursor_column,
                                editor->cursor_row);
}

/* Move one cell with row-edge crossing and visible-screen clamping. */
static void move_cursor(struct screen_editor *editor,
    enum screen_editor_action action)
{
    if (action == SCREEN_EDITOR_LEFT) {
        if (editor->cursor_column > 0) {
            --editor->cursor_column;
        } else if (editor->cursor_row > 0) {
            --editor->cursor_row;
            editor->cursor_column = editor->console->columns - 1;
        }
    } else if (action == SCREEN_EDITOR_RIGHT) {
        if (editor->cursor_column + 1 < editor->console->columns) {
            ++editor->cursor_column;
        } else if (editor->cursor_row + 1 < editor->console->rows) {
            ++editor->cursor_row;
            editor->cursor_column = 0;
        }
    } else if (action == SCREEN_EDITOR_UP) {
        if (editor->cursor_row > 0) {
            --editor->cursor_row;
        }
    } else if (action == SCREEN_EDITOR_DOWN) {
        if (editor->cursor_row + 1 < editor->console->rows) {
            ++editor->cursor_row;
        }
    }
    select_at_cursor(editor);
}

/* Edit one byte at the selected line or create a padded line in an unmanaged
 * row. Growth inserts rows before following content; no neighboring line is
 * overwritten or merged. */
static enum screen_editor_result type_character(
    struct screen_editor *editor, unsigned char character)
{
    if (!supported_printable(character)) {
        return SCREEN_EDITOR_IGNORED;
    }
    int line_index = editor->cursor_line;
    if (line_index < 0) {
        line_index = allocate_line(editor, (int64_t)editor->cursor_row);
        if (line_index < 0) {
            return SCREEN_EDITOR_FULL;
        }
        editor->cursor_line = line_index;
        editor->cursor_offset = editor->cursor_column;
    }
    struct screen_editor_line *line = &editor->lines[line_index];
    uint64_t old_span = line_span(editor, line_index);
    enum logical_line_result result = logical_line_edit(&line->text,
        editor->cursor_offset,
        editor->insert_mode ? LOGICAL_LINE_INSERT : LOGICAL_LINE_OVERWRITE,
        character);
    if (result == LOGICAL_LINE_FULL) {
        editor->capacity_full = true;
        return SCREEN_EDITOR_FULL;
    }
    if (result != LOGICAL_LINE_OK) {
        return SCREEN_EDITOR_ERROR;
    }
    uint64_t new_span = line_span(editor, line_index);
    if (new_span > old_span &&
        !insert_rows(editor, line->start_row + (int64_t)old_span,
                     new_span - old_span, line_index)) {
        return SCREEN_EDITOR_ERROR;
    }
    if (!redraw_line(editor, line_index)) {
        return SCREEN_EDITOR_ERROR;
    }
    ++editor->cursor_offset;
    editor->capacity_full = false;
    return position_from_offset(editor) ?
        SCREEN_EDITOR_ACCEPTED : SCREEN_EDITOR_ERROR;
}

/* Erase before or at the cursor within one logical line. Backspace moves left;
 * Delete keeps its counted offset. Neither merges separate logical records.
 * Blank/end Delete is a no-op. Existing row shrink preserves following lines. */
static enum screen_editor_result erase_character(struct screen_editor *editor, bool before)
{
    if (editor->cursor_line < 0) {
        if (!before) return SCREEN_EDITOR_IGNORED;
        move_cursor(editor, SCREEN_EDITOR_LEFT);
        return SCREEN_EDITOR_ACCEPTED;
    }
    struct screen_editor_line *line = &editor->lines[editor->cursor_line];
    if (editor->cursor_offset > line->text.length) {
        if (!before) return SCREEN_EDITOR_IGNORED;
        --editor->cursor_offset;
        editor->capacity_full = false;
        return position_from_offset(editor) ?
            SCREEN_EDITOR_ACCEPTED : SCREEN_EDITOR_ERROR;
    }
    if ((before && editor->cursor_offset == 0) ||
        (!before && editor->cursor_offset >= line->text.length)) {
        return SCREEN_EDITOR_IGNORED;
    }
    uint64_t old_span = line_span(editor, editor->cursor_line);
    size_t erase_offset = editor->cursor_offset + (before ? 0 : 1);
    if (logical_line_delete_before(&line->text, &erase_offset) !=
        LOGICAL_LINE_OK) {
        return SCREEN_EDITOR_ERROR;
    }
    editor->cursor_offset = erase_offset;
    uint64_t new_span = line_span(editor, editor->cursor_line);
    if (new_span < old_span &&
        !remove_rows(editor, line->start_row + (int64_t)new_span,
                     old_span - new_span, editor->cursor_line)) {
        return SCREEN_EDITOR_ERROR;
    }
    if (!redraw_line(editor, editor->cursor_line)) {
        return SCREEN_EDITOR_ERROR;
    }
    editor->capacity_full = false;
    return position_from_offset(editor) ?
        SCREEN_EDITOR_ACCEPTED : SCREEN_EDITOR_ERROR;
}

/* Submit one complete counted line, then insert a new blank logical row after
 * its final content row. Existing following rows move down intact. */
static enum screen_editor_result submit_line(struct screen_editor *editor)
{
    int line_index = editor->cursor_line;
    if (line_index < 0) {
        line_index = allocate_line(editor, (int64_t)editor->cursor_row);
        if (line_index < 0) {
            return SCREEN_EDITOR_FULL;
        }
    }
    if (!logical_line_submit(&editor->lines[line_index].text,
                             editor->submit, editor->submit_context)) {
        return SCREEN_EDITOR_ERROR;
    }
    int64_t target = editor->lines[line_index].start_row +
        (int64_t)line_span(editor, line_index);
    while (target >= (int64_t)editor->console->rows) {
        if (!scroll_one_row(editor)) {
            return SCREEN_EDITOR_ERROR;
        }
        --target;
    }
    if (!insert_rows(editor, target, 1, line_index)) {
        return SCREEN_EDITOR_ERROR;
    }
    int next_line = allocate_line(editor, target);
    if (next_line < 0) {
        return SCREEN_EDITOR_FULL;
    }
    editor->cursor_line = next_line;
    editor->cursor_offset = 0;
    editor->capacity_full = false;
    return position_from_offset(editor) ?
        SCREEN_EDITOR_SUBMITTED : SCREEN_EDITOR_ERROR;
}

/* Initialize all embedded records, create one new line after existing output,
 * and paint the default overwrite block without reading screen pixels. */
bool screen_editor_initialize(struct screen_editor *editor,
    struct text_console *console, logical_line_submit_fn submit,
    void *submit_context)
{
    if (editor == NULL) {
        return false;
    }
    editor->initialized = false;
    editor->cursor_visible = false;
    editor->capacity_full = false;
    editor->insert_mode = false;
    editor->cursor_line = -1;
    if (console == NULL || !console->initialized || submit == NULL ||
        console->columns == 0 || console->columns > 160 ||
        console->rows == 0 || console->rows > SCREEN_EDITOR_MAX_ROWS ||
        !text_console_putc(console, '\n')) {
        return false;
    }
    editor->console = console;
    editor->submit = submit;
    editor->submit_context = submit_context;
    for (int index = 0; index < SCREEN_EDITOR_MAX_LINES; ++index) {
        editor->lines[index].used = false;
    }
    if (console->row == console->rows) {
        editor->cursor_row = console->row;
        editor->cursor_column = 0;
        if (!scroll_one_row(editor)) {
            return false;
        }
    } else {
        editor->cursor_row = console->row;
        editor->cursor_column = 0;
    }
    int line_index = allocate_line(editor, (int64_t)editor->cursor_row);
    if (line_index < 0) {
        return false;
    }
    editor->cursor_line = line_index;
    editor->cursor_offset = 0;
    editor->initialized = true;
    (void)text_console_position(editor->console, editor->cursor_column,
                                editor->cursor_row);
    return toggle_cursor(editor);
}

/* Associate already-visible output with an immutable-at-import logical record.
 * Pixels are deliberately untouched; later edits redraw from this stored copy.
 * All overlap/capacity checks precede allocation and copying. */
bool screen_editor_seed_line(struct screen_editor *editor, uint64_t start_row,
    const unsigned char *bytes, size_t length)
{
    if (editor == NULL || !editor->initialized ||
        (length > 0 && bytes == NULL) ||
        length > SCREEN_EDITOR_LINE_CAPACITY ||
        start_row >= editor->console->rows) {
        return false;
    }
    uint64_t span = length == 0 ? 1 :
        (uint64_t)((length - 1) / editor->console->columns + 1);
    if (span > editor->console->rows - start_row) {
        return false;
    }
    for (uint64_t row = start_row; row < start_row + span; ++row) {
        if (line_at_row(editor, row) >= 0) {
            return false;
        }
    }
    int line_index = allocate_line(editor, (int64_t)start_row);
    if (line_index < 0) {
        return false;
    }
    for (size_t offset = 0; offset < length; ++offset) {
        if (logical_line_append(&editor->lines[line_index].text, bytes[offset]) !=
            LOGICAL_LINE_OK) {
            editor->lines[line_index].used = false;
            return false;
        }
    }
    return true;
}

/* Hide the cursor, mutate model/pixels, and always attempt to restore the
 * cursor in its resulting mode and position. Insert toggles only when the
 * decoder emits its make event; break events never reach this interface. */
enum screen_editor_result screen_editor_accept(struct screen_editor *editor,
    enum screen_editor_action action, unsigned char character)
{
    if (editor == NULL || !editor->initialized || !hide_cursor(editor)) {
        return SCREEN_EDITOR_ERROR;
    }
    enum screen_editor_result result;
    if (action == SCREEN_EDITOR_CHARACTER) {
        result = type_character(editor, character);
    } else if (action == SCREEN_EDITOR_BACKSPACE) {
        result = erase_character(editor, true);
    } else if (action == SCREEN_EDITOR_DELETE) {
        result = erase_character(editor, false);
    } else if (action == SCREEN_EDITOR_ENTER) {
        result = submit_line(editor);
    } else if (action == SCREEN_EDITOR_TOGGLE_INSERT) {
        editor->insert_mode = !editor->insert_mode;
        result = SCREEN_EDITOR_ACCEPTED;
    } else if (action == SCREEN_EDITOR_LEFT || action == SCREEN_EDITOR_RIGHT ||
               action == SCREEN_EDITOR_UP || action == SCREEN_EDITOR_DOWN) {
        move_cursor(editor, action);
        result = SCREEN_EDITOR_ACCEPTED;
    } else {
        result = SCREEN_EDITOR_IGNORED;
    }
    if (!toggle_cursor(editor)) {
        return SCREEN_EDITOR_ERROR;
    }
    return result;
}
