/* Purpose: retain one complete editable command independently of screen pixels.
 * Dependencies: logical-line.h and caller-owned bounded storage.
 * Invariants: length <= capacity; no heap, I/O or concurrent/interrupt use.
 * References: docs/decisions/ADR-screen-editor-logical-lines.md. */
#include <stddef.h>
#include "logical-line.h"

/* Establish a valid empty line over borrowed writable bytes. False rejects
 * NULL/zero-capacity arguments without accessing storage. */
bool logical_line_initialize(struct logical_line *line,
    unsigned char *storage, size_t capacity)
{
    if (line == NULL) {
        return false;
    }
    line->bytes = NULL;
    line->length = 0;
    line->capacity = 0;
    if (storage == NULL || capacity == 0) {
        return false;
    }
    line->bytes = storage;
    line->capacity = capacity;
    return true;
}

/* Add one byte after checking all bounds. Writing at length is safe because
 * length < capacity; increment happens only after the store succeeds. */
enum logical_line_result logical_line_append(
    struct logical_line *line, unsigned char byte)
{
    if (line == NULL || line->bytes == NULL ||
        line->length > line->capacity) {
        return LOGICAL_LINE_INVALID;
    }
    if (line->length == line->capacity) {
        return LOGICAL_LINE_FULL;
    }
    line->bytes[line->length] = byte;
    ++line->length;
    return LOGICAL_LINE_OK;
}

/* Remove only within this logical line. Decrement first selects the last valid
 * byte index; no screen row or command terminator is involved. */
enum logical_line_result logical_line_backspace(
    struct logical_line *line, unsigned char *removed)
{
    if (line == NULL || line->bytes == NULL ||
        line->length > line->capacity) {
        return LOGICAL_LINE_INVALID;
    }
    if (line->length == 0) {
        return LOGICAL_LINE_AT_BOUNDARY;
    }
    --line->length;
    if (removed != NULL) {
        *removed = line->bytes[line->length];
    }
    return LOGICAL_LINE_OK;
}

/* Call exactly once with the complete stored byte count. The callback borrows
 * bytes only during this call; this function never clears data automatically. */
bool logical_line_submit(const struct logical_line *line,
    logical_line_submit_fn callback, void *context)
{
    if (line == NULL || line->bytes == NULL ||
        line->length > line->capacity || callback == NULL) {
        return false;
    }
    return callback(context, line->bytes, line->length);
}

/* Retain borrowed storage/capacity while making the logical content empty.
 * Stale bytes beyond length are inaccessible through the public contract. */
void logical_line_reset(struct logical_line *line)
{
    if (line != NULL && line->bytes != NULL &&
        line->length <= line->capacity) {
        line->length = 0;
    }
}
