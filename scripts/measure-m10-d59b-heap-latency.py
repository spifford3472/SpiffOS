#!/usr/bin/env python3
"""Observational unchanged-heap workload benchmark, never a release gate.

Creates and instruments ONLY a disposable source/ISO. No accepted image,
physical disk or production source is modified. The largest payload and 1023
active records are measured through the actual D5.7/context/heap code. Reports
nominal guest PIT/TSC estimates, not exhaustive bounds or physical guarantees.
References: ADR-m10-d59b-runtime-memory-wiring.md and AGENTS.md.
"""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
out=root/'build/d59b-heap-latency'
out.mkdir(parents=True,exist_ok=True)
work=Path(tempfile.mkdtemp(prefix='spiffos-d59b-latency-'))/'source'
shutil.copytree(root,work,symlinks=True,ignore=shutil.ignore_patterns('.git','build','__pycache__'))
prefix=r'''/* Purpose: observational QEMU maximum-size heap latency experiment.
 * Dependencies: unchanged real heap, authenticated D5.7 adapter, PIT clock.
 * Invariants: private fixture only; no production source or accepted ISO edits.
 * Outcomes are measurements, not certification or a physical CPU-time bound.
 * References: ADR-m10-d59b-service-transactions.md and AGENTS.md. */
#include "m10-d59b-context.h"
#include "kernel-heap.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum { CASES=4, TRIALS=8, SMALL_COUNT=KERNEL_HEAP_MAX_BLOCKS-2 };
static _Alignas(16) uint8_t arena[KERNEL_HEAP_MAX_ALLOCATION_BYTES+16*SMALL_COUNT+4096];
static struct kernel_heap heap;
static struct kernel_heap_block blocks[KERNEL_HEAP_MAX_BLOCKS];
static void *small[SMALL_COUNT];
static const size_t sizes[CASES]={32,4096,65536,KERNEL_HEAP_MAX_ALLOCATION_BYTES};
static uint64_t spans[CASES], clock_delta[CASES],tsc_per_ms;
static uint64_t bulk_span,bulk_clock_delta;
static bool completed;
/* LFENCE orders TSC observations; units are guest cycles, not host time. */
static uint64_t read_tsc(void)
{
    uint32_t low,high;
    __asm__ volatile("lfence; rdtsc":"=a"(low),"=d"(high)::"memory");
    return ((uint64_t)high<<32)|low;
}
'''
cal=(root/'tests/m10-d59b-latency/calibrate.c').read_text()
suffix=r'''
/* Unexpected allocator state is never repaired or force-released. */
static bool fault(struct d59b_context *context)
{ context->guard->faulted=true;return false; }
/* Exclusive real allocation/release; report an allocation's full IF-off span.
 * The private caller retains payload until its matching exact-owner release. */
bool d59b_latency_measure(struct d59b_context *context,bool (*clock_ms)(uint64_t *))
{
    if(context==NULL || !calibrate(clock_ms,&tsc_per_ms)) return false;
    uint32_t cookie=0;
    if(!d59b_enter(context,&cookie)) return false;
    if(kernel_heap_initialize(&heap,arena,sizeof arena,blocks,KERNEL_HEAP_MAX_BLOCKS)!=KERNEL_HEAP_OK)
        return fault(context);
    if(!d59b_exit(context,cookie)) return false;
    /* Real API calls build 1023 active records, leaving one free metadata
     * slot for each measured split. No handwritten allocator is substituted. */
    for(size_t i=0;i<SMALL_COUNT;++i) {
        if(!d59b_enter(context,&cookie)) return false;
        if(kernel_heap_allocate(&heap,KERNEL_HEAP_OWNER_SESSION,16,&small[i])!=KERNEL_HEAP_OK)
            return fault(context);
        uint8_t *bytes=small[i];
        for(size_t j=0;j<16;++j) bytes[j]=(uint8_t)(i%253u+1u);
        if(!d59b_exit(context,cookie)) return false;
    }
    for(size_t c=0;c<CASES;++c) for(unsigned trial=0;trial<TRIALS;++trial) {
        uint64_t before_ms=0,after_ms=0;
        if(!clock_ms(&before_ms)) return false;
        /* Reset only a diagnostic maximum, never a guard/fault/cookie.
         * No other task executes yet; preserve the historical maximum. */
        uint64_t prior_max=context->max_exclusion_tsc;
        context->max_exclusion_tsc=0;
        void *allocation=NULL;
        if(!d59b_enter(context,&cookie)) return false;
        if(kernel_heap_allocate(&heap,KERNEL_HEAP_OWNER_SESSION,sizes[c],&allocation)!=KERNEL_HEAP_OK)
            return fault(context);
        if(!d59b_exit(context,cookie)) return false;
        uint64_t span=context->max_exclusion_tsc;
        if(prior_max>context->max_exclusion_tsc) context->max_exclusion_tsc=prior_max;
        if(!clock_ms(&after_ms) || after_ms<before_ms) return false;
        if(span>spans[c]) {spans[c]=span;clock_delta[c]=after_ms-before_ms;}
        /* Checking initialized client bytes may run with IF on: this
         * fixture owns the private allocation and the IRQ selector is idle. */
        const uint8_t *bytes=allocation;
        for(size_t j=0;j<sizes[c];++j) if(bytes[j]!=0) return false;
        if(!d59b_enter(context,&cookie)) return false;
        if(kernel_heap_release(&heap,KERNEL_HEAP_OWNER_SESSION,allocation)!=KERNEL_HEAP_OK)
            return fault(context);
        if(!d59b_exit(context,cookie)) return false;
    }
    for(size_t i=0;i<SMALL_COUNT;++i) {
        const uint8_t *bytes=small[i];
        for(size_t j=0;j<16;++j) if(bytes[j]!=(uint8_t)(i%253u+1u)) return false;
    }
    /* Measure the real owner-wide cleanup separately from ordinary free. */
    uint64_t prior_max=context->max_exclusion_tsc,before_ms=0,after_ms=0;
    if(!clock_ms(&before_ms)) return false;
    context->max_exclusion_tsc=0;
    if(!d59b_enter(context,&cookie)) return false;
    if(kernel_heap_release_session(&heap)!=KERNEL_HEAP_OK) return fault(context);
    if(!d59b_exit(context,cookie)) return false;
    bulk_span=context->max_exclusion_tsc;
    if(prior_max>context->max_exclusion_tsc) context->max_exclusion_tsc=prior_max;
    if(!clock_ms(&after_ms) || after_ms<before_ms) return false;
    bulk_clock_delta=after_ms-before_ms;
    for(size_t i=0;i<SMALL_COUNT;++i) small[i]=NULL;
    if(!d59b_enter(context,&cookie)) return false;
    bool good=kernel_heap_check(&heap) && heap.live_allocations==0 && heap.free_bytes==sizeof arena;
    if(!good) return fault(context);
    if(!d59b_exit(context,cookie)) return false;
    completed=true;
    return true;
}
/* Report decimal values only after returning to surviving boot stack. */
static void number(bool (*writer)(const char *),uint64_t value)
{
    char text[21];unsigned p=20;text[p]='\0';
    do {text[--p]=(char)('0'+value%10u);value/=10u;} while(value!=0);
    (void)writer(&text[p]);
}
/* Output never runs in IRQ, critical allocation or on a retired worker. */
void d59b_latency_report(bool (*writer)(const char *))
{
    if(writer==NULL || !completed) return;
    (void)writer("D59B_LATENCY_TSC_PER_MS:");number(writer,tsc_per_ms);
    for(size_t i=0;i<CASES;++i) {
        (void)writer("\r\nD59B_LATENCY_SIZE:");number(writer,sizes[i]);
        (void)writer("\r\nD59B_LATENCY_ALLOC_TSC:");number(writer,spans[i]);
        (void)writer("\r\nD59B_LATENCY_CLOCK_DELTA_MS:");number(writer,clock_delta[i]);
    }
    (void)writer("\r\nD59B_BULK_CLEANUP_TSC:");number(writer,bulk_span);
    (void)writer("\r\nD59B_BULK_CLOCK_DELTA_MS:");number(writer,bulk_clock_delta);
    (void)writer("\r\nSPIFFOS_D59B_LATENCY_OBSERVED\r\n");
}
'''
# Fix raw-string C NUL escape; all remaining escapes are C string escapes.
suffix=suffix.replace(chr(92)*2+'r'+chr(92)*2+'n',chr(92)+'r'+chr(92)+'n')
source=prefix+cal+suffix.replace("text[p]='\\\\0'","text[p]='\\0'")
(work/'src/kernel/d59b-latency-measure.c').write_text(source)
(work/'src/kernel/d59b-latency-measure.h').write_text('#include "m10-d59b-context.h"\nbool d59b_latency_measure(struct d59b_context *,bool (*)(uint64_t *));\nvoid d59b_latency_report(bool (*)(const char *));\n')
def patch(path,old,new,count=1):
 p=work/path;s=p.read_text();assert s.count(old)==count,(path,old,s.count(old));p.write_text(s.replace(old,new))
