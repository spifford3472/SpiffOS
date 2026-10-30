#!/usr/bin/env python3
"""Persistent, bounded SpiffOS memory work; feature commits only, no deployment."""
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import time
from datetime import datetime, timezone

ROOT = Path('/home/geoff/projects/spiffos-memory-night-20261029')
LOG = Path(__file__).resolve().parent
QWEN = '/home/geoff/.local/share/ki5vnb-coding/qwen-code/node_modules/.bin/qwen'
CODEX = '/home/geoff/.local/npm-global/bin/codex'
BRANCH = os.environ.get('SPIFFOS_WORK_BRANCH', 'feat/memory-services-night')
HARD_STOP = datetime(2026, 10, 30, 7, tzinfo=timezone.utc).timestamp()
START_STEP = int(os.environ.get('SPIFFOS_RESUME_STEP', '0'))
START = time.time()
END = min(START + 3 * 3600, HARD_STOP - 300)
ENV = {k:v for k,v in os.environ.items() if k in ('PATH','HOME','USER','LOGNAME','LANG','TERM','XDG_RUNTIME_DIR','DBUS_SESSION_BUS_ADDRESS')}
ENV['PYTHONDONTWRITEBYTECODE'] = '1'
STATE = {'started_utc':datetime.now(timezone.utc).isoformat(), 'budget_seconds':int(END-START), 'branch':BRANCH, 'status':'starting', 'resume_from_step':START_STEP, 'steps':[]}

# Ordered dependency slices: Qwen sees one file/function; Codex coordinates the
# specific checkpoint after reviewing the local attempt, never the entire OS.
TASKS = [
 ('page-contract', 'docs/decisions/ADR-memory-services.md', 'Clarify only the M3 checked page-access API and failure/ownership/zeroing contracts in this ADR after reading pinned Limine header and current allocator. Retain approved scope and history.', 'Finalize documented M3 design and a small page-access header. Read the pinned Limine declarations and local boot references; prove supported direct-map assumptions with the existing pinned boot pair. Do not implement a new page-table manager.'),
 ('page-policy', 'src/kernel/page-access.c', 'Implement only one pure checked address/range translation function for the documented page-access contract. Use a header already present; if absent, report NEEDS_CODEX without inventing an API.', 'Implement the pure checked page-access policy and focused sanitizer tests. Verify ownership, full-frame membership, alignment, arithmetic and supported canonical addressing. Keep physical access behind an injectable backend; numeric addresses alone cannot authorize writes.'),
 ('page-boot', 'src/kernel/memory-boot.c', 'Add only a small helper that verifies a complete page is zero, if the M3 adapter/header exists. Otherwise report NEEDS_CODEX.', 'Integrate the M3 validated boot direct-map adapter and real zero/write/release/reallocate/zero probe. Roll back failed allocations. Add SPIFFOS_PAGE_ACCESS_OK only after actual checks; preserve unsupported boot paths and existing editor pixels. Make test-memory exercise new host tests and verify the marker at all RAM sizes.'),
 ('page-errors', 'tests/page-access-test.c', 'Add one focused test group for page-access output/state preservation on rejected arithmetic/ranges using the existing API. No other files.', 'Review/fix M3 failure paths and complete adversarial host coverage including ownership rejection, backend failures, rollback, null output, boundaries and guard preservation. Run relevant tests and document actual results before expanding to heap work.'),
 ('heap-contract', 'docs/decisions/ADR-memory-services.md', 'Document only bounded heap alignment, size and metadata policy in the existing ADR; retain ownership/reserve requirements.', 'Define a simple bounded kernel heap header and detailed design: alignment, limits, split/coalesce, explicit failures, owner lifetimes and metadata safety. Prefer caller-owned external metadata so invalid pointers never cause unchecked metadata dereferences. No final user ABI or VM design.'),
 ('heap-allocate', 'src/kernel/kernel-heap.c', 'Implement only the heap allocation function against the existing documented header. If prerequisite API is absent, report NEEDS_CODEX.', 'Implement bounded heap initialization and allocation over validated owned backing pages with checked arithmetic, alignment and split handling. Zero before publish and preserve state/output on failed allocation. Add meaningful host tests and make test-memory dependencies; do not consume reserve.'),
 ('heap-release', 'src/kernel/kernel-heap.c', 'Implement only heap release against the existing documented header. Validate before mutation; no other files.', 'Implement checked heap free and adjacent block coalescing. Reject invalid, interior, double and wrong-owner frees. Preserve accounting and reserve; add fragmentation/reuse tests with guarded RAM under sanitizers.'),
 ('heap-guest', 'src/kernel/memory-boot.c', 'Implement one small helper for the existing heap boot diagnostic; if unavailable, report NEEDS_CODEX.', 'Integrate actual kernel heap smoke probe using validated page access in QEMU. Prove write/read, alignment, OOM recovery, free/reuse and ownership separation. Emit SPIFFOS_HEAP_OK only on success, validate at all three guest sizes and retain exact editor captures.'),
 ('owner-cleanup', 'src/kernel/kernel-heap.c', 'Implement only a documented heap release-owner function if its API exists. It must not free other owners; otherwise report NEEDS_CODEX.', 'Implement internal owner-scoped workspace cleanup and separate session lifetime handling. Prove workspace1 reset cannot free workspace2/kernel/session/reserve, using host tests and a real guest probe. Add SPIFFOS_OWNER_CLEANUP_OK after checks. No real workspace UI or process-manager claims.'),
 ('pressure-tests', 'tests/kernel-heap-test.c', 'Add one deterministic heap fragmentation/cleanup test group using the existing public internal API. No random time-dependent behavior.', 'Exercise realistic mixed owners and adversarial deterministic allocation/free sequences against an independent accounting model. Check OOM atomicity, full coalescence, repeated workspace cleanup, guards, overflow and zero-on-reuse. Fix concrete failures without broadening API.'),
 ('review-invariants', 'docs/operations/memory-services-night-20261029.md', 'Record one accurate limitations/provenance section from actual logs and diffs; do not claim tests you did not run.', 'Perform a fresh correctness review of the complete memory diff: mapping authorization, physical range bounds, metadata lifetime, owner accounting, reserve isolation, rollback, invalid frees, zero-before-publish and unsupported boot paths. Fix actionable findings and run affected tests. Record evidence and unresolved limitations; do not merely repeat tests without a reason.'),
 ('package', 'docs/DEVELOPERS_GUIDE.md', 'Add a short teaching section explaining the implemented physical-page to heap ownership path, only when supported by actual code.', 'Finish integration and documentation. Run full host sanitizer, three-size memory QEMU, keyboard/workspace/scroll and safe-launcher suites sequentially. Record commands/exit results, modules/model provenance, unimplemented limits, rollback and exact source identity. Ensure tests exercise M3/M4/M5 actual behavior, not just markers. Prepare reviewable feature commits; no merge/deployment.'),
]

