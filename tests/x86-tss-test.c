/* Hosted tests for the M9-3A TSS system-descriptor encoder.
 * No privileged register access or guest side effects. */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "x86-tss.h"

/* Verify selector-independent hardware layout, error rejection and immutability.
 * All storage is owned by this test; no I/O or allocation. */
int main(void)
{
    uint64_t out[2] = {UINT64_C(0xdeadbeef), UINT64_C(0xcafebabe)};
    uint64_t before[2];
    memcpy(before, out, sizeof out);
    assert(!x86_tss_make_descriptor(out, 0, 103));
    assert(!x86_tss_make_descriptor(out, 0x12340000, 102));
    assert(!x86_tss_make_descriptor(out, 0x12340000, 0x100000));
    assert(!x86_tss_make_descriptor(NULL, 0x12340000, 103));
    assert(memcmp(before, out, sizeof out) == 0);
    assert(x86_tss_make_descriptor(out, UINT64_C(0xffff800012345678), 103));
    assert((out[0] & 0xffffu) == 103);
    assert(((out[0] >> 16) & 0xffffffu) == 0x345678u);
    assert(((out[0] >> 40) & 0xffu) == 0x89u);
    assert(((out[0] >> 56) & 0xffu) == 0x12u);
    assert((out[0] >> 48 & 0xfu) == 0);
    assert(out[1] == UINT64_C(0xffff8000));
    return 0;
}
