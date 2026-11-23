/* Purpose: host the BASIC preview on standard input and output.
 * Dependencies: hosted C11 stdio and the device-independent line evaluator.
 * Invariants: one session per process, one prompt per submitted line,
 * fixed input storage, and no partial evaluation of an overlong line.
 * Reference: docs/basic/interactive-preview.md. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "interactive.h"

/* Write one caller-owned NUL-terminated response followed by a newline.
 * Return false if either write fails. The caller flushes after the prompt. */
static bool write_line(const char *text)
{
    return fputs(text, stdout) != EOF && fputc('\n', stdout) != EOF;
}

/* Run one session from stdin and send results to stdout until EOF.
 * No arguments. Return failure on input, output, or flush errors.
 * Storage is local to this process and never survives the session. */
int main(void)
{
    struct spiffos_basic_session session = {0};
    /* The extra byte holds a possible CR at the maximum payload length. */
    char line[SPIFFOS_BASIC_LINE_MAX + 1];

    if (!write_line("READY.") || fflush(stdout) == EOF)
        return EXIT_FAILURE;

    for (;;) {
        size_t length = 0;
        bool excess = false;
        int byte;
        while ((byte = fgetc(stdin)) != EOF && byte != '\n') {
            if (length < sizeof line)
                line[length++] = (char)byte;
            else
                excess = true; /* Drain this line before the next prompt. */
        }

        if (ferror(stdin))
            return EXIT_FAILURE;
        if (byte == EOF && length == 0)
            return EXIT_SUCCESS;
        if (byte == '\n' && length != 0 && line[length - 1] == '\r')
            --length;

        char response[SPIFFOS_BASIC_RESPONSE_CAPACITY];
        const char *text = "ERROR: Line too long";
        if (!excess && length <= SPIFFOS_BASIC_LINE_MAX) {
            spiffos_basic_execute(&session, line, length, response);
            text = response;
        }
        if ((*text != '\0' && !write_line(text)) ||
            !write_line("READY.") || fflush(stdout) == EOF)
            return EXIT_FAILURE;
        if (byte == EOF)
            return EXIT_SUCCESS;
    }
}
