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
