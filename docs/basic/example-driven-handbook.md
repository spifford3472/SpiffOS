# SpiffOS BASIC — Example-Driven Programmer's Handbook (Draft v0.1)

**Status:** design examples, **not executed or conformance-tested**. Written 2026-10-20. This is a companion to `docs/basic/programmers-reference.md`, not a replacement for the canonical command catalog.

## How to read this handbook
- **ACCEPTED BEHAVIOR** means the architecture/language decision is approved.
- **PROPOSED SPELLING** means the exact BASIC syntax has not been ratified. All code blocks here are illustrative programs pending interpreter/compiler implementation.
- **EXPECTED** is the intended result, not output observed on a running SpiffOS system.
- Every final command entry must eventually state signature, parameters, types, return, errors, permissions, blocking behavior, side effects, version, and a passing test.

## 1. PRINT — write text and values
**Accepted:** BASIC can print strings and numbers. **Proposed:** comma-separated arguments.

```basic
PRINT "Hello, SpiffOS!"
score = 125
PRINT "Score:", score
```
**Expected console:** `Hello, SpiffOS!` then `Score: 125` (exact spacing to be specified).
**Why:** quickest way to show results and debug programs. **Open:** formatting, separators, newline suppression.

## 2. Variables and arithmetic — no line numbers
**Accepted:** implicit variables, default INT64 whole numbers, FLOAT64 fractional values, automatic promotion for implicit variables.

```basic
apples = 3
apples = apples + 2
price = 1.5
total = apples * price
PRINT apples
PRINT total
```
**Expected:** `5` and `7.5`. **Why:** calculations and program state.
**Error case:** checked INT64 overflow raises a catchable error; it must not silently wrap.

## 3. IF / ELSE — make a decision
**Accepted:** structured BASIC without line numbers. **Proposed spelling:**

```basic
temperature = 72
IF temperature >= 70 THEN
    PRINT "Warm"
ELSE
    PRINT "Cool"
END IF
```
**Expected:** `Warm`. **Open:** exact grammar for THEN and END IF.

## 4. FOR / NEXT — repeat work
**Accepted:** structured loops. **Proposed spelling:**

```basic
FOR i = 1 TO 3
    PRINT i
NEXT i
```
**Expected:** three lines: `1`, `2`, `3`. **Open:** STEP, scope, loop-variable rules.

## 5. Arrays — numbered collections
**Accepted:** zero-based, inclusive upper bound, initialized, bounds-checked, up to eight dimensions.

```basic
DIM scores(2) AS INT64
scores(0) = 10
scores(1) = 20
scores(2) = 30
PRINT scores(0) + scores(1) + scores(2)
```
**Expected:** `60`. `DIM scores(2)` allocates **three** elements (0,1,2).
**Error case:** `scores(3)` raises an array-bounds error; it cannot overwrite other memory.
**Why:** lists of scores, pixel data, game objects.

## 6. SUB — reusable procedure
**Accepted:** named procedures and local variables. **Proposed signature:**

```basic
SUB Greet(person AS STRING)
    PRINT "Hello, " + person
END SUB

Greet("Ada")
```
**Expected:** `Hello, Ada`. **Open:** argument passing, concatenation operator, declaration order.

## 7. TRY / CATCH — recover from errors
**Accepted:** catchable runtime errors. **Proposed spelling:**

```basic
TRY
    text = FILE.READ("NOT-THERE.TXT")
    PRINT text
CATCH err
    PRINT "Cannot read file:", err.MESSAGE
END TRY
```
**Expected:** readable error if file is absent; no process crash.
**Why:** disk, network and arithmetic failures are normal conditions that applications should handle.

## 8. FILE.WRITE and FILE.READ — simplest file operations
**Accepted:** beginner file functions, SpiffFS crash-consistent operations. **Proposed spelling:**

