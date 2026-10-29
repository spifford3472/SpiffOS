# First QEMU boot and workspace recovery — 2026-10-28

## Scope and authorization

Owner-authorized direct ChatGPT-guided work on `feat/first-qemu-boot`.
No Qwen or separate Codex coding task was invoked. The old Qwen-first policy
is retained; this session's scoped exception is recorded in the accepted register
and ADR-011. Read AGENTS.md, accepted/open registers, system overview, ADR-011,
and ADR-003 before implementation. No commits, pushes, merges, physical-device
writes, USB changes or network infrastructure changes were performed.

## Recovery evidence

Initial HEAD: `49eb083abb4613584e6c1ec7b2bf11654702e153`.
Initial status had `AD` for Makefile, src/kernel/main.c and src/kernel/start.S:
the files were in the index but missing from the working tree.

Verified snapshot: `/home/geoff/projects/spiffos-recovery-20261028T214335Z/`.
It contains checkout.tar.gz (full checkout including .git and untracked files),
SHA-256 checksum, manifest.json, index-entries.txt, staged.patch, unstaged.patch,
status.txt and VERIFIED.txt. All 207 archived file/link records and permission
bits matched the source; the checkout was stable during capture. Three missing
files were restored byte-for-byte using their staged blobs without updating the
index. The original staged entries and staged binary patch still match the
snapshot exactly. Raw index metadata later changed; staged content did not.

The Codex process PID 491936 and parent 491924 were inspected read-only. Both
had this checkout as cwd; neither was terminated or given work. Its presence
means concurrent external edits remain possible; inspect status before future work.

Existing untracked drafts, test_simple.S, limine.cfg, dependency checkouts and
logs were preserved. The old limine.cfg is historical and is not packaged;
Limine v10 uses the new limine.conf. The initial build directory was first
preserved in the full snapshot; the first passing build directory was later moved
to `.local-dev/first-boot-evidence/build-first-pass` to permit a fresh build.

## Build and protocol changes

- Vendored unchanged official `include/limine.h`, license and provenance.
  Protocol commit: `3a0526b700e356f0eac1b71a77697b3fd1c707a3`.
  Header SHA-256: `0aadf2633c85d8cb145344c968add158aeb1deb68af567d55ba8431c492b6d4c`.
- Reference template commit: `40dcbdf9602a0255b1fd20e148c6343476df17c3`.
- Binary commit: `7a9013f305de0bea1f6310e76e7baba30499fef0` (v10.8.5).
  ISO packager checks this commit and verifies both binary files against Git blobs.
  It uses the existing `.local-dev/limine` checkout; it does not fetch or reset it.
- Request base revision 5. Initial revision 6 failed: GDB read `{magic, 5, 6}`,
  demonstrating fallback to 5 and an unsupported request. After correction GDB
  read `{magic, 5, 0}`, confirming acceptance. Revision 5 is deprecated in the
  newer pinned specification; it is explicitly used for compatibility with this
  pinned binary. Future bootloader upgrades must revalidate the pair.
- Linker entry is C `kmain`, using Limine's stack and higher-half mappings.
  Writable request markers are retained with KEEP. Code is RX, constants R.
  Recovered start.S is unchanged and excluded from compilation; its fixed
  stack and mixed assembler syntax are unsuitable for this boot path.
- COM1 uses x86 port I/O, divisor 1 (115200 baud), 8N1 and bounded polling.
  The marker is printed only after the requested base revision is accepted.
  The CPU then halts with interrupts disabled. No host C library is linked.
- scripts/build-image.py packages a UEFI-only El Torito ISO with BOOTX64.EFI,
  limine-uefi-cd.bin, limine.conf and the kernel. No BIOS installer is run.
- scripts/qemu.py uses Q35, TCG, 256 MiB, no networking, ISO boot (no -kernel),
  read-only OVMF_CODE_4M.fd and a private writable copy of OVMF_VARS_4M.fd.
  Test mode has a 20-second deadline and checks the exact full serial line;
  it terminates/reaps its own QEMU child and removes the private variables copy.
  A file lock prevents competing runner instances. Debug listens only on localhost.

## Local workflow and VS Code

Open `/home/geoff/projects/spiffos/SpiffOS.code-workspace` with VS Code.
The `code --reuse-window SpiffOS.code-workspace` request succeeded on Forge.
The installed ms-vscode.cpptools extension provides C browsing and GDB support.
Existing settings and prerequisite tasks were retained. C settings target
freestanding x86-64 Clang and the vendored include directory.

