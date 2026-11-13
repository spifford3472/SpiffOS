/* Purpose: switch two cooperative x86-64 ring-0 task contexts.
 * Dependencies: x86-task-context.h, SysV AMD64 calling convention.
 * Invariants: no red zone, preserve RBX RBP R12 R13 R14 R15, context
 * saved_rsp at byte offset 0, resumed RIP carried on saved stack.
 * IF remains unchanged: no CLI/STI/pushfq/popfq. Caller establishes IF=1
 * and forbids task yield inside critical sections or IRQ context.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
.section .text
.global x86_task_switch
.type x86_task_switch,@function
x86_task_switch:
    /* SysV callee-saved registers survive a C call, even when the call
     * returns into a different logical task much later. */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    /* RDI points to old->saved_rsp, RSI to next->saved_rsp. The C caller
     * has already checked ownership and the lifetimes of both contexts. */
    movq %rsp, (%rdi)
    movq (%rsi), %rsp
    /* LIFO reverse order; RET resumes at the original caller's call site,
     * or at a fabricated first-run trampoline on a new aligned stack. */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    retq
.size x86_task_switch,.-x86_task_switch
.section .note.GNU-stack,"",@progbits
