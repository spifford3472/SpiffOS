# SpiffOS BASIC interactive preview

**Approved for hosted implementation and verification • November 22, 2026**

The same interpreter core should first run in a hosted C harness and later run as a Ring 3 application on SpiffOS. The first QEMU milestone is a keyboard-driven prompt that displays `READY.`, accepts `PRINT 2 + 2`, displays `4`, and returns to `READY.`. The Ring 3 proof and the interactive console contract are separate prerequisites for the guest integration.

## Commands and expressions

| Form | Approved preview behavior |
| --- | --- |
| `PRINT expression` | Print one signed integer in decimal followed by a newline. |
| `name = expression` | Evaluate the expression, then create or replace the named session variable. Successful assignment emits only the next prompt. |
| Blank or whitespace-only line | Show a fresh prompt with no result or error. |
| Expressions | Decimal integers, variables, parentheses, unary `+` and `-`, and binary `*`, `\`, `+` and `-`. Unary signs bind first, then multiplication and integer division, then addition and subtraction. Binary operators at the same precedence associate left to right. |

Commands and names are ASCII case-insensitive. A name starts with a letter and can contain letters, digits and underscores. `PRINT` is reserved. Spaces and tabs may separate tokens. Each line contains one complete statement, and the interpreter checks the entire line before emitting a result or changing a variable.

Values are signed 64-bit integers from −9223372036854775808 through 9223372036854775807. Integer division truncates toward zero. The preview uses the BASIC-style backslash for integer division so the meaning of `/` can be decided alongside floating-point division. Signed decimal input supports the minimum value when a unary minus directly precedes the decimal token, with optional whitespace. The positive magnitude alone is out of range. Each evaluated operation checks range, including unary negation and minimum-value division by −1.

A first assignment creates the variable. A read before assignment reports an undefined variable. This catches typos and leaves the initialization rules for future declared variables and arrays intact. Variables last for the session and each new session starts empty.

## Input, limits and recovery

The session writes `READY.\n` at startup and after each submitted line, including a failed line. The host reads LF or CRLF-delimited input and ends the session on EOF. A nonempty final line ending at EOF is submitted once. Input echo and editing belong to the console or terminal. The interpreter emits only results, errors and prompts.

Use bounded preview limits of 255 bytes per line excluding its terminator, 32 variables per session, 31 characters per name and 32 levels of parenthesis or unary-sign nesting combined. Report an exceeded limit and return to the prompt. On overlong input, discard the entire line through its terminator so its suffix cannot become a command. When the variable table is full, existing names remain assignable.

Errors report a short reason and emit no result. Any failed assignment preserves the previous value and does not create a new variable. A syntax error or unsupported statement reports the first unexpected token or end of line with a one-based byte column. Numeric errors cover overflow and division by zero. The C core should receive complete lines and return a bounded response without device, terminal, or kernel dependencies. It should use explicit session state so tests can verify isolation and retry behavior.

## Hosted acceptance cases

Run cases in order within each group using a new session for each group. Assert the initial prompt and each resulting prompt as well as the listed result or error. Results and errors end in a newline. Hosted conformance execution is pending implementation.

| Group | Input | Expected before the next `READY.` |
| --- | --- | --- |
| Arithmetic | `PRINT 2 + 2` | `4` |
| Arithmetic | `print 2 + 3 * 4` | `14` |
| Arithmetic | `PRINT (2 + 3) * 4` | `20` |
| Arithmetic | `PRINT 9 - 4 - 2` | `3` |
| Arithmetic | `PRINT -7 \ 3` | `-2` |
| Arithmetic | `PRINT 20 \ 3 * 2` | `12` |
| Arithmetic | `PRINT -(1 + 2)` | `-3` |
| Recovery | `Count = 7` then `PRINT count` | Assignment emits no result, then `7` |
| Recovery | `COUNT = 7 \ 0` then `PRINT COUNT` | `ERROR: Division by zero`, then `7` |
| Recovery | `Count = 9 trailing` then `PRINT count` | `ERROR: Syntax at column 11`, then `7` |
| Recovery | `PRINT (2 +` then `PRINT 2` | `ERROR: Syntax at column 11`, then `2` |
| Recovery | `missing = 1 \ 0` then `PRINT missing` | `ERROR: Division by zero`, then `ERROR: Undefined variable MISSING` |
| Boundaries | `PRINT -9223372036854775808` | `-9223372036854775808` |
| Boundaries | `PRINT 9223372036854775808` | `ERROR: Integer overflow` |
| Boundaries | `PRINT 9223372036854775807 + 1` | `ERROR: Integer overflow` |
| Boundaries | `PRINT 3037000500 * 3037000500` | `ERROR: Integer overflow` |
| Boundaries | `PRINT -9223372036854775808 \ -1` | `ERROR: Integer overflow` |
| Boundaries | `PRINT -(-9223372036854775808)` | `ERROR: Integer overflow` |

Also automate these generated cases.

- Prefix `PRINT 1` with 248 spaces to make exactly 255 bytes and expect `1`. Add one space for the 256-byte case and expect `ERROR: Line too long`. Send a new `PRINT 2` line and expect `2`.
- Submit `PRINT 1` wrapped as `PRINT ` followed by 32 opening parentheses, `1`, and 32 closing parentheses. Expect `1`. Use 33 pairs and expect `ERROR: Expression too deep`.
- Assign `v0` through `v31`. Expect `ERROR: Too many variables` when assigning `v32`, then reassign `v0` and confirm its value. Use names of 31 and 32 characters and require `ERROR: Name too long` only for the latter.
- Check an empty line, CRLF, and a final complete line without a newline at EOF. Reject embedded NUL or other unsupported input bytes without evaluating a prefix. Verify a new session cannot see an earlier session’s variables.

## Guest acceptance and next scope

After the QEMU-only Ring 3 diagnostic passes, review a small console and launch contract for one interactive task. Then boot an isolated image on Forge and inject keyboard input through QEMU. Verify the displayed prompt, the result of `PRINT 2 + 2`, an error followed by a successful command, and continued workspace input and supervisor progress. Check the baseline profiles and the exact proposed merge tree before requesting promotion.

RAM-only program editing and execution follow the interactive milestone. Strings, floating point, persistent storage, graphics, audio, networking and the native compiler remain outside this preview.

The governing references are [ADR-005](https://github.com/spifford3472/SpiffOS/blob/main/docs/architecture/adr-005-basic-language-native-compiler.md), [ADR-006](https://github.com/spifford3472/SpiffOS/blob/main/docs/architecture/adr-006-basic-process-ipc-safety.md), the [Programmer’s Reference outline](https://github.com/spifford3472/SpiffOS/blob/main/docs/basic/programmers-reference.md) and the [M11 diagnostic proposal](https://github.com/spifford3472/SpiffOS/pull/97).
