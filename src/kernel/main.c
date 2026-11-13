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
#include "kernel-scheduler-clock.h"
#include "acpi-discovery.h"
#include "apic-preflight.h"
#include "x86-page-walk.h"
#include "mmio-page-policy.h"
#include "apic-memory-type.h"
#include "apic-readonly.h"
#include "apic-irq0.h"
#include "x86-mmio-map.h"

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

/* Ask Limine for a mapped ACPI RSDP, never for arbitrary APIC MMIO.
 * The firmware owns the table bytes for the duration of this boot phase. */
__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
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

/* Map only complete ACPI reclaimable/NVS byte spans using Limine's known
 * HHDM. This is read-only firmware discovery, NOT a generic MMIO mapper.
 * Every arithmetic step rejects overflow. An unrecognized physical region
 * cannot be treated as a valid virtual pointer. Boot CPU, IF=0. */
struct acpi_map_context {
    const struct limine_memmap_response *map;
    uint64_t offset;
};
static const uint8_t *acpi_map_read_only(void *context, uint64_t physical,
                                        size_t bytes)
{
    const struct acpi_map_context *state = context;
    if (state == NULL || state->map == NULL || state->map->entries == NULL ||
        bytes == 0 || bytes > UINT64_MAX - physical ||
        state->offset > UINT64_MAX - physical ||
        bytes > UINT64_MAX - (state->offset + physical))
        return NULL;
    const uint64_t end = physical + bytes;
    for (uint64_t i = 0; i < state->map->entry_count; ++i) {
        const struct limine_memmap_entry *region = state->map->entries[i];
        if (region == NULL || region->length > UINT64_MAX - region->base)
            continue;
        if ((region->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE ||
             region->type == LIMINE_MEMMAP_ACPI_NVS) &&
            physical >= region->base && end <= region->base + region->length)
            return (const uint8_t *)(uintptr_t)(physical + state->offset);
    }
    return NULL;
}

/* Borrow only complete, RAM-backed page-table frames from the validated
 * Limine map. This does not map MMIO or authorize a controller write.
 * Supports bootloader-owned or ordinary RAM table pages; rejects unknown
 * firmware and device address ranges. The mapper never changes ownership. */
static const uint8_t *boot_table_map(void *context, uint64_t physical_page)
{
    const struct acpi_map_context *state=context;
    if (state==NULL || state->map==NULL || state->map->entries==NULL ||
        state->map->entry_count>128 ||
        (physical_page&4095u)!=0 ||
        physical_page>UINT64_MAX-4096 ||
        state->offset>UINT64_MAX-physical_page ||
        state->offset+physical_page>UINT64_MAX-4096)
        return NULL;
    for(uint64_t i=0;i<state->map->entry_count;++i) {
        const struct limine_memmap_entry *region=state->map->entries[i];
        if(region==NULL || region->length>UINT64_MAX-region->base)
            continue;
        if ((region->type==LIMINE_MEMMAP_USABLE ||
             region->type==LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
             region->type==LIMINE_MEMMAP_RESERVED_MAPPED) &&
            physical_page>=region->base &&
            physical_page+4096<=region->base+region->length)
            return (const uint8_t *)(uintptr_t)(physical_page+state->offset);
    }
    return NULL;
}

/* Inspect but never modify the current boot CPU translation tables. Only
 * confirm whether the firmware-reported MMIO locations are already mapped.
 * An address match is NOT permission to access device registers: PAT/MTRR
 * cache attributes still require proof before any APIC initialization. */
static bool apic_boot_pages_present(const struct acpi_apic_topology *topology)
{
    if(topology==NULL || hhdm_request.response==NULL ||
       paging_mode_request.response==NULL || memory_request.response==NULL)
       return false;
    uint8_t levels;
    if(paging_mode_request.response->mode==LIMINE_PAGING_MODE_X86_64_4LVL)
        levels=4;
    else if(paging_mode_request.response->mode==LIMINE_PAGING_MODE_X86_64_5LVL)
        levels=5;
    else return false;
    uint64_t cr3;
    __asm__ volatile ("movq %%cr3, %0" : "=r"(cr3));
    const struct acpi_map_context ctx={
        .map=memory_request.response,
        .offset=hhdm_request.response->offset
    };
    const struct x86_page_walker walker={
        .map_table=boot_table_map,
        .context=(void *)(uintptr_t)&ctx,
        .cr3_physical=cr3,
        .paging_levels=levels
    };
    const uint64_t physical[2]={topology->lapic_physical,
                                topology->ioapic_physical};
    for(size_t i=0;i<2;++i) {
        if(physical[i]>UINT64_MAX-ctx.offset) return false;
        struct x86_page_translation translation;
        if(!x86_page_walk_read_only(&walker,
                physical[i]+ctx.offset,&translation) ||
           translation.physical!=physical[i] ||
           !translation.supervisor_only) return false;
    }
    return true;
}

