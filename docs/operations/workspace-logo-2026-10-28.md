# Initial logo content and uniform workspace — 2026-10-28

## Owner corrections and precedence

The owner corrected the fixed-header treatment: the power-up logo must scroll
off with content and never return automatically. It returns only on explicit
workspace reset or boot, and its former space is usable. The owner then requested
one consistent screen background rather than different header/panel colors.
These explicit corrections supersede the fixed header, permanent border and
inset console in the earlier scroll/logo record. Older rationale is retained.

Read AGENTS.md and applicable decision/architecture/boot/graphics records;
recorded latest corrections before implementation. Direct ChatGPT-guided work,
no delegated coding. No packages, physical devices, USB, infrastructure, merge
or push changes. Preserve existing staged assembly and untracked drafts.
Full verified pre-correction backup: /home/geoff/projects/spiffos-workspace-logo-20261029T000747Z. A further copy of touched
files before uniform-color changes is in its before-uniform-color directory.
This work and the preceding scrolling/logo changes remain uncommitted.

## Final behavior

- One RGB(9,27,53) navy background covers the entire physical display. The old
  purple border and separate console panel are removed. Text stays lavender.
- The authoritative original SVG is unchanged. The boot-only derivative changes
  its backing rectangle to the same flat navy and removes rounded backing
  corners. Mountains, sunset, path, wordmark and motto retain their colors.
  Regenerated PNG/RGB/JSON provenance records this approved backdrop policy.
- `boot_workspace_reset()` initializes the entire character grid at (0,0),
  draws the logo initially at centered x,y=24, and positions the cursor at
  column 0,row 13 (y=208). This skips over initial art without reserving rows.
- At 1280x800, all 80 columns and 50 rows are usable. Other sizes use whole
  16x16 cells; remaining sub-cell edge pixels share the same navy background.
- Scroll copies the entire grid upward by 16 pixels, including the logo. Once
  the bitmap leaves the viewport there is no stored display layer to restore
  it. More scrolling, cursor movement and writing never invoke logo rendering.
- `text_console_position()` moves the logical cursor to any valid cell without
  drawing. Former logo cells and edge cells are ordinary writable cells.
- Only `boot_workspace_reset()` (used by startup) restores initial logo content.
  This helper resets one boot workspace, not a complete nine-workspace manager.
  Keyboard input and user reset commands are not yet wired.

## Normal boot versus test-only scrolling

Normal startup shows the logo and two sample lines:

```text
SPIFFOS WORKSPACE
OUTPUT SCROLLS THE LOGO.
```

It adds no delay and does not immediately force the logo away. The existing
FRAMEBUFFER_OK, LOGO_OK, TEXT_OK, CONSOLE_OK and BOOT_OK markers verify that path.

A separate `SPIFFOS_SCROLL_TEST` compile-time variant prints enough numbered
lines to force scrolling, ending with SCROLLING OK. The test build also emits
SPIFFOS_SCROLL_OK. At 1280x800, final visible lines are LINE 3 through LINE 51,
then SCROLLING OK, with no logo or header reservation anywhere on the screen.

`make test-workspace-scroll` invokes a dedicated script which builds this
variant in a fresh temporary directory. It leaves the normal kernel/ISO and
any active interactive QEMU session alone. Do not enable test flags in the live
build to run this test; use the target to avoid mixed/stale compiler variants.

## Actual verification

- Sanitizer workspace test PASS: full-grid dimensions, uniform corners,
  exact first-scroll pixel copy, complete logo removal, two further waves of
  scrolling without reappearance, usable first/last cells, unchanged padding
  and guards, rejected cursor coordinates, byte-identical restoration on reset.
- Updated logo, console and scroll host tests PASS with the unified color.
- Clean isolated `make test-workspace`: PASS. Includes all previous memory
  tests, ordinary asset packing, strict freestanding Clang/LLD build, UEFI ISO,
  QEMU initial screen, five serial markers and every 1280x800 screen pixel.
- `make test-workspace-scroll`: PASS in a second clean nested build. Forced
  variant, six serial markers and every final logo-free screen pixel verified.
- Both actual QEMU PNG captures visually inspected: initial logo blends into
  uniform navy, final text fills the whole display and the logo is gone.
- Negative screen tests PASS: reinserting the logo into a scrolled capture is
  rejected; adding a border-colored corner pixel to initial output is rejected.
- Syntax/whitespace, tested source/asset hash and original staging/draft checks
  PASS. The original branding SVG and website copy remain unchanged.

Evidence: build/workspace-initial.ppm/.png, workspace-initial-serial.log,
workspace-scrolled.ppm/.png and workspace-scrolled-serial.log. Full test logs
are preserved with the pre-correction backup.
Initial PPM SHA-256: 93768b4a4bfd4913cebbc31e9109b8c176fd81714ce795d048acd4f75d7fa761.
Scrolled PPM SHA-256: dec1b47b29bbee4988a89b203741a2ca0eaf87b0251c8f43396f87c640c5abf8.

## Local use and limitations

Close an old QEMU window before loading the new normal image:

```sh
make run                    # initial logo on uniform navy, ready for later output
make test-workspace         # reset/bounds tests and normal power-up capture
make test-workspace-scroll  # isolated forced-scroll capture, no changes to live ISO
```

No mouse wheel, keyboard, visible cursor, reset shortcut, input interpreter or
BASIC exists yet. Scrolling currently means output-driven scrolling through
console calls; the tests simulate future output beyond the page. Reset policy
is implemented and tested through the explicit helper, not a user command.
Physical hardware and ring 3 remain untested/unimplemented.

For rollback, preserve current work and compare this correction with its full
backup (which includes the preceding uncommitted logo/scroll implementation).
The last local commit remains `711168c`. Extract recovery archives into a sibling
directory, leaving the active index and unrelated user files intact.
