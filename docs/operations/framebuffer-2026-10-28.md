# Framebuffer boot diagnostic — 2026-10-28

## Scope

Owner-authorized next step after local first-boot commit `be74fe2`:
request Limine screen memory, validate its format, draw a pattern and verify
QEMU output. Direct ChatGPT-guided implementation; no delegated coding tasks.
Read AGENTS.md, accepted/open registers, system overview, ADR-011, ADR-003 and
ADR-009. Scoped approval was recorded in the decision documents before code.
No physical device, USB, network infrastructure, merge or push changes.

## Backup and preservation

Verified full pre-edit archive: /home/geoff/projects/spiffos-preframebuffer-20261028T220445Z.
The archive includes Git index and untracked files. Every file hash, symlink
and permission matched, and the checkout stayed unchanged during capture.
Original staged assembly, untracked drafts and experiments remain preserved.
The framebuffer changes were initially left unstaged for review. The owner
subsequently authorized a local milestone commit; pushing remains unauthorized.

## What changed and why

- `src/kernel/main.c` adds the official revision-0 framebuffer request in the
  retained Limine requests section. It borrows the first framebuffer response
  and calls the diagnostic before the existing boot-success marker.
- `src/kernel/framebuffer.c` and `.h` isolate screen-memory operations from
  serial hardware. Supported input is 32-bit RGB with three disjoint 8-bit
  color fields, dimensions greater than 32 pixels in each axis, and aligned
  address/pitch. Missing or unsupported metadata causes no screen writes.
- Width multiplication, total span and address wrap are checked before drawing.
  Caller still relies on Limine supplying valid descriptor pointers and mapped
  screen memory; arithmetic checks cannot prove arbitrary addresses are mapped.
- Each row begins at address + y * pitch, measured in bytes. Only visible
  pixels are written. Padding at the end of each row is untouched.
- The pattern is a 16-pixel border RGB(160,160,255), enclosing red, green and
  blue vertical bars. Pixel packing follows reported channel shifts. No fixed
  resolution or color-byte ordering is assumed.
- `SPIFFOS_FRAMEBUFFER_OK` means the drawing routine completed. Unsupported
  metadata prints `SPIFFOS_FRAMEBUFFER_UNAVAILABLE`. Both paths preserve
  `SPIFFOS_BOOT_OK`, so the serial boot diagnostic remains available.
- Makefile links the drawing module. `test-framebuffer-memory` builds host-only
  tests against ordinary RAM; the kernel continues to use freestanding Clang.
- `scripts/qemu.py test --screenshot` additionally requires the framebuffer
  marker and uses a private QMP Unix socket to capture the actual QEMU display
  as `build/framebuffer.ppm`. It reaps its child and removes temporary variables
  and socket files on completion.
- `scripts/check-framebuffer.py` checks every pixel in the QEMU PPM output.
  `make test-framebuffer` combines memory tests, ISO boot, capture and pixel
  verification. PPM verification uses only Python's standard library.

## Actual validation

1. Strict kernel Clang/LLD build: PASS (-Wall -Wextra -Werror).
   First compile caught missing stddef.h for NULL; corrected and rebuilt.
   `nm -u build/spiffos-kernel.elf` reported no undefined symbols.
2. Guarded RAM tests: PASS for padded rows, guard words before/after the
   screen buffer, RGB and reversed channel layouts, NULL, unaligned address,
   unsupported format, overlapping/invalid masks, invalid pitch and arithmetic
   overflow. Rejected descriptors left the complete buffer unchanged.
3. Host sanitizers: GCC 13.3 AddressSanitizer + UndefinedBehaviorSanitizer PASS.
   Forge's Clang sanitizer runtime libraries were absent; the host test uses
   `HOST_CC ?= cc` (GCC here). No packages were installed. This does not change
   the Clang kernel toolchain.
