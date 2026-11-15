#!/usr/bin/env python3
"""M10-D5.9-A read-only C function inventory and change-detection gate.

This is a conservative source-level review aid, NOT a C parser, concurrency
validator or permission to enable production task preemption. It only checks
an explicit list of reviewed paths and syntactically recognizable functions.
Each unreviewed function/changed source file fails closed until re-reviewed.
Dependencies: Python 3 standard library, immutable repo source and manifest.
References: AGENTS.md, ADR-m10-d59-production-preemption-boundaries.md.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/operations/m10-d59-a-source-manifest.json"
# Categories represent requested review queues, NOT permissions to preempt.
CATEGORIES = {
    "entry": ("main.c",),
    "allocator": (
        "frame-allocator.c", "frame-allocate.c", "kernel-heap.c",
        "memory-service.c", "memory-boot.c", "page-access.c", "m10-d59b-context.c",
    ),
    "keyboard": (
        "keyboard.c", "keyboard-ps2.c", "keyboard-console.c", "logical-line.c",
    ),
    "display": (
        "screen-editor.c", "workspace-manager.c", "text-console.c",
        "framebuffer.c", "console-font.c",
    ),
    "task-sync": (
        "kernel-task.c", "kernel-task-resources.c",
        "kernel-task-reaper.c", "kernel-task-cleanup.c",
        "kernel-sync-d53.c", "kernel-dispatch-d54.c",
    ),
    "irq-timer": (
        "apic-irq0.c", "kernel-timer.c",
        "kernel-scheduler-clock.c", "x86-idt.c", "x86-tss.c",
    ),
    "mmio": (
        "x86-mmio-map.c", "x86-page-walk.c",
        "apic-memory-type.c", "mmio-page-policy.c",
    ),
}
# Assembly is reviewed as immutable whole-file text. It cannot be parsed
# with the C-definition scanner; source-digest drift is mandatory instead.
ASSEMBLY_SOURCES = ("x86-irq0.S", "x86-exceptions.S")
UNSAFE_CALLS = (
    "frame_allocate", "frame_release", "kernel_heap_allocate",
    "kernel_heap_free", "memory_service_allocate", "memory_service_release",
    "d53_acquire", "d53_release", "kernel_task_irq_handoff",
    "apic_irq0_eoi", "framebuffer_invert_rectangle",
)
# Restrict this code-scanner's matches to declarations at column 0 after
# removing comments and string literals. Reviewers MUST manually verify
# non-C and macro-expanded code and the full transitive call graph.
FUNCTION = re.compile(
    r"(?m)^(?!\s*(?:if|else|for|while|switch|return|typedef|#)\b)"
    r"(?:[A-Za-z_][\w\s*]*?)\b([A-Za-z_]\w*)\s*"
    r"\((?:[^();{}]|\([^();{}]*\))*\)\s*\{"
)
COMMENT_OR_LITERAL = re.compile(
    r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
    re.DOTALL,
)


def strip_comment_and_literals(source):
    """Mask comments/quoted spans while retaining newlines/byte positions."""
    return COMMENT_OR_LITERAL.sub(
        lambda m: re.sub(r"[^\n]", " ", m.group()), source)


def functions(source):
    """Return name and source line of simple top-level C function definitions.

    This intentionally excludes nontrivial macros, attributes and assembly;
    unchanged source digests are required for paths the regex cannot parse.
    A reviewer must not mistake inventory completeness for static semantics.
    """
    cleaned = strip_comment_and_literals(source)
    return [
        {"function": match.group(1),
         "line": source.count("\n", 0, match.start(1)) + 1}
        for match in FUNCTION.finditer(cleaned)
    ]


def inventory():
    """Produce stable whole-file hashes and bounded textual function cues."""
    result = {}
    for category, names in CATEGORIES.items():
        for filename in names:
            key = f"src/kernel/{filename}"
            path = ROOT / key
            if not path.is_file():
                raise RuntimeError(f"Required audited source missing: {key}")
            source = path.read_text(encoding="utf-8")
            evidence = functions(source)
            if not evidence:
                raise RuntimeError(
                    f"Cannot identify any C definitions in {key}; review scanner")
            result[key] = {
                "category": category,
                "state": "REVIEW_REQUIRED_NO_PREEMPTION",
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "functions": evidence,
                "candidate_calls": sorted(
                    name for name in UNSAFE_CALLS
                    if re.search(r"\b" + re.escape(name) + r"\s*\(", source)
                ),
            }
    for filename in ASSEMBLY_SOURCES:
        key = f"src/kernel/{filename}"
        path = ROOT / key
        if not path.is_file():
            raise RuntimeError(f"Required IRQ assembly missing: {key}")
        result[key] = {
            "category": "irq-assembly",
            "state": "REVIEW_REQUIRED_NO_PREEMPTION",
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "functions": [],
            "candidate_calls": [],
        }
    # No new .c or .S file may escape this gate through an incomplete
    # category allowlist. Unreviewed paths are hashed but NEVER certified;
    # the only safe next action on changed bytes is source/caller review.
    for path in sorted((ROOT / "src/kernel").iterdir()):
        if path.suffix not in (".c", ".S") or not path.is_file():
            continue
        key = f"src/kernel/{path.name}"
        if key in result:
            continue
        result[key] = {
            "category": "unreviewed",
            "state": "UNREVIEWED_NO_PREEMPTION",
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "functions": [],
            "candidate_calls": [],
        }
    return result


def verify(expected, actual):
    """Fail closed for removed/added source paths, changed bytes or functions."""
    if expected.get("schema") != 1 or expected.get("entries") != actual:
        expected_entries = expected.get("entries", {})
        keys = set(actual) | set(expected_entries)
        changes = [
            key for key in sorted(keys)
            if expected_entries.get(key) != actual.get(key)
        ]
        raise RuntimeError(
            "M10-D5.9 audit changed: " + ", ".join(changes) +
            "; do NOT auto-accept. Review path, ownership, IF and call sites "
            "before updating the manifest."
        )


def main(argv=None):
    """Emit scan or verify an owner-reviewed manifest; never change files."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit", action="store_true",
                        help="print candidate manifest to stdout only")
    args = parser.parse_args(argv)
    actual = inventory()
    if args.emit:
        print(json.dumps({
            "schema": 1,
            "warning": "Read-only source inventory; NO paths certified safe.",
            "entries": actual,
        }, indent=2, sort_keys=True))
        return 0
    try:
        expected = json.loads(MANIFEST.read_text(encoding="utf-8"))
        verify(expected, actual)
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print(
        f"SPIFFOS_M10_D59_AUDIT_GUARD_OK: {len(actual)} tracked C/ASM "
        "files (explicit path entries and remainder explicitly UNREVIEWED); "
        "inventory only; no runtime safety certification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