/* Read CPU NX/PAT capabilities before an APIC MMIO PTE may be constructed.
 * CPUID feature bits gate the corresponding MSR reads. This is strictly
 * read-only boot CPU work with IF=0, not permission to map device registers.
 * A refused CPU feature leaves all firmware/controller mappings unchanged. */
static bool mmio_boot_cpu_policy(const struct acpi_apic_topology *topology,
                                 struct mmio_page_cpu_policy *out)
{
    if (out==NULL || topology==NULL) return false;
    uint32_t a=1,b,c,d;
    __asm__ volatile ("cpuid" : "+a"(a),"=b"(b),"=c"(c),"=d"(d));
    /* CPUID leaf 1 EDX bit16: PAT MSR is supported. */
    if((d & (1u<<16))==0) return false;
    a=UINT32_C(0x80000000);
    __asm__ volatile ("cpuid" : "+a"(a),"=b"(b),"=c"(c),"=d"(d));
    if(a<UINT32_C(0x80000008)) return false;
    a=UINT32_C(0x80000001);
    __asm__ volatile ("cpuid" : "+a"(a),"=b"(b),"=c"(c),"=d"(d));
    /* Extended CPUID EDX bit20: NX page-table bit is implemented. */
    if((d&(1u<<20))==0) return false;
    a=UINT32_C(0x80000008);
    __asm__ volatile ("cpuid" : "+a"(a),"=b"(b),"=c"(c),"=d"(d));
    uint8_t physical_bits=(uint8_t)(a&0xff);
    uint32_t lo,hi;
    __asm__ volatile ("rdmsr" : "=a"(lo),"=d"(hi) : "c"(UINT32_C(0x277)));
    uint64_t pat=((uint64_t)hi<<32)|lo;
    __asm__ volatile ("rdmsr" : "=a"(lo),"=d"(hi) : "c"(UINT32_C(0xc0000080)));
    uint64_t efer=((uint64_t)hi<<32)|lo;
    struct mmio_page_cpu_policy policy={
        .ia32_pat_msr=pat,
        .nx_supported=true,
        .nxe_enabled=(efer&(UINT64_C(1)<<11))!=0,
        .physical_address_bits=physical_bits
    };
    /* Prove both desired entries using the same pure policy as host tests. */
    uint64_t lapic_leaf=0, ioapic_leaf=0;
    if(mmio_page_plan_leaf(UINT64_C(0xffffc80000000000),
                           topology->lapic_physical,0,&policy,&lapic_leaf)!=MMIO_PAGE_OK ||
       mmio_page_plan_leaf(UINT64_C(0xffffc80000001000),
                           topology->ioapic_physical,0,&policy,&ioapic_leaf)!=MMIO_PAGE_OK)
        return false;
    *out=policy;
    return true;
}

/* Verify the borrowed live CR3 root is readable within an allowed RAM map,
 * and the two reserved virtual slots do not already contain translations.
 * A missing ancestor table is not a successful free-slot proof: check the
 * PML4/PML5 root at the requested index before planning new tables. */
static bool mmio_boot_root_ready(uint64_t **root_out, uint8_t *levels_out,
                                 uint64_t *root_physical_out)
{
    if(root_out==NULL || levels_out==NULL || root_physical_out==NULL ||
       memory_request.response==NULL || hhdm_request.response==NULL ||
       paging_mode_request.response==NULL) return false;
    const struct acpi_map_context state={
        .map=memory_request.response,
        .offset=hhdm_request.response->offset
    };
    uint64_t cr3;
    __asm__ volatile ("movq %%cr3, %0" : "=r"(cr3));
    if ((cr3&UINT64_C(0xfff))!=0) return false;
    uint64_t root=cr3&UINT64_C(0x000ffffffffff000);
    const uint8_t *table=boot_table_map((void *)(uintptr_t)&state,root);
    if(table==NULL) return false;
    uint64_t virt=UINT64_C(0xffffc80000000000);
    unsigned shift=paging_mode_request.response->mode==LIMINE_PAGING_MODE_X86_64_5LVL ? 48u:39u;
    if(shift==39u && paging_mode_request.response->mode!=LIMINE_PAGING_MODE_X86_64_4LVL)
        return false;
    size_t index=(size_t)((virt>>shift)&UINT64_C(511));
    uint64_t existing=0;
    for(size_t i=0;i<8;++i)
        existing |= ((uint64_t)table[index*8+i])<<(8*i);
    if(existing!=0) return false;
    *root_out=(uint64_t *)(uintptr_t)table;
    *levels_out=(uint8_t)(shift==48u ? 5u : 4u);
    *root_physical_out=root;
    return true;
}

