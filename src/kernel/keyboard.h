/* Purpose: pure translated-set-1 keyboard decoder for the boot diagnostic.
 * Dependencies: fixed-width C types only. State belongs to the caller.
 * Invariants: one caller, no interrupts/allocation; no hardware access.
 * See docs/decisions/ADR-qemu-keyboard-input.md. */
#ifndef SPIFFOS_KEYBOARD_H
#define SPIFFOS_KEYBOARD_H
#include <stdint.h>
struct keyboard_decoder { uint8_t modifiers, skip; int extended; };
/* Clear caller-owned decoder; no allocation, I/O or failure. Single caller. */
void keyboard_decoder_reset(struct keyboard_decoder *decoder);
/* Consume one translated set-1 byte; return accepted ASCII or zero for none.
 * Releases/prefixes/modifiers never return text. Uppercase US subset only.
 * Borrow non-NULL state; synchronous and no ownership transfer. */
unsigned char keyboard_decode(struct keyboard_decoder *decoder, uint8_t byte);
#endif
