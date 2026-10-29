/* Purpose: bounded i8042/PS2 transport, separated from text decoding.
 * Dependencies: callbacks to real ring-0 ports or a host test fake.
 * Invariants: single caller; no interrupts, allocation or ownership transfer.
 * See docs/decisions/ADR-qemu-keyboard-input.md. */
#ifndef SPIFFOS_KEYBOARD_PS2_H
#define SPIFFOS_KEYBOARD_PS2_H
#include <stdbool.h>
#include <stdint.h>
struct keyboard_io {
    uint8_t (*read)(void *context, uint16_t port);
    void (*write)(void *context, uint16_t port, uint8_t byte);
    void *context;
};
enum keyboard_poll { KEYBOARD_NONE, KEYBOARD_BYTE, KEYBOARD_DROPPED };
/* Configure keyboard set 2 with controller translation to set 1 and IRQs off.
 * Borrow callbacks/context for call lifetime. False on timeout/unexpected ACK.
 * Hardware state may be partially changed on failure; caller must stop input.
 * Bounded iteration waits, not timeouts in milliseconds. Startup CPU only. */
bool keyboard_ps2_initialize(const struct keyboard_io *io);
/* Read at most one available byte. BYTE writes *byte; NONE/DROPPED do not.
 * DROPPED means mouse/parity/timeout byte discarded; reset decoder state.
 * Borrow valid callbacks and non-NULL byte pointer. Single caller, no heap. */
enum keyboard_poll keyboard_ps2_poll(const struct keyboard_io *io, uint8_t *byte);
#endif
