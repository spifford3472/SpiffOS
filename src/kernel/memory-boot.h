/* Purpose: adapt borrowed Limine memory metadata to M1-M5 boot diagnostics.
 * Dependencies: M1/M2/M3 memory, M4 heap, M5 cleanup and a
 * borrowed synchronous NUL-string writer.
 * Invariants: boot CPU, interrupts disabled; only an exactly owned M3 probe
 * page is touched, and all successful probes restore ownership before return.
 * References: ADR-memory-foundation.md, ADR-frame-allocator.md and
 * ADR-memory-services.md. */
#ifndef SPIFFOS_MEMORY_BOOT_H
#define SPIFFOS_MEMORY_BOOT_H
#include <stdbool.h>
struct limine_memmap_response;
struct limine_hhdm_response;
struct limine_paging_mode_response;
struct frame_allocator;
struct page_access_policy;
/* Borrow all response mappings synchronously. Copy at most 512 entries into
 * private static state; never reclaim loader storage. Invalid/missing maps
 * emit UNAVAILABLE and return the writer result. Valid maps emit entries,
 * summary and MEMORY_OK in order. False means writer failure, possibly after
 * a prefix. No physical frame access, allocation, concurrency or ownership
 * transfer; one boot CPU caller only, interrupts disabled. */
bool memory_boot_report(const struct limine_memmap_response *response,
    bool (*write_text)(const char *text));
/* Once per boot, initialize owner metadata after a successful map report and
 * exercise allocate/free without accessing returned physical frames. Unsupported
 * capacity/map emits ALLOCATOR_UNAVAILABLE and returns writer status; self-test
 * or output failure returns false. Borrow callback synchronously, one CPU. */
bool memory_boot_allocator_probe(bool (*write_text)(const char *text));

/* Convert validated revision-0 Limine HHDM and x86-64 paging-mode responses
 * into pure M3 policy. Accept only the pinned header's explicit 4- or 5-level
 * modes. All objects are borrowed; success publishes policy once, while any
 * missing/newer/unknown response preserves it. No logging or memory access. */
bool memory_boot_page_policy(const struct limine_hhdm_response *hhdm,
    const struct limine_paging_mode_response *paging,
    const struct frame_allocator *allocator,
    struct page_access_policy *policy);

/* If the validated direct map is available, exercise a real ordinary kernel
 * page through zero/write/read/release/reallocate/zero and restore all M2 pool
 * counters before PAGE_ACCESS_OK. Unsupported boot responses emit UNAVAILABLE
 * and return the writer result so the editor continues. Self-test, rollback or
 * output failure returns false. Borrow responses/callback; one boot CPU, IRQs
 * disabled; the narrow kernel backend alone dereferences an authorized map. */
bool memory_boot_page_probe(const struct limine_hhdm_response *hhdm,
    const struct limine_paging_mode_response *paging,
    bool (*write_text)(const char *text));

/* Allocate one ordinary kernel frame through M3, build a bounded one-page heap,
 * and prove byte access, alignment, owner separation, recoverable OOM, scoped
 * workspace reset, separate session teardown and free/reuse with zeroing.
 * Success releases all blocks/backing and restores M2 counters before
 * OWNER_CLEANUP_OK and HEAP_OK. Unsupported policy emits UNAVAILABLE and preserves
 * the editor path; self-test, rollback or writer failure returns false. Borrow
 * responses/callback synchronously; one boot CPU, interrupts disabled. */
bool memory_boot_heap_probe(const struct limine_hhdm_response *hhdm,
    const struct limine_paging_mode_response *paging,
    bool (*write_text)(const char *text));
/* Retain one ordinary M3 page and ten service roots through the boot lifetime.
 * Missing mapping/OOM reports unavailable; invariant/serial errors return false.
 * Borrow metadata/callback synchronously, exclusive boot CPU, IRQs disabled. */
bool memory_boot_service_start(const struct limine_hhdm_response *hhdm,
    const struct limine_paging_mode_response *paging, bool (*writer)(const char *));
/* Read-only late proof after screen setup; same exclusive callback contract. */
bool memory_boot_service_check(bool (*writer)(const char *));
/* Read-only retained-state proof after editor initialization; boot CPU only. */
bool memory_boot_service_editor_check(bool (*writer)(const char *));
#endif
