/* Purpose: verify all internal M9 result-code values and bounded names.
 * Dependencies: kernel-result.h, C11 hosted assert/string library.
 * Invariants: one-to-one stable internal result mapping, unknown fallback.
 * Does not exercise interrupt execution or settle any public ABI.
 * Reference: docs/decisions/ADR-m9-kernel-foundation.md. */
#include <assert.h>
#include <string.h>
#include "../src/kernel/kernel-result.h"

/* Check each expected numeric code and borrowed constant name; no ownership
 * transfer or mutation, and no interrupt restrictions in this hosted test. */
static void check_result(kernel_result result, int expected, const char *name)
{
    assert((int)result == expected);
    assert(strcmp(kernel_result_to_string(result), name) == 0);
}

/* Exercise every mapped case plus invalid enumerations; returns 0 when all
 * assertions pass. No persistent side effects or allocation. */
int main(void)
{
    check_result(KERNEL_RESULT_OK, 0, "OK");
    check_result(KERNEL_RESULT_INVALID_ARGUMENT, 1, "INVALID_ARGUMENT");
    check_result(KERNEL_RESULT_NOT_READY, 2, "NOT_READY");
    check_result(KERNEL_RESULT_NO_DEVICE, 3, "NO_DEVICE");
    check_result(KERNEL_RESULT_TIMEOUT, 4, "TIMEOUT");
    check_result(KERNEL_RESULT_OUT_OF_MEMORY, 5, "OUT_OF_MEMORY");
    check_result(KERNEL_RESULT_BUSY, 6, "BUSY");
    check_result(KERNEL_RESULT_NOT_SUPPORTED, 7, "NOT_SUPPORTED");
    check_result(KERNEL_RESULT_BAD_STATE, 8, "BAD_STATE");
    check_result(KERNEL_RESULT_IO_ERROR, 9, "IO_ERROR");
    check_result(KERNEL_RESULT_CORRUPT, 10, "CORRUPT");
    check_result((kernel_result)-1, -1, "UNKNOWN");
    check_result((kernel_result)99, 99, "UNKNOWN");
    return 0;
}
