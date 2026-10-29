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
    puts("PASS bounded logical line, capacity and exact submission");
}
