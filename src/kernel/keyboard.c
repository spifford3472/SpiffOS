/* Purpose: decode translated set-1 bytes into text and editor key events.
 * Dependencies: keyboard.h. No ports, framebuffer, heap or host runtime.
 * Invariants: caller owns state; single CPU, interrupts disabled.
 * References: keyboard and screen-editor ADRs in docs/decisions. */
#include <stddef.h>
#include "keyboard.h"

enum {
    SHIFT_LEFT = 1,
    SHIFT_RIGHT = 2,
    CTRL_LEFT = 4,
    CTRL_RIGHT = 8,
    ALT_LEFT = 16,
    ALT_RIGHT = 32,
    SHIFT_MASK = 3,
    CHORD_MASK = 60
};

/* Reset all modifier/prefix state after startup or a corrupted byte stream.
 * Borrow non-NULL state; no I/O, allocation or concurrency. */
void keyboard_decoder_reset(struct keyboard_decoder *decoder)
{
    decoder->modifiers = 0;
    decoder->skip = 0;
    decoder->num_lock = true;
    decoder->num_lock_down = false;
    decoder->extended = 0;
}

/* Store one event in caller-owned output. This small helper centralizes the
 * invariant that named keys never carry a stray text byte. */
static bool emit_event(struct keyboard_event *event,
    enum keyboard_event_type type, unsigned char character)
{
    event->type = type;
    event->character = character;
    return true;
}

/* Consume a byte and emit one supported make event. Break bit 7 marks release;
 * E0 names extended keys. Ctrl/Alt chords are intentionally filtered before
 * arrows/Insert so future shortcuts cannot leak into the editor. */
bool keyboard_decode_event(struct keyboard_decoder *decoder, uint8_t byte,
    struct keyboard_event *event)
{
    if (decoder == NULL || event == NULL) {
        return false;
    }
    event->type = KEYBOARD_EVENT_NONE;
    event->character = 0;
    if (byte == 0 || byte == 0xff || byte == 0xfa || byte == 0xfe) {
        keyboard_decoder_reset(decoder);
        return false;
    }
    if (decoder->skip != 0) {
        --decoder->skip;
        return false;
    }
    if (byte == 0xe1) {
        decoder->skip = 5;
        decoder->extended = 0;
        return false;
    }
    if (byte == 0xe0) {
        decoder->extended = 1;
        return false;
    }

    uint8_t code = byte & UINT8_C(0x7f);
    bool released = (byte & UINT8_C(0x80)) != 0;
    int extended = decoder->extended;
    decoder->extended = 0;
    uint8_t modifier = 0;
    if (code == 0x1d) modifier = extended ? CTRL_RIGHT : CTRL_LEFT;
    if (code == 0x38) modifier = extended ? ALT_RIGHT : ALT_LEFT;
    if (!extended && code == 0x2a) modifier = SHIFT_LEFT;
    if (!extended && code == 0x36) modifier = SHIFT_RIGHT;
    if (modifier != 0) {
        if (released) {
            decoder->modifiers &= (uint8_t)~modifier;
        } else {
            decoder->modifiers |= modifier;
        }
        return false;
    }
    /* Lock keys latch until break, so make autorepeat cannot toggle repeatedly.
     * No LED/host lock state is assumed; reset starts in numeric keypad mode. */
    if (!extended && code == 0x45) {
        if (released) decoder->num_lock_down = false;
        else if (!decoder->num_lock_down && !(decoder->modifiers & CHORD_MASK)) {
            decoder->num_lock = !decoder->num_lock;
            decoder->num_lock_down = true;
        }
        return false;
    }
    /* F1..F9 are the exclusive workspace selectors. Set-1 scancodes
     * 0x3b..0x43 are consecutive; do not interpret break codes or E0 keys.
     * Desktop environments may reserve Ctrl+Alt+digits before QEMU receives
     * them, so this path deliberately needs no modifiers. */
    if (!released && !extended &&
        (decoder->modifiers & CHORD_MASK) == 0 &&
        code >= 0x3b && code <= 0x43) {
        return emit_event(event, KEYBOARD_EVENT_WORKSPACE,
                          (unsigned char)(code - 0x3a));
    }
    /* F10 is a system view; F12 requests a transient workspace indicator.
     * F11 is reserved. Ignore releases and modified function keys. */
    if (!released && !extended && !(decoder->modifiers & CHORD_MASK)) {
        if (code == 0x44) return emit_event(event, KEYBOARD_EVENT_SYSTEM, 0);
        if (code == 0x58) return emit_event(event, KEYBOARD_EVENT_IDENTIFY, 0);
    }
    if (released || (decoder->modifiers & CHORD_MASK) != 0) {
        return false;
    }
    if (extended) {
        if (code == 0x1c) return emit_event(event, KEYBOARD_EVENT_ENTER, 0);
        if (code == 0x35) return emit_event(event, KEYBOARD_EVENT_CHARACTER, '/');
        if (code == 0x53) return emit_event(event, KEYBOARD_EVENT_DELETE, 0);
        if (code == 0x4b) return emit_event(event, KEYBOARD_EVENT_LEFT, 0);
        if (code == 0x4d) return emit_event(event, KEYBOARD_EVENT_RIGHT, 0);
        if (code == 0x48) return emit_event(event, KEYBOARD_EVENT_UP, 0);
        if (code == 0x50) return emit_event(event, KEYBOARD_EVENT_DOWN, 0);
        if (code == 0x52) return emit_event(event, KEYBOARD_EVENT_INSERT, 0);
        return false;
    }

