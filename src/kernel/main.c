/*
 * Purpose: boot via Limine, draw initial workspace and poll diagnostic input.
 * Dependencies: pinned official limine.h, Limine-supplied stack and mappings.
 * Invariants: ring 0, one CPU, interrupts disabled; no heap or host C library.
 * See ADR-011, ADR-memory-services and first-boot-2026-10-28.md.
 * This smoke test does not implement BASIC, user processes or storage.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limine.h>
#include "framebuffer.h"
#include "text-console.h"
#include "boot-logo.h"
#include "keyboard.h"
#include "keyboard-ps2.h"
#include "screen-editor.h"
#include "memory-boot.h"
#include "workspace-manager.h"
#include "x86-idt.h"
#include "x86-pic.h"
#include "kernel-timer.h"

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

/* Borrow loader-owned map metadata; all non-USABLE regions remain reserved.
 * M1 only reports inventory and retains the existing loader mappings/stack. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

/* Request the loader's higher-half direct map explicitly. Missing or newer
 * revision responses leave M3 unavailable without changing the editor path. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

/* Ask the pinned loader to report either supported x86-64 paging depth. M3
 * derives canonical width only from the validated revision-0 response. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_paging_mode_request paging_mode_request = {
    .id = LIMINE_PAGING_MODE_REQUEST_ID,
    .revision = 0,
    .mode = LIMINE_PAGING_MODE_X86_64_DEFAULT,
    .max_mode = LIMINE_PAGING_MODE_X86_64_5LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL
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

#ifndef SPIFFOS_SCROLL_TEST
/* Adapt the existing x86 port helpers to the keyboard transport callbacks.
 * Context is unused; ring 0 startup CPU only, synchronous hardware access. */
static uint8_t keyboard_port_read(void *context, uint16_t port)
{
    (void)context;
    return port_read(port);
}
/* Write a transport byte; borrowed context unused. Ring 0, IRQs disabled. */
static void keyboard_port_write(void *context, uint16_t port, uint8_t byte)
{
    (void)context;
    port_write(port, byte);
}

/* The screen editor contains about 266 KiB of bounded line storage, so it must
 * live in kernel BSS rather than Limine's small entry stack. One boot CPU owns
 * this diagnostic state for the lifetime of the kernel. */
static struct workspace_manager boot_workspaces;

/* Diagnostic submission boundary: emit the complete counted logical line.
 * context is unused. The callback borrows bytes only during this call.
 * It executes no BASIC and returns false if serial output becomes incomplete. */
static bool keyboard_submit_diagnostic(
    void *context, const unsigned char *bytes, size_t length)
{
    (void)context;
    if (!serial_write("SUBMIT:")) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        char byte_text[2] = {(char)bytes[index], '\0'};
        if (!serial_write(byte_text)) {
            return false;
        }
    }
    return serial_write("\r\n");
}

/* Use PIT channel 2 as a private read-only-in-loop elapsed-time source.
 * Unlike firmware channel 0, its mode is explicitly set to mode 2, with a
 * 65536-tick reload and a count that decrements by ONE per 1.193 MHz tick.
 * Channel 2 has no interrupt output. Preserve the speaker port's prior bits
 * and keep the speaker output disconnected while the overlay is active. */
static uint8_t indicator_speaker_saved;
static void indicator_clock_start(void)
{
    indicator_speaker_saved = port_read(0x61);
    port_write(0x61, (uint8_t)((indicator_speaker_saved | 1u) & ~2u));
    port_write(0x43, 0xb4); /* Channel 2, low/high, mode 2, binary. */
    port_write(0x42, 0);
    port_write(0x42, 0);
}
static void indicator_clock_stop(void)
{
    port_write(0x61, indicator_speaker_saved);
}
static uint16_t pit_read_counter(void)
{
    port_write(0x43, 0x80); /* Latch channel 2 only. */
    uint16_t low = port_read(0x42);
    uint16_t high = port_read(0x42);
    return (uint16_t)(low | (high << 8));
}

