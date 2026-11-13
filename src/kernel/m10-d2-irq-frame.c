/* Purpose: test-only full register/frame validation under real IRQ0.
 * Dependencies: m10-d2-irq-frame.h, x86-64 IRQ0 assembly, no runtime IO.
 * Invariants: only one boot CPU, no concurrent probes, no frame mutation;
 * the existing interrupt handler owns exactly one LAPIC EOI.
 * Reference: docs/decisions/ADR-m10-d-preemption-proposal.md.
 */
#include "m10-d2-irq-frame.h"
#include <stddef.h>
#include <stdint.h>

/* Only the probe assembly writes its expected stack/segments and arms the
 * loop. The IRQ hook is the sole writer of done, seen, failed after arming.
 * The compiler uses atomic byte/word accesses, not a scheduling lock. */
volatile uint32_t m10_d2_probe_armed = 0;
volatile uint32_t m10_d2_probe_done = 0;
volatile uint32_t m10_d2_probe_failed = 0;
volatile uint32_t m10_d2_probe_seen = 0;
uint64_t m10_d2_expected_rsp = 0;
uint64_t m10_d2_expected_cs = 0;
uint64_t m10_d2_expected_ss = 0;

extern const char m10_d2_probe_spin_begin[];
extern const char m10_d2_probe_spin_end[];

/* Expected distinct full-width GPRs written by m10_d2_probe_run.S.
 * The index order matches the literal stack push order in x86-irq0.S,
 * reversed by the downward-growing stack. */
static const uint64_t expected_gprs[M10_D2_RIP] = {
#ifdef SPIFFOS_M10_D2_BAD_EXPECTED_TEST
    /* Deliberately wrong R15 for disposable negative guest only. The
     * real interrupt still saves and restores the correct CPU register. */
    UINT64_C(0x1515151515151516), /* R15 mismatch injection */
#else
    UINT64_C(0x1515151515151515), /* r15 */
#endif
    UINT64_C(0x1414141414141414), /* r14 */
    UINT64_C(0x1313131313131313), /* r13 */
    UINT64_C(0x1212121212121212), /* r12 */
    UINT64_C(0x1111111111111111), /* r11 */
    UINT64_C(0x1010101010101010), /* r10 */
    UINT64_C(0x0909090909090909), /* r9 */
    UINT64_C(0x0808080808080808), /* r8 */
    UINT64_C(0x0b0b0b0b0b0b0b0b), /* rbp */
    UINT64_C(0x0d0d0d0d0d0d0d0d), /* rdi */
    UINT64_C(0x0e0e0e0e0e0e0e0e), /* rsi */
    UINT64_C(0x0c0c0c0c0c0c0c0c), /* rdx */
    UINT64_C(0x0303030303030303), /* rcx */
    UINT64_C(0x0a0a0a0a0a0a0a0a), /* rbx */
    UINT64_C(0x0101010101010101)  /* rax */
};

/* IRQ diagnostic hook: validate live real-hardware trap frame. In 64-bit
 * mode the CPU always saves SS:RSP (including an IRQ at CPL 0). A CPU
 * interrupt realigns the handler stack independently of interrupted RSP.
 * Non-IRET-switching demonstration only: no task frame ownership acquired.
 */
void m10_d2_probe_on_irq(const uint64_t *frame)
{
    if (!m10_d2_probe_armed || m10_d2_probe_done) return;
    bool valid = frame != NULL;
    if (valid) {
        for (size_t index = 0; index < M10_D2_RIP; ++index)
            if (frame[index] != expected_gprs[index]) valid = false;
        uint64_t rip = frame[M10_D2_RIP];
        valid = valid &&
            rip >= (uintptr_t)m10_d2_probe_spin_begin &&
            rip < (uintptr_t)m10_d2_probe_spin_end &&
            frame[M10_D2_CS] == m10_d2_expected_cs &&
            frame[M10_D2_SS] == m10_d2_expected_ss &&
            frame[M10_D2_RSP] == m10_d2_expected_rsp &&
            (frame[M10_D2_RFLAGS] & UINT64_C(0x202)) ==
                UINT64_C(0x202);
    }
    if (!valid) {
        m10_d2_probe_failed = 1;
        m10_d2_probe_done = 1;
        return;
    }
    ++m10_d2_probe_seen;
    if (m10_d2_probe_seen >= 3) m10_d2_probe_done = 1;
}

/* Boot loop resumes through IRETQ before it can read this result.
 * The independent assembly runner also checks callee-saved registers
 * after the sentinel loop; it marks failed on any corruption. */
bool m10_d2_probe_passed(void)
{
    return m10_d2_probe_done && !m10_d2_probe_failed &&
           m10_d2_probe_seen >= 3;
}
