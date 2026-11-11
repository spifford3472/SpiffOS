/* Purpose: static x86-64 CPU-exception entry stubs for the M9-2 IDT.
 * Dependencies: x86 long-mode interrupt stack conventions and C panic hook.
 * Invariants: interrupts remain disabled; all exceptions are fatal; the
 * current Limine stack must remain usable. No IST or user-return ABI.
 * Reference: ADR-m9-exception-idt.md. */

.section .text
.extern x86_exception_panic

/* Normalize both CPU frame shapes to [vector,error, ...]. Exceptions with
 * hardware-pushed error words insert vector only; all others insert a zero
 * synthetic code before vector. No IRET occurs on this fatal path. */
.macro EXCEPTION_STUB number has_error
.global x86_exception_stub_\number
.type x86_exception_stub_\number, @function
x86_exception_stub_\number:
.if \has_error
    pushq $\number
.else
    pushq $0
    pushq $\number
.endif
    jmp x86_exception_common
.size x86_exception_stub_\number, . - x86_exception_stub_\number
.endm

/* Read normalized exception vector/error before aligning for the System V
 * x86-64 C ABI. Since the C handler never returns, saved registers are not
 * required; aligning RSP guarantees conformant entry regardless of trap RSP. */
.global x86_exception_common
.type x86_exception_common, @function
x86_exception_common:
    movq 0(%rsp), %rdi
    movq 8(%rsp), %rsi
    andq $-16, %rsp
    call x86_exception_panic
1:
    cli
    hlt
    jmp 1b
.size x86_exception_common, . - x86_exception_common

EXCEPTION_STUB 0, 0
EXCEPTION_STUB 1, 0
EXCEPTION_STUB 2, 0
EXCEPTION_STUB 3, 0
EXCEPTION_STUB 4, 0
EXCEPTION_STUB 5, 0
EXCEPTION_STUB 6, 0
EXCEPTION_STUB 7, 0
EXCEPTION_STUB 8, 1
EXCEPTION_STUB 9, 0
EXCEPTION_STUB 10, 1
EXCEPTION_STUB 11, 1
EXCEPTION_STUB 12, 1
EXCEPTION_STUB 13, 1
EXCEPTION_STUB 14, 1
EXCEPTION_STUB 15, 0
EXCEPTION_STUB 16, 0
EXCEPTION_STUB 17, 1
EXCEPTION_STUB 18, 0
EXCEPTION_STUB 19, 0
EXCEPTION_STUB 20, 0
EXCEPTION_STUB 21, 1
EXCEPTION_STUB 22, 0
EXCEPTION_STUB 23, 0
EXCEPTION_STUB 24, 0
EXCEPTION_STUB 25, 0
EXCEPTION_STUB 26, 0
EXCEPTION_STUB 27, 0
EXCEPTION_STUB 28, 0
EXCEPTION_STUB 29, 1
EXCEPTION_STUB 30, 1
EXCEPTION_STUB 31, 0

/* The C loader copies this list of code addresses into the static IDT. */
.section .rodata
.balign 8
.global x86_exception_stub_table
.type x86_exception_stub_table, @object
x86_exception_stub_table:
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .quad x86_exception_stub_\n
.endr
.size x86_exception_stub_table, . - x86_exception_stub_table

.section .note.GNU-stack,"",@progbits
