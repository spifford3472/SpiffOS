# M10-D5.9-B Phase 0 — Isolated hosted exclusion gate evidence

Date: 2026-11-15. Status: **small hosted diagnostic implemented; production allocator NOT protected, D5.9-B not complete**.

## Governing instructions and design

Read root `AGENTS.md`, accepted/open decision registers, system overview, developer guide, M10-D5.9 audit decision and report. Proposal: `docs/decisions/ADR-m10-d59b-allocator-exclusion-contract.md`. This isolated diagnostic is stacked on draft PR #89; it does not change `src/kernel`, production interrupt entry, task scheduling or the owner ISO.

## Implemented files

- `tests/m10-d59b-gate/gate.h`: opt-in hosted interface with exact slot + nonzero generation, trusted off-IRQ execution witness and mockable IF state callbacks.
- `tests/m10-d59b-gate/gate.c`: one-CPU gate state machine. Saves initial IF on first enter, disables interrupts through the supplied fake/real callback, permits bounded nesting only for the same exact generation, and restores original IF on final matching exit. Rejects malformed state/owner/mismatched generation/IRQ context/nesting overflow without mutation.
- `tests/m10-d59b-gate/test_gate.c`: hosted IF-on/IF-off, nested ownership, untrusted/IRQ rejection, stale generation, wrong slot, double exit, invalid generation, corrupt depth and nesting-cap tests.

**Security limit:** `trusted` and `in_irq` are caller-provided values and are NOT authenticated by this module. Likewise the backend is trusted and no verification of actual RFLAGS.IF, task-stack ownership or CPU identity is performed. This is strictly a state-machine test fixture; it MUST NOT be used to protect production memory without independent execution-context authentication and actual IRQ/QEMU verification.

## Local Qwen delegation and review

Forge's Ollama endpoint `http://127.0.0.1:11434` served installed `qwen3-coder:30b`. A bounded single-purpose request generated the initial `gate.h` and `gate.c` in a disposable `/tmp/spiffos-d59b-qwen` fixture. The coordinator independently reviewed the result, found missing generation-zero admission checks and missing corrupted-depth rejection, corrected those issues, expanded comments to conform to source-code conventions, and prepared independent negative tests. Neither raw Qwen output nor an agent assertion was treated as accepted code.

## Actually executed on Forge

Run from `tests/m10-d59b-gate/`:

```sh
gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    gate.c test_gate.c -o /tmp/spiffos-d59b-gate-gcc
/tmp/spiffos-d59b-gate-gcc

clang -std=c11 -Wall -Wextra -Werror -Wpedantic -g -O1 \
    gate.c test_gate.c -o /tmp/spiffos-d59b-gate-clang
/tmp/spiffos-d59b-gate-clang
```

- GCC 14/strict C11 ASan+UBSan: **PASS**, `SPIFFOS_M10_D59B_GATE_HOSTED_OK`.
- Clang/strict C11 non-sanitized: **PASS**, same marker.
- Clang sanitizer build: **BLOCKED by missing Clang 18 ASan runtime libraries**, not by code compilation. No packages installed.
- Forge normal `spiffos-qemu-main` working tree checked clean (`main...origin/main`); accepted ISO SHA-256 still `0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6`.

## Remaining gates before D5.9-B completion

1. Independently test failure paths of each real allocator mutation point, rollback and OOM, plus exact task-generation provenance.
2. Validate authenticated CPU/interrupt context, real saved RFLAGS.IF and bounded IRQ-off duration in QEMU; nested off-IRQ entry must not block interrupts incorrectly.
3. Link only in an isolated diagnostic build and review all frame, heap, service, page and stack callers. Preserve D5.9-A manifest gate.
4. Verify real QEMU at 128/256/512MiB, full inherited regression and keyboard/editor/F1–F12; exact-head independent GitHub CI for this PR.
5. Obtain applicable approval before production wiring/merge/deployment; ordinary editor/BASIC preemption remains disabled.

Rollback: discard this stacked draft branch. No production path, host operating system, physical media or infrastructure modified.

## Phase 0b — real M2 frame allocator hosted fixture (2026-11-15)

Qwen's first small `frame_gate_test.c` draft was rejected at the compilation/review gate: it had Markdown code fences, a nonexistent `struct frame_slot`, wrong gate-enter/exit signatures, a faulty IF restoration assertion, and omitted `main`. The coordinator corrected the isolated fixture against the actual public headers, preserving source-level provenance and not changing the allocator implementation. This is a documented first attempt, **not** a successful Qwen implementation acceptance.

