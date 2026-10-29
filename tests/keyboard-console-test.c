/* Purpose: verify bounded keyboard echo/Enter/Backspace in guarded host RAM.
 * Dependencies: real console/font/framebuffer with fake pixel memory.
 * Invariants: no hardware, heap or interrupts; ADR-qemu-keyboard-input. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limine.h>
#include "text-console.h"
#include "keyboard-console.h"
struct guarded { uint32_t before[8], pixels[64][1040], after[8]; };
/* Check edit boundaries, width/cap, pending scroll and every guard/padding word.
 * Assertion/sanitizer abort means failure; no ownership transfer or real I/O. */
int main(void)
{
    static struct guarded memory, saved;
    memset(&memory,0xa5,sizeof memory);
    struct limine_framebuffer fb={
        .address=memory.pixels,.width=1024,.height=64,.pitch=1040*4,
        .bpp=32,.memory_model=LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size=8,.red_mask_shift=16,
        .green_mask_size=8,.green_mask_shift=8,
        .blue_mask_size=8,.blue_mask_shift=0
    };
    struct text_console console; struct keyboard_console input;
    assert(!keyboard_console_initialize(&input,NULL));
    assert(!keyboard_console_accept(&input,'A'));
    assert(text_console_initialize(&console,&fb,0,0,64,4));
    assert(text_console_write(&console,"BOOT MESSAGE"));
    assert(keyboard_console_initialize(&input,&console));
    saved=memory;
    assert(!keyboard_console_accept(&input,'\b'));
    assert(memcmp(&saved,&memory,sizeof memory)==0);
    assert(!keyboard_console_accept(&input,'a'));
    assert(keyboard_console_accept(&input,'A'));
    assert(input.length==1 && console.column==1);
    assert(keyboard_console_accept(&input,'\b'));
    assert(memcmp(&saved,&memory,sizeof memory)==0);
    for(unsigned int n=0;n<63;++n) assert(keyboard_console_accept(&input,'B'));
    saved=memory;
    assert(!keyboard_console_accept(&input,'C'));
    assert(memcmp(&saved,&memory,sizeof memory)==0 && console.column==63);
    assert(keyboard_console_accept(&input,'\b'));
    assert(keyboard_console_accept(&input,'Z'));
    assert(keyboard_console_accept(&input,'\n'));
    assert(input.length==0 && console.column==0 && console.row==2);
    saved=memory;
    assert(!keyboard_console_accept(&input,'\b'));
    assert(memcmp(&saved,&memory,sizeof memory)==0);
    for(unsigned int n=0;n<20;++n) {
        assert(keyboard_console_accept(&input,'A'));
        assert(keyboard_console_accept(&input,'\n'));
    }
    assert(keyboard_console_accept(&input,'A')); /* Pending bottom scroll. */
    assert(keyboard_console_accept(&input,'\b'));
    for(unsigned int y=0;y<64;++y)
        for(unsigned int x=1024;x<1040;++x) assert(memory.pixels[y][x]==0xa5a5a5a5);
    for(unsigned int n=0;n<8;++n)
        assert(memory.before[n]==0xa5a5a5a5 && memory.after[n]==0xa5a5a5a5);
    assert(text_console_initialize(&console,&fb,0,0,2,1));
    assert(keyboard_console_initialize(&input,&console));
    assert(input.limit==1);
    assert(keyboard_console_accept(&input,'A'));
    assert(!keyboard_console_accept(&input,'B'));
    assert(keyboard_console_accept(&input,'\b'));
    assert(text_console_initialize(&console,&fb,0,0,1,1));
    assert(!keyboard_console_initialize(&input,&console));
    puts("PASS bounded keyboard editing, Enter/Backspace, scroll and guards");
}