/* Run the diagnostic input loop. The decoder emits named navigation/mode
 * events; the editor owns pixels and stored lines. Text keys retain hexadecimal
 * serial evidence while named keys use readable EVENT records. */
__attribute__((noreturn))
static void keyboard_run(struct text_console *console)
{
    const struct keyboard_io io = {
        keyboard_port_read, keyboard_port_write, NULL
    };
    struct keyboard_decoder decoder;
    keyboard_decoder_reset(&decoder);
    if (!keyboard_ps2_initialize(&io) ||
        !workspace_manager_initialize(&boot_workspaces, console,
            keyboard_submit_diagnostic, NULL)) {
        (void)serial_write("SPIFFOS_KEYBOARD_UNAVAILABLE\r\n");
        halt_forever();
    }
    if (!memory_boot_service_editor_check(serial_write)) halt_forever();
    (void)serial_write("SPIFFOS_KEYBOARD_READY\r\n");
    bool first = true;
    bool indicator_timed = false;
    uint16_t previous_pit = 0;
    uint32_t elapsed_pit = 0;
    unsigned int indicator_fade_step = 0;
    for (;;) {
        if (indicator_timed) {
            uint16_t current = pit_read_counter();
            elapsed_pit += (uint16_t)(previous_pit - current);
            previous_pit = current;
            if (elapsed_pit >= 1133523u && indicator_fade_step < 2) {
                workspace_manager_fade_indicator(&boot_workspaces, 2);
                indicator_fade_step = 2;
            } else if (elapsed_pit >= 1014205u && indicator_fade_step < 1) {
                workspace_manager_fade_indicator(&boot_workspaces, 1);
                indicator_fade_step = 1;
            }
            if (elapsed_pit >= 1193182u) {
                workspace_manager_hide_indicator(&boot_workspaces);
                indicator_clock_stop();
                indicator_timed = false;
                (void)serial_write("EVENT:INDICATOR_HIDDEN\r\n");
            }
        }
        uint8_t byte = 0;
        enum keyboard_poll poll = keyboard_ps2_poll(&io, &byte);
        if (poll == KEYBOARD_DROPPED) {
            keyboard_decoder_reset(&decoder);
        }
        if (poll == KEYBOARD_BYTE) {
            if (first) {
                (void)serial_write("SPIFFOS_KEYBOARD_BYTE_OK\r\n");
                first = false;
            }
            struct keyboard_event event;
            if (keyboard_decode_event(&decoder, byte, &event)) {
                if (event.type == KEYBOARD_EVENT_IDENTIFY) {
                    workspace_manager_hide_indicator(&boot_workspaces);
                    if (indicator_timed) indicator_clock_stop();
                    indicator_timed = false;
                    if (workspace_manager_identify(&boot_workspaces)) {
                        indicator_clock_start();
                        previous_pit = pit_read_counter();
                        elapsed_pit = 0;
                        indicator_fade_step = 0;
                        indicator_timed = true;
                        (void)serial_write("EVENT:INDICATOR_OK\r\n");
                    } else (void)serial_write("EVENT:INDICATOR_UNAVAILABLE\r\n");
                    continue;
                }
                if (indicator_timed) {
                    workspace_manager_hide_indicator(&boot_workspaces);
                    indicator_clock_stop();
                    indicator_timed = false;
                }
                if (event.type == KEYBOARD_EVENT_SYSTEM) {
                    (void)serial_write(workspace_manager_system(&boot_workspaces) ?
                        "EVENT:SYSTEM_OK\r\n" : "EVENT:SYSTEM_UNAVAILABLE\r\n");
                    continue;
                }
                if (boot_workspaces.system_visible &&
                    event.type != KEYBOARD_EVENT_WORKSPACE) continue;
                if (event.type == KEYBOARD_EVENT_WORKSPACE) {
                    if (workspace_manager_switch(&boot_workspaces, event.character)) {
                        (void)serial_write("EVENT:WORKSPACE_OK\r\n");
                    } else {
                        (void)serial_write("EVENT:WORKSPACE_UNAVAILABLE\r\n");
                    }
                    continue;
                }
                enum screen_editor_action action = SCREEN_EDITOR_CHARACTER;
                unsigned char character = event.character;
                const char *named = NULL;
                if (event.type == KEYBOARD_EVENT_ENTER) {
                    action = SCREEN_EDITOR_ENTER;
                    character = '\n';
                } else if (event.type == KEYBOARD_EVENT_BACKSPACE) {
                    action = SCREEN_EDITOR_BACKSPACE;
                    character = '\b';
                } else if (event.type == KEYBOARD_EVENT_LEFT) {
                    action = SCREEN_EDITOR_LEFT; named = "EVENT:LEFT\r\n";
                } else if (event.type == KEYBOARD_EVENT_RIGHT) {
                    action = SCREEN_EDITOR_RIGHT; named = "EVENT:RIGHT\r\n";
                } else if (event.type == KEYBOARD_EVENT_UP) {
                    action = SCREEN_EDITOR_UP; named = "EVENT:UP\r\n";
                } else if (event.type == KEYBOARD_EVENT_DOWN) {
                    action = SCREEN_EDITOR_DOWN; named = "EVENT:DOWN\r\n";
                } else if (event.type == KEYBOARD_EVENT_DELETE) {
                    action = SCREEN_EDITOR_DELETE; named = "EVENT:DELETE\r\n";
                } else if (event.type == KEYBOARD_EVENT_INSERT) {
                    action = SCREEN_EDITOR_TOGGLE_INSERT;
                    named = "EVENT:INSERT\r\n";
                }
                enum screen_editor_result accepted =
                    screen_editor_accept(workspace_manager_active(&boot_workspaces), action, character);
                if (accepted == SCREEN_EDITOR_ERROR) {
                    (void)serial_write("SPIFFOS_INPUT_ERROR\r\n");
                    halt_forever();
                }
                if (accepted == SCREEN_EDITOR_FULL &&
                    !serial_write("SPIFFOS_INPUT_FULL\r\n")) {
                    halt_forever();
                }
                if (accepted == SCREEN_EDITOR_ACCEPTED ||
                    accepted == SCREEN_EDITOR_SUBMITTED) {
                    if (named != NULL) {
                        if (!serial_write(named)) {
                            halt_forever();
                        }
                    } else {
                        static const char hex[] = "0123456789ABCDEF";
                        char message[] = "KEY:00\r\n";
                        message[4] = hex[character >> 4];
                        message[5] = hex[character & 15];
                        if (!serial_write(message)) {
                            halt_forever();
                        }
                    }
                }
            }
        }
        __asm__ volatile ("pause" : : : "memory");
    }
}
#endif

