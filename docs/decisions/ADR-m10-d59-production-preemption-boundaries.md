# ADR M10-D5.9 — Production-kernel preemption safety boundaries

Status: approved isolated D5.9 audit and safety-gate development on 2026-11-14. Normal kernel preemption is explicitly NOT approved or enabled.

## Evidence and context

Read AGENTS.md, accepted/open registers, system overview, DEVELOPERS_GUIDE, ADR-m10-d57-preemption-and-priority.md, ADR-m10-d58c-c2b-real-irq-bridge.md and m10-d57-production-preemption-audit.md. D5.8c C2b PR #88 exact-head GitHub CI run #88 completed SUCCESS; on Forge all targeted 128/256/512MiB tests, 15 real-IRQ negatives, inherited acceptance and unflagged binary identity passed. That proves a *separate flagged diagnostic*, not that the ordinary editor and allocators are reentrant.

Preemption during a sequence of updates to externally owned metadata can expose partially updated list links, counters, ready-queue state, cursor pixels, or IRQ register windows to a different task. A global guard implemented only in diagnostic C files does not protect production call sites. Blanket CLI/ STI insertion is inappropriate: CLI cannot be used as a generic driver mutex, restore must preserve original IF, interrupt latency may violate service budgets, and overlapping nested owners cannot be released by guessed task identity.

## Decision

D5.9 is staged:
1. **D5.9-A read-only mutation-path audit with executable inventory** (this checkpoint). Declare every production path and its call-site execution category; distinguish boot-only/IF-off, IRQ handler, normal editor loop and candidate future worker. Inspect exact source functions and call chains; do not certify a path only because a source comment claims IF-off. Include negative regressions for changes to included C/IRQ-assembly paths; new C/ASM kernel files and changes to unreviewed paths are now hash-protected but are NOT function-audited or certified. Identify all production mutators with *unproven exclusion*.
2. **D5.9-B narrow allocator/ownership gate**, only after identifying a trustworthy entry/saved-IF/task-generation contract. Prove interrupt exclusion across allocation metadata transactions and rollback/oom without adding D5.3 or heap work to ISR. Incremental test-only wrappers and hosted fault injection before real production API wiring.
3. **D5.9-C keyboard/editor/display gate**, preserving a single writer until it can prove independent ownership and redraw/cursor consistency; independent real IRQ worker-load QEMU, special keys, 9 workspaces and F12.
4. **D5.9-D timer/driver/exception/low-level MMIO gate**, validate EOI-once, IF nesting, frame provenance, IRQ event passing, corruption quarantine and real off-stack recovery. Never forcibly release orphaned mutexes or free referenced stacks.
5. **D5.9-E integration review**: exact caller coverage, bounded IRQ latency, two or more worker stacks, supervisor service completion and full regression; only THEN seek separate owner permission to enable normal program preemption.

D5.9-A is permitted to add a read-only static source-audit script and hosted tests, documentation and CI acceptance wiring only. It may not install a global production lock, change normal runtime semantics, merge branches or deploy the ISO. Any unknown call chain remains OPEN rather than presumed preemptible.

## Alternatives and tradeoffs

Rejected immediate automatic preemption of EDIT/BASIC; blanket disabling interrupts around every function; using D5.3 mutexes inside IRQ; treating source comments as a proof; modifying allocator/free APIs without callsite review; using a simulated single-stack test to claim IRQ-safe multiworker execution. The first incremental audit supplies an explicit baseline and deterministic guardrail but cannot prove dynamic absence of race, CPU/ASM call graph completeness, or ownership provenance. Subsequent stages require physical QEMU and full inherited regression.

## Acceptance, rollback, history

D5.9-A requires a reproducible nonmutating inventory of C/assembly production paths with named ownership, root entry/caller and required exclusion, plus a test that detects an unreviewed entry/unsafe flag. Strict Python standard library tests, baseline unflagged ELF identical to D4, existing QEMU 128/256/512MiB inherited acceptance and independent exact-head GitHub CI. Fail closed on unexpected code drift; avoid source modifications outside test-only build wiring. Discard isolated branch for rollback; owner checkout and accepted ISO unchanged.

2026-11-14: owner selected D5.9 as next phase; docs-first decision. Phase A is a prerequisite, not a waiver of later gates.
