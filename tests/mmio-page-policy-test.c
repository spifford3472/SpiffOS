/* Purpose: host-only MMIO leaf policy and failure-atomicity regression.
 * Dependencies: mmio-page-policy.c; no privileged registers or MMIO.
 * Invariants: all test objects caller-owned and diagnostics deterministic.
 * Reference: docs/decisions/ADR-m9-3d4-mmio-mapping.md. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mmio-page-policy.h"

#define SLOT UINT64_C(0xffffc80000000000)
#define PHYS UINT64_C(0xfee00000)
#define SENTINEL UINT64_C(0xabcddcba87654321)

/* Verify rejected requests never alter the caller's entry output. */
static void reject(enum mmio_page_result expected, uint64_t virt,
                   uint64_t phys, uint64_t old,
                   const struct mmio_page_cpu_policy *cpu)
{
    uint64_t output = SENTINEL;
    assert(mmio_page_plan_leaf(virt,phys,old,cpu,&output)==expected);
    assert(output==SENTINEL);
}

/* Exercise UC, NX, supervisor, collisions, address width, PAT and NXE. */
int main(void)
{
    struct mmio_page_cpu_policy cpu={
        .ia32_pat_msr=UINT64_C(0x0007040600070406),
        .nx_supported=true,
        .nxe_enabled=true,
        .physical_address_bits=48
    };
    uint64_t leaf=SENTINEL;
    assert(mmio_page_plan_leaf(SLOT,PHYS,0,&cpu,&leaf)==MMIO_PAGE_OK);
    assert((leaf & UINT64_C(0x000ffffffffff000))==PHYS);
    assert((leaf&UINT64_C(0x800000000000001b))==
           UINT64_C(0x800000000000001b));
    assert((leaf&(UINT64_C(4)|UINT64_C(128)))==0);
    reject(MMIO_PAGE_CONFLICT,SLOT,PHYS,1,&cpu);
    reject(MMIO_PAGE_CONFLICT,SLOT,PHYS,UINT64_C(1)<<9,&cpu);
    reject(MMIO_PAGE_INVALID,SLOT+1,PHYS,0,&cpu);
    reject(MMIO_PAGE_INVALID,UINT64_C(0x1000),PHYS,0,&cpu);
    reject(MMIO_PAGE_INVALID,SLOT,PHYS+1,0,&cpu);
    reject(MMIO_PAGE_INVALID,SLOT,0,0,&cpu);
    reject(MMIO_PAGE_INVALID,SLOT,UINT64_C(1)<<49,0,&cpu);
    assert(mmio_page_plan_leaf(UINT64_MAX & ~UINT64_C(4095),PHYS,0,&cpu,&leaf)==MMIO_PAGE_OK);
    reject(MMIO_PAGE_INVALID,SLOT,PHYS,0,NULL);
    cpu.nx_supported=false;
    reject(MMIO_PAGE_UNSUPPORTED,SLOT,PHYS,0,&cpu);
    cpu.nx_supported=true;cpu.nxe_enabled=false;
    reject(MMIO_PAGE_UNSUPPORTED,SLOT,PHYS,0,&cpu);
    cpu.nxe_enabled=true;
    cpu.ia32_pat_msr |= UINT64_C(6)<<24;
    reject(MMIO_PAGE_UNSUPPORTED,SLOT,PHYS,0,&cpu);
    cpu.ia32_pat_msr=UINT64_C(0x0007040600070406);
    cpu.physical_address_bits=35;
    reject(MMIO_PAGE_INVALID,SLOT,PHYS,0,&cpu);
    cpu.physical_address_bits=52;
    assert(mmio_page_plan_leaf(SLOT,PHYS,0,&cpu,&leaf)==MMIO_PAGE_OK);
    assert(mmio_page_plan_leaf(SLOT,PHYS,0,&cpu,NULL)==MMIO_PAGE_INVALID);
    return 0;
}
