# SpiffOS BASIC Gap Analysis — C64 V2, VB6 Family, FreeBASIC, ISO Full BASIC

**Status:** comparative research draft, 2026-10-21. No implementation has been verified. All recommendations are **proposals**, not accepted decisions.

## Sources and limits
- Commodore 64 BASIC V2 vocabulary and guide: https://www.c64-wiki.com/wiki/BASIC_V2.0 and https://www.commodore.ca/commodore-manuals/commodore-64-programmers-reference-guide/
- Microsoft Visual Basic language reference (VBA is a **close relative**, not identical to VB6): https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/visual-basic-language-reference ; VB6-specific choices require confirmation against actual VB6 reference.
- FreeBASIC official manual: https://www.freebasic.net/wiki/DocToc
- ISO/IEC 10279:1991 Full BASIC: https://webstore.iec.ch/en/publication/9499 ; the ISO text refers to ANSI X3.113-1987 for detailed syntax/semantics. A publicly accessible preview is https://webstore.ansi.org/preview-pages/ISO/preview_ISO%2BIEC%2B10279-1991.pdf . **Do not claim ISO compliance** without access to normative text and conformance testing.
- A language feature being in another BASIC does not mean SpiffOS should implement it. Favor consistency, learnability, safety, and OS-native interfaces.

## Status vocabulary
- **DIRECTION:** prior SpiffOS decision in architecture/reference, exact grammar still open.
- **PROPOSE:** recommended for review, not accepted.
- **DEFER:** plausible post-preview feature.
- **OMIT:** recommended intentionally not to support.
- **UNRESOLVED:** needs a deliberate choice.

## C64 BASIC V2 — individual keyword coverage

| C64 V2 vocabulary | SpiffOS disposition proposal | Why / replacement |
|---|---|---|
| ABS, ATN, COS, EXP, INT, LOG, SGN, SIN, SQR, TAN | DIRECTION / clarify INT | Keep math; define radians, domain errors and whether INT floors negatives |
| RND | DIRECTION | Specify PRNG, seed/reproducibility, cryptographic randomness separately |
| AND, OR, NOT | DIRECTION | Define logical vs bitwise operators and short-circuit behavior |
| ASC, CHR$, LEFT$, LEN, MID$, RIGHT$, STR$, VAL | DIRECTION | Keep familiar text functions; define Unicode scalar vs bytes and 0/1-based string offsets |
| CLOSE, OPEN, INPUT#, GET#, PRINT# | MODERNIZE | File handles and typed FILE.* streams; consider aliases only if useful |
| CLR | UNRESOLVED | Clearing variables vs clearing console; no accidental data loss |
| CMD | OMIT | Replace implicit output redirection with explicit streams |
| CONT | PROPOSE | Resume stopped program only when execution state remains valid |
| DATA, READ, RESTORE | UNRESOLVED | Useful for beginner data tables, but arrays/constants may be clearer |
| DEF FN, FN | OMIT legacy form | Use FUNCTION ... END FUNCTION |
| DIM | DIRECTION | Zero-based inclusive upper bound, up to 8 dimensions |
| END, STOP | DIRECTION | Distinguish normal exit, debugging pause and process termination |
| FOR, NEXT, STEP, TO | DIRECTION | Structured loops with defined overflow/step-zero behavior |
| FRE | MODERNIZE | MEMORY.FREE / MEMORY.INFO; protected process semantics |
| GET | MODERNIZE | Nonblocking keyboard input API and event interface |
| GOSUB, GOTO, ON ... GOTO/GOSUB | OMIT or optional compatibility mode | Structured SUB/FUNCTION and SELECT CASE, no line numbers |
| IF, THEN | DIRECTION | Multiline IF/ELSEIF/ELSE/END IF; decide one-line IF |
| INPUT | DIRECTION | Define prompt, parsing, EOF, cancel and timeouts |
| LET | PROPOSE optional alias | Assignment without mandatory LET |
| LIST | DIRECTION | Display current unnumbered source with editor integration |
| LOAD, SAVE, RUN, NEW | DIRECTION | Define filenames, unsaved changes, file formats and current-program state |
| PEEK, POKE | DIRECTION | Protected virtual byte map; width variants and atomicity |
| PRINT | DIRECTION | Exact separator/newline/format rules missing |
| REM | DIRECTION | Also support apostrophe comment; clarify inline placement |
| RETURN | DIRECTION | Procedure/function return; no line-number subroutines |
| SYS, USR | OMIT raw unrestricted execution | Capability-checked native modules/ABI, privileged access only |
| POS, SPC, TAB | UNRESOLVED | Console cursor and formatting APIs; compatibility convenience |
| STATUS/ST | MODERNIZE | Explicit error/status objects, not implicit device channel status |
| TIME/TI, TIME$/TI$ | MODERNIZE | Time/date/timer APIs; separate monotonic clock and wall clock |
| VERIFY | UNRESOLVED | Compare saved file/checksum vs editor buffer; useful educational command? |
| WAIT | MODERNIZE | Sleep/event wait, not unrestricted busy-wait on hardware addresses |
| π | PROPOSE | Named PI constant |
| Reserved syntax: comma, colon, semicolon, parentheses | UNRESOLVED | Grammar, PRINT separators and statement separator rules must be specified |