```basic
FILE.WRITE "GREETING.TXT", "Hello from SpiffOS"
message = FILE.READ("GREETING.TXT")
PRINT message
```
**Expected:** `Hello from SpiffOS`. **Why:** saving a note or game score.
**Important:** exact relative path and overwrite rules are pending; SpiffFS durability requires a successful commit/flush.

## 9. FILE.OPEN / WRITELINE / CLOSE — advanced files
**Accepted:** explicit file handles for advanced I/O. **Proposed spelling:**

```basic
f = FILE.OPEN("LOG.TXT", "WRITE")
FILE.WRITELINE f, "First record"
FILE.CLOSE f
PRINT FILE.READ("LOG.TXT")
```
**Expected:** contents include `First record` and a line ending.
**Why:** streaming large files or controlling handle lifetime. **Open:** newline encoding, close/commit behavior, lock modes.

## 10. BYTES — binary data is not text
**Accepted:** dynamic BYTES distinct from UTF-8 STRING, explicit conversions. **Proposed spelling:**

```basic
packet = BYTES(4)
packet(0) = 72
packet(1) = 73
packet(2) = 0
packet(3) = 255
PRINT packet(0)
```
**Expected:** `72`. **Why:** binary file, network packet, sound sample.
**Open:** buffer indexing syntax, length, slicing and conversion functions.

## 11. Understanding memory — 64-bit OS, byte-addressed storage
**Accepted direction:** x86-64 uses **one 8-bit byte per address**; 64-bit values span eight consecutive byte addresses. Little-endian means least-significant byte first. BASIC's default INT64 type does not change the byte-addressed memory model.

**Important scope:** Examples below assume a **writable, ordinary logical RAM region**, not the currently proposed virtual device registers. The final memory allocation/address API has not been defined. `&H20000` is **illustrative only** and is not guaranteed to be allocated or writable. These examples must be updated with a supported memory-allocation method before they can run.

### 11A. Write one byte
```basic
POKE &H20000, 232
PRINT PEEK(&H20000)
```
**Expected:** `232`; address `$20000` contains hex `E8`.

### 11B. Write a 16-bit number in one operation
```basic
POKE16 &H20000, 1000
PRINT PEEK16(&H20000)
```
**Expected:** `1000`. Byte view after the write: `$20000 = E8` (232), `$20001 = 03` (3).

