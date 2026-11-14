/* Purpose: D5.5b actual-hardware IRQ0 context observational proof.
 * Responsibilities: examine immutable 20-qword trap frame from x86-irq0.S,
 * correlate saved RIP/RSP/segments with independently armed D2 assembly,
 * and sample actual IF=0 inside the interrupt callback.
 * Dependencies: m10-d2-irq-frame.h and m10-d55b-evidence.h.
 * Invariants: no second scheduler, frame writes, memory allocation,
 * serial I/O, locking, interrupt enabling or EOI ownership.
 * Concurrency: one boot CPU; hook called with IF=0 after M9 tick/EOI.
 * Reference: ADR-m10-d55b-x86-evidence.md, AGENTS.md. */
#include "m10-d55b-evidence.h"
#include "m10-d2-irq-frame.h"
#include <stddef.h>
#include <stdint.h>

extern volatile uint32_t m10_d2_probe_armed;
extern volatile uint32_t m10_d2_probe_done;
extern uint64_t m10_d2_expected_rsp;
extern uint64_t m10_d2_expected_cs;
extern uint64_t m10_d2_expected_ss;
extern uint64_t m10_d2_expected_flags;
extern const char m10_d2_probe_spin_begin[];
extern const char m10_d2_probe_spin_end[];

static volatile uint32_t observed;
static volatile bool rejected;
static volatile bool if_was_clear;
static volatile bool saved_if_was_set;

/* For the QEMU-only four-level paging mode, virtual bits 63:48 must
 * exactly sign-extend bit 47. No claim about LA57 support is made. */
static bool canonical_48(uint64_t address)
{
    uint64_t upper=address>>48u;
    return upper==(((address>>47u)&1u)!=0 ? UINT64_C(0xffff) : 0);
}

/* Called with real assembly R12 pointing to saved first GPR. It must not
 * dereference the frame until 8-byte alignment is proved; the test-owned
 * source is not a public arbitrary user pointer interface. */
void m10_d55b_observe_irq(const uint64_t *frame)
{
    if(!m10_d2_probe_armed || m10_d2_probe_done || rejected)
        return;
    bool valid=frame!=NULL && (((uintptr_t)frame & 7u)==0);
    if(valid) {
        uint64_t cpu_flags=0;
        __asm__ volatile("pushfq; popq %0":"=r"(cpu_flags)::"memory");
        /* The gate clears live IF while hardware saves pre-interrupt IF.
         * Never infer interrupted RFLAGS from the observer's own flags. */
        if_was_clear=(cpu_flags&UINT64_C(0x200))==0;
        saved_if_was_set=(frame[M10_D2_RFLAGS]&UINT64_C(0x200))!=0;
        valid=if_was_clear && saved_if_was_set &&
            (frame[M10_D2_RFLAGS]&UINT64_C(0x2))!=0 &&
            canonical_48(frame[M10_D2_RIP]) &&
            canonical_48(frame[M10_D2_RSP]) &&
            frame[M10_D2_RIP]>=(uintptr_t)m10_d2_probe_spin_begin &&
            frame[M10_D2_RIP]<(uintptr_t)m10_d2_probe_spin_end &&
            frame[M10_D2_RSP]==m10_d2_expected_rsp &&
            frame[M10_D2_CS]==m10_d2_expected_cs &&
            frame[M10_D2_SS]==m10_d2_expected_ss &&
            frame[M10_D2_RFLAGS]==m10_d2_expected_flags;
#ifdef SPIFFOS_M10_D55B_BAD_EXPECTED_TEST
        /* Negative test tampers with *expected* evidence only. Real
         * interrupt frame remains untouched, and the hook must reject. */
        valid=valid && frame[M10_D2_RSP]==
            m10_d2_expected_rsp+UINT64_C(8);
#endif
    }
    if(!valid) {
        rejected=true;
        return;
    }
    if(observed!=UINT32_MAX) ++observed;
}

/* No IRQ-side logging. Validate independent D2 frame/GPR proof and three
 * separately observed hardware IRQ callbacks on returning boot stack. */
bool m10_d55b_evidence_passed(void)
{
    return m10_d2_probe_passed() && !rejected &&
        observed>=3 && if_was_clear && saved_if_was_set;
}