def status(message):
    """Publish an atomic status snapshot and append timestamped progress."""
    STATE['status'] = message
    STATE['updated_utc'] = datetime.now(timezone.utc).isoformat()
    temp = LOG / 'status.json.next'
    temp.write_text(json.dumps(STATE, indent=2)+'\n')
    temp.replace(LOG / 'status.json')
    with (LOG/'progress.log').open('a') as f:
        f.write(STATE['updated_utc']+' '+message+'\n')
    print(message, flush=True)

def invoke(argv, name, seconds, env=ENV):
    """Run one child group, retaining logs; timeout terminates only that group."""
    remaining = END - time.time()
    if remaining < 30:
        return 124
    with (LOG/(name+'.log')).open('w') as output:
        proc = subprocess.Popen(argv, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            return proc.wait(timeout=min(seconds, remaining))
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGTERM)
            try: proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
            return 124

def git(*args):
    """Run a repository-only Git command; never change main or other checkouts."""
    return subprocess.check_output(['git',*args], cwd=ROOT, text=True).strip()

def allowed(path):
    """Limit automatic feature commits to the authorized memory code and docs."""
    return (path in ('Makefile', 'scripts/qemu.py') or path.startswith(('src/kernel/','tests/','docs/')) or
            (path.startswith('scripts/test-memory') and path.endswith('.py')))

def checkpoint(name):
    """Commit scoped progress as WIP, never interpret a commit as passing tests."""
    paths = git('diff','--name-only','HEAD').splitlines()
    paths += git('ls-files','--others','--exclude-standard').splitlines()
    paths = sorted(set(p for p in paths if not p.startswith('build/')))
    if any(not allowed(p) for p in paths):
        raise RuntimeError('Out-of-scope changes require review: '+repr(paths))
    if not paths: return
    subprocess.run(['git','diff','--check'],cwd=ROOT,check=True)
    subprocess.run(['git','add','--',*paths],cwd=ROOT,check=True)
    subprocess.run(['git','commit','-m','WIP memory services: '+name],cwd=ROOT,check=True)
    result = invoke(['git','push','origin',BRANCH],name+'-push',90)
    STATE['last_commit'] = git('rev-parse','HEAD')
    STATE['last_push_exit'] = result

