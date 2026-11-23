"""Independent, byte-exact acceptance checks for the hosted BASIC preview."""
import subprocess
import sys

PROMPT = b"READY.\n"


def check(binary, label, data, outcomes):
    """Require the whole transcript and a clean process exit for one session."""
    expected = PROMPT
    for outcome in outcomes:
        if outcome:
            expected += outcome + b"\n"
        expected += PROMPT
    completed = subprocess.run(
        [binary], input=data, capture_output=True, timeout=10, check=False
    )
    if (completed.returncode, completed.stdout, completed.stderr) != (0, expected, b""):
        raise AssertionError(
            f"{label}: exit {completed.returncode}, stderr {completed.stderr!r}, "
            f"expected {expected!r}, got {completed.stdout!r}"
        )


def run(binary):
    """Exercise the published grammar, limits and recovery through actual input."""
    check(binary, "operators and precedence",
          b"PRINT 2 + 2\nprint 2 + 3 * 4\nPRINT (2 + 3) * 4\nPRINT 9 - 4 - 2\n"
          b"PRINT -7 \\ 3\nPRINT 20 \\ 3 * 2\nPRINT -(1 + 2)\n",
          [b"4", b"14", b"20", b"3", b"-2", b"12", b"-3"])
    check(binary, "case, atomic assignment and recovery",
          b"Count = 7\nPRINT count\nCOUNT = 7 \\ 0\nPRINT COUNT\n"
          b"Count = 9 trailing\nPRINT count\nPRINT (2 +\nPRINT 2\n"
          b"missing = 1 \\ 0\nPRINT missing\n",
          [b"", b"7", b"ERROR: Division by zero", b"7", b"ERROR: Syntax at column 11",
           b"7", b"ERROR: Syntax at column 11", b"2", b"ERROR: Division by zero",
           b"ERROR: Undefined variable MISSING"])
    check(binary, "new session", b"PRINT COUNT\n", [b"ERROR: Undefined variable COUNT"])
    check(binary, "signed 64-bit boundaries",
          b"PRINT -9223372036854775808\nPRINT 9223372036854775808\n"
          b"PRINT 9223372036854775807 + 1\nPRINT 3037000500 * 3037000500\n"
          b"PRINT -9223372036854775808 \\ -1\nPRINT -(-9223372036854775808)\n",
          [b"-9223372036854775808"] + [b"ERROR: Integer overflow"] * 5)
    check(binary, "empty and mixed line endings", b"\n\t \r\nPRINT 2\r\nPRINT 3",
          [b"", b"", b"2", b"3"])
    check(binary, "empty EOF", b"", [])
    check(binary, "NUL and lone carriage return", b"PRINT 1\0 + 2\nPRINT 1\r",
          [b"ERROR: Invalid character", b"ERROR: Invalid character"])
    line = b" " * 248 + b"PRINT 1"
    check(binary, "line length and excess discarded",
          line + b"\r\n" + b" " + line + b"PRINT 9\nPRINT 2\n" + b" " + line,
          [b"1", b"ERROR: Line too long", b"2", b"ERROR: Line too long"])
    nested = lambda count: b"PRINT " + b"(" * count + b"1" + b")" * count + b"\n"
    check(binary, "nesting limits",
          nested(32) + nested(33) + b"PRINT " + b"+" * 32 + b"1\nPRINT " + b"+" * 33 + b"1\n",
          [b"1", b"ERROR: Expression too deep", b"1", b"ERROR: Expression too deep"])
    name = b"a" * 31
    check(binary, "name limits", name + b" = 4\nPRINT " + name.upper() + b"\n" + name + b"a = 3\n",
          [b"", b"4", b"ERROR: Name too long"])
    capacity = b"".join(f"v{n} = {n}\n".encode() for n in range(32))
    check(binary, "capacity leaves existing assignments usable",
          capacity + b"v32 = 9\nv0 = 42\nPRINT V0\nPRINT v32\n",
          [b""] * 32 + [b"ERROR: Too many variables", b"", b"42", b"ERROR: Undefined variable V32"])
    print("SPIFFOS_BASIC_INTERACTIVE_ACCEPTANCE_OK")


if __name__ == "__main__":
    run(sys.argv[1])
