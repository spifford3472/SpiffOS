# M6 retained boot service — 2026-11-08

Owner explicitly authorized write, test, merge and local QEMU deployment.
Read AGENTS, accepted/open, system overview and the memory ADRs. M6 now links
into boot, keeps one ordinary kernel-owned page and ten heap roots, exercises
wrong-owner/root-release rejection and reset isolation, then verifies backing,
counts and identities after console and actual editor initialization. No new
workspace UI, multi-page growth, ring-3 mapping or concurrent allocation.

## Provenance and tests
A one-function Qwen serial-summary job hit its 60s wall-clock limit, exit 55,
without source changes. Prior memory authorization permits direct Codex fallback;
the coordinator implemented the boot adapter, retirement API, linkage, serial
assertions, retirement host checks and documentation. Log: build/m6-qwen.log.

Final make test-memory test-keyboard test-workspace test-workspace-scroll,
followed by python3 scripts/test-safe-launcher.py, passed with combined exit 0
in 59.89 seconds. Normal host ASan/UBSan tests passed. QEMU 128/256/512 MiB
proved exactly one retained ordinary frame, session/nine workspace roots, marker
ordering across screen/editor initialization, unchanged reserve64, and exact
existing pixel expectations. Ordinary free counts after retention were
18846/51614/117150; candidate counts18911/51679/117215. Initial image SHA256
at all RAM sizes: d22bd34e7998e134c9ec8824ce3e68cddca003234b1a3e87c9fc79a3eeb46863.
Real keyboard/reflow/shrink, forced scrolling, launcher lock/close/signal cleanup
passed. git diff --check passed. Evidence: build/m6-final-validation.log,
build/m6-launcher.log and build/memory-inventory-results.json.

## Environment and rollout
Forge's current connector account is rt-remote, device
17ded838-f6f9-4468-a82f-01a352b6be92. Geoff's original checkout remains clean
at6bdc594. Account ownership prevented direct development/deployment there.
An isolated checkout was made from a read-only Git bundle in
/home/rt-remote/spiffos-m6. The pinned Limine dependency was similarly copied;
its identity remains7a9013f305de0bea1f6310e76e7baba30499fef0. No ownership or
security settings were changed. No infrastructure/services/network/physical
hardware was changed. Initial image build failed only because Git rejected
borrowed dependency ownership; a private dependency copy resolved it.

The tested ISO is staged as /tmp/spiffos-m6-tested.iso with mode0644 and SHA256
f7d91bceb22d574dd392f79128f187bc5bf48040aea8725604ed07721316f68d.
The standard /home/geoff/projects/spiffos-qemu-main/build/spiffos.iso cannot be
replaced by rt-remote. Installation there remains pending an owner-account copy;
do not claim that standard deployment completed. Preserve the existing image as
spiffos.iso.pre-m6 before replacing it. Rollback is that image or a build of
6bdc594. No running emulator was terminated.
