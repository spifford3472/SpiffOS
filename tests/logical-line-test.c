/* Purpose: verify bounded logical text and exact counted submission.
 * Dependencies: logical-line only; ordinary guarded host memory.
 * Invariants: no framebuffer, hardware, allocator or concurrent access. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "logical-line.h"

struct capture {
    unsigned char bytes[16];
    size_t length;
    unsigned int calls;
};

/* Copy one borrowed submission into bounded test evidence exactly once. */
static bool capture_submit(
    void *context, const unsigned char *bytes, size_t length)
{
    struct capture *capture = context;
    assert(length <= sizeof capture->bytes);
    memcpy(capture->bytes, bytes, length);
    capture->length = length;
    ++capture->calls;
    return true;
}

/* Exercise boundary, capacity, complete ordering, reset and invalid arguments. */
int main(void)
{
    struct logical_line line;
    unsigned char storage[4] = {0};
    struct capture capture = {0};
    assert(!logical_line_initialize(NULL, storage, sizeof storage));
    assert(!logical_line_initialize(&line, NULL, sizeof storage));
    assert(logical_line_initialize(&line, storage, sizeof storage));
    assert(logical_line_submit(&line, capture_submit, &capture));
    assert(capture.calls == 1 && capture.length == 0);
    assert(logical_line_append(&line, 'A') == LOGICAL_LINE_OK);
    assert(logical_line_append(&line, 'B') == LOGICAL_LINE_OK);
    assert(logical_line_append(&line, 'C') == LOGICAL_LINE_OK);
    assert(logical_line_append(&line, 'D') == LOGICAL_LINE_OK);
    assert(logical_line_append(&line, 'E') == LOGICAL_LINE_FULL);
    assert(line.length == 4 && memcmp(line.bytes, "ABCD", 4) == 0);
    assert(logical_line_submit(&line, capture_submit, &capture));
    assert(capture.calls == 2 && capture.length == 4);
    assert(memcmp(capture.bytes, "ABCD", 4) == 0);
    unsigned char removed = 0;
    assert(logical_line_backspace(&line, &removed) == LOGICAL_LINE_OK);
    assert(removed == 'D' && line.length == 3);
    logical_line_reset(&line);
    assert(line.length == 0);
    assert(logical_line_backspace(&line, NULL) == LOGICAL_LINE_AT_BOUNDARY);

    assert(logical_line_edit(&line, 2, LOGICAL_LINE_OVERWRITE, 'C') ==
           LOGICAL_LINE_OK);
    assert(line.length == 3 && memcmp(line.bytes, "  C", 3) == 0);
    assert(logical_line_edit(&line, 1, LOGICAL_LINE_OVERWRITE, 'B') ==
           LOGICAL_LINE_OK);
    assert(memcmp(line.bytes, " BC", 3) == 0);
    assert(logical_line_edit(&line, 1, LOGICAL_LINE_INSERT, 'A') ==
           LOGICAL_LINE_OK);
    assert(line.length == 4 && memcmp(line.bytes, " ABC", 4) == 0);
    unsigned char before_full[4];
    memcpy(before_full, line.bytes, sizeof before_full);
    assert(logical_line_edit(&line, 0, LOGICAL_LINE_INSERT, 'X') ==
           LOGICAL_LINE_FULL);
    assert(line.length == 4 && memcmp(line.bytes, before_full, 4) == 0);
    size_t offset = 3;
    assert(logical_line_delete_before(&line, &offset) == LOGICAL_LINE_OK);
    assert(offset == 2 && line.length == 3 &&
           memcmp(line.bytes, " AC", 3) == 0);
    offset = 0;
    assert(logical_line_delete_before(&line, &offset) ==
           LOGICAL_LINE_AT_BOUNDARY);
    offset = 4;
    assert(logical_line_delete_before(&line, &offset) ==
           LOGICAL_LINE_INVALID);
    puts("PASS bounded logical line, editing and exact submission");
}
