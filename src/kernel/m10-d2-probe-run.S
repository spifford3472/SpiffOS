/* Purpose: test-only M10-D2 assembly sentinel runner under real PIT0 IRQ0.
 * Dependencies: m10-d2-irq-frame.c, M9 IRQ0 IRETQ path, x86-64 SysV ABI.
 * Invariants: only marker memory writes after filling GPRs; never alter
 * RSP/IF during spin; restore all six callee-saved registers before return;
 * do not switch tasks or change IRQ controller or production kernel.
 * Reference: docs/decisions/ADR-m10-d-preemption-proposal.md.
 */
.section .text

.extern m10_d2_probe_armed
.extern m10_d2_probe_done
.extern m10_d2_probe_failed
.extern m10_d2_expected_rsp
.extern m10_d2_expected_cs
.extern m10_d2_expected_ss
.extern m10_d2_expected_flags

.global m10_d2_probe_run
.type m10_d2_probe_run,@function
m10_d2_probe_run:
    /* Preserve every SysV callee-saved register because our diagnostic
     * deliberately fills them with distinct values while IRQ0 runs. */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Original application RSP lives in the CPU's saved 64-bit hardware
     * frame even at CPL0, and differs from IRQ handler's aligned RSP. */
    movq %rsp,m10_d2_expected_rsp(%rip)
    movw %cs,%ax
    movzwq %ax,%rax
    movq %rax,m10_d2_expected_cs(%rip)
    movw %ss,%ax
    movzwq %ax,%rax
    movq %rax,m10_d2_expected_ss(%rip)

    /* The same CMP used in the spin loop sets a known deterministic
     * FLAGS pattern. PUSHFQ/POP-to-memory takes an exact snapshot while
     * leaving every general-purpose register and the final RSP intact.
     * MOV instructions below do not alter FLAGS; before each IRQ,
     * the loop CMP reproduces this same value while done remains zero. */
    cmpl $0,m10_d2_probe_done(%rip)
    pushfq
    popq m10_d2_expected_flags(%rip)

    movabsq $0x0101010101010101,%rax
    movabsq $0x0a0a0a0a0a0a0a0a,%rbx
    movabsq $0x0303030303030303,%rcx
    movabsq $0x0c0c0c0c0c0c0c0c,%rdx
    movabsq $0x0e0e0e0e0e0e0e0e,%rsi
    movabsq $0x0d0d0d0d0d0d0d0d,%rdi
    movabsq $0x0b0b0b0b0b0b0b0b,%rbp
    movabsq $0x0808080808080808,%r8
    movabsq $0x0909090909090909,%r9
    movabsq $0x1010101010101010,%r10
    movabsq $0x1111111111111111,%r11
    movabsq $0x1212121212121212,%r12
    movabsq $0x1313131313131313,%r13
    movabsq $0x1414141414141414,%r14
    movabsq $0x1515151515151515,%r15

    /* This store can be interrupted. If IRQ0 arrives before the next
     * instruction, saved RIP points to spin_begin as required. */
    movl $1,m10_d2_probe_armed(%rip)
.global m10_d2_probe_spin_begin
m10_d2_probe_spin_begin:
    cmpl $0,m10_d2_probe_done(%rip)
    je m10_d2_probe_spin_begin
.global m10_d2_probe_spin_end
m10_d2_probe_spin_end:
    /* Prove IRETQ restored callee-saved registers, independently of
     * the saved-frame inspection in C. No call occurs until after pop. */
    movabsq $0x0a0a0a0a0a0a0a0a,%rax
    cmpq %rax,%rbx
    jne .Lm10_d2_corrupt
    movabsq $0x0b0b0b0b0b0b0b0b,%rax
    cmpq %rax,%rbp
    jne .Lm10_d2_corrupt
    movabsq $0x1212121212121212,%rax
    cmpq %rax,%r12
    jne .Lm10_d2_corrupt
    movabsq $0x1313131313131313,%rax
    cmpq %rax,%r13
    jne .Lm10_d2_corrupt
    movabsq $0x1414141414141414,%rax
    cmpq %rax,%r14
    jne .Lm10_d2_corrupt
    movabsq $0x1515151515151515,%rax
    cmpq %rax,%r15
    jne .Lm10_d2_corrupt
    jmp .Lm10_d2_restore

.Lm10_d2_corrupt:
    movl $1,m10_d2_probe_failed(%rip)

.Lm10_d2_restore:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret
.size m10_d2_probe_run,.-m10_d2_probe_run

.section .note.GNU-stack,"",@progbits
