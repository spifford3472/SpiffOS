/*
 * Purpose: prove native C entry via COM1 and draw a boot framebuffer pattern.
 * Dependencies: pinned official limine.h, Limine-supplied stack and mappings.
 * Invariants: ring 0, one CPU, interrupts disabled; no heap or host C library.
 * See ADR-011 and docs/operations/first-boot-2026-10-28.md.
 * This smoke test does not implement BASIC, user processes or storage.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include "framebuffer.h"

/* 'used' retains these in the object; linker KEEP retains them in the ELF.
 * Limine writes the base-revision array, hence volatile and writable storage. */
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
/* Pinned v10.8.5 supports revision 5; the newer template requests 6. */
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(5);
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

/* Limine fills response if a framebuffer is available; no resolution is forced.
 * The official request ID and revision-0 fields come from the pinned header. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

enum {
    COM1_DATA = 0x3f8, COM1_INTERRUPT_ENABLE = 0x3f9,
    COM1_FIFO_CONTROL = 0x3fa, COM1_LINE_CONTROL = 0x3fb,
    COM1_MODEM_CONTROL = 0x3fc, COM1_LINE_STATUS = 0x3fd,
    UART_DIVISOR_LOW = 1, UART_DIVISOR_HIGH = 0,
    UART_DLAB = 0x80, UART_8N1 = 0x03,
    UART_FIFO_ENABLE_CLEAR = 0x07, UART_DTR_RTS = 0x03,
    UART_TRANSMITTER_EMPTY = 0x20, UART_POLL_LIMIT = 1000000
};

/* Write one byte to an x86 I/O port (port number, not a memory address).
 * Ring 0 only; immediate hardware side effect; no ownership or allocation.
 * a selects AL; Nd selects an immediate port or DX. Memory clobber prevents
 * compiler reordering of surrounding memory accesses across the operation. */
static void port_write(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

/* Read and return one I/O-port byte. Ring 0 only; no allocation or errors.
 * The output constraint tells C that the instruction supplies AL's value. */
static uint8_t port_read(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

/* Configure COM1 for 115200 baud, eight data bits, no parity, one stop bit.
 * No return/error indication; assumes a PC-compatible 16550 UART.
 * Startup CPU only with interrupts disabled; takes no ownership. */
static void serial_initialize(void)
{
    /* Clear DLAB first so the next write addresses IER, not divisor high. */
    port_write(COM1_LINE_CONTROL, UART_8N1);
    port_write(COM1_INTERRUPT_ENABLE, 0);
    /* DLAB temporarily remaps base+0/base+1 to divisor low/high.
     * A divisor of 1 divides the UART's 115200 baud reference by one. */
    port_write(COM1_LINE_CONTROL, UART_DLAB | UART_8N1);
    port_write(COM1_DATA, UART_DIVISOR_LOW);
    port_write(COM1_INTERRUPT_ENABLE, UART_DIVISOR_HIGH);
    port_write(COM1_LINE_CONTROL, UART_8N1);
    port_write(COM1_FIFO_CONTROL, UART_FIFO_ENABLE_CLEAR);
    port_write(COM1_MODEM_CONTROL, UART_DTR_RTS);
}

/* Send a borrowed NUL-terminated string; true means every byte was queued.
 * False means polling exhausted; output may be partial. Bound is iterations,
 * not wall-clock time. Startup-only, no interrupts, allocation or locking. */
static bool serial_write(const char *text)
{
    for (; *text != '\0'; ++text) {
        unsigned int polls = 0;
        while ((port_read(COM1_LINE_STATUS) & UART_TRANSMITTER_EMPTY) == 0) {
            if (++polls == UART_POLL_LIMIT) {
                return false;
            }
        }
        port_write(COM1_DATA, (uint8_t)*text);
    }
    return true;
}

/* Stop this CPU forever. No arguments, return, allocation or error path.
 * CLI keeps interrupts disabled until a future kernel installs an IDT.
 * HLT waits without busy-spinning; the loop also handles an NMI wake-up. */
__attribute__((noreturn))
static void halt_forever(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

/* ELF entry called using Limine's x86-64 C-compatible stack and mappings.
 * No arguments or return; owns no allocated resources. Single startup CPU.
 * Unsupported protocol or failed UART output halts without a success claim. */
__attribute__((noreturn))
void kmain(void)
{
    serial_initialize();
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        (void)serial_write("SPIFFOS_BOOT_ERROR: unsupported Limine revision\r\n");
        halt_forever();
    }
    /* The response and its pointer array are owned by the bootloader. Borrow
     * them while those mappings remain intact; never free or reclaim them here. */
    const struct limine_framebuffer_response *response = framebuffer_request.response;
    bool drawn = response != NULL && response->framebuffer_count > 0 &&
                 response->framebuffers != NULL &&
                 framebuffer_draw_boot_pattern(response->framebuffers[0]);
    if (drawn) {
        (void)serial_write("\r\nSPIFFOS_FRAMEBUFFER_OK\r\n");
        if (framebuffer_draw_boot_title(response->framebuffers[0])) {
            (void)serial_write("SPIFFOS_TEXT_OK\r\n");
        } else {
            (void)serial_write("SPIFFOS_TEXT_UNAVAILABLE\r\n");
        }
    } else {
        (void)serial_write("\r\nSPIFFOS_FRAMEBUFFER_UNAVAILABLE\r\n");
    }
    if (!serial_write("\r\nSPIFFOS_BOOT_OK\r\n")) {
        halt_forever();
    }
    halt_forever();
}
