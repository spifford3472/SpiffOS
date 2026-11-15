/* Purpose: declare an isolated bounded M6 transaction diagnostic.
 * Dependencies: actual D5.9-B execution adapter and existing M6 service.
 * Invariants: same guard/manager; probe only, not a general allocation API.
 * References: ADR-m10-d59b-service-transactions.md and AGENTS.md. */
#ifndef SPIFFOS_M10_D59B_SERVICE_H
#define SPIFFOS_M10_D59B_SERVICE_H
#include "m10-d59b-context.h"
#include "memory-service.h"
/* Probe one 32-byte temporary client for workspace owner index 0 or 1.
 * Caller borrows valid context/service objects; no pointer escapes. Argument
 * rejection performs no operation. Admitted failure faults and keeps IF=0.
 * Only the one-page/64-record service is admitted. No IRQ callers. */
bool d59b_service_probe(struct d59b_context *context,
    struct memory_service *service,unsigned index);
#endif