patch('src/kernel/m10-d56-guest.c','#include <stdbool.h>','#include "d59b-latency-measure.h"\nstatic bool (*latency_clock)(uint64_t *);\n#include <stdbool.h>')
patch('src/kernel/m10-d56-guest.c','    const uint64_t entry=enter_critical();\n#ifdef SPIFFOS_M10_D57_IRQ_TEST','''    if(!d59b_latency_measure(&d59b_context,latency_clock))
        for(;;) __asm__ volatile("cli; hlt":::"memory");
    const uint64_t entry=enter_critical();
#ifdef SPIFFOS_M10_D57_IRQ_TEST''')
patch('src/kernel/m10-d56-guest.c','    if(clock_ms==NULL) {reason="CLOCK";return false;}','    if(clock_ms==NULL) {reason="CLOCK";return false;}\n    latency_clock=clock_ms;')
patch('src/kernel/m10-d56-guest.c','    if(write==NULL || !returned_to_boot || failed) return;','    if(write==NULL || !returned_to_boot || failed) return;\n    d59b_latency_report(write);')
patch('Makefile','M10D59B_SERVICE_OBJECTS := build/m10-d59b-service.o','M10D59B_SERVICE_OBJECTS := build/m10-d59b-service.o build/d59b-latency-measure.o')
with (work/'Makefile').open('a') as f:f.write('\nbuild/d59b-latency-measure.o: src/kernel/d59b-latency-measure.c\n\tmkdir -p build\n\t$(CC) $(CFLAGS) -c $< -o $@\n')
flags='-DSPIFFOS_M10_D56_IRQ_TEST -DSPIFFOS_M10_D57_IRQ_TEST -DSPIFFOS_M10_D59B_CONTEXT_TEST -DSPIFFOS_M10_D59B_SERVICE_TEST'
env=os.environ.copy();env.update({'GIT_CONFIG_COUNT':'1','GIT_CONFIG_KEY_0':'safe.directory','GIT_CONFIG_VALUE_0':str((root/'.local-dev/limine').resolve())})
with (out/'build.log').open('w') as log:subprocess.run(['make','-B',f'BOOT_TEST_FLAGS={flags}','iso'],cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=150)
results=[]
for mib in (128,256,512):
 run=subprocess.run([sys.executable,'scripts/qemu.py','test','--memory-mib',str(mib),'--wait-marker','SPIFFOS_KEYBOARD_READY','--timeout','40'],cwd=work,capture_output=True,text=True,timeout=48)
 serial=(work/'build/serial.log').read_text();(out/f'serial-{mib}.log').write_text(serial)
 if run.returncode:raise RuntimeError((run.stdout+run.stderr)[-1800:]+serial[-1800:])
 assert 'SPIFFOS_D59B_LATENCY_OBSERVED' in serial and 'SPIFFOS_M10_D56_IRQ_OK' in serial
 rate=int(re.search(r'D59B_LATENCY_TSC_PER_MS:([0-9]+)',serial).group(1))
 samples=[{'bytes':int(a),'cycles':int(b),'estimated_us':int(b)*1000//rate,'clock_delta_ms':int(c)} for a,b,c in re.findall(r'D59B_LATENCY_SIZE:([0-9]+)\s+D59B_LATENCY_ALLOC_TSC:([0-9]+)\s+D59B_LATENCY_CLOCK_DELTA_MS:([0-9]+)',serial)]
 assert len(samples)==4
 bulk=int(re.search(r'D59B_BULK_CLEANUP_TSC:([0-9]+)',serial).group(1));delta=int(re.search(r'D59B_BULK_CLOCK_DELTA_MS:([0-9]+)',serial).group(1));result={'mib':mib,'tsc_per_ms':rate,'samples':samples,'bulk_cleanup_us':bulk*1000//rate,'bulk_clock_delta_ms':delta};results.append(result);print(json.dumps(result),flush=True)
(out/'results.json').write_text(json.dumps({'root':str(root),'temporary_source':str(work),'heap_source_sha256':hashlib.sha256((root/'src/kernel/kernel-heap.c').read_bytes()).hexdigest(),'results':results},indent=2))
print('OBSERVATIONAL_HEAP_LATENCY_COMPLETE',str(work),flush=True)
