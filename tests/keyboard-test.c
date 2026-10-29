/* Purpose: host-only scan decoder and controller transaction regression tests.
 * Dependencies: assert, fake port callbacks; no privileged instructions.
 * Invariants: deterministic single caller, no actual devices.
 * See docs/decisions/ADR-qemu-keyboard-input.md. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "keyboard.h"
#include "keyboard-ps2.h"
struct fake { uint8_t queue[64], status, config, write_config; unsigned int head,tail,writes,resends; int stuck, silent,bad_ack; };
/* Emulate controller status/data reads; fake is borrowed, no real I/O. */
static uint8_t read_fake(void *context, uint16_t port)
{
    struct fake *f=context;
    if (port==0x64) return f->stuck ? 2 : (uint8_t)(f->status | (f->head<f->tail ? 1 : 0));
    assert(port==0x60 && f->head<f->tail);
    return f->queue[f->head++];
}
/* Queue one fake reply, checking fixed-array bounds; no ownership transfer. */
static void queue(struct fake *f,uint8_t byte)
{
    assert(f->tail<sizeof f->queue);
    f->queue[f->tail++]=byte;
}
/* Model config read/write and keyboard ACKs; assert no reset/output-port writes. */
static void write_fake(void *context,uint16_t port,uint8_t byte)
{
    struct fake *f=context;
    ++f->writes;
    if (port==0x64) {
        assert(byte==0xad || byte==0xa7 || byte==0xae || byte==0x20 || byte==0x60);
        if (byte==0x20 && !f->silent) queue(f,f->config);
        if (byte==0x60) f->write_config=1;
    } else {
        assert(port==0x60);
        if (f->write_config) { f->config=byte; f->write_config=0; }
        else if (!f->silent) {
            if (f->resends) { --f->resends; queue(f,0xfe); }
            else queue(f,f->bad_ack ? 0xfc : 0xfa);
        }
    }
}
/* Verify pure decoding, modifier lifetimes, sequences and malformed bytes. */
static void decoder_test(void)
{
    struct keyboard_decoder d; keyboard_decoder_reset(&d);
    assert(keyboard_decode(&d,0x1e)=='A');
    assert(keyboard_decode(&d,0x1e)=='A'); /* Make repeats are text. */
    assert(keyboard_decode(&d,0x9e)==0);
    assert(keyboard_decode(&d,0x2a)==0);
    assert(keyboard_decode(&d,0x36)==0);
    assert(keyboard_decode(&d,0xaa)==0);
    assert(keyboard_decode(&d,0x27)==':'); /* Right shift still down. */
    assert(keyboard_decode(&d,0x35)=='?');
    assert(keyboard_decode(&d,0x02)==0);
    assert(keyboard_decode(&d,0xb6)==0);
    assert(keyboard_decode(&d,0x02)=='1');
    assert(keyboard_decode(&d,0x0b)=='0');
    assert(keyboard_decode(&d,0x34)=='.');
    assert(keyboard_decode(&d,0x39)==' ');
    assert(keyboard_decode(&d,0x1c)=='\n');
    assert(keyboard_decode(&d,0x0e)=='\b');
    assert(keyboard_decode(&d,0x1d)==0);
    assert(keyboard_decode(&d,0x1e)==0);
    assert(keyboard_decode(&d,0x9d)==0);
    assert(keyboard_decode(&d,0xe0)==0);
    assert(keyboard_decode(&d,0x38)==0);
    assert(keyboard_decode(&d,0x1e)==0);
    assert(keyboard_decode(&d,0xe0)==0);
    assert(keyboard_decode(&d,0xb8)==0);
    assert(keyboard_decode(&d,0xe0)==0);
    assert(keyboard_decode(&d,0x1c)==0); /* Extended keys unsupported. */
    uint8_t pause[]={0xe1,0x1d,0x45,0xe1,0x9d,0xc5};
    for(size_t n=0;n<sizeof pause;++n) assert(keyboard_decode(&d,pause[n])==0);
    assert(keyboard_decode(&d,0x1e)=='A');
    assert(keyboard_decode(&d,0xe0)==0);
    assert(keyboard_decode(&d,0xff)==0);
    assert(keyboard_decode(&d,0x1e)=='A');
    assert(keyboard_decode(&d,0xfa)==0);
    assert(keyboard_decode(&d,0xfe)==0);
    assert(keyboard_decode(&d,0)==0);
    assert(keyboard_decode(&d,0x3b)==0);
}
/* Verify initialization sequence, ACK failure/retry, finite timeouts and polling. */
static void transport_test(void)
{
    struct fake f={.config=0x47};
    struct keyboard_io io={read_fake,write_fake,&f};
    assert(!keyboard_ps2_initialize(NULL));
    assert(keyboard_ps2_initialize(&io)); assert(f.config==0x64);
    f=(struct fake){.resends=2};
    assert(keyboard_ps2_initialize(&io));
    f=(struct fake){.resends=3};
    assert(!keyboard_ps2_initialize(&io));
    f=(struct fake){.bad_ack=1};
    assert(!keyboard_ps2_initialize(&io));
    f=(struct fake){.stuck=1};
    assert(!keyboard_ps2_initialize(&io)); assert(f.writes==0);
    f=(struct fake){.silent=1};
    assert(!keyboard_ps2_initialize(&io));
    f=(struct fake){0};
    uint8_t byte=77;
    assert(keyboard_ps2_poll(&io,&byte)==KEYBOARD_NONE && byte==77);
    queue(&f,0x1e);
    assert(keyboard_ps2_poll(&io,&byte)==KEYBOARD_BYTE && byte==0x1e);
    f.status=0x20; queue(&f,0x1e);byte=77;
    assert(keyboard_ps2_poll(&io,&byte)==KEYBOARD_DROPPED && byte==77);
    f.status=0x80; queue(&f,0x1e);
    assert(keyboard_ps2_poll(&io,&byte)==KEYBOARD_DROPPED && byte==77);
    f.status=0x40; queue(&f,0x1e);
    assert(keyboard_ps2_poll(&io,&byte)==KEYBOARD_DROPPED && byte==77);
}
/* Run deterministic tests; assertion/sanitizer failures are a nonzero exit. */
int main(void)
{
    decoder_test(); transport_test(); puts("PASS keyboard decoder and bounded transport");
}
