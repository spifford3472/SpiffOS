/* Purpose: boot-CPU 0x30 external IRQ and 0xff spurious vector stubs.
 * Dependencies: x86-64 interrupt stack convention and C IRQ0 callback.
 * Invariants: preserve ALL interrupted GPRs, no floating point/SIMD,
 * no nested IRQ or red-zone use, return with IRETQ; never PIC EOI.
 * Reference: docs/decisions/ADR-m9-3d6-irq0-delivery.md. */
.section .text
.extern x86_irq0_handler
#if defined(SPIFFOS_M10_D2_IRQ_FRAME_TEST) || defined(SPIFFOS_M10_D55B_IRQ_EVIDENCE_TEST)
.extern m10_d2_probe_on_irq
#endif
#ifdef SPIFFOS_M10_D55B_IRQ_EVIDENCE_TEST
.extern m10_d55b_observe_irq
#endif
#if defined(SPIFFOS_M10_D3_PREEMPT_TEST) || defined(SPIFFOS_M10_D4_PRIORITY_TEST)
.extern m10_d3_irq_select
#endif
#ifdef SPIFFOS_M10_D4_COMPLETE_TEST
.extern m10_d4_complete_irq_select
#endif

.global x86_irq0_entry
.type x86_irq0_entry,@function
x86_irq0_entry:
    /* Hardware interrupt gate clears IF. Save every general-purpose register
     * including RBX (which an interrupted C function may be using). */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    /* CPU may have interrupted a function at any RSP alignment. R12 is
     * already saved and the SysV C callee must preserve it. */
    movq %rsp,%r12
    andq $-16,%rsp
    call x86_irq0_handler
#ifdef SPIFFOS_M10_D55B_IRQ_EVIDENCE_TEST
    /* D5.5b-only: real entry R12 points to the immutable saved frame.
     * Observer checks live IF=0 and CPU-saved fields; never changes R12,
     * IRQ0 callback, EOI count, or selected execution context. */
    movq %r12,%rdi
    call m10_d55b_observe_irq
#endif
#if defined(SPIFFOS_M10_D2_IRQ_FRAME_TEST) || defined(SPIFFOS_M10_D55B_IRQ_EVIDENCE_TEST)
    /* Test-only: check immutable saved frame AFTER normal tick and EOI.
     * R12 was saved in the trap frame; SysV C preserves our temporary
     * pointer. Normal production IRQ assembly contains no extra call. */
    movq %r12,%rdi
    call m10_d2_probe_on_irq
#endif
#if defined(SPIFFOS_M10_D3_PREEMPT_TEST) || defined(SPIFFOS_M10_D4_PRIORITY_TEST)
    /* D3-only: normal bounded IRQ0 callback and EOI already completed.
     * C chooses only an already-owned, saved 20-qword IRETQ frame. The
     * return address is a frame pointer, NOT a cooperative return RIP.
     * IF remains cleared until IRETQ restores the chosen task's RFLAGS. */
    movq %r12,%rdi
    call m10_d3_irq_select
    movq %rax,%r12
#endif
#ifdef SPIFFOS_M10_D4_COMPLETE_TEST
    /* Separate four-worker completion fixture. The real C tick and LAPIC
     * EOI have already finished. Select one validated saved IRQ frame. */
    movq %r12,%rdi
    call m10_d4_complete_irq_select
    movq %rax,%r12
#endif
    movq %r12,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    iretq
.size x86_irq0_entry,.-x86_irq0_entry

/* xAPIC spurious interrupt does not set ISR and requires no EOI. */
.global x86_apic_spurious_entry
.type x86_apic_spurious_entry,@function
x86_apic_spurious_entry:
    iretq
.size x86_apic_spurious_entry,.-x86_apic_spurious_entry
.section .note.GNU-stack,"",@progbits