/* Pass the existing M2/M3 exact-owner table service to the atomic
 * page-table transaction. Never allocate physical MMIO pages as RAM.
 * Only ordinary KERNEL-owned pages back new page tables; no reserve use. */
static bool mmio_boot_acquire(void *unused, uint64_t *physical,
                              uint64_t **mapped)
{
    (void)unused;
#ifdef SPIFFOS_MMIO_FAULT_SECOND_ALLOCATION
    /* Dedicated negative-test build: the first owned frame succeeds, the
     * second fails before acquiring RAM. The transaction must release the
     * first frame, preserve the root and boot with IF=0. Never in production. */
    static unsigned attempts;
    if(++attempts==2) return false;
#endif
    return memory_boot_mmio_table_acquire(physical,mapped);
}

/* Release only a previously tracked kernel table frame after unlinking. */
static bool mmio_boot_release(void *unused,uint64_t physical)
{
    (void)unused;
    return memory_boot_mmio_table_release(physical);
}

/* Discard a stale local TLB translation after changing one table entry.
 * This does not synchronize other CPUs; this is a boot-CPU-only foundation. */
static void mmio_boot_invlpg(void *unused,uint64_t virtual_address)
{
    (void)unused;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

static struct x86_mmio_mapping boot_mmio_mapping;

/* Create two UC supervisor-only NX MMIO translations, but never access the
 * controller registers in this checkpoint. Re-walk the running CR3 before
 * publishing the readiness marker. If verification fails, clear the root
 * link and return owned table pages or halt on a failed rollback. */
static bool mmio_boot_map_apic(const struct acpi_apic_topology *topology,
                               const struct mmio_page_cpu_policy *policy)
{
    uint64_t *root=NULL,root_physical=0;
    uint8_t levels=0;
    if(topology==NULL || policy==NULL ||
       !mmio_boot_root_ready(&root,&levels,&root_physical)) return false;
    const struct x86_mmio_map_backend backend={
        mmio_boot_acquire,mmio_boot_release,mmio_boot_invlpg,NULL
    };
    if(!x86_mmio_map_create(&boot_mmio_mapping,levels,root,
          topology->lapic_physical,topology->ioapic_physical,policy,&backend)) {
        if(boot_mmio_mapping.poisoned) {
            (void)serial_write("SPIFFOS_MMIO_ROLLBACK_FATAL\r\n");
            halt_forever();
        }
        return false;
    }
    const struct acpi_map_context context={
        .map=memory_request.response,.offset=hhdm_request.response->offset
    };
    const struct x86_page_walker walker={
        .map_table=boot_table_map,
        .context=(void *)(uintptr_t)&context,
        .cr3_physical=root_physical,
        .paging_levels=levels
    };
    const uint64_t addresses[2]={MMIO_APIC_LAPIC_VIRTUAL,
                                  MMIO_APIC_IOAPIC_VIRTUAL};
    const uint64_t targets[2]={topology->lapic_physical,
                                topology->ioapic_physical};
    bool verified=true;
    for(size_t i=0;i<2;++i) {
        struct x86_page_translation translation;
        if(!x86_page_walk_read_only(&walker,addresses[i],&translation) ||
           translation.physical!=targets[i] || translation.page_size!=4096 ||
           !translation.supervisor_only || !translation.writable ||
           (translation.leaf_entry&(UINT64_C(1)<<63))==0 ||
           (translation.leaf_entry&UINT64_C(24))!=UINT64_C(24) ||
           (translation.leaf_entry&UINT64_C(132))!=0) {
            verified=false;
            break;
        }
    }
    if(!verified) {
        if(!x86_mmio_map_destroy(&boot_mmio_mapping,&backend)) {
            (void)serial_write("SPIFFOS_MMIO_ROLLBACK_FATAL\r\n");
            halt_forever();
        }
        return false;
    }
    return true;
}

/* Count mapping-authority failures during read-only HHDM alias walks;
 * each probe remains independent and never accesses APIC registers. */
static bool apic_alias_mapping_denied;
static const uint8_t *apic_alias_checked_table(void *context,uint64_t page)
{
    const uint8_t *table=boot_table_map(context,page);
    if(table==NULL) apic_alias_mapping_denied=true;
    return table;
}

/* Read-only CPU cache state and **proven absent** known HHDM aliases.
 * x86 UC PAT index 3 is checked again by pure apic_memory_type_safe().
 * This does not read APIC device registers or change MTRRs/any tables. */
static bool apic_boot_cache_gate(const struct acpi_apic_topology *topology,
                                  const struct mmio_page_cpu_policy *policy)
{
    if(topology==NULL || policy==NULL ||
       memory_request.response==NULL || hhdm_request.response==NULL ||
       paging_mode_request.response==NULL) return false;
    uint32_t a=1,b,c,d;
    __asm__ volatile ("cpuid" : "+a"(a),"=b"(b),"=c"(c),"=d"(d));
    if((d&(1u<<12))==0) return false; /* MTRR MSRs unsupported */
    uint32_t lo,hi;
    __asm__ volatile ("rdmsr" : "=a"(lo),"=d"(hi) : "c"(UINT32_C(0xfe)));
    uint64_t cap=((uint64_t)hi<<32)|lo;
    __asm__ volatile ("rdmsr" : "=a"(lo),"=d"(hi) : "c"(UINT32_C(0x2ff)));
    uint64_t default_type=((uint64_t)hi<<32)|lo;
    uint8_t levels;
    if(paging_mode_request.response->mode==LIMINE_PAGING_MODE_X86_64_4LVL)
        levels=4;
    else if(paging_mode_request.response->mode==LIMINE_PAGING_MODE_X86_64_5LVL)
        levels=5;
    else return false;
    uint64_t cr3;
    __asm__ volatile ("movq %%cr3,%0":"=r"(cr3));
    struct acpi_map_context ctx={
        .map=memory_request.response,.offset=hhdm_request.response->offset
    };
    struct x86_page_walker walker={
        .map_table=apic_alias_checked_table,.context=&ctx,
        .cr3_physical=cr3,.paging_levels=levels
    };
    if(topology->lapic_physical>UINT64_MAX-ctx.offset ||
       topology->ioapic_physical>UINT64_MAX-ctx.offset)
        return false;
    apic_alias_mapping_denied=false;
    struct apic_memory_type_evidence evidence={
        .mtrr_cpuid_supported=true,
        .ia32_mtrrcap=cap,
        .ia32_mtrr_def_type=default_type,
        .ia32_pat=policy->ia32_pat_msr,
        .lapic_hhdm=x86_page_probe_read_only(&walker,
             topology->lapic_physical+ctx.offset),
        .ioapic_hhdm=x86_page_probe_read_only(&walker,
             topology->ioapic_physical+ctx.offset)
    };
    (void)serial_write(apic_alias_mapping_denied ?
         "SPIFFOS_ALIAS_TABLE_MAPPING_DENIED\r\n" :
         "SPIFFOS_ALIAS_TABLE_MAPPING_ACCESSIBLE\r\n");
    (void)serial_write(evidence.lapic_hhdm==X86_PAGE_PROBE_ABSENT ?
          "SPIFFOS_ALIAS_LAPIC_ABSENT\r\n" :
          evidence.lapic_hhdm==X86_PAGE_PROBE_PRESENT ?
          "SPIFFOS_ALIAS_LAPIC_PRESENT\r\n" :
          "SPIFFOS_ALIAS_LAPIC_UNKNOWN\r\n");
    (void)serial_write(evidence.ioapic_hhdm==X86_PAGE_PROBE_ABSENT ?
          "SPIFFOS_ALIAS_IOAPIC_ABSENT\r\n" :
          evidence.ioapic_hhdm==X86_PAGE_PROBE_PRESENT ?
          "SPIFFOS_ALIAS_IOAPIC_PRESENT\r\n" :
          "SPIFFOS_ALIAS_IOAPIC_UNKNOWN\r\n");
    (void)serial_write((default_type&UINT64_C(0x800))!=0 ?
          "SPIFFOS_MTRR_ENABLED\r\n" :
          "SPIFFOS_MTRR_DISABLED\r\n");
    if(evidence.lapic_hhdm==X86_PAGE_PROBE_ABSENT &&
       evidence.ioapic_hhdm==X86_PAGE_PROBE_ABSENT)
        (void)serial_write("SPIFFOS_APIC_HHDM_ALIASES_ABSENT\r\n");
    else
        (void)serial_write("SPIFFOS_APIC_HHDM_ALIASES_UNVERIFIED\r\n");
#ifdef SPIFFOS_APIC_READONLY_BLOCK_TEST
    /* Dedicated negative guest: simulate unverified cache policy. This
     * branch is compiled only in a disposable test ISO and MUST forbid all
     * APIC register reads while retaining normal console/keyboard boot. */
    return false;
#else
    return apic_memory_type_safe(&evidence);
#endif
}

/* Read only the local APIC ID at offset 0x20 from the private verified
 * UC mapping. Access is permitted ONLY after cache/alias proof; this adapter
 * owns no mapping and never changes any LAPIC register, mask or EOI. */
static bool apic_hw_lapic_id_read(void *context,uint32_t *value)
{
    (void)context;
    if(value==NULL) return false;
    const volatile uint32_t *base=(const volatile uint32_t *)
                                   (uintptr_t)MMIO_APIC_LAPIC_VIRTUAL;
    *value=base[0x20u/sizeof(uint32_t)];
    return true;
}

/* IOAPIC register reads are indirect. Read the previous 8-bit selector,
 * select only ID(0) or VERSION(1), read IOWIN(0x10), then restore selector.
 * The two IOREGSEL MMIO writes alter ONLY the index latch, never masks,
 * redirection, EOI, interrupt delivery or any configured device state.
 * One boot CPU, IF=0; no concurrent IOAPIC client is yet supported. */
static bool apic_hw_ioapic_index_read(void *context,uint8_t index,
                                      uint32_t *value)
{
    (void)context;
    if(value==NULL || index>1) return false;
    volatile uint32_t *base=(volatile uint32_t *)
                             (uintptr_t)MMIO_APIC_IOAPIC_VIRTUAL;
    uint32_t original=base[0] & UINT32_C(0xff);
    base[0]=index;
    __asm__ volatile ("" : : : "memory");
    uint32_t result=base[0x10u/sizeof(uint32_t)];
    __asm__ volatile ("" : : : "memory");
    base[0]=original;
    *value=result;
    return true;
}

/* Inspect bounded identity/version register values after all memory mapping,
 * MADT, CPU and cache/alias checks. This never enables an interrupt source. */
static bool apic_boot_readonly_probe(const struct acpi_apic_topology *topology,
                                     struct apic_readonly_result *out)
{
    struct apic_cpu_snapshot cpu;
    if(topology==NULL || !apic_cpu_read_snapshot(&cpu) ||
       !apic_cpu_topology_preflight(topology,&cpu)) return false;
    const struct apic_readonly_backend backend={
        apic_hw_lapic_id_read,apic_hw_ioapic_index_read,NULL
    };
    return out!=NULL && apic_readonly_validate(topology,cpu.cpuid_apic_id,
                                                 &backend,out);
}

#ifndef SPIFFOS_IRQ0_DISABLED_TEST
/* Physical APIC MMIO accesses are permitted only after the complete
 * M9-3D5 mapping/cache/identity proof. This backend cannot access physical
 * addresses, and rejects unknown offsets; no heap or IRQ disabling here. */
static bool apic_boot_io_read(void *unused,bool local,uint32_t offset,
                              uint32_t *value)
{
    (void)unused;
    if(value==NULL || (offset&3u)!=0 ||
       (local ? offset>=4096u : (offset!=0 && offset!=0x10))) return false;
    uintptr_t base=local ? (uintptr_t)MMIO_APIC_LAPIC_VIRTUAL :
                           (uintptr_t)MMIO_APIC_IOAPIC_VIRTUAL;
    *value=*(volatile const uint32_t *)(base+offset);
    return true;
}

/* Mutate only the LAPIC or IOAPIC 4KiB register window already verified.
 * IOAPIC index/data writes are further restricted by apic-irq0.c; no
 * arbitrary offsets can escape this narrowly scoped adapter. */
static bool apic_boot_io_write(void *unused,bool local,uint32_t offset,
                               uint32_t value)
{
    (void)unused;
    if((offset&3u)!=0 ||
       (local ? (offset!=0xf0 && offset!=0xb0) :
                (offset!=0 && offset!=0x10))) return false;
    uintptr_t base=local ? (uintptr_t)MMIO_APIC_LAPIC_VIRTUAL :
                           (uintptr_t)MMIO_APIC_IOAPIC_VIRTUAL;
    *(volatile uint32_t *)(base+offset)=value;
    return true;
}

/* Interrupt context shares no mutable global except the atomic monotonic
 * timer. The boot CPU's xAPIC EOI is required after each validated tick.
 * No serial/heap/locks/scheduler/port use, with IF cleared by interrupt gate. */
void x86_irq0_handler(void)
{
    if(!kernel_timer_boot_tick() || !kernel_scheduler_clock_boot_note_tick()) {
        for(;;) __asm__ volatile ("cli; hlt" : : : "memory");
    }
    *(volatile uint32_t *)(uintptr_t)
        (MMIO_APIC_LAPIC_VIRTUAL+UINT64_C(0xb0))=0;
}

/* Explicit PIT channel0 configuration; preserve PIT2 used for F12.
 * 1,193,182Hz/11,932 ~99.999Hz. PIT mode2 emits one IRQ per reload.
 * Startup-only with IF=0 and all device gates already installed. */
static void apic_boot_pit0_100hz(void)
{
    port_write(UINT16_C(0x43),UINT8_C(0x34));
    port_write(UINT16_C(0x40),(uint8_t)(11932u&255u));
    port_write(UINT16_C(0x40),(uint8_t)(11932u>>8));
}

#endif /* SPIFFOS_IRQ0_DISABLED_TEST: no unused active-route backend */
#ifdef SPIFFOS_IRQ0_DISABLED_TEST
/* The disabled-IRQ test still links the assembly table but deliberately
 * installs no IRQ0 IDT gate. If unexpectedly called, fail closed without
 * touching LAPIC registers or advertising a live timer. */
void x86_irq0_handler(void)
{
    halt_forever();
}
#endif


/* Activate the one verified IRQ0 route; anything unsupported leaves IF=0.
 * A programming failure while the hardware state might be partially changed
 * must stop the CPU rather than start an unverified interrupt subsystem. */
static bool apic_boot_irq0_activate(
    const struct acpi_apic_topology *topology,
    const struct apic_readonly_result *identified)
{
#ifdef SPIFFOS_IRQ0_DISABLED_TEST
    (void)topology;
    (void)identified;
    (void)serial_write("SPIFFOS_IRQ0_DISABLED_TEST\r\n");
    return false;
#else
    if(topology==NULL || identified==NULL) return false;
    const struct apic_irq0_io io={
        apic_boot_io_read,apic_boot_io_write,NULL
    };
    struct apic_irq0_route route={0};
    if(!apic_irq0_probe(&io,topology,identified->lapic_id,
                        identified->redirection_entries,&route)) {
        (void)serial_write("SPIFFOS_IRQ0_PREFLIGHT_UNAVAILABLE\r\n");
        return false;
    }
    (void)serial_write("SPIFFOS_IRQ0_PREFLIGHT_OK\r\n");
    if(!x86_idt_enable_apic_timer_gates()) {
        (void)serial_write("SPIFFOS_IRQ0_IDT_UNAVAILABLE\r\n");
        return false;
    }
    if(!apic_irq0_prepare(&io,&route)) {
        (void)serial_write("SPIFFOS_IRQ0_SETUP_FATAL\r\n");
        halt_forever();
    }
    apic_boot_pit0_100hz();
    if(!apic_irq0_unmask(&io,&route)) {
        (void)serial_write("SPIFFOS_IRQ0_UNMASK_FATAL\r\n");
        halt_forever();
    }
    /* IF must become 1 only after all readbacks and the full timer ISR.
     * No other IOAPIC pin is unmasked, and PIC masks remain 0xffff. */
    (void)serial_write("SPIFFOS_IRQ0_ROUTE_READY\r\n");
    __asm__ volatile ("sti" : : : "memory");
    (void)serial_write("SPIFFOS_IRQ0_ENABLED\r\n");
    return true;
#endif
}

/* Bounded read-only probe; a failed table lookup is reported but never
 * enables external interrupts or changes a kernel device's configuration.
 * Only ACPI table bytes in Limine-owned mappings may be inspected here. */
static bool acpi_boot_discover(struct acpi_apic_topology *topology)
{
    if (topology == NULL || rsdp_request.response == NULL ||
        rsdp_request.response->address == NULL ||
        hhdm_request.response == NULL || memory_request.response == NULL)
        return false;
    uint64_t xsdt_address;
    if (!acpi_rsdp_xsdt_physical(rsdp_request.response->address, 36,
                                   &xsdt_address)) return false;
    const struct acpi_map_context context = {
        .map = memory_request.response,
        .offset = hhdm_request.response->offset
    };
    const struct acpi_mapper mapper = {acpi_map_read_only,
                                       (void *)(uintptr_t)&context};
    return acpi_discover_madt(&mapper, xsdt_address, topology);
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
    /* Move one-time editor creation, full-frame snapshot allocation and
     * initial logo rendering to boot. Failure preserves lazy activation:
     * constrained-memory guests still retain the original keyboard path.
     * Workspaces remain editor data, never independent CPU tasks. */
    if (workspace_manager_prewarm(&boot_workspaces))
        (void)serial_write("SPIFFOS_WORKSPACES_PREWARMED\r\n");
    else
        (void)serial_write("SPIFFOS_WORKSPACES_LAZY_FALLBACK\r\n");
    (void)serial_write("SPIFFOS_KEYBOARD_READY\r\n");
    bool first = true;
    bool indicator_timed = false;
    uint16_t previous_pit = 0;
    uint32_t elapsed_pit = 0;
    unsigned int indicator_fade_step = 0;
#ifdef SPIFFOS_IRQ0_TIMING_TEST
    /* Independent real-QEMU wall-clock test observes serial milestones while
     * this original polled editor stays responsive. No delay/sleep is added
     * to ordinary images; no logging occurs inside the ISR. */
    bool tick_100_reported=false,tick_200_reported=false;
#endif
#ifdef SPIFFOS_M10_C_REQUEST_TEST
    bool m10_c_request_seen=false;
#endif
    for (;;) {
#ifdef SPIFFOS_M10_C_REQUEST_TEST
        /* This is a cooperative safe point in the existing keyboard loop,
         * NEVER an IRQ handler or an asynchronous preemption boundary. */
        if(!m10_c_request_seen &&
           kernel_scheduler_clock_boot_take_request()) {
            uint64_t tick_count=0;
            if(!kernel_scheduler_clock_boot_read_ticks(&tick_count) ||
               tick_count<10u) {
                (void)serial_write("SPIFFOS_M10_C_REQUEST_ERROR\r\n");
                halt_forever();
            }
            (void)serial_write("SPIFFOS_M10_C_REQUEST_OK\r\n");
            m10_c_request_seen=true;
        }
#endif
#ifdef SPIFFOS_IRQ0_TIMING_TEST
        uint64_t now_ms=0;
        if(kernel_timer_boot_now_ms(&now_ms)) {
            if(now_ms>=1000u && !tick_100_reported) {
                (void)serial_write("SPIFFOS_IRQ0_TICK_100\r\n");
                tick_100_reported=true;
            }
            if(now_ms>=2000u && !tick_200_reported) {
                (void)serial_write("SPIFFOS_IRQ0_TICK_200\r\n");
                tick_200_reported=true;
            }
        }
#endif
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
    /* M10-C initializes before any real IRQ. This clock only requests
     * safe-point scheduling after ten ticks; it never changes CPU context. */
    if (!kernel_scheduler_clock_boot_init(10u)) {
        (void)serial_write("SPIFFOS_SCHEDULER_CLOCK_ERROR\r\n");
        halt_forever();
    }
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
    /* ACPI discovery is read-only; later validated M9-3D5 cache/ID checks
     * gate every active M9-3D6 LAPIC/IOAPIC timer configuration write. */
    struct acpi_apic_topology boot_apic = {0};
    if (acpi_boot_discover(&boot_apic)) {
        (void)serial_write("SPIFFOS_ACPI_MADT_OK\r\n");
        /* Compare firmware's controller base with the boot CPU's APIC MSR.
         * Reading these registers is not permission to touch MMIO, and all
         * hardware interrupts remain masked while this probe runs. */
        struct apic_cpu_snapshot cpu_apic;
        if (apic_cpu_read_snapshot(&cpu_apic) &&
            apic_cpu_topology_preflight(&boot_apic, &cpu_apic))
            {
                (void)serial_write("SPIFFOS_APIC_CPU_MATCH\r\n");
                struct mmio_page_cpu_policy mmio_policy;
                bool mmio_cpu_ok=mmio_boot_cpu_policy(&boot_apic,&mmio_policy);
                (void)serial_write(mmio_cpu_ok ?
                    "SPIFFOS_MMIO_POLICY_OK\r\n" :
                    "SPIFFOS_MMIO_POLICY_UNSUPPORTED\r\n");
                uint64_t *root_table=NULL,root_physical=0;
                uint8_t depth=0;
                bool mmio_root_ok=mmio_boot_root_ready(&root_table,&depth,
                                                        &root_physical);
                (void)serial_write(mmio_root_ok ?
                    "SPIFFOS_MMIO_ROOT_READY\r\n" :
                    "SPIFFOS_MMIO_ROOT_UNVERIFIED\r\n");
                bool mmio_mapped=false;
                if(mmio_cpu_ok && mmio_root_ok) {
                    mmio_mapped=mmio_boot_map_apic(&boot_apic,&mmio_policy);
                    (void)serial_write(mmio_mapped ?
                        "SPIFFOS_MMIO_MAPPED_OK\r\n" :
                        "SPIFFOS_MMIO_MAPPING_UNAVAILABLE\r\n");
                    if(mmio_mapped) {
                        bool cache_safe=apic_boot_cache_gate(&boot_apic,&mmio_policy);
                        (void)serial_write(cache_safe ?
                            "SPIFFOS_APIC_CACHE_SAFE\r\n" :
                            "SPIFFOS_APIC_CACHE_UNVERIFIED\r\n");
                        if(cache_safe) {
                            struct apic_readonly_result identified;
                            bool registers_ok=apic_boot_readonly_probe(&boot_apic,&identified);
                            (void)serial_write(registers_ok ?
                                "SPIFFOS_APIC_REGISTERS_OK\r\n" :
                                "SPIFFOS_APIC_REGISTERS_UNAVAILABLE\r\n");
                            if(registers_ok)
                                (void)apic_boot_irq0_activate(&boot_apic,&identified);
                        }
                        else
                            (void)serial_write("SPIFFOS_APIC_REGISTERS_SKIPPED\r\n");
                    }
                }
                (void)serial_write(apic_boot_pages_present(&boot_apic) ?
                    "SPIFFOS_APIC_HHDM_PAGES_UNVERIFIED_CACHE\r\n" :
                    "SPIFFOS_APIC_HHDM_MAPPING_UNVERIFIED\r\n");
            }
        else
            (void)serial_write("SPIFFOS_APIC_CPU_UNAVAILABLE\r\n");
    } else {
        (void)serial_write("SPIFFOS_ACPI_MADT_UNAVAILABLE\r\n");
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
#ifdef SPIFFOS_IRQ0_DISABLED_TEST
    /* Negative guest ensures no imaginary clock is published when the
     * APIC routing step is deliberately skipped and IF remains zero. */
    uint64_t absent_time=UINT64_MAX;
    if(kernel_timer_boot_now_ms(&absent_time) || absent_time!=UINT64_MAX) {
        (void)serial_write("SPIFFOS_IRQ0_DISABLED_CLOCK_ERROR\r\n");
        halt_forever();
    }
    (void)serial_write("SPIFFOS_IRQ0_DISABLED_CLOCK_UNAVAILABLE\r\n");
#endif
    if (!serial_write("\r\nSPIFFOS_BOOT_OK\r\n")) {
        halt_forever();
    }
#ifdef SPIFFOS_M10_B_SWITCH_TEST
    /* Test-only: prove two independent kernel stacks can cooperatively
     * resume while PIT0 interrupts continue. Never switch inside IRQ0.
     * No physical stack ownership is claimed at this checkpoint. */
    extern bool m10_b_guest_smoke(bool (*read_clock_ms)(uint64_t *));
    if (!m10_b_guest_smoke(kernel_timer_boot_now_ms)) {
        extern const char *m10_b_guest_failure_reason(void);
        (void)serial_write("SPIFFOS_M10_B_SWITCH_ERROR:");
        (void)serial_write(m10_b_guest_failure_reason());
        (void)serial_write("\r\n");
        halt_forever();
    }
    (void)serial_write("SPIFFOS_M10_B_SWITCH_OK\r\n");
#endif
#ifndef SPIFFOS_SCROLL_TEST
    if (console.initialized) {
        keyboard_run(&console);
    }
#endif
    halt_forever();
}
