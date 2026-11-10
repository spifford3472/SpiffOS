# Punctuation, forward Delete and numeric keypad — 2026-11-09

Owner reported missing special characters, Delete and keypad during successful
M6 acceptance. Read AGENTS, decision registers, system overview, input/editor
ADRs and the new docs-first ADR-keyboard-completeness.md. This is a tested new
input checkpoint prepared for owner review; no new merge/deployment claimed.

US punctuation now has scan mappings, editor acceptance and original 5x7 glyphs.
Existing uppercase alphabet/digit glyphs remain unchanged. Delete erases at the
cursor, keeps offset and shrinks wrapped rows using the existing reflow path.
Backspace retains its old behavior. Keypad digits/decimal start enabled;
NumLock latches until release and Shift reverses numeric/navigation mode.
Operators and keypad Enter are independent of NumLock. Navigation mode supports
arrows/Insert/Delete; Home/End/Page keys, lowercase, Unicode, other layouts and
hardware lock LEDs remain deferred. Software NumLock state is authoritative.

## Model provenance
A tiny Qwen mapping-header task exhausted its120s wall-clock budget. It produced
only extern declarations rather than data definitions and could not be used.
The draft/log were retained in the pre-clean evidence directory. Coordinator
fallback supplied explicit tables and completed editor/font integration and
host/guest tests; no unverified Qwen source was included. Two old decoder
expectations intentionally changed: Shift+1 emits ! and E0 Enter emits newline.
No existing runtime test assertion was removed to hide a regression.

## Verification
Focused host tests passed under ASan/UBSan for every US plain/shift punctuation
mapping, keypad digits/operators/Enter, NumLock repeat/release, Shift reversal,
Ctrl suppression and Delete make/break. Editor tests cover forward delete cursor
position, no-op at end, wrapped shrink and retained following records/guards.
Real QMP injected all32 punctuation glyphs plus keypad characters, both Deletes,
NumLock arrow mode, Shift-digit reversal and keypad submission. Full-frame
checks compare keyboard-symbols.ppm and keyboard-delete.ppm against fixed font
fixtures; the symbol capture was also visually inspected.

ALL old build artifacts were moved outside build before final validation.
make test-memory test-keyboard test-workspace test-workspace-scroll followed by
scripts/test-safe-launcher.py and git diff --check passed, exit0 in71.51 seconds.
This included clean strict kernel compilation, build-rule regression checks,
M1-M6 host/128/256/512MiB memory tests, unchanged original screen/cursor/reflow/
scroll captures and real launcher close/signal/lock cleanup. QMP final trace:
405 accepted text bytes and105 named events. Logs:
/home/rt-remote/spiffos-input-final.log and spiffos-input-launcher.log.
Earlier evidence: /home/rt-remote/spiffos-inputs-pre-clean-20261109.
No sanitizer workaround or expectation changes to original screenshots.

## Rollout and rollback
The feature is not merged/installed by this record. Standard owner checkout
remains outside rt-remote write permission. Tested image staged at
/tmp/spiffos-inputs-tested.iso with SHA256:
31cffee3ec775076c8e77b0f20bf10660945d451c28742382467b6224fa8c90a
Rollback source is clean-build fix merge87d5fef. No guest, host service, network,
physical media, firmware or infrastructure permissions were changed.