This is a feature/keyword review, not a claim of C64 binary/source compatibility.

## Cross-language gap matrix

| Capability | C64 V2 | VB6 family | FreeBASIC | ISO Full BASIC | SpiffOS current state / decision |
|---|---|---|---|---|---|
| Line-number-free structured code | Mostly no | Yes | Yes | Standard language differs | DIRECTION; formal grammar missing |
| Typed variables and constants | Limited | Yes | Yes | Numeric/string formalism | DIRECTION; explicit vs implicit declaration open |
| Subs/functions with parameters | DEF FN limited | Yes | Yes | Functions/subprograms | DIRECTION; ByVal/ByRef, defaults, recursion open |
| Modules and visibility | No | Yes | Yes | Standard includes program organization | DIRECTION; imports, packaging and namespacing open |
| SELECT CASE / ELSEIF | No | Yes | Yes | Structured constructs | PROPOSE syntax/semantics |
| FOR EACH and iterators | No | Yes | Yes | Not necessarily analogous | PROPOSE, collection protocol missing |
| Error handling | Minimal | On Error | Error handling facilities | Defines error handling | DIRECTION TRY/CATCH; finally, error types open |
| Arrays, resizing, bounds | DIM | ReDim, bounds | Dynamic arrays | Arrays | DIRECTION; preserve and indexing edge cases open |
| Dynamic collections/maps | No | Collection | Library/container support | Not core assumption | DEFER maps/lists? Critical for JSON/network apps |
| File records/random access | OPEN channels | Random/binary | File I/O | File I/O | DIRECTION file streams; record layout/seek open |
| String manipulation and Unicode | PETSCII | Legacy codepages/Unicode variants | String functions | String model | DIRECTION UTF-8; graphemes/indexing open |
| Bitwise and binary buffers | PEEK/POKE | Operators/Byte arrays | Bit operations/pointers | Not hardware oriented | DIRECTION BYTES; endian helpers and masks open |
| Date/time, timers | TI | Date/time | Date/time | Time extensions vary | PROPOSE explicit wall/monotonic clocks |
| Formatting/printing | PRINT, TAB, SPC | Print, Format | Print, formatting | PRINT semantics | UNRESOLVED exact separators and formatting |
| File paths, directories | Device numbers | Filesystem ops | File/Dir APIs | Filesystem model | DIRECTION SpiffFS; path syntax and encoding open |
| Events and async I/O | No | Event-driven forms | Runtime/library | Not modern device model | DIRECTION Message Manager; callbacks vs polling open |
| USB HID/gamepads | Joystick via hardware registers | External controls/APIs | Platform APIs | Not specified | PROPOSE generic GAMEPAD.* and DEVICE.* |
| Serial, USB audio | Device channels | Comm/audio controls/APIs | Platform libs | Not specified | DIRECTION generic SERIAL.* and AUDIO.* |
| Graphics, sprites, sound | Registers/POKE | Forms/graphics controls | Graphics library | Not core hardware API | DIRECTION virtual graphics/audio; full syntax open |
| TCP/IP, DNS, sockets | No | Winsock controls/APIs | Libraries | Not specified | DIRECTION TCP/UDP; DNS, TLS, HTTP and partial I/O open |
| Compiler/runtime equivalence | Interpreter | Native/P-code ecosystem | Compiler | Standard semantics | DIRECTION; conformance suite and ABI open |
| Modern structured data JSON | No | Not native | Library | Not core | PROPOSE JSON.PARSE/STRINGIFY as standard library |
| Encryption/TLS | No | External APIs | Libraries | Not core | PROPOSE TLS client + secure randomness before web-browser claims |
| UI toolkit/forms | Text/sprites | Major feature | Libraries | Not core | DEFER widgets/forms; graphics primitives first |
| Accessibility/input remapping | No | OS integration | Host-dependent | Not core | PROPOSE controller mappings, keyboard accessibility and contrast requirements |

