/* Purpose: bounded stored text for one editable logical input line.
 * Dependencies: fixed-size caller-owned byte storage only.
 * Invariants: no allocation; length never exceeds capacity; single caller.
 * References: docs/decisions/ADR-screen-editor-logical-lines.md. */
#ifndef SPIFFOS_LOGICAL_LINE_H
#define SPIFFOS_LOGICAL_LINE_H
#include <stdbool.h>
#include <stddef.h>

enum logical_line_result {
    LOGICAL_LINE_OK,
    LOGICAL_LINE_AT_BOUNDARY,
    LOGICAL_LINE_FULL,
    LOGICAL_LINE_INVALID
};

enum logical_line_edit_mode {
    LOGICAL_LINE_OVERWRITE,
    LOGICAL_LINE_INSERT
};

struct logical_line {
    unsigned char *bytes;
    size_t length;
    size_t capacity;
};

typedef bool (*logical_line_submit_fn)(
    void *context, const unsigned char *bytes, size_t length);

/* Initialize caller-owned state over writable storage. False leaves an empty,
 * unusable line. Storage remains owned by the caller for the line lifetime.
 * No allocation, I/O, interrupt or concurrency behavior. */
bool logical_line_initialize(struct logical_line *line,
    unsigned char *storage, size_t capacity);

/* Append one byte atomically. FULL and INVALID leave the line unchanged.
 * Borrow state/storage; no allocation or ownership transfer. */
enum logical_line_result logical_line_append(
    struct logical_line *line, unsigned char byte);

/* Edit one byte at a counted offset. Overwrite replaces an existing byte or
 * pads with spaces through a new end; insert shifts the suffix right. FULL and
 * INVALID are atomic and leave bytes/length unchanged. */
enum logical_line_result logical_line_edit(struct logical_line *line,
    size_t offset, enum logical_line_edit_mode mode, unsigned char byte);

/* Delete the byte immediately before offset and shift the suffix left.
 * AT_BOUNDARY rejects offset zero; INVALID rejects offsets beyond length.
 * On success offset is decremented. No allocation or ownership transfer. */
enum logical_line_result logical_line_delete_before(
    struct logical_line *line, size_t *offset);

/* Remove the final byte. AT_BOUNDARY leaves an empty line unchanged.
 * removed may be NULL; otherwise receives the deleted byte on success. */
enum logical_line_result logical_line_backspace(
    struct logical_line *line, unsigned char *removed);

/* Borrow the complete counted byte sequence for one synchronous callback.
 * Empty lines are valid. False means invalid state/callback or rejection.
 * Does not reset or mutate the line and never submits a partial prefix. */
bool logical_line_submit(const struct logical_line *line,
    logical_line_submit_fn callback, void *context);

/* Make the existing storage an empty logical line; byte contents are ignored.
 * Invalid/NULL state is harmless. No memory is freed or cleared. */
void logical_line_reset(struct logical_line *line);

#endif
