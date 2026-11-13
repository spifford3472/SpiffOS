/* Purpose: independent M10-B hosted verification of callee-saved GPRs.
 * Dependencies: x86-task-switch.S, SysV AMD64 test ABI, no kernel services.
 * Invariants: save original test caller's RBX/RBP/R12-R15, write six unique
 * sentinels, yield to another task, then compare after old task resumes.
 * Correctly align the call stack; no red zone or floating point use.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
.section .text
.extern x86_task_switch
.global x86_task_register_probe
.type x86_task_register_probe,@function
x86_task_register_probe:
    /* Preserve our own C caller's registers before using them as probes. */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    /* Entry RSP is 8 mod 16; six pushes retain that offset. Extra 8
     * makes RSP aligned to 16 immediately before the CALL. */
    subq $8,%rsp
    movabsq $0x123456789abcdef0,%rbx
    movabsq $0x23456789abcdef01,%rbp
    movabsq $0x3456789abcdef012,%r12
    movabsq $0x456789abcdef0123,%r13
    movabsq $0x56789abcdef01234,%r14
    movabsq $0x6789abcdef012345,%r15
    call x86_task_switch
    movabsq $0x123456789abcdef0,%rax
    cmpq %rax,%rbx
    jne .Lbad
    movabsq $0x23456789abcdef01,%rax
    cmpq %rax,%rbp
    jne .Lbad
    movabsq $0x3456789abcdef012,%rax
    cmpq %rax,%r12
    jne .Lbad
    movabsq $0x456789abcdef0123,%rax
    cmpq %rax,%r13
    jne .Lbad
    movabsq $0x56789abcdef01234,%rax
    cmpq %rax,%r14
    jne .Lbad
    movabsq $0x6789abcdef012345,%rax
    cmpq %rax,%r15
    jne .Lbad
    movl $1,%eax
    jmp .Ldone
.Lbad:
    xorl %eax,%eax
.Ldone:
    addq $8,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    retq
.size x86_task_register_probe,.-x86_task_register_probe
.section .note.GNU-stack,"",@progbits
