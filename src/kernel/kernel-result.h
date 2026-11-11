/* Purpose: internal M9 kernel result vocabulary for bounded, recoverable
 * kernel and driver operations. Not a stable syscall or driver ABI.
 * Dependencies: none. Invariants: explicit wire-independent enum values,
 * constant strings; no storage ownership, allocation, I/O or blocking.
 * Safe for interrupt/exception context; see ADR-m9-kernel-foundation. */
#ifndef KERNEL_KERNEL_RESULT_H
#define KERNEL_KERNEL_RESULT_H

/**
 * Kernel result codes for internal error handling
 * ISR-safe: Can be called from interrupt service routines
 */
typedef enum {
    KERNEL_RESULT_OK = 0,
    KERNEL_RESULT_INVALID_ARGUMENT = 1,
    KERNEL_RESULT_NOT_READY = 2,
    KERNEL_RESULT_NO_DEVICE = 3,
    KERNEL_RESULT_TIMEOUT = 4,
    KERNEL_RESULT_OUT_OF_MEMORY = 5,
    KERNEL_RESULT_BUSY = 6,
    KERNEL_RESULT_NOT_SUPPORTED = 7,
    KERNEL_RESULT_BAD_STATE = 8,
    KERNEL_RESULT_IO_ERROR = 9,
    KERNEL_RESULT_CORRUPT = 10
} kernel_result;

/* Return borrowed constant static storage naming result, or "UNKNOWN" for
 * unrecognized values. Cannot fail, allocate, block or mutate state; callable
 * from an ISR, panic path, or early boot. Result is an internal enum. */
static inline const char* kernel_result_to_string(kernel_result result)
{
    switch (result) {
        case KERNEL_RESULT_OK:
            return "OK";
        case KERNEL_RESULT_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case KERNEL_RESULT_NOT_READY:
            return "NOT_READY";
        case KERNEL_RESULT_NO_DEVICE:
            return "NO_DEVICE";
        case KERNEL_RESULT_TIMEOUT:
            return "TIMEOUT";
        case KERNEL_RESULT_OUT_OF_MEMORY:
            return "OUT_OF_MEMORY";
        case KERNEL_RESULT_BUSY:
            return "BUSY";
        case KERNEL_RESULT_NOT_SUPPORTED:
            return "NOT_SUPPORTED";
        case KERNEL_RESULT_BAD_STATE:
            return "BAD_STATE";
        case KERNEL_RESULT_IO_ERROR:
            return "IO_ERROR";
        case KERNEL_RESULT_CORRUPT:
            return "CORRUPT";
        default:
            return "UNKNOWN";
    }
}

#endif /* KERNEL_KERNEL_RESULT_H */