4. `make test-framebuffer`: PASS, including the final integrated target.
   Captured `SPIFFOS_FRAMEBUFFER_OK` and `SPIFFOS_BOOT_OK`.
   QEMU display: 1280x800; every screenshot pixel verified. Purple border:
   65,536 pixels; each red/green/blue bar: 319,488 pixels.
   Screenshot was also converted locally to PNG and visually inspected.
5. Corrupted screenshot: checker returned nonzero for a deliberately altered
   final pixel (row 799). The original screenshot remained intact.
6. Original `make test`: PASS with exact serial boot marker.
7. GDB fault injection at kmain set framebuffer_request.response to NULL in
   the guest. Captured FRAMEBUFFER_UNAVAILABLE and BOOT_OK, with no framebuffer
   success marker. This verifies the absent-response startup path; it does not
   claim physical display-loss recovery or an actual no-video hardware boot.
8. System OVMF variables hash stayed unchanged:
   5d2ac383371b408398accee7ec27c8c09ea5b74a0de0ceea6513388b15be5d1e.
9. Python syntax and unstaged git diff whitespace checks: PASS.
   The staged binary patch still matches the pre-edit snapshot.

Evidence: `build/framebuffer-test.log`, `build/framebuffer-regression.log`,
`build/serial.log`, `build/framebuffer.ppm`, `build/framebuffer.png`,
`build/framebuffer-verification.json`, `build/framebuffer-fallback-gdb.log`
and `build/framebuffer-fallback-serial.log` (generated/ignored artifacts).

PPM SHA-256: c5e3bf172dbe009d21600d308450ab0b37fcb238e02d910f397cce3cbe63aa60.

## Try it locally

From `/home/geoff/projects/spiffos`:

```sh
make test-framebuffer  # memory tests, headless boot, screenshot and pixel check
make run               # display the pattern in an interactive QEMU window
```

Close the QEMU window when finished; the kernel intentionally halts after
rendering. `make test` continues to run the serial regression. Host memory
tests require a C compiler with AddressSanitizer/UndefinedBehaviorSanitizer;
other machines may need those development dependencies provisioned.

## Interactive display prerequisite and owner confirmation

The initial `make run` on Forge opened no window: `build/run-qemu.log`
reported a VNC server on 127.0.0.1:5900. At diagnosis QEMU listed only
none/curses display backends; qemu-system-gui was absent. Serial still
contained both success markers. Headless screenshot validation had passed,
but the interactive window dependency had not been verified.

On Linux Mint/Ubuntu, install the graphical backend in a local terminal:

```sh
sudo apt install qemu-system-gui
```

Then run `make run` from the graphical desktop session. The owner installed
the package and confirmed seeing the pattern. Before this milestone commit,
dpkg-query confirmed the package installed and QEMU listed gtk/sdl backends.
This host package installation was performed by the owner. It changes no
SpiffOS kernel behavior. Inspect `build/run-qemu.log` if no window appears.
The automated headless screenshot test does not verify desktop visibility.

## Limitations and rollback

Boot-only direct drawing, first framebuffer only. No font, text console,
keyboard, BASIC READY, workspace surfaces, native video driver, modesetting,
ring-3 graphics, alpha blending or physical hardware validation. This does not
settle ADR-009's graphics interfaces, logical resolution or composition policy.

For rollback, compare this work against `be74fe2`. Preserve current changes
before selectively restoring only this step's files. For full recovery, verify
and extract the pre-edit archive into a sibling directory; never overwrite the
live .git/index or use git clean/reset --hard with user changes present.

Change history: read-only inspection; verified snapshot; scoped docs-first
record; request/drawing module; strict compile correction; guarded memory tests;
host sanitizer tool selection; QMP capture; full pixel and corrupted-image
checks; serial regression; GDB absent-response test; documentation and review.

## Later text milestone

The fixed-label step adds SPIFFOS over the pattern. Current test-framebuffer
checks the complete pattern plus title; plain-bar pixel counts above describe
the original `8f48fa7` milestone. See [text validation](text-2026-10-28.md) for
current screenshots and `make test-text`.
