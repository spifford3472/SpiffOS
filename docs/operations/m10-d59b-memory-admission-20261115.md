# D5.9-B runtime memory entry and retirement integration

Date: 2026-11-15. Isolated child of PR #93 at
928e080df68bc946a91a32069a13af7f0c2a3931. Parent exact-head CI #110,
ID43691784073, completed SUCCESS; every acceptance job step was verified.
No merge, deployment, ordinary kernel preemption or M11 preview claim.

## Authority and implementation

Read AGENTS.md, accepted/open registers, system overview, developer guide,
ADR-m10-d59b-runtime-memory-wiring.md, authentic-context/service ADRs, and the
M2/M3/M4/M6, M10-A and D5.2b source/contracts. Docs-first phase originally
621a31c was rebased without source changes as 6375902 onto the exact published
parent. The exact docs-first tree is published as GitHub
add018713bb0f1e851c73b435094ebb6775f2580. No architecture exception, parallel
lock, allocator or scheduler added.

The implementation is behind SPIFFOS_M10_D59B_MEMORY_WIRING_TEST, requiring the
already-proved context/service/page diagnostics. It is absent from the default
kernel. Existing allocator bodies remain authoritative. Translation-unit-local
macro renames let public entries authenticate execution before calling those
same bodies; internal calls stay within their admitted outer transaction.
The page claim uses a function-like rename to preserve its separate struct tag.

| Boundary | Behavior in the flagged runtime |
| --- | --- |
| 5 M2 entries | Actual CPU/IF/IRQ/task/stack admission; existing D5.7 cookies; tracking capped at 131072 frames |
| 6 M4 entries | Same heap implementation; allocation/release/check admitted; initialize and bulk lifetime cleanup boot-only |
| 9 M6 entries | Same service implementation; ordinary operations admitted; initialize/reset/retire boot-only |
| 4 M3 entries | Resolve admitted; generic callbacks boot-only; uninitialized claim requires a caller-owned outer transaction |
| 17 memory-boot entries | Legacy reports, callbacks, snapshots, MMIO, stack setup/release and trusted policy provider reject runtime before mutation/output/callback |

One-time bootstrap runs after real CLI before any allocator use. Bind happens
from the surviving boot stack to the exact registered reserved supervisor,
while IRQ handoff is inactive. The selector independently refuses handoff
without binding. Runtime scopes reuse actual CPU identity, architectural IF,
assembly IRQ depth, executing RSP, task generation and the SAME D5.7 guard.
Unbind requires no RUNNING task, off every installed stack and zero guard depth.
The existing ledger stays borrowed until all bindings are retired and all live
descriptors reclaimed. Rebinding or fault reset is forbidden.

Boot/retirement scopes are internal structured irq-save scopes on one verified
CPU with no independent scheduled task; they do not fabricate a task identity.
Boot-only API admission releases its short check scope before a legacy long
snapshot/report body. IRQs cannot dispatch an independent task while unbound,
and tagged IRQs cannot enter allocators. No new mask spans a multi-page snapshot.

## IF and ownership safety

The EXISTING d59b_context.original_if is the sole runtime IF restoration
authority. The domain entry records its actual pre-CLI sample there at the
outermost guard. A caller token cannot enable or suppress IF by modifying its
saved flag: final exit cross-checks it against the trusted adapter. The token
is marked inactive BEFORE d59b_exit may enable IF, and runtime success returns
immediately afterward without reading shared guard/context state that another
task could legitimately change. Uncertain public-wrapper exit is nonreturning
and IF-off; it cannot falsely return an error after output was published.

A native corruption test first reproduced the token-restoration defect (exit
-6 from its assertion); the corrected implementation passes both forged-enable
and forged-hide cases. That rejected behavior was never deployed.

Physical stack checks use existing D5.2b bindings and directly obtained validated
HHDM, not another registry. Runtime cannot release any installed stack frame.
Off-stack retirement requires exact TERMINATED generation, no references and no
quarantine. A FREE allocator slot still covered by a retained binding is skipped.
On partial backing-release failure, failed and not-yet-released physical page
identities remain recorded; poisoning/quarantine forbids automatic retries.

Generic caller-supplied zero callbacks and root reset/retirement are refused
before invocation or mutation. Claims stay private through the existing fixed
direct kernel zero leaf. Ordinary returned heap clients retain their documented
borrow/release lifetimes: this does not add automatic client reclamation on task
termination, Ring 3 allocation authority or a public process memory ABI.

## Actual verification

- GCC strict C11 ASan/UBSan: 20,000 nested IF-on/off scope cycles, mixed direct
  adapter/wrapper nesting, exact bind/unbind and complete D5.2b retirement PASS.
  Eleven fresh-process sticky negatives PASS: boot IF, bind from registered
  stack, IRQ/CPU/RSP mismatch, early unbind, bad cookie, nested IF, retirement on
  retained stack, forged-enable token and forged-hide token.
