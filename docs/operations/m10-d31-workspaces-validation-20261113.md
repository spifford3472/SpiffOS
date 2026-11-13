# M10-D3.1 validation ledger — 2026-11-13

Status: **implemented in isolated Forge/GitHub branch and regression-tested**;
no merge, installation or physical media changes.

## Decisions

Owner approved nine boot-prepared *logical* text workspace models with
lazy framebuffer snapshots, followed by read-only F10 diagnostics and
then M10-D4 scheduling. Governing references: `AGENTS.md`,
`docs/decisions/accepted.md`, `docs/decisions/open.md`,
`docs/architecture/system-overview.md`,
`docs/decisions/ADR-m7-text-workspace-switching.md`,
`docs/decisions/ADR-m10-d31-workspace-diagnostics.md`,
`docs/decisions/ADR-m10-d-preemption-proposal.md`,
`docs/DEVELOPERS_GUIDE.md`.

The editor/presentation split prepares independent embedded buffers
and self-owned pointers for nine workspaces while withholding
`console->initialized` from offscreen models. First activation
redraws the logo and cursor; images remain allocated only when
switches require backing. The F10 screen samples actual M9 monotonic
clock and M10-C scheduler tick counts through read-only APIs on the
boot keyboard polling path; it labels unavailable metrics rather than
inventing them. Existing F1–F9/F10/F12 semantics remain intact.

## Actual tests

- `make HOST_CC=gcc test-workspace-manager-memory`: PASS with hosted
  sanitizers; asserts all nine editors have independent line storage,
  offscreen input/activation is rejected, no inactive snapshots are
  allocated at boot, original logo pixels are unchanged, and F10
  diagnostics show/restored pixels correctly.
- `make -B HOST_CC=gcc iso`: strict Clang freestanding build PASS.
- Fresh QEMU normal boots at 128, 256 and 512MiB: each reached
  `SPIFFOS_WORKSPACES_READY_9`, without exception markers.
- Full `make HOST_CC=gcc test-m10-d3-acceptance`: PASS (164.23
  seconds, `/tmp/spiffos-m10-d31-acceptance.log`), with
  `SPIFFOS_M10_D2_ACCEPTANCE_OK`,
  `SPIFFOS_M10_D3_ACCEPTANCE_OK` and `D31_REGRESSION_PASS`.
  This includes real timer-driven D3 positive/negative proof at 3
  RAM sizes, D2 frame preservation, D1 policy, M10-A/B/C,
  framebuffer pixel and editor regression, IRQ0 measured 1.003,
  0.991, 0.994s for 100 ticks, #UD/#DF, APIC/MMIO.

Two test-first issues were correctly caught:
1. An offscreen editor could draw because the borrowed console
   descriptor was still marked initialized. The fix marks the
   metadata-only console uninitialized after model construction and
   requires `boot_workspace_reset` before activation.
2. The first test incorrectly expected the visible boot image to
   remain globally pixel-identical across normal workspace-1 cursor
   creation. It now checks an unchanged logo/background pixel and
   separately verifies cursor overlay behavior.

A shared review tree had residual D3 test-flag object files after
earlier tests, producing an undefined D3 symbol in an incremental
normal ISO build. A clean `make -B iso` resolved the stale object,
not a C/ASM source defect. Production installation remains on
the accepted M10-D3 image pending later approval and validation.

## Review and next step

QWEN local `qwen3-coder:30b` performed the narrow D3.1
model/presentation API review; coordinator implemented the pixel-free
preparation and corrected the hosted regression issues without Codex.
M10-D4 next integrates the already-tested four-band D1 selector into
the test-only D3 IRQ exit path, with high-priority CPU hogs and a
reserved supervisor service budget. Neither D3.1 nor D4 enables
preemption of the ordinary editor.
