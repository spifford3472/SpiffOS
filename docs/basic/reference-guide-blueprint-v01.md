# SpiffOS Programmer's Reference Guide — Editorial and Content Blueprint v0.1

**Status:** original draft for review, not a language specification or implemented features.

## Editorial goal
Capture the joy of the 8-bit programmer's desk reference: begin with something a beginner can type, explain exactly why it works, then offer the bit-level details, tables, and schematics that invite exploration. This book is an **original SpiffOS publication**, not a reproduction of the Commodore manual.

## Visual identity and publication
- Two editions from one source: navigable accessible HTML and printable PDF.
- Page: warm ivory background, deep navy text, turquoise and sunset accents from the SpiffOS mountain logo. Monospaced code and outputs; numbered figures, tables, chapter running heads, printed page numbers, generous margins.
- Use explicit labels, icons, patterns and text in addition to color (colorblind accessible).
- Code samples: every complete listing has a filename, expected output, minimum runtime version, implementation status and future test identifier.
- Each chapter has: learning objectives, type-in tutorial, explanation, technical reference, troubleshooting, experiments, and a small complete project.
- Each command entry: NAME, PURPOSE, FORMAT, PARAMETERS, RETURNS, DESCRIPTION, EXAMPLES, OUTPUT, ERRORS, NOTES, RELATED COMMANDS, COMPATIBILITY, STATUS, TESTS.
- Do not borrow original Commodore artwork, distinctive C logo, or copyrighted prose. Original diagrams and original code only.

## Proposed detailed contents

### Part I — Your computer is ready
1. **Welcome to SpiffOS** — instant BASIC, first commands, 8-bit spirit and modern differences.
2. **Using the screen and editor** — direct vs program mode, editor focus, running/stopping, listing, saving, debugging, help and keyboard shortcuts.
3. **Writing your first programs** — PRINT, INPUT, comments, expressions, branching, loops, a guessing game.

### Part II — BASIC programming rules
4. **Source grammar** — tokens, whitespace, continuation, case sensitivity, comments, identifier rules, keyword namespaces, reserved words, literals, precedence.
5. **Values and variables** — INT64, FLOAT64, BYTE, INT32, BOOLEAN, STRING, BYTES, TYPE; coercion, overflow, NULL/optional, Unicode, scope and lifetime.
6. **Control flow** — IF, SELECT, FOR, FOR EACH, WHILE, DO, EXIT, RETURN, early termination.
7. **Collections and data** — DIM, REDIM, multidimensional arrays, bounds, structures, DATA/READ decision, dynamic collections decision.
8. **Procedures and modules** — SUB, FUNCTION, parameter passing, recursion, modules/imports, visibility, library versioning.
9. **Errors and debugging** — TRY/CATCH/FINALLY, error codes, stack traces, editor breakpoints, TRACE, ASSERT.

### Part III — Alphabetical BASIC vocabulary
10. **Statements and functions A–Z** — one reference entry for *every* supported keyword and callable function, including modern OS APIs; index generated from a versioned registry.
11. **Operator and type reference** — precedence tables, conversions, numeric limits, text/byte behavior.

### Part IV — Programming the machine
12. **Graphics** — logical resolutions, colors, pixels, sprites, tiles, layers, collision and animation.
13. **Sound and music** — virtual voices, instruments, sequences, SFX, mixing across nine workspaces.
14. **Memory and virtual registers** — hexadecimal, byte-addressed virtual map, PEEK/POKE and width variants, little endian, permissions, atomicity, safe examples.
15. **Input and controllers** — keyboard, mouse, USB HID gamepads, button/axis mappings, polling and events, calibration and unplug.
16. **Files and storage** — directories, text/binary I/O, atomic writes, file handles, crash consistency.
17. **USB and external hardware** — discovery, USB serial, HID reports, permissions, USB audio capture/playback, failure handling and practical external-device tutorial.
18. **Networking** — Ethernet, IP/DNS, TCP/UDP, sockets, timeouts, HTTP client and simple server.
19. **Processes and workspaces** — tasks, Message Manager, asynchronous events, permissions, concurrency.
20. **Native compilation and deployment** — interpreter/compiler equivalence, .SPX builds, modules, errors, packaging.

### Part V — Complete projects
21. **Text adventure and guessing game**
22. **Animated sprite arcade game** — keyboard and USB controller input.
23. **Music sequencer with live effects**
24. **Data logger** — filesystem, timers and structured records.
25. **Network information client** — sockets and UTF-8.
26. **USB serial monitor** — safe generic read-only first example.
27. **Audio level meter** — USB microphone/line-in.
28. **Multitasking demonstration** — music in background while another workspace runs.

### Appendices
A. Complete alphabetized command inventory and quick syntax cards.
B. C64 BASIC V2 comparison: retained, modernized, intentionally omitted, replaced.
C. VB6-family and FreeBASIC comparison: concepts adopted, deferred, intentionally excluded.
D. ISO Full BASIC conformance discussion: alignments and intentional divergences (no claim of compliance).
E. Virtual memory/register maps, USB class support matrix, audio formats and error codes.
F. Data types, operator precedence, ASCII/Unicode, hex and endian reference.
G. All sample programs, expected outputs and conformance test IDs.
H. Glossary, index and version history.

## Example page: PRINT (original illustrative entry)

**PURPOSE:** Display a value or expression in the active BASIC output pane.

**FORMAT (proposed):** `PRINT expression [, expression ...]`

**EXAMPLE:**
```basic
PRINT "SPIFFOS IS READY"
FOR count = 1 TO 3
    PRINT "COUNT "; count
NEXT count
```

**EXPECTED OUTPUT (provisional):**
```text
SPIFFOS IS READY
COUNT 1
COUNT 2
COUNT 3
```

**UNRESOLVED:** Comma/semicolon formatting, newline suppression, numeric format, console width and cursor state, Unicode width, redirected output, and what happens if output pane loses focus. Test identifier: `BASIC-CONSOLE-PRINT-001` (planned, not executed).

## Editorial acceptance gate
Before calling a chapter complete: every named API has a registry entry and syntax decision; every example is either prominently labeled *illustrative* or has passed its conformance test on SpiffOS; diagrams and wording are original; both HTML and PDF can be generated reproducibly; links work offline where practical.