**Interpretation:** The highest-risk gaps are semantic, not missing keywords: expression evaluation, implicit conversions, parameter passing, UTF-8 indexing, errors, blocking, device lifetimes, and file/stream ownership.

## Priority decisions for the next review (ordered)
1. **BASIC syntax policy:** readable structured BASIC with optional classic aliases; no numbered GOTO/GOSUB by default. Should a compatibility subset exist at all?
2. **Variables:** implicit declarations for instant mode vs OPTION EXPLICIT in source; default INT64/FLOAT64 selection and conversion rules.
3. **Strings:** whether MID$/LEFT$/LEN count Unicode code points, grapheme clusters, or bytes; 0- vs 1-based string positions.
4. **Functions:** pass by value or reference, optional parameters, return types, recursive calls, scope and module imports.
5. **Arrays/collections:** REDIM PRESERVE behavior, FOR EACH, dynamic LIST/MAP, BYTES slicing.
6. **Errors:** exception hierarchy, TRY/CATCH/FINALLY, OS errors vs BASIC errors, cleanup on device unplug.
7. **I/O:** nonblocking GET/KEY, PRINT punctuation, INPUT cancellation, streams, console redirection.
8. **USB gamepads:** HID normalized controls, raw report capability, simultaneous controllers, calibration, mapping, hotplug and events.
9. **USB serial and audio:** DTR/RTS safety, duplex, buffers, timing, concurrency and capability permissions.
10. **Networking for browser ambition:** DNS, TLS, HTTP, encoding, URLs, JSON, timeouts and certificate trust; TCP alone is insufficient for modern HTTPS.
11. **Implementation boundary:** smallest v0.1 interpreter feature set vs features specified now and deferred.

## Recommendation: three tiers
- **Tier A (must work in first QEMU BASIC preview):** console, editor, assignments, math, strings, IF, loops, SUB/FUNCTION, DIM, TRY/CATCH, LOAD/SAVE/RUN, file persistence, keyboard input and 3–5 verified sample programs.
- **Tier B (required to publish broader Developer Preview):** graphics, sprites, virtual sound, network basics, USB HID gamepad where testable, generic device enumeration and serial, audio I/O; real hardware validation required for USB audio and serial.
- **Tier C (specified but can ship later):** full native compiler optimizations, advanced networking/TLS/browser libraries, forms/widgets, vendor-specific HID, Wi-Fi, Bluetooth, sophisticated collections.

Do not mark a command as implemented based on documentation. Each approved entry needs parser, interpreter, compiler and device-mock conformance evidence as applicable.

## Suggested review ledger format
`ID | capability | origin (C64/VB6/FreeBASIC/ISO) | recommendation | user decision INCLUDE/DEFER/OMIT | syntax decision | runtime gate | test ID`

Review one decision at a time, preserving rejected features in the ledger so gaps are intentional and documented.