/* Fatal CPU exception reporter. The assembly entry gives the normalized
 * exception vector and error code. No allocation, recursion, or callback to
 * screen/driver state is allowed. The UART has already been initialized.
 * Output is bounded (the existing serial writer has a fixed poll limit) and
 * failure simply halts; the handler cannot return to a potentially corrupted
 * instruction stream. Boot CPU only, interrupts disabled. */
__attribute__((noreturn))
void x86_exception_panic(uint64_t vector, uint64_t error)
{
    static const char hex[] = "0123456789ABCDEF";
    char line[] = "SPIFFOS_EXCEPTION:00,0000000000000000\r\n";
    line[18] = hex[(vector >> 4) & 15u];
    line[19] = hex[vector & 15u];
    for (unsigned int n = 0; n < 16; ++n)
        line[21 + n] = hex[(error >> ((15 - n) * 4)) & 15u];
    (void)serial_write(line);
    (void)serial_write("SPIFFOS_EXCEPTION_HALT\r\n");
    halt_forever();
}

/* ELF entry called using Limine's x86-64 C-compatible stack and mappings.
 * No arguments or return; owns no allocated resources. Single startup CPU.
 * Unsupported protocol or failed UART output halts without a success claim. */
__attribute__((noreturn))
void kmain(void)
{
    serial_initialize();
    /* The exception-only IDT has no external IRQ gates. Keep IF cleared and
     * preserve PS/2 polling and PIT2 overlay timing. */
    __asm__ volatile ("cli" : : : "memory");
    if (!x86_idt_initialize()) {
        (void)serial_write("SPIFFOS_IDT_ERROR\r\n");
        halt_forever();
    }
    (void)serial_write("SPIFFOS_IDT_READY\r\n");
    /* PIC remapping occurs only with IF=0. Every external IRQ stays masked;
     * no PIC vector is present in the exception-only IDT yet. */
    if (!x86_pic_initialize_hardware()) {
        (void)serial_write("SPIFFOS_PIC_ERROR\r\n");
        halt_forever();
    }
    (void)serial_write("SPIFFOS_PIC_MASKED\r\n");
    /* A timer state is initialized before hardware IRQs exist. It must remain
     * unavailable until Phase D confirms the first controller tick. */
    if (!kernel_timer_initialize_boot(100u)) {
        (void)serial_write("SPIFFOS_TIMER_ERROR\r\n");
        halt_forever();
    }
    uint64_t early_timer_ms = 0;
    if (kernel_timer_boot_now_ms(&early_timer_ms)) {
        (void)serial_write("SPIFFOS_TIMER_EARLY_ERROR\r\n");
        halt_forever();
    }
    (void)serial_write("SPIFFOS_TIMER_WAITING\r\n");
#ifdef SPIFFOS_DOUBLE_FAULT_TEST
    /* Test-only: poison RSP then execute invalid opcode. The failure to
     * push the #UD frame triggers a genuine #DF, using IST1 from the TSS.
     * Never include this branch in a production kernel image. */
    (void)serial_write("SPIFFOS_DOUBLE_FAULT_TEST_TRIGGER\r\n");
    __asm__ volatile ("xorq %%rsp, %%rsp\n\tud2" : : : "memory");
    halt_forever();
#endif
#ifdef SPIFFOS_EXCEPTION_TEST
    /* TEST-ONLY invalid opcode, never linked in a production ISO. */
    (void)serial_write("SPIFFOS_EXCEPTION_TEST_TRIGGER\r\n");
    __asm__ volatile ("ud2");
#endif
    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
        (void)serial_write("SPIFFOS_BOOT_ERROR: unsupported Limine revision\r\n");
        halt_forever();
    }
    if (!memory_boot_report(memory_request.response, serial_write) ||
        !memory_boot_allocator_probe(serial_write)) {
        halt_forever();
    }
    if (!memory_boot_page_probe(hhdm_request.response,
                                paging_mode_request.response, serial_write)) {
        halt_forever();
    }
    if (!memory_boot_heap_probe(hhdm_request.response,
                                paging_mode_request.response, serial_write)) {
        halt_forever();
    }
    if (!memory_boot_service_start(hhdm_request.response, paging_mode_request.response, serial_write)) {
        halt_forever();
    }
    /* The response and its pointer array are owned by the bootloader. Borrow
     * them while those mappings remain intact; never free or reclaim them here. */
    const struct limine_framebuffer_response *response = framebuffer_request.response;
    const struct limine_framebuffer *screen = NULL;
    if (response != NULL && response->framebuffer_count > 0 && response->framebuffers != NULL) {
        screen = response->framebuffers[0];
    }
    struct text_console console={0};
    bool drawn = screen != NULL && framebuffer_fill_rectangle(screen,
        0, 0, screen->width, screen->height, 0x091b35);
    if (drawn) {
        (void)serial_write("\r\nSPIFFOS_FRAMEBUFFER_OK\r\n");
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
    if (!memory_boot_service_check(serial_write)) halt_forever();
    if (!serial_write("\r\nSPIFFOS_BOOT_OK\r\n")) {
        halt_forever();
    }
#ifndef SPIFFOS_SCROLL_TEST
    if (console.initialized) {
        keyboard_run(&console);
    }
#endif
    halt_forever();
}
