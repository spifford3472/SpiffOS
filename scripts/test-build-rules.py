#!/usr/bin/env python3
"""Reject header/PCH input when a kernel rule claims to build an ELF object."""
from pathlib import Path
import shlex
import subprocess


def main():
    """Inspect forced dry-run compile inputs without mutating build artifacts."""
    root = Path(__file__).resolve().parents[1]
    plan = subprocess.check_output(["make", "-Bn", "all"], cwd=root, text=True)
    compiled = set()
    for line in plan.splitlines():
        args = shlex.split(line)
        if "-c" not in args or "-o" not in args:
            continue
        source = Path(args[args.index("-c") + 1])
        output = Path(args[args.index("-o") + 1])
        assert source.suffix in (".c", ".S", ".s"), (source, output)
        assert source.stem == output.stem and output.suffix == ".o", (source, output)
        compiled.add(output.name)
    assert "memory-boot.o" in compiled, "boot compilation absent"
    print(f"PASS build rules: {len(compiled)} kernel objects use translation units")


if __name__ == "__main__":
    main()
