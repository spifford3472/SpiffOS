/* Purpose: configure and poll QEMU's PC keyboard controller without IRQs.
 * Dependencies: injected port callbacks. No physical disk/output-port writes.
 * Invariants: startup CPU only; bounded waits; no allocation or concurrency.
 * References: ADR-qemu-keyboard-input; QEMU hw/input/pckbd.c and ps2.c.
 * Port 0x64 reads status/writes commands; port 0x60 reads/writes data.
 * These are x86 I/O addresses, not memory pointers. */
#include <stddef.h>
#include "keyboard-ps2.h"
enum { DATA=0x60, STATUS=0x64, OUTPUT_FULL=1, INPUT_FULL=2,
       AUXILIARY=0x20, BAD_BYTE=0xc0, POLL_LIMIT=100000,
       DISABLE_KEYBOARD=0xad, DISABLE_MOUSE=0xa7, ENABLE_KEYBOARD=0xae,
       READ_CONFIG=0x20, WRITE_CONFIG=0x60, TRANSLATE=0x40,
       IRQ_BITS=3, KEYBOARD_DISABLED=0x10, MOUSE_DISABLED=0x20,
       ACK=0xfa, RESEND=0xfe, DISABLE_SCAN=0xf5, SET_SCAN=0xf0,
       SCAN_SET_2=2, ENABLE_SCAN=0xf4 };
/* Wait for host-to-controller buffer space then write one byte.
 * False means bounded wait exhausted, with no write. Borrow I/O; single CPU. */
static bool write_ready(const struct keyboard_io *io, uint16_t port, uint8_t byte)
{
    for (unsigned int n=0; n<POLL_LIMIT; ++n) {
        if ((io->read(io->context, STATUS) & INPUT_FULL) == 0) {
            io->write(io->context, port, byte);
            return true;
        }
    }
    return false;
}
/* Return one clean keyboard/controller byte; discard AUX/errors while waiting.
 * False means bounded wait exhausted. Borrow I/O/output; single CPU, no heap. */
static bool read_reply(const struct keyboard_io *io, uint8_t *byte)
{
    for (unsigned int n=0; n<POLL_LIMIT; ++n) {
        uint8_t status=io->read(io->context, STATUS);
        if ((status & OUTPUT_FULL) != 0) {
            uint8_t data=io->read(io->context, DATA);
            if ((status & (AUXILIARY|BAD_BYTE)) == 0) {
                *byte=data;
                return true;
            }
        }
    }
    return false;
}
/* Send a device byte and require ACK; retry RESEND at most twice.
 * False on timeout/other reply. Borrow I/O; device side effects, single CPU. */
static bool device_command(const struct keyboard_io *io, uint8_t command)
{
    for (unsigned int attempt=0; attempt<3; ++attempt) {
        uint8_t reply=0;
        if (!write_ready(io, DATA, command) || !read_reply(io, &reply)) return false;
        if (reply==ACK) return true;
        if (reply!=RESEND) return false;
    }
    return false;
}
/* Initialize translated set 1 explicitly, rather than trusting firmware state.
 * Preserve unrelated config bits. Never touch controller output port/A20/reset.
 * False leaves input unusable; caller must not poll it. No heap/interrupts. */
bool keyboard_ps2_initialize(const struct keyboard_io *io)
{
    if (io==NULL || io->read==NULL || io->write==NULL) return false;
    if (!write_ready(io, STATUS, DISABLE_KEYBOARD) ||
        !write_ready(io, STATUS, DISABLE_MOUSE)) return false;
    /* A finite drain prevents stale firmware keystrokes/ACKs becoming text. */
    unsigned int n;
    for (n=0; n<256; ++n) {
        if ((io->read(io->context, STATUS) & OUTPUT_FULL)==0) break;
        (void)io->read(io->context, DATA);
    }
    if (n==256) return false;
    uint8_t config=0;
    if (!write_ready(io, STATUS, READ_CONFIG) || !read_reply(io, &config)) return false;
    config=(uint8_t)((config & (uint8_t)~(IRQ_BITS|TRANSLATE|KEYBOARD_DISABLED)) |
                    MOUSE_DISABLED);
    if (!write_ready(io, STATUS, WRITE_CONFIG) ||
        !write_ready(io, DATA, config) ||
        !write_ready(io, STATUS, ENABLE_KEYBOARD) ||
        !device_command(io, DISABLE_SCAN) ||
        !device_command(io, SET_SCAN) ||
        !device_command(io, SCAN_SET_2)) return false;
    /* Keyboard emits set 2; controller translates it into our decoder's set 1. */
    if (!write_ready(io, STATUS, WRITE_CONFIG) ||
        !write_ready(io, DATA, (uint8_t)(config|TRANSLATE)) ||
        !device_command(io, ENABLE_SCAN)) return false;
    return true;
}
/* Poll once; status belongs to the data byte before it is consumed.
 * Discard mouse/error bytes. Caller must reset its decoder on DROPPED.
 * Valid initialized I/O/output required; no waits, heap or ownership transfer. */
enum keyboard_poll keyboard_ps2_poll(const struct keyboard_io *io, uint8_t *byte)
{
    uint8_t status=io->read(io->context, STATUS);
    if ((status & OUTPUT_FULL)==0) return KEYBOARD_NONE;
    uint8_t data=io->read(io->context, DATA);
    if ((status & (AUXILIARY|BAD_BYTE))!=0) return KEYBOARD_DROPPED;
    *byte=data;
    return KEYBOARD_BYTE;
}
