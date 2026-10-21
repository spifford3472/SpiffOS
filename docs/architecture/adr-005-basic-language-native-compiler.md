# ADR-005 — BASIC language model and native compiler

Status: Accepted design direction; detailed syntax/ABI still open
Date: 2026-10-20

## Principle
Anyone who can write a BASIC program should be able to produce a native SpiffOS executable without learning a separate language, linker or build system. Interpreted and compiled BASIC must share language semantics and OS APIs.

## Runtime and compilation
- Include interpreter **and native x86-64 compiler** in every SpiffOS installation; no optional developer package.
- BASIC programs are structured without mandatory line numbers.
- Proposed editor shortcuts: F5 run interpreted, F9 compile with errors shown in editor. Proposed command: `COMPILE "MYGAME.SPX"`; launch with `RUN "MYGAME.SPX"`. Exact grammar is not finalized.
- Native SPX programs run without BASIC source and use OS runtime services. Compiler automatically handles linking, memory layout, entry point and runtime dependencies. Source is retained when compiling.
- SPX is a proposed native executable format containing architecture/version metadata, entry point, code/data and relocation requirements. Binary layout, ABI, compatibility and distribution policies remain open.
- Native compiled code should materially improve compute-bound performance; benchmark rather than guarantee speedups. Optimization can follow a correct initial compiler.
- A compiled SPX is normally a separate Ring 3 process; current workspace is the proposed default launch location, with cross-workspace launch options to be finalized.
- Distinguish `RUN` of an SPX, `BLOAD` of raw bytes into permitted logical memory, and controlled `SYS` execution of native code. Raw bytes do not automatically become executable. Enforce W^X and process isolation; native-code ABI, helper-process isolation and calling conventions remain open.
- Proposed future BASIC-to-native function calls (e.g. `NATIVE.CALL`) are not approved syntax.

## Types and numeric behavior
- Undeclared variables are allowed; automatic typing for beginners and optional explicit type declarations for advanced users.
- Default whole-number type signed 64-bit integer; default floating-point type IEEE 754 binary64. Include `INT32`, `BYTE` (unsigned 0–255), `BOOLEAN`, UTF-8 `STRING`, arrays and structured records.
- Automatically typed variables may promote from integer to floating-point; explicitly declared types reject lossy implicit conversion. Explicit conversion functions are available.
- Numeric overflow and conversion errors are catchable, not silent wraparound by default. Interpreter/compiler must behave consistently, including overflow and conversions.
- `TRY/CATCH` is a standard language feature for filesystem, networking, arithmetic, memory and other recoverable errors.

## Arrays and memory
- Zero-based arrays with **inclusive** upper bounds: `DIM a(10)` has 11 elements, indices 0..10.
- Support **up to eight dimensions**, row-major layout (last index varies fastest), automatic initialization and bounds checks in interpreted and compiled programs.
- `REDIM` and `REDIM PRESERVE` support resizing; exact multidimensional preserve rules are open.
- No arbitrary per-dimension element cap beyond representable indices, checked total allocation size and process memory limits.
- Default values: numeric/byte 0, Boolean FALSE, string empty, structures recursively initialized.
- Automatic memory management for strings, arrays, structures and dynamic buffers.

## Text and binary
- Strings are UTF-8 text, dynamically sized. Text length should count Unicode characters while `BYTELEN` counts encoded bytes; exact Unicode segmentation semantics remain open.
- `BYTE` arrays are normal numeric arrays. Native dynamic `BYTES` buffers are first-class, byte-preserving values for files, sockets, audio, graphics and native routines.
- Text/binary conversions must be explicit (proposed `TEXT.ENCODE` and `TEXT.DECODE`). Never silently reinterpret arbitrary binary data as text.
- Provide endian-aware binary reads/writes and efficient buffer slicing; support zero-copy or ownership transfer where safe, without exposing shared writable process memory.

## Open questions
Complete type inference rules; numeric literal rules; Unicode text length/index semantics; fixed vs dynamic structure layout; buffer API signatures; compiler intermediate representation, diagnostics, runtime ABI, linking and SPX file format; `SYS` safety and native function ABI; exception semantics and conformance tests.

No code or implementation authorization is implied by this ADR.
