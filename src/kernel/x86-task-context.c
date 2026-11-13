/* Purpose: construct validated initial M10-B cooperative task frames.
 * Dependencies: x86-task-context.h, mapped caller-owned stack storage.
 * Invariants: six callee-saved slots + trampoline RIP match assembly POP/RET,
 * trampoline's first RSP modulo 16 equals 8 (SysV x86-64 ABI).
 * No allocator, I/O, interrupts or switching occurs in this function.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#include "x86-task-context.h"

#include <stdint.h>

/* The first stack frame is fabricated as if x86_task_switch had already
 * saved six GPRs. The final return address enters the trampoline with
 * RSP = aligned_stack_end - 8, which is 8 mod 16 for a normal C entry.
 * Check all pointers and lengths before writing any stack bytes. */
bool x86_task_context_init(struct x86_task_context *context,
                           void *stack, size_t stack_bytes,
                           void (*trampoline)(void))
{
    if (context == NULL || stack == NULL || trampoline == NULL ||
        stack_bytes < X86_TASK_STACK_BYTES ||
        ((uintptr_t)stack & UINTPTR_C(15)) != 0 ||
        (stack_bytes & 15u) != 0 ||
        (uintptr_t)stack > UINTPTR_MAX - stack_bytes)
        return false;
    uintptr_t bottom = (uintptr_t)stack;
    uintptr_t end = bottom + stack_bytes;
    if ((end & 15u) != 0 || end - bottom < 64 ||
        ((uintptr_t)context >= bottom &&
         (uintptr_t)context < end)) return false;
    /* Seven words live below end-8: six saved registers and return RIP.
     * The last word at end-8 is left unused to retain ABI entry alignment. */
    uint64_t *slots = (uint64_t *)(end - 64u);
    for (size_t i = 0; i < 6u; ++i) slots[i] = 0;
    slots[6] = (uint64_t)(uintptr_t)trampoline;
    context->saved_rsp = (uint64_t)(uintptr_t)slots;
    return true;
}
