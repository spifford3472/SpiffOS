/* Purpose: authorize only strongly guarded APIC MMIO PTE construction.
 * Dependencies: mmio-page-policy.h and x86-64 PAT/NX architectural bits.
 * Invariants: pure computation; no PTE writes, MMIO, heap, interrupts or
 * direct map assumption. All failure paths preserve caller output.
 * Reference: docs/decisions/ADR-m9-3d4-mmio-mapping.md. */
#include "mmio-page-policy.h"

#define MMIO_PAGE_BYTES UINT64_C(4096)
#define MMIO_PTE_PRESENT UINT64_C(1)
#define MMIO_PTE_WRITE UINT64_C(2)
#define MMIO_PTE_PWT UINT64_C(8)
#define MMIO_PTE_PCD UINT64_C(16)
#define MMIO_PTE_NX (UINT64_C(1)<<63)
#define MMIO_PTE_PAT_4K (UINT64_C(1)<<7)
#define MMIO_PTE_USER UINT64_C(4)
#define MMIO_ADDR_MASK UINT64_C(0x000ffffffffff000)

/* True iff the index-3 PAT entry is the architecture's UC type (0).
 * PCD=PWT=1 and PAT=0 choose index 3 for a 4KiB leaf, not an MTRR.
 * A UC PAT type wins the effective-type combination on supported x86. */
static bool pat_index_three_is_uc(uint64_t pat_msr)
{
    return ((pat_msr >> 24) & UINT64_C(0xff)) == 0;
}

/* Validate a numeric 48-bit canonical kernel virtual page. 5-level
 * paging also recognizes high sign-extended 48-bit addresses, so choosing
 * this subset avoids dependence on a new public virtual memory ABI. */
static bool kernel_slot_valid(uint64_t address)
{
    return address >= UINT64_C(0xffff800000000000) &&
           (address & (MMIO_PAGE_BYTES-1)) == 0 &&
           address <= UINT64_MAX-(MMIO_PAGE_BYTES-1);
}

/* Compute the fully guarded entry without changing any live tables.
 * The physical field excludes high reserved bits, and NX is mandatory.
 * A caller can pass the result only to a separately validated transaction. */
enum mmio_page_result mmio_page_plan_leaf(
    uint64_t virt, uint64_t phys, uint64_t original,
    const struct mmio_page_cpu_policy *cpu, uint64_t *planned)
{
    if (cpu == 0 || planned == 0 || !kernel_slot_valid(virt) ||
        (phys & (MMIO_PAGE_BYTES-1)) != 0 || phys == 0)
        return MMIO_PAGE_INVALID;
    if (original != 0) return MMIO_PAGE_CONFLICT;
    if (!cpu->nx_supported || !cpu->nxe_enabled ||
        !pat_index_three_is_uc(cpu->ia32_pat_msr))
        return MMIO_PAGE_UNSUPPORTED;
    if (cpu->physical_address_bits < 36 ||
        cpu->physical_address_bits > 52 ||
        phys > UINT64_MAX - (MMIO_PAGE_BYTES-1) ||
        (phys >> cpu->physical_address_bits) != 0)
        return MMIO_PAGE_INVALID;
    uint64_t leaf = (phys & MMIO_ADDR_MASK) |
        MMIO_PTE_PRESENT | MMIO_PTE_WRITE |
        MMIO_PTE_PWT | MMIO_PTE_PCD | MMIO_PTE_NX;
    /* Defensive postcondition; no executable, user or PAT-bit mapping. */
    if ((leaf & (MMIO_PTE_USER | MMIO_PTE_PAT_4K)) != 0 ||
        (leaf & MMIO_PTE_NX) == 0)
        return MMIO_PAGE_UNSUPPORTED;
    *planned = leaf;
    return MMIO_PAGE_OK;
}