- Actual M2/M3/M4/M6 plus actual memory-boot translation unit: 20,000 direct
  frame/service cycles PASS. The test sets up the real private boot descriptor,
  roots and stack records without adding a kernel injection API. Unsupported
  capacity, output aliases, callback/lifecycle/snapshot/MMIO/stack refusals,
  private claim gating and original IF preservation PASS.
- Link-only exact release failure: one stack page releases, the next refuses;
  remaining physical identities stay recorded, the D5.2b owner quarantines,
  a second release does not retry, and allocation skips the freed page still
  covered by that quarantined binding. PASS. A void runtime retire stops via
  expected native SIGABRT; child-process core dumps are disabled locally.
- Native Clang passes the same positive/direct/partial-release and scope
  negative cases. Strict freestanding Clang passes the seven memory modules.
  Clang sanitizer runtime is not installed; GCC supplies sanitizer evidence.
- Real QEMU 128/256/512MiB with actual assembly IRQ0, CPU and owned-stack samples:
  frame/service/page work, direct read-only/refused callers outside an existing
  guard, fixed full-page zeroing, IF restoration, bind and off-stack retirement
  all PASS. Both workers progress, 220 authentic IRQ entries per boot, maximum
  supervisor service gap 3 ticks. Actual IRQ-context negative rejects allocator
  entry and prevents false editor/success markers at every memory size.
- New direct probes acquire resources only inside their existing bounded outer
  transaction; read-only/refused probes carry NO resource across an IF-on gap.
  A final refusal probe may be suspended before its progress increment; the
  harness accepts at most one such difference from the completed frame count.
- Fresh default stripped ELF PASS:
  82628f2e640bb4a56f8c5f18f75c2e1da43ec5e3fdaee2caad921ef03d48f2fb.
  Installed accepted ISO unchanged:
  0f2dbca116bd991cd44ad7b477042a8bf13caae67711819290b30b8b5bdb9cf6.
- Final full inherited Forge acceptance PASS: exit 0 after 627.34 seconds,
  terminal SPIFFOS_M10_D59B_HOSTED_ACCEPTANCE_OK. Exact-head child CI remains
  required after publication; parent CI is not child acceptance.

Commands: make HOST_CC=gcc test-m10-d59b-memory-scope-host
and test-m10-d59b-memory-direct-host; python3
scripts/test-m10-d59b-memory-admission-guest.py; full highest target
make HOST_CC=gcc test-m10-d59b-acceptance with the existing per-process Limine
safe.directory. Native runner and guest target are dependencies of that highest
target. No persistent Git/host configuration change was made.

Logs: /tmp/spiffos-d59b-memory-host-final.log,
/tmp/spiffos-d59b-memory-guest-final.log,
/tmp/spiffos-d59b-memory-clang-final.log,
/tmp/spiffos-d59b-memory-full-acceptance.log and memory-full-exit.txt.
Observations are single-CPU QEMU evidence, not exhaustive physical deadlines,
SMP certification or general production kernel-preemption permission.

## Source review and model provenance

Reviewed exactly nine changed source files: frame-allocate.c,
frame-allocator.c, kernel-heap.c, memory-service.c, page-access.c, memory-boot.c,
m10-d59b-context.c, m10-d56-guest.c and main.c. No private implementation name
has a caller outside its authoritative translation unit. frame_owner_valid and
M1 arithmetic remain pure. The source inventory still tracks 66 C/ASM files,
31 UNREVIEWED entries, with every NO_PREEMPTION status preserved.

Local Qwen authored the off-stack predicate, scope entry/exit and thin-wrapper
template. The coordinator instantiated the reviewed wrapper mechanically at
24 existing entries, wired lifecycle/policy/boot gates and wrote independent
native/hardware tests. The off-stack overflow check was clarified. The initial
IF repair draft retained an unsafe second restore path and was rejected; a
smaller Qwen block supplied the correct inactive-before-exit/immediate-return
sequence. Coordinator connected actual captured IF to the existing trusted
adapter. No new Codex fallback or blanket allocator rewrite was used.
Qwen prompts/results remain /tmp/spiffos-d59b-qwen-{offstack,memory-enter,
memory-leave,frame-wrapper,trusted-if-leave,if-block}.*.

## Remaining release scope and rollback

This closes the isolated memory-entry/retirement integration only after its
final acceptance/CI gates pass. Keyboard/display/driver caller models and the
remaining D5.9-C/D/E checks precede any normal-preemption release. M11 integrated
preview, BASIC/editor application isolation and later VFS/graphics/audio remain
unfinished. Do not represent this diagnostic branch as a runnable M11 preview.

Discard the isolated child to roll back to PR #93. Accepted D4 remains untouched.
No merge/deploy, physical media, host privilege/package, network/OPNsense,
infrastructure or other-project changes. No owner interaction was needed for
this offline integration; merge/deployment authorization is still separate.
