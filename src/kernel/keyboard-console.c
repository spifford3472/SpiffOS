/* Purpose: echo keyboard text and constrain Backspace to the active input row.
 * Dependencies: text-console. No hardware, heap, history or command execution.
 * Invariants: at most min(63, columns-1) printable bytes per line.
 * References: docs/decisions/ADR-qemu-keyboard-input.md. */
#include <stddef.h>
#include "text-console.h"
#include "keyboard-console.h"
/* Borrow initialized console, move to a fresh row and set the character limit.
 * False disables input; screen output may have advanced on failure. No heap. */
bool keyboard_console_initialize(struct keyboard_console *input, struct text_console *console)
{
    if (input==NULL) return false;
    input->console=NULL;
    input->length=0;
    input->limit=0;
    if (console==NULL || !console->initialized || console->columns<2 ||
        !text_console_putc(console, '\n')) return false;
    input->limit=console->columns-1;
    if (input->limit>63) input->limit=63;
    input->console=console;
    return true;
}
/* Edit only the current unsubmitted line. Leaving one spare column prevents
 * wrapping, so Backspace cannot reach older output or the initial artwork.
 * False means ignored/failed; no heap or ownership transfer, single caller. */
bool keyboard_console_accept(struct keyboard_console *input, unsigned char character)
{
    if (input==NULL || input->console==NULL) return false;
    struct text_console *console=input->console;
    if (character=='\b') {
        if (input->length==0) return false;
        uint64_t column=console->column-1;
        if (!text_console_position(console, column, console->row) ||
            !text_console_putc(console, ' ') ||
            !text_console_position(console, column, console->row)) return false;
        --input->length;
        return true;
    }
    if (character=='\n') {
        if (!text_console_putc(console, character)) return false;
        input->length=0;
        return true;
    }
    bool supported=(character>='A' && character<='Z') ||
        (character>='0' && character<='9') || character==' ' ||
        character=='.' || character==':' || character=='?';
    if (!supported || input->length>=input->limit) return false;
    if (!text_console_putc(console, character)) return false;
    ++input->length;
    return true;
}
