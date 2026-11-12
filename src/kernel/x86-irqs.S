/* Purpose: x86-64 IRQ0..15 entry stubs and full-register save/restore.
 * Dependencies: C x86_irq_dispatch, ring-0 interrupt gates from IDT.
 * Invariants: no user mode; IF cleared on interrupt-gate entry; preserve
 * every interrupted general-purpose register; restore stack before IRETQ;
 * -mno-red-zone and -mgeneral-regs-only kernel compilation required.
 * Reference: docs/decisions/ADR-m9-3-irq0-delivery.md. */

.section .text
.extern x86_irq_dispatch

/* Each PIC vector pushes its IRQ index 0..15; this is not a CPU error
 * code. The hardware stack below contains RIP, CS, RFLAGS, and possibly
 * RSP/SS for a privilege transition, which is not supported here. */
.macro IRQ_STUB number
.global x86_irq_stub_\number
.type x86_irq_stub_\number, @function
x86_irq_stub_\number:
    pushq $\number
    jmp x86_irq_common
.size x86_irq_stub_\number, . - x86_irq_stub_\number
.endm

/* Preserve all interrupted GPRs, not just the caller-saved registers:
 * IRQs can arrive between *any* instructions, including inside a callee.
 * Fifteen pushes (120 bytes) put the IRQ index at 120(%rsp).
 * R12 is saved by the push sequence before using it to retain the original
 * stack pointer for the C ABI alignment and restoration. */
.global x86_irq_common
.type x86_irq_common, @function
x86_irq_common:
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
    movq 120(%rsp), %rdi
    movq %rsp, %r12
    andq $-16, %rsp
    call x86_irq_dispatch
    movq %r12, %rsp
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
    addq $8, %rsp
    iretq
.size x86_irq_common, . - x86_irq_common

IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

/* Borrowed immutable kernel handler addresses in IRQ index order. */
.section .rodata
.balign 8
.global x86_irq_stub_table
.type x86_irq_stub_table, @object
x86_irq_stub_table:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    .quad x86_irq_stub_\n
.endr
.size x86_irq_stub_table, . - x86_irq_stub_table

.section .note.GNU-stack,"",@progbits
