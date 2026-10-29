/* Purpose: turn set-1 make/break bytes into a small uppercase text subset.
 * Dependencies: keyboard.h. No ports, framebuffer, heap or host runtime.
 * Invariants: caller owns state; single CPU, interrupts disabled.
 * See docs/decisions/ADR-qemu-keyboard-input.md for scope and limitations. */
#include "keyboard.h"
enum { SHIFT_LEFT=1, SHIFT_RIGHT=2, CTRL_LEFT=4, CTRL_RIGHT=8,
       ALT_LEFT=16, ALT_RIGHT=32, SHIFT_MASK=3, CHORD_MASK=60 };
/* Reset all modifier/prefix state after startup or a corrupted byte stream.
 * Borrow non-NULL state; no I/O, allocation or concurrency. */
void keyboard_decoder_reset(struct keyboard_decoder *decoder)
{
    decoder->modifiers = 0;
    decoder->skip = 0;
    decoder->extended = 0;
}
/* Consume a byte and return ASCII, or zero when it is not supported text.
 * Break bit 7 marks release; low seven bits identify the physical key.
 * Borrow non-NULL state; no I/O, allocation or concurrent use. */
unsigned char keyboard_decode(struct keyboard_decoder *decoder, uint8_t byte)
{
    if (byte == 0 || byte == 0xff || byte == 0xfa || byte == 0xfe) {
        keyboard_decoder_reset(decoder);
        return 0;
    }
    if (decoder->skip != 0) { --decoder->skip; return 0; }
    if (byte == 0xe1) { decoder->skip = 5; decoder->extended = 0; return 0; }
    if (byte == 0xe0) { decoder->extended = 1; return 0; }
    uint8_t code = byte & 0x7f;
    int extended = decoder->extended;
    decoder->extended = 0;
    uint8_t modifier = 0;
    if (code == 0x1d) modifier = extended ? CTRL_RIGHT : CTRL_LEFT;
    if (code == 0x38) modifier = extended ? ALT_RIGHT : ALT_LEFT;
    if (!extended && code == 0x2a) modifier = SHIFT_LEFT;
    if (!extended && code == 0x36) modifier = SHIFT_RIGHT;
    if (modifier != 0) {
        if ((byte & 0x80) != 0) decoder->modifiers &= (uint8_t)~modifier;
        else decoder->modifiers |= modifier;
        return 0;
    }
    if (extended || (byte & 0x80) != 0 ||
        (decoder->modifiers & CHORD_MASK) != 0) return 0;
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
    if (letters[code] != 0) return letters[code];
    if (code == 0x1c) return '\n';
    if (code == 0x0e) return '\b';
    if (code == 0x39) return ' ';
    if ((decoder->modifiers & SHIFT_MASK) != 0) {
        if (code == 0x27) return ':';
        if (code == 0x35) return '?';
        return 0;
    }
    if (code >= 0x02 && code <= 0x0a) return (unsigned char)('1' + code - 2);
    if (code == 0x0b) return '0';
    if (code == 0x34) return '.';
    return 0;
}
