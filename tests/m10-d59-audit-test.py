#!/usr/bin/env python3
"""Hosted regressions for read-only D5.9 production-path inventory.

Tests are pure Python and do not edit any source, manifest or system file.
The audit does not establish real-time mutual exclusion or thread safety.
Reference: ADR-m10-d59-production-preemption-boundaries.md.
"""
import importlib.util
from pathlib import Path
import unittest
import sys

# The audit is read-only even in a disposable source tree; importing its
# helper must not create Python bytecode artifacts next to source.
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "spiffos_d59_audit", ROOT / "scripts/audit-m10-d59.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class AuditGateTest(unittest.TestCase):
    """Verify unchanged source, changed source, added function and false cues."""

    def test_normal_inventory(self):
        actual = audit.inventory()
        # The separately flagged context adapter is one new audited source.
        self.assertEqual(65, len(actual))
        self.assertIn("src/kernel/m10-d59b-context.c", actual)
        self.assertEqual("allocator", actual["src/kernel/m10-d59b-context.c"]["category"])
        audit.verify({
            "schema": 1, "entries": actual,
        }, actual)

    def test_reject_changed_byte_no_function_added(self):
        actual = audit.inventory()
        copied = {
            key: dict(value)
            for key, value in actual.items()
        }
        first = sorted(copied)[0]
        copied[first]["sha256"] = "0" * 64
        with self.assertRaisesRegex(RuntimeError, "do NOT auto-accept"):
            audit.verify({"schema": 1, "entries": copied}, actual)

    def test_reject_unreviewed_entry_and_changed_function(self):
        actual = audit.inventory()
        copy = {key: dict(value) for key, value in actual.items()}
        first = sorted(copy)[0]
        copy[first]["functions"] = (
            copy[first]["functions"] + [
                {"function": "unreviewed_irq_mutator", "line": 9999}])
        with self.assertRaisesRegex(RuntimeError, first):
            audit.verify({"schema": 1, "entries": copy}, actual)
        with self.assertRaises(RuntimeError):
            audit.verify({"schema": 1, "entries": {}}, actual)

    def test_mask_comments_literals_keep_lines(self):
        src = ('/* fake int broken(int x) { */\n'
               'bool real(\n int x)\n{ return x != 0; }\n'
               'const char *literal = "int also_fake() {";\n'
               '// void another_fake(void) {\n')
        parsed = audit.functions(src)
        self.assertEqual([{"function": "real", "line": 2}], parsed)

    def test_irq_assembly_is_included_as_immutable_text(self):
        actual = audit.inventory()
        self.assertIn("src/kernel/main.c", actual)
        for path in ("src/kernel/x86-irq0.S",
                     "src/kernel/x86-exceptions.S"):
            self.assertIn(path, actual)
            self.assertEqual([], actual[path]["functions"])
            self.assertEqual("irq-assembly", actual[path]["category"])

    def test_new_source_or_unreviewed_drift_rejected(self):
        actual = audit.inventory()
        unreviewed = [
            key for key, entry in actual.items()
            if entry["category"] == "unreviewed"
        ]
        self.assertEqual(31, len(unreviewed))
        self.assertTrue(all(
            actual[key]["state"] == "UNREVIEWED_NO_PREEMPTION"
            for key in unreviewed
        ))
        new_source = "src/kernel/new_irq_driver.c"
        copied = {key: dict(value) for key, value in actual.items()}
        copied[new_source] = {
            "category": "unreviewed",
            "state": "UNREVIEWED_NO_PREEMPTION",
            "sha256": "0" * 64, "functions": [], "candidate_calls": [],
        }
        with self.assertRaisesRegex(RuntimeError, new_source):
            audit.verify({"schema": 1, "entries": copied}, actual)
        copied = {key: dict(value) for key, value in actual.items()}
        copied[unreviewed[0]]["sha256"] = "0" * 64
        with self.assertRaisesRegex(RuntimeError, "do NOT auto-accept"):
            audit.verify({"schema": 1, "entries": copied}, actual)

    def test_guard_only_no_production_activation(self):
        # Test flags are in test-only linkage, not preemptible user code.
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        irq = (ROOT / "src/kernel/x86-irq0.S").read_text(encoding="utf-8")
        self.assertIn("SPIFFOS_M10_D58C_C2B_REAL_IRQ_TEST", makefile)
        self.assertIn("#ifdef SPIFFOS_M10_D58C_C2B_REAL_IRQ_TEST", irq)
        self.assertNotIn("-DSPIFFOS_M10_D59_ENABLE_PRODUCTION_PREEMPTION",
                         makefile)


if __name__ == "__main__":
    unittest.main(verbosity=2)