### 11C. Write the same 16-bit number as two bytes
```basic
value = 1000
POKE &H20000, value MOD 256
POKE &H20001, (value \ 256) MOD 256
PRINT PEEK(&H20000) + 256 * PEEK(&H20001)
```
**Expected:** `1000`. **Proposed:** `\` is integer division; exact operator spelling still requires approval.
**Why:** learn byte layout or edit part of an ordinary RAM value.

### 11D. Read the same bytes in different widths
Assume bytes at `$20000..$20003` are `E8 03 00 00`.

```basic
PRINT PEEK(&H20000)
PRINT PEEK16(&H20000)
PRINT PEEK32(&H20000)
```
**Expected:** `232`, `1000`, `1000`. Wider reads consume more adjacent bytes, not larger address slots.

### 11E. Change only the high byte
```basic
POKE16 &H20000, 1000
POKE &H20001, 4
PRINT PEEK16(&H20000)
```
**Expected:** `1256` (hex `04E8` = 4 × 256 + 232).

### 11F. A 64-bit value spans eight bytes
```basic
POKE64 &H20000, 1000000
PRINT PEEK64(&H20000)
```
**Expected:** `1000000`. Bytes in ascending addresses: `40 42 0F 00 00 00 00 00`.

### 11G. Why device registers are different
**Accepted design direction, pending ADR-011:** a device register can have read/write side effects. A single `POKE32` to a 32-bit register should commit one coherent register update; four individual byte writes might cause intermediate states or be disallowed for that register. Never assume arbitrary device-register byte writes behave like RAM. Use documented named constants and the register's specified width.

```basic
' PROPOSED ONLY — name/address not yet assigned
POKE32 SPRITE_X(1), 400
```
**Why:** use a named constant and the correct register width rather than memorizing hex offsets.

## 12. SPRITE.MOVE — move a character
**Accepted:** virtual sprite engine; default sprite size 32×32; full per-pixel RGBA; collision detection enabled by default. **Proposed syntax:**

```basic
SPRITE.LOAD hero, "HERO"
SPRITE.MOVE hero, 100, 150
SPRITE.LAYER hero, 4
```
**Expected:** a loaded hero at logical (100,150), visually in layer 4.
**Why:** game movement without direct pixel drawing. **Open:** sprite creation/asset handle and numbering, valid coordinate ranges, exact layer range.

## 13. TILE.SET — edit a shared tile definition
**Accepted:** modifying a tile definition updates all visible uses, synchronized to presentation.

```basic
TILE.SET 12, "STONE"
```
**Expected:** visible uses of tile 12 display the new definition. **Open:** tile asset format and exact command signature.

## 14. MUSIC.PLAY and SOUND.PLAY — music plus effects
**Accepted:** 32 virtual voices **per workspace**, shared pool for music and effects; Audio Manager mixes across workspaces; switching focus does not stop playback.

```basic
MUSIC.PLAY "THEME"
SOUND.PLAY "LASER"
```
**Expected:** music and effect overlap if voice capacity and global mix budget permit. **Error case:** attempting to allocate an unavailable 33rd voice in one workspace reports an error; no automatic voice stealing.
**Open:** voice reservation/accounting, sound asset formats, exact API names.

## 15. TCP.CONNECT / RECEIVE — start a network request
**Accepted:** TCP/IP, DNS, timeouts, partial reads; blocking suspends only the calling process.

```basic
TRY
    sock = TCP.CONNECT("example.com", 80, TIMEOUT=5000)
    data = TCP.RECEIVE(sock, 4096)
    TCP.CLOSE sock
CATCH err
    PRINT err.MESSAGE
END TRY
```
**Expected:** either a connection and zero-or-more received bytes, or a catchable error. This is **not a complete HTTP client**: it sends no HTTP request. `RECEIVE` can return fewer bytes than requested. **Open:** buffer types and timeout units.

## 16. TASK.START and MESSAGE.SEND — independent processes
**Accepted:** processes are isolated, messages delivered through one kernel Message Manager, not shared memory. **Proposed syntax:**

```basic
worker = TASK.START("WORKER.SPX")
MESSAGE.SEND worker, "START"
```
**Expected:** worker process is launched and receives a message if ready/authorized. **Open:** inbox readiness, queue-full behavior, exact message schema and send API.

## 17. Compilation — native executable from BASIC
**Accepted:** built-in interpreter and native x86-64 compiler share semantics; compiled SPX launches independently.

```basic
COMPILE "HELLO.SPX"
RUN "HELLO.SPX"
```
**Expected:** a compiled executable is produced from current source and launched, assuming compilation succeeds. **Open:** syntax, executable ABI, source/project selection, errors.

## Reference completion checklist
For **every** BASIC command, create a canonical entry with:
1. Name, accepted/proposed status, introduction version.
2. Exact signature, arguments, types, defaults, return.
3. Behavior and side effects; process/workspace ownership.
4. Blocking and asynchronous/event semantics.
5. Valid ranges, boundary conditions and catchable errors.
6. Permissions and safe PEEK/POKE equivalents if any.
7. Minimal example, realistic combined example, expected output/state.
8. A runnable conformance test once an implementation exists.

## Known decisions needed before labeling examples runnable
- Exact BASIC grammar, operator spellings and string concatenation.
- Allocated logical RAM interface and memory safety for PEEK/POKE.
- ADR-011 final virtual device addresses, widths, atomicity and named register constants.
- Sprite asset loading, tile asset formats, events and collision syntax.
- Audio asset formats, per-workspace/global mix limits.
- Network binary/text API signatures, IPC message schema.
- Runtime test harness and interpreter/compiler implementation.