    /* Keypad operators ignore NumLock; Shift temporarily reverses digit mode. */
    if (code == 0x37) return emit_event(event, KEYBOARD_EVENT_CHARACTER, '*');
    if (code == 0x4a) return emit_event(event, KEYBOARD_EVENT_CHARACTER, '-');
    if (code == 0x4e) return emit_event(event, KEYBOARD_EVENT_CHARACTER, '+');
    static const unsigned char keypad[128] = {
        [0x47]='7',[0x48]='8',[0x49]='9',[0x4b]='4',[0x4c]='5',
        [0x4d]='6',[0x4f]='1',[0x50]='2',[0x51]='3',[0x52]='0',[0x53]='.'
    };
    if (keypad[code]) {
        bool numeric = decoder->num_lock != ((decoder->modifiers & SHIFT_MASK) != 0);
        if (numeric) return emit_event(event, KEYBOARD_EVENT_CHARACTER, keypad[code]);
        if (code == 0x48) return emit_event(event, KEYBOARD_EVENT_UP, 0);
        if (code == 0x50) return emit_event(event, KEYBOARD_EVENT_DOWN, 0);
        if (code == 0x4b) return emit_event(event, KEYBOARD_EVENT_LEFT, 0);
        if (code == 0x4d) return emit_event(event, KEYBOARD_EVENT_RIGHT, 0);
        if (code == 0x52) return emit_event(event, KEYBOARD_EVENT_INSERT, 0);
        if (code == 0x53) return emit_event(event, KEYBOARD_EVENT_DELETE, 0);
        return false;
    }
    /* Sparse designated entries describe physical US keys, not ASCII indexes.
     * Unlisted keys are zero-initialized and intentionally ignored. */
    static const unsigned char letters[128] = {
        [0x10]='Q',[0x11]='W',[0x12]='E',[0x13]='R',[0x14]='T',
        [0x15]='Y',[0x16]='U',[0x17]='I',[0x18]='O',[0x19]='P',
        [0x1e]='A',[0x1f]='S',[0x20]='D',[0x21]='F',[0x22]='G',
        [0x23]='H',[0x24]='J',[0x25]='K',[0x26]='L',
        [0x2c]='Z',[0x2d]='X',[0x2e]='C',[0x2f]='V',
        [0x30]='B',[0x31]='N',[0x32]='M'
    };
    if (letters[code] != 0) {
        return emit_event(event, KEYBOARD_EVENT_CHARACTER, letters[code]);
    }
    if (code == 0x1c) return emit_event(event, KEYBOARD_EVENT_ENTER, 0);
    if (code == 0x0e) return emit_event(event, KEYBOARD_EVENT_BACKSPACE, 0);
    if (code == 0x39) return emit_event(event, KEYBOARD_EVENT_CHARACTER, ' ');
    static const unsigned char plain[128] = {
        [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
        [12]='-',[13]='=',[26]='[',[27]=']',[39]=';',[40]=39,[41]='`',[43]=92,[51]=',',[52]='.',[53]='/'
    };
    static const unsigned char shifted[128] = {
        [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',[10]='(',[11]=')',
        [12]='_',[13]='+',[26]='{',[27]='}',[39]=':',[40]='"',[41]='~',[43]='|',[51]='<',[52]='>',[53]='?'
    };
    unsigned char text = (decoder->modifiers & SHIFT_MASK) ? shifted[code] : plain[code];
    if (text) return emit_event(event, KEYBOARD_EVENT_CHARACTER, text);
    return false;
}

/* Adapt event decoding for the earlier byte-oriented interface. The decoder is
 * consumed exactly once; named navigation/mode events intentionally map to 0. */
unsigned char keyboard_decode(struct keyboard_decoder *decoder, uint8_t byte)
{
    struct keyboard_event event;
    if (!keyboard_decode_event(decoder, byte, &event)) {
        return 0;
    }
    if (event.type == KEYBOARD_EVENT_CHARACTER) {
        return event.character;
    }
    if (event.type == KEYBOARD_EVENT_ENTER) {
        return '\n';
    }
    if (event.type == KEYBOARD_EVENT_BACKSPACE) {
        return '\b';
    }
    return 0;
}