From the repository root:

```sh
make all      # strict kernel ELF build
make iso      # package UEFI ISO
make test     # build ISO, boot QEMU, require exact serial marker
make run      # interactive QEMU window; serial is build/run-serial.log
make debug    # headless QEMU paused at reset, GDB 127.0.0.1:1234
```

VS Code: Ctrl+Shift+B builds the kernel. Terminal > Run Task exposes ISO, test,
run and debug targets. For debugging, start the "SpiffOS: Start QEMU for GDB"
task, choose "SpiffOS: Attach to paused QEMU", press F5, then Continue to reach
the hardware breakpoint at kmain. Stop the runner task afterward to close QEMU.
The GDB remote connection and hardware breakpoint were tested from the CLI;
the VS Code graphical debugger controls and interactive QEMU display were not
exercised. JSON configuration was parsed successfully.

Build and local dependency outputs are ignored via /build/ and /.local-dev/.
Editor configuration, source, scripts and dependency license/provenance remain
reviewable. No package installation or automatic dependency download is needed
on this Forge installation; another checkout must provision the pinned binary
checkout and required tools before running make iso.

## Actual validation

- Clang 18.1.3, LLD 18.1.3, QEMU 8.2.2, GDB 15.1, xorriso 1.5.6.
- `make all`: PASS with -Wall -Wextra -Werror and freestanding x86-64 flags.
- `readelf -h -l -S build/spiffos-kernel.elf`: ELF64 x86-64 EXEC,
  entry 0xffffffff80001000, retained .limine_requests, RW/RX/R load segments.
- `nm -u build/spiffos-kernel.elf`: no undefined symbols.
- Initial `make test` with requested revision 6: FAIL as expected after the
  observed mismatch; script returned 1, make returned 2. Captured unsupported
  revision error, no success marker. Preserved under
  `.local-dev/first-boot-evidence/revision-6-failure/`.
- Corrected `make test`: PASS. The literal serial line was `SPIFFOS_BOOT_OK`.
- Moved existing build directory aside, then ran `make test` from an absent
  build directory: PASS again. This recompiles, links, packages and boots.
  Full output: `.local-dev/first-boot-evidence/fresh-build-test.log`.
- System OVMF variables SHA-256 unchanged across the fresh boot:
  `5d2ac383371b408398accee7ec27c8c09ea5b74a0de0ceea6513388b15be5d1e`.
- ISO El Torito report confirms UEFI; extracted packaged kernel equals the
  built ELF byte-for-byte. Report: `build/iso-inspection.log`.
- GDB connected at firmware reset and hit hardware breakpoint at kmain, with
  accepted revision 5. Evidence: `build/gdb-entry.log`.
- Python scripts parsed, VS Code JSON parsed, unstaged `git diff --check` passed.
  The original staged content is intentionally preserved; new work is unstaged.

Final evidence artifacts (ISO timestamps can change on a later rebuild):

| Artifact | SHA-256 |
| --- | --- |
| build/spiffos-kernel.elf | 0e1f585671cd68df6b940001c43811c891cfcaef30a5a69c22e03813dc62764d |
| build/spiffos.iso | e73d285a2065f19a5da6babb81f5da40190b3343594db5db22b105afebb83fa7 |

## Limitations and rollback

This proves native C entry, protocol acceptance and serial output in QEMU UEFI.
BASIC READY, ring 3, kernel memory ownership/allocators, interrupt handlers,
scheduling, graphics, storage, A/B recovery, Secure Boot and physical hardware
are not implemented or validated. No claim of those features follows from this
marker. No automatic clean target is supplied; preserve build artifacts before
removing them manually.

For rollback, verify the archive checksum and extract the snapshot into a new
sibling directory, not over this checkout. Compare staged.patch, unstaged.patch
and the manifest before selectively restoring files. The archive's .git/index
preserves the original staging state, and its missing working-tree boot files
reflect the original state faithfully. Use the staged blobs if restoring those
files again. Do not use git reset --hard, git clean or overwrite the live .git
while user/editor/agent changes may be in progress.

Change history: read-only inspection; verified full snapshot; three staged-file
recoveries; VS Code workspace and ignores; scoped documentation; strict ELF;
UEFI ISO; observed revision mismatch and GDB diagnosis; revision-5 correction;
passing serial test; fresh-build repeat; final GDB and ISO inspection.
