/* Purpose: M10-B private cooperative x86-64 kernel context contracts.
 * Responsibilities: initialize independently backed task stacks and switch
 * using a dedicated SysV callee-saved frame (not an IRQ trap frame).
 * Dependencies: C11 integer types and x86-64 assembly switch routine.
 * Invariants: no red zone; caller owns stable 16KiB stack and context;
 * trampoline is noreturn and must switch away rather than fall through.
 * Concurrency: one CPU; caller forbids switching in IRQ/critical sections.
 * Reference: docs/decisions/ADR-m10-task-scheduler-foundation.md. */
#ifndef SPIFFOS_X86_TASK_CONTEXT_H
#define SPIFFOS_X86_TASK_CONTEXT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { X86_TASK_STACK_BYTES = 16384 };

/* Saved RSP points to six callee-saved words followed by a call-site return
 * address. RIP is preserved by that return address; no exposed user ABI. */
struct x86_task_context { uint64_t saved_rsp; };

/* Initialize caller-owned, 16-byte-aligned mapped stack of at least 16KiB,
 * with a one-shot no-argument noreturn trampoline. Must not initialize a live
 * context or overlapping owner. Failure preserves context and stack bytes.
 * Does not allocate or validate physical frame ownership; caller does that. */
bool x86_task_context_init(struct x86_task_context *context,
                           void *stack, size_t stack_bytes,
                           void (*trampoline)(void));

/* Save running SysV callee-saved registers and call-site resume RIP on old
 * stack; transfer to new stack and RET to its saved resume RIP.
 * old and next must be valid, distinct, exclusively owned contexts. Does not
 * modify IF, CR3, FS/GS, SIMD, PIC/APIC or memory ownership. Caller must have
 * IF=1 and no critical section held; no IRQ-context callers. */
void x86_task_switch(struct x86_task_context *old,
                     const struct x86_task_context *next);

#endif
