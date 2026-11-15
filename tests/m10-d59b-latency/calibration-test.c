/* Purpose: independent ratio arithmetic/availability regression.
 * Dependencies: the exact helper used by the real QEMU instrumenter.
 * Invariants: fake samples only, no privileged operations or hardware claim.
 * Reference: ADR-m10-d59b-runtime-memory-wiring.md and AGENTS.md. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
static uint64_t calls,base_ms;
static unsigned mode;
/* Fake ordered cycle sample; mode 3 forces an invalid zero elapsed span. */
static uint64_t read_tsc(void) {return mode==3?1u:calls*1000u;}
/* Emit deterministic milliseconds with unavailable/rollback edge cases.
 * Test-owned out is written only for available samples; no hardware access. */
static bool fake_clock(uint64_t *out)
{
    ++calls;
    if(calls<=3 || (mode==1 && calls%3u==0)) return false;
    if(mode==2 && calls==5) {*out=base_ms+3u;return true;}
    *out=base_ms+calls;
    return true;
}
#include "calibrate.c"

/* Verify independent arithmetic, unavailable samples and untouched outputs.
 * These synthetic tests do not provide clock or hardware timing evidence. */
int main(void)
{
    for(mode=0;mode<4;++mode) {
        calls=0;base_ms=1000;
        uint64_t out=77;
        bool ok=calibrate(fake_clock,&out);
        if(mode<2) {assert(ok && out==1000 && calls>=204);}
        else {assert(!ok && out==77);}
    }
    mode=0;calls=0;base_ms=UINT64_MAX-150u;
    uint64_t out=77;
    assert(!calibrate(fake_clock,&out) && out==77);
    assert(!calibrate(NULL,&out) && out==77);
    assert(!calibrate(fake_clock,NULL));
    puts("D59B_CALIBRATION_ARITHMETIC_OK");
}
