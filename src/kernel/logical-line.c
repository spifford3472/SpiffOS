/* Purpose: retain one complete editable command independently of screen pixels.
 * Dependencies: logical-line.h and caller-owned bounded storage.
 * Invariants: length <= capacity; no heap, I/O or concurrent/interrupt use.
 * References: docs/decisions/ADR-screen-editor-logical-lines.md. */
#include <stddef.h>
#include <stdint.h>
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

/* Replace or insert at one counted byte position. Capacity is calculated
 * before mutation so every failure preserves the prior line exactly. Padding
 * bytes are ordinary spaces and therefore participate in later submission. */
enum logical_line_result logical_line_edit(struct logical_line *line,
    size_t offset, enum logical_line_edit_mode mode, unsigned char byte)
{
    if (line == NULL || line->bytes == NULL ||
        line->length > line->capacity ||
        (mode != LOGICAL_LINE_OVERWRITE && mode != LOGICAL_LINE_INSERT)) {
        return LOGICAL_LINE_INVALID;
    }
    size_t required;
    if (offset > line->length) {
        if (offset == SIZE_MAX) {
            return LOGICAL_LINE_FULL;
        }
        required = offset + 1;
    } else if (mode == LOGICAL_LINE_INSERT || offset == line->length) {
        if (line->length == SIZE_MAX) {
            return LOGICAL_LINE_FULL;
        }
        required = line->length + 1;
    } else {
        required = line->length;
    }
    if (required > line->capacity) {
        return LOGICAL_LINE_FULL;
    }
    if (offset > line->length) {
        for (size_t index = line->length; index < offset; ++index) {
            line->bytes[index] = ' ';
        }
        line->bytes[offset] = byte;
        line->length = required;
        return LOGICAL_LINE_OK;
    }
    if (mode == LOGICAL_LINE_INSERT && offset < line->length) {
        /* Walk backward so each source byte is read before its destination is
         * overwritten. required <= capacity was established above. */
        for (size_t index = line->length; index > offset; --index) {
            line->bytes[index] = line->bytes[index - 1];
        }
    }
    line->bytes[offset] = byte;
    line->length = required;
    return LOGICAL_LINE_OK;
}

/* Delete one byte before a validated cursor offset. Forward copying is safe
 * because every source lies one byte above its destination. */
enum logical_line_result logical_line_delete_before(
    struct logical_line *line, size_t *offset)
{
    if (line == NULL || line->bytes == NULL || offset == NULL ||
        line->length > line->capacity || *offset > line->length) {
        return LOGICAL_LINE_INVALID;
    }
    if (*offset == 0) {
        return LOGICAL_LINE_AT_BOUNDARY;
    }
    size_t removed = *offset - 1;
    for (size_t index = removed; index + 1 < line->length; ++index) {
        line->bytes[index] = line->bytes[index + 1];
    }
    --line->length;
    --*offset;
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
