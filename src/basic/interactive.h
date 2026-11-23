/* Purpose: expose the session state and complete-line BASIC evaluator.
 * Dependencies: C11 size and fixed-width integer types.
 * Invariants: caller supplies bounded, separate storage for every session.
 * Reference: docs/basic/interactive-preview.md. */
#ifndef SPIFFOS_BASIC_INTERACTIVE_H
#define SPIFFOS_BASIC_INTERACTIVE_H

#include <stddef.h>
#include <stdint.h>

#define SPIFFOS_BASIC_LINE_MAX 255
#define SPIFFOS_BASIC_RESPONSE_CAPACITY 80
#define SPIFFOS_BASIC_VARIABLE_MAX 32
#define SPIFFOS_BASIC_NAME_MAX 31
#define SPIFFOS_BASIC_DEPTH_MAX 32

struct spiffos_basic_variable {
    char name[SPIFFOS_BASIC_NAME_MAX + 1];
    int64_t value;
};

struct spiffos_basic_session {
    struct spiffos_basic_variable variables[SPIFFOS_BASIC_VARIABLE_MAX];
    size_t variable_count;
};

/* Evaluate length input bytes from line. The non-overlapping session,
 * line, and response buffers are caller-owned. Initialize the session to
 * zero. Write one NUL-terminated result or error without a newline, or an
 * empty response for blank input or successful assignment. Failed input
 * leaves session intact. Serialize access to each individual session. */
void spiffos_basic_execute(struct spiffos_basic_session *session, const char *line,
                          size_t length,
                          char response[static SPIFFOS_BASIC_RESPONSE_CAPACITY]);

#endif /* SPIFFOS_BASIC_INTERACTIVE_H */