COMMON = '''Read AGENTS.md and docs/decisions/ADR-memory-services.md first. Work only in this isolated SpiffOS checkout. Do not read secrets, alter infrastructure/services/firmware/disks, change boot dependency pins, kill the owner QEMU, install packages, merge, push, deploy or spawn agents. No changes to AGENTS.md or runner controls. Preserve M1/M2 and editor behavior. Explain source file/function contracts as AGENTS requires. Report actual tests and blockers honestly. Never weaken existing tests to claim success.\n'''

def main():
    """Process the dependency queue within three hours and publish exact readiness."""
    with open('/home/geoff/.local/share/ki5vnb-coding/worker.lock','a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        assert git('branch','--show-current') == BRANCH
        status('running: memory services queue')
        for index,(name,file,small,goal) in enumerate(TASKS):
            if index < START_STEP: continue
            if END-time.time() < 150: break
            status('step '+str(index+1)+'/'+str(len(TASKS))+': '+name+' Qwen')
            qenv=dict(ENV,QWEN_HOME='/home/geoff/.local/share/ki5vnb-coding/qwen-home',OLLAMA_API_KEY='ollama')
            qp=COMMON+'Focused task; edit only '+file+'. Read its existing header and directly named API references. One function/file only.\n'+small
            qrc=invoke([QWEN,'--auth-type','openai','--model','qwen3-coder:30b','--openai-base-url','http://127.0.0.1:11434/v1','--openai-api-key','ollama','--approval-mode','auto-edit','--allowed-tools','run_shell_command','--max-session-turns','8','--max-tool-calls','12','--max-wall-time','180s','--output-format','text','-p',qp],name+'-qwen',200,qenv)
            status('step '+str(index+1)+': '+name+' Codex verification/fallback; Qwen exit='+str(qrc))
            prompt=COMMON+'You are coordinating this one checkpoint. Read accepted/open, system-overview, ADR-011, ADR-memory-foundation and ADR-frame-allocator, then the current diff and previous operations notes. The owner explicitly authorized direct Codex fallback for memory after Qwen timeouts. The small Qwen attempt for this slice exited '+str(qrc)+'. Its log is not available inside your sandbox; verify actual source instead of assuming success.\nCheckpoint: '+goal+'\nDo not hand the full project to another model. No further delegation. Keep this slice reviewable. Add truthful progress to docs/operations/memory-services-night-20261029.md, including your work, the Qwen result, tests and limitations. If prerequisites are absent, repair only the smallest necessary prerequisite. Time budget is bounded; leave unfinished work clearly identified. No Git commits; the outer runner records feature commits.'
            crc=invoke([CODEX,'exec','--sandbox','workspace-write','-c','approval_policy="never"','-c','sandbox_workspace_write.network_access=false','--color','never','-C',str(ROOT),'-o',str(ROOT/'build/night'/ (name+'-final.md')),prompt],name+'-codex',1100)
            STATE['steps'].append({'step':name,'qwen_exit':qrc,'codex_exit':crc})
            checkpoint(name)
            status('checkpoint '+name+' recorded; Codex exit='+str(crc))
            if crc != 0:
                status('blocked: Codex worker exit '+str(crc)+' at '+name)
                return
        status('final validation')
        gates=invoke(['make','test-memory','test-keyboard','test-workspace','test-workspace-scroll'],'final-gates',360)
        launcher=invoke(['python3','scripts/test-safe-launcher.py'],'final-launcher',180) if gates==0 else None
        markers=True
        for size in (128,256,512):
            p=ROOT/'build'/('memory-'+str(size)+'-serial.log')
            serial=p.read_text(errors='replace') if p.exists() else ''
            markers &= all(m in serial for m in ('SPIFFOS_PAGE_ACCESS_OK','SPIFFOS_HEAP_OK','SPIFFOS_OWNER_CLEANUP_OK'))
        STATE.update(gates_exit=gates,launcher_exit=launcher,all_milestone_markers=markers)
        record=ROOT/'docs/operations/memory-services-night-20261029.md'
        with record.open('a') as f:
            f.write('\n## Outer runner final validation\nSource before this record: '+git('rev-parse','HEAD')+'\n\nFull make gate exit: '+str(gates)+'; safe launcher exit: '+str(launcher)+';\nall M3/M4/M5 markers present at each RAM size: '+str(markers)+'.\nThese results do not substitute for review of test sufficiency. No merge or deployment.\n')
        checkpoint('validation record')
        status('ready for review' if gates==0 and launcher==0 and markers else 'partial or blocked: inspect evidence before merge')

if __name__ == '__main__':
    try: main()
    except Exception as error:
        status('blocked: '+type(error).__name__+': '+str(error))
        raise
