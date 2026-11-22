# SpiffOS D5.9 multitasking preview

The isolated preview is developed against PR #94, with the contract in
[the preview ADR](../decisions/ADR-m10-d59-multitasking-preview.md).

## Execution and review

- `main.c:preview_prepare_foreground` initializes the PS/2 controller and
  editor, verifies the retained boot service, and reserves snapshot backing.
  That order matters because the boot retained-service check measures the
  free-frame count before workspace allocations.
- `main.c:keyboard_run` is the only runtime input, cursor, workspace and
  framebuffer owner. `workspace-manager.c:snapshot_transfer` uses prefaulted
  boot-owned pages during preview. The guarded M6 workspace selection is a
  short state update after rendering, and the existing error path restores
  the prior pixels. F12 uses the IRQ0 monotonic clock to avoid lost PIT wraps
  while the foreground is off-core.
- `m10-d56-guest.c:m10_d56_irq_select` still takes the real IF-off frame from
  the existing IRQ0 assembly after normal accounting and EOI. M10-A owns
  selection and task states. Eligibility checks require exact D5.2b stack
  backing and generation. The supervisor and two workers share no foreground
  display/input state. Supervisor service counts at most once per hardware
  tick and F10 copies all metrics under the existing D5.7/D5.9-B guard.
- `m10-d56-guest.c:m10_d56_guest_smoke` creates and enrolls four exact owned
  contexts. The bounded test returns through the supervisor to the surviving
  boot dispatcher and retires all four stacks through D5.2b. The fatal
  worker #UD uses the existing halt path and retains uncertain backing.
- Updated manifest entries cover only four changed C paths. The static
  inventory still does not certify production preemption or the rest of the
  kernel. The IRQ assembly, EOI, IDT and MMIO setup are the existing paths.

Qwen (Forge `qwen3-coder:30b`) drafted the ADR and supplied the boot snapshot
helper and F10 sampling structure. The CLI initially lacked explicit auth
selection. The configured CLI made progress reading sources but was stopped
in favor of smaller requests to the same local Ollama model. Its first F10
sample omitted guard entry and left counters unset. The second supplied the
guarded capture structure. The coordinator unified the checked guard-exit path,
reviewed the function types and ownership, and integrated the scheduler,
foreground, test and CI wiring.

## Build, test and launch

Use an isolated checked-out source tree with the pinned Limine checkout.
`make iso-m10-d59-preview` rebuilds that checkout's `build/spiffos.iso`.
`make HOST_CC=gcc test-m10-d59-preview-acceptance` runs the inherited full
acceptance suite and then builds disposable interactive, bounded and
fatal-fault profiles. `python3 scripts/test-m10-d59-preview.py` runs only the
preview suite. Independent CI uses the same overall target.

At boot, expect the SpiffOS workspace editor. Type a line using punctuation,
Delete and cursor keys. Switch among F1–F9 and return to check each workspace's
own contents. Press F10 twice with a pause to verify both worker counters and
supervisor service advance. F1 returns to the editor. F12 briefly shows the
workspace number and restores the previous pixels.

From a candidate-only checkout containing the preview ISO, launch
`python3 scripts/qemu.py run --memory-mib 256`. In the GTK window,
Ctrl+Alt+G releases captured input. Close the window to end the preview.
The launcher gives the guest private OVMF variables and no network or
physical disk. The preview never replaces the accepted D4 image.

For normal D4 behavior, close the preview and run
`python3 /home/geoff/projects/spiffos-qemu-main/scripts/qemu.py run`.
The accepted D4 checksum is
`0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6`.

## Observed development evidence

The initial passing 128 MiB interactive regression covered exact
framebuffer restoration, full editor and key coverage, and a 1.048 second F12
display. The next 128 MiB run measured both workers, foreground and service
progress in two F10 samples, with a maximum unserviced gap of four IRQ ticks.
The unflagged debug-stripped executable during integration matched D4
`82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb`.
Candidate acceptance requires the complete matrix and independent CI.

The final Forge preview matrix passed at 128, 256 and 512 MiB. It verified
keyboard events, workspace restoration, two advancing F10 snapshots, F12
timing, launcher close and signal cleanup, four exact off-stack retirements,
and fail-closed worker #UD at each guest memory size. Forge also passed the
full inherited GCC sanitizer and QEMU suite in 635.42 seconds. Native Clang
editor tests passed without sanitizers because that Forge Clang installation
lacks the sanitizer runtime. The freestanding Clang default build matched D4.

Independent run #112 passed inherited acceptance and all three interactive
preview runs. Its launcher test stopped because the disposable runner had
QEMU's core package without the separate GTK module. The CI dependency now
includes `qemu-system-gui` so the same launch validation can run in CI.
