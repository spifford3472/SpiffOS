/* Purpose: D5.8b test-only IF=0 blocked-task CPU-stack departure.
 * Responsibilities: save the six SysV AMD64 callee-saved registers and
 * transfer to an independently saved surviving dispatcher context.
 * Dependencies: the already-tested x86_task_context layout (saved_rsp
 * at byte offset zero), 16-byte stack alignment, NO RED ZONE.
 * Invariants: NEVER toggles IF, allocates/frees memory or changes M10-A.
 * Caller MUST enter after exact RUNNING->BLOCKED transition with IF=0
 * and prove the destination is a separately owned surviving stack.
 * The resumed task returns to its own saved IF restoration site.
 * Diagnostic-only; does not alter the established x86_task_switch API.
 * Reference: ADR-m10-d58b-frame-ownership-gate.md and AGENTS.md. */
.section .text
.global x86_d58b_block_switch
.type x86_d58b_block_switch,@function
x86_d58b_block_switch:
    /* Argument RDI: old context; RSI: surviving context. Both point to
     * writable, independently owned context structs; saved_rsp offset=0. */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,(%rdi)
    movq (%rsi),%rsp
    /* RET enters surviving context. IF remains whatever the CPU carried,
     * specifically zero during blocked-task departure. No frame fabrication. */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    retq
.size x86_d58b_block_switch,.-x86_d58b_block_switch
.section .note.GNU-stack,"",@progbits
