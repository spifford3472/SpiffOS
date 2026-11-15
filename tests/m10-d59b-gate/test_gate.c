/* Hosted-only D5.9-B gate negative regression tests. No privileged opcodes. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gate.h"
struct fake_cpu {bool enabled; unsigned reads, disables, restores; bool last_restore;};
static bool read_if(void *p) {struct fake_cpu *cpu=p; ++cpu->reads; return cpu->enabled;}
static void disable_if(void *p) {struct fake_cpu *cpu=p; ++cpu->disables; cpu->enabled=false;}
static void restore_if(void *p,bool enabled) {struct fake_cpu *cpu=p; ++cpu->restores; cpu->last_restore=enabled; cpu->enabled=enabled;}
static void run(bool initial_if) {
 struct fake_cpu cpu={.enabled=initial_if};
 struct gate_backend backend={read_if,disable_if,restore_if,&cpu};
 struct gate_state state={0};
 struct gate_context a={true,false,{3,42}};
 struct gate_context b={true,false,{3,43}};
 struct gate_context c={true,false,{4,42}};
 assert(gate_enter(&state,&a,&backend)==GATE_OK);
 assert(state.held && state.depth==1 && state.saved_if==initial_if && !cpu.enabled);
 struct gate_state snapshot=state;
 assert(gate_enter(&state,&b,&backend)!=GATE_OK && memcmp(&state,&snapshot,sizeof(state))==0);
 assert(gate_enter(&state,&c,&backend)!=GATE_OK && memcmp(&state,&snapshot,sizeof(state))==0);
 struct gate_context invalid=a; invalid.in_irq=true;
 assert(gate_enter(&state,&invalid,&backend)!=GATE_OK && memcmp(&state,&snapshot,sizeof(state))==0);
 invalid=a; invalid.trusted=false;
 assert(gate_enter(&state,&invalid,&backend)!=GATE_OK && memcmp(&state,&snapshot,sizeof(state))==0);
 assert(gate_enter(&state,&a,&backend)==GATE_OK && state.depth==2);
 assert(gate_exit(&state,&a,&backend)==GATE_OK && state.depth==1 && cpu.restores==0);
 snapshot=state;
 assert(gate_exit(&state,&b,&backend)!=GATE_OK && memcmp(&state,&snapshot,sizeof(state))==0);
 assert(gate_exit(&state,&a,&backend)==GATE_OK && !state.held && state.depth==0);
 assert(cpu.restores==1 && cpu.enabled==initial_if && cpu.last_restore==initial_if);
 assert(gate_exit(&state,&a,&backend)!=GATE_OK);
}
static void reject_corrupt_and_excess_nesting(void) {
 struct fake_cpu cpu={.enabled=true};
 struct gate_backend backend={read_if,disable_if,restore_if,&cpu};
 struct gate_state state={0};
 struct gate_context owner={true,false,{3,42}};
 struct gate_context bad_generation={true,false,{3,0}};
 assert(gate_enter(&state,&bad_generation,&backend)==GATE_INVALID);
 assert(!state.held && cpu.disables==0);
 state.held=true;state.depth=0;state.owner=owner.owner;
 assert(gate_exit(&state,&owner,&backend)==GATE_INVALID && state.depth==0);
 state.held=false;state.depth=0;
 assert(gate_enter(&state,&owner,&backend)==GATE_OK);
 for (unsigned i=1;i<8;i++) assert(gate_enter(&state,&owner,&backend)==GATE_OK);
 assert(state.depth==8);
 assert(gate_enter(&state,&owner,&backend)==GATE_OVERFLOW && state.depth==8);
 for (unsigned i=0;i<8;i++) assert(gate_exit(&state,&owner,&backend)==GATE_OK);
 assert(cpu.enabled && cpu.restores==1 && !state.held);
}
int main(void) {run(true);run(false);reject_corrupt_and_excess_nesting();puts("SPIFFOS_M10_D59B_GATE_HOSTED_OK");}