- `tests/m10-d59b-gate/frame_gate_test.c` links unchanged `src/kernel/frame-allocator.c`, `src/kernel/frame-allocate.c`, `src/kernel/memory-map.c` and the hosted gate.
- The parent branch and `main` have identical Git blob SHAs for all three allocator sources and `src/kernel/frame-allocator.h`. The test compiles the same real implementation instead of a handwritten allocator mock.
- Hosted fixture-only wrapper denies calls without exact generation-bound gate ownership, and verifies rejected IRQ/untrusted/stale callers do not mutate allocator state. **It is not a production enforcement wrapper.**
- Tests ordinary frame allocation, release, OOM with untouched output/metadata, reserved-frame preservation, forbidden reserve requests, complete owner/counter invariants, and saved IF on/off restoration. All memory/map addresses are simulated numeric physical addresses and never dereferenced.
- Strict GCC C11 with ASan+UBSan: **PASS** marker `SPIFFOS_M10_D59B_REAL_FRAME_HOSTED_OK`.
- Strict Clang C11 (no sanitizers): **PASS**, same marker.
- Both suites were rerun directly on Forge; working owner checkout remained clean and accepted ISO SHA remained `0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6`.
- `Makefile` adds `test-m10-d59b-gate` and `test-m10-d59b-acceptance`, which depends on the inherited `test-m10-d59-acceptance`. `.github/workflows/kernel-ci.yml` now selects the D5.9-B acceptance target if available, requiring `SPIFFOS_M10_D59B_HOSTED_ACCEPTANCE_OK`.

**Still unproven:** direct unsafe production allocator callers remain possible; no true CPU/IF provenance or IRQ gate has been authenticated; no injected faults inside the three-store allocator mutation sequence; no real QEMU IRQ-off latency, scheduler handoff, task stack retirement or production allocator preemption proof. Do not treat these hosted successes as D5.9-B completion.

Change history: 2026-11-15 Phase 0b added a reviewed, real-source hosted fixture and CI target. No merge/deploy.

## Phase 0c — mutation-boundary hosted injection (2026-11-15)

The opt-in `tests/m10-d59b-gate/instrument-frame-mutations.py` generates a **disposable copy** of the genuine `frame-allocate.c`, inserting a fixture callback after each of its three owner-slot/counter mutations. It requires exactly one recognized source anchor per mutation point and refuses changed source rather than silently running a partial test. It makes no changes to `src/kernel`.

`tests/m10-d59b-gate/frame_mutation_test.c` exercises the instrumented real allocator while holding the hosted exact-generation gate. The callback asserts the simulated IF remains disabled, confirms the gate is held by the exact generation, and verifies simulated IRQ/stale-generation gate admissions reject at *every one* of the three mutation points. The final frame checks and release succeed.

Verified on Forge:

- GCC strict C11 + ASan/UBSan fixture: **PASS** `SPIFFOS_M10_D59B_MUTATION_HOOKS_OK`.
- Clang strict C11 fixture: **PASS**, same marker.
- Intentional source mutation replacing the owner increment: **EXPECTED REJECTION** (`unsafe source drift at mutation point 2`); no instrumented output created.
- Existing M2 allocator baseline recompiled to isolated `/tmp` executable: **PASS** (owners, holes, reserve/OOM, rejected frees/counters). Default `make test-frame-allocator` could not overwrite owner-owned `build/frame-allocator-test` from Desktop Commander user `rt-remote`; no permission changes were made.
- `Makefile` now requires all three hosted gate, real-allocator, and mutation-hook tests under `test-m10-d59b-gate`, and the CI-inherited D5.9-B acceptance target invokes those tests.

**Limits:** These callbacks simulate an attempted interrupt-context admission, rather than delivering actual IRQ0 between x86 machine instructions. The temporary metadata may be inconsistent between stores and must not be read concurrently. The tests do not authenticate production task provenance, bound interrupt latency, prevent bypassing the fixture wrapper, or protect the real production allocator. Actual IRQ and scheduler admission remain OPEN.

Change history: 2026-11-15 Phase 0c added three mutation-boundary checks, an explicit source-drift negative, and baseline host validation; no merge or deployment.
