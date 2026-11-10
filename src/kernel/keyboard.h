/* Purpose: pure translated-set-1 keyboard decoder for the boot diagnostic.
 * Dependencies: fixed-width C types only. State belongs to the caller.
 * Invariants: one caller, no interrupts/allocation; no hardware access.
 * See docs/decisions/ADR-qemu-keyboard-input.md and the screen-editor ADR. */
#ifndef SPIFFOS_KEYBOARD_H
#define SPIFFOS_KEYBOARD_H
#include <stdbool.h>
#include <stdint.h>

struct keyboard_decoder {
    uint8_t modifiers;
    uint8_t skip;
    int extended;
    bool num_lock;
    bool num_lock_down;
};

enum keyboard_event_type {
    KEYBOARD_EVENT_NONE,
    KEYBOARD_EVENT_CHARACTER,
    KEYBOARD_EVENT_ENTER,
    KEYBOARD_EVENT_BACKSPACE,
    KEYBOARD_EVENT_LEFT,
    KEYBOARD_EVENT_RIGHT,
    KEYBOARD_EVENT_UP,
    KEYBOARD_EVENT_DOWN,
    KEYBOARD_EVENT_DELETE,
    KEYBOARD_EVENT_INSERT
};

struct keyboard_event {
    enum keyboard_event_type type;
    unsigned char character;
};

/* Clear caller-owned decoder; NumLock starts on, without changing keyboard
 * LEDs. No allocation, I/O or failure. Single caller. */
void keyboard_decoder_reset(struct keyboard_decoder *decoder);

/* Consume one translated set-1 byte and emit at most one make event. Breaks,
 * prefixes and modifiers update state but return false. E0 arrows and Insert
 * and Delete are named events. Keypad digits/operators/Enter are text; NumLock
 * selects digits versus supported navigation, temporarily reversed by Shift.
 * Borrow caller state/output; no hardware LED synchronization. */
bool keyboard_decode_event(struct keyboard_decoder *decoder, uint8_t byte,
    struct keyboard_event *event);

/* Compatibility helper for text-only callers and legacy tests. Named keys
 * return zero; Enter/Backspace retain their historical byte values. */
unsigned char keyboard_decode(struct keyboard_decoder *decoder, uint8_t byte);

#endif
