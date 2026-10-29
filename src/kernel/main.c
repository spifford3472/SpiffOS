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
#include "text-console.h"
#include "boot-logo.h"

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

#ifdef SPIFFOS_SCROLL_TEST
/* Write an unsigned line number in decimal without a host printf/library.
 * Borrow console; false on output error, prefix may be printed. Stack buffer
 * lives through synchronous write. Startup-only, no heap or interrupts. */
static bool console_write_number(struct text_console *console, uint64_t number)
{
    char digits[21]; /* 20 decimal digits fit every uint64_t, plus terminator. */
    unsigned int first = 20;
    digits[first] = '\0';
    do {
        digits[--first] = (char)('0' + number % 10);
        number /= 10;
    } while (number != 0);
    return text_console_write(console, &digits[first]);
}

/* Print more numbered lines than this viewport can retain, then a final label.
 * Borrow initialized console; false means output failed. No delay, allocation
 * or ownership transfer; startup CPU only. Successful output forces scrolling. */
static bool console_scroll_demo(struct text_console *console)
{
    if (!text_console_write(console, "SPIFFOS CONSOLE\nSCROLL TEST START\n")) {
        return false;
    }
    /* Initialization bounds rows*16, so rows+2 cannot overflow uint64_t. */
    for (uint64_t line = 0; line < console->rows + 2; ++line) {
        if (!text_console_write(console, "LINE ") ||
            !console_write_number(console, line) ||
            !text_console_putc(console, '\n')) {
            return false;
        }
    }
    return text_console_write(console, "SCROLLING OK.");
}

#endif

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
    const struct limine_framebuffer *screen = NULL;
    if (response != NULL && response->framebuffer_count > 0 && response->framebuffers != NULL) {
        screen = response->framebuffers[0];
    }
    bool drawn = screen != NULL && framebuffer_fill_rectangle(screen,
        0, 0, screen->width, screen->height, 0x091b35);
    if (drawn) {
        (void)serial_write("\r\nSPIFFOS_FRAMEBUFFER_OK\r\n");
        struct text_console console;
        bool logo_ok = boot_workspace_reset(&console, screen);
        (void)serial_write(logo_ok ? "SPIFFOS_LOGO_OK\r\n" : "SPIFFOS_LOGO_UNAVAILABLE\r\n");
        bool console_ok = false;
        if (logo_ok) {
#ifdef SPIFFOS_SCROLL_TEST
            /* Test build only: force the initial image out of the full workspace. */
            console_ok = console_scroll_demo(&console);
#else
            /* Normal boot leaves initial content visible, ready for later output. */
            console_ok = text_console_write(&console,
                "SPIFFOS WORKSPACE\nOUTPUT SCROLLS THE LOGO.");
#endif
        }
        if (console_ok) {
            (void)serial_write("SPIFFOS_TEXT_OK\r\nSPIFFOS_CONSOLE_OK\r\n");
#ifdef SPIFFOS_SCROLL_TEST
            (void)serial_write("SPIFFOS_SCROLL_OK\r\n");
#endif
        } else {
            (void)serial_write("SPIFFOS_CONSOLE_UNAVAILABLE\r\n");
        }
    } else {
        (void)serial_write("\r\nSPIFFOS_FRAMEBUFFER_UNAVAILABLE\r\n");
    }
    if (!serial_write("\r\nSPIFFOS_BOOT_OK\r\n")) {
        halt_forever();
    }
    halt_forever();
}
