# M6 clean-build repair — 2026-11-09

## Trigger and cause
Geoff's make run compiled memory-service.h into build/memory-boot.o and LLD
rejected it as unknown file type. The coordinator's final Makefile dependency
edit in PR25 put that header first. The recipe used Make's $< (first prerequisite),
so Clang generated a precompiled header rather than an ELF relocatable object.
That dependency-only edit happened AFTER the previous 59.89-second runtime
validation; the published final rule was not clean-build validated. The previous
passing runtime tests do not establish correctness of that final Makefile.

## Correction
Keep memory-boot.c first and explicitly name that translation unit in its compile
recipe. A dependency header can no longer silently become compiler input.
The new scripts/test-build-rules.py inspects forced dry-run kernel compilation,
rejects non-translation-unit inputs and checks source/object stems. It is included
in make test-memory. The previous committed Makefile was separately dry-run and
confirmed to produce the exact bad header-as-object command before the fix.

This is a coordinator-written narrow correction using the already authorized
memory fallback after Qwen timeouts. No runtime C code, dependencies, mappings
or memory policies changed. References read: AGENTS, accepted/open, system
overview and the existing M6 ADR/evidence. User's failed installation report
triggered correction within the authorized write/test/merge/deploy task.

## Actual validation
All prior build artifacts were moved to
/home/rt-remote/spiffos-build-evidence-pre-fix before validation. No kernel object
or host-test binary remained in build. make test-memory test-keyboard
test-workspace test-workspace-scroll, followed by scripts/test-safe-launcher.py,
file build/memory-boot.o and git diff --check completed exit0 in65.42 seconds.
Clang compiled memory-boot.c and file identified the output as ELF64 x86-64
relocatable. Host sanitizers, three RAM-size M1-M6 probes, exact editor/scroll
pixels, real keyboard/reflow and launcher close/signal/lock cleanup all passed.
Evidence: /home/rt-remote/spiffos-build-fix-validation.log and
/home/rt-remote/spiffos-build-fix-launcher.log.

Fresh tested ISO SHA256:
f0a688c0334e4b4cf0eca6053af1268499b3a85e81bbd441006dc34fa9511309
Staged as /tmp/spiffos-m6-buildfix-tested.iso. Standard checkout is still owned
by geoff and cannot be modified by rt-remote. After merge, the owner runs git
pull --ff-only followed by make run in the standard checkout. The changed
Makefile dependency causes the invalid object to be replaced automatically;
no manual header/object deletion is necessary. This does not claim that the
owner checkout or running image was updated remotely. No guest or unrelated
service was stopped; no infrastructure permissions or configuration changed.
