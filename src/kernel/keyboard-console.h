/* Purpose: bounded current-line editing on the diagnostic text console.
 * Dependencies: text-console. Caller-owned state, no heap/history/BASIC.
 * Invariants: one row per editable line, no concurrent/interrupt use.
 * See docs/decisions/ADR-qemu-keyboard-input.md. */
#ifndef SPIFFOS_KEYBOARD_CONSOLE_H
#define SPIFFOS_KEYBOARD_CONSOLE_H
#include <stdbool.h>
#include <stdint.h>
struct text_console;
struct keyboard_console { struct text_console *console; uint64_t length, limit; };
/* Start input on the next row. Borrow console throughout editing lifetime.
 * False on invalid console/output failure; caller must stop editing. No heap. */
bool keyboard_console_initialize(struct keyboard_console *input, struct text_console *console);
/* Apply supported ASCII byte. True only when accepted; false for line limit,
 * Backspace at start, unsupported byte or drawing failure. Enter resets length.
 * No command execution or logo restoration; synchronous, single caller. */
bool keyboard_console_accept(struct keyboard_console *input, unsigned char character);
#endif
