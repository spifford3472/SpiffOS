/* Purpose: verify stored soft-wrapped input in guarded host framebuffer RAM.
 * Dependencies: logical line, real console/font/framebuffer implementations.
 * Invariants: no hardware, heap or interrupts; accepted screen-editor ADR. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limine.h>
#include "text-console.h"
#include "keyboard-console.h"

struct guarded {
    uint32_t before[8];
    uint32_t pixels[96][136];
    uint32_t after[8];
};

struct capture {
    unsigned char bytes[4096];
    size_t length;
    unsigned int calls;
};

/* Copy complete counted submission into test-owned evidence. */
static bool capture_submit(
    void *context, const unsigned char *bytes, size_t length)
{
    struct capture *capture = context;
    assert(length <= sizeof capture->bytes);
    memcpy(capture->bytes, bytes, length);
    capture->length = length;
    ++capture->calls;
    return true;
}

/* Make one 8-column by 6-row console over guarded/padded host RAM. */
static void initialize_console(struct guarded *memory,
    struct limine_framebuffer *framebuffer, struct text_console *console)
{
    memset(memory, 0xa5, sizeof *memory);
    *framebuffer = (struct limine_framebuffer){
        .address = memory->pixels,
        .width = 128,
        .height = 96,
        .pitch = 136 * 4,
        .bpp = 32,
        .memory_model = LIMINE_FRAMEBUFFER_RGB,
        .red_mask_size = 8,
        .red_mask_shift = 16,
        .green_mask_size = 8,
        .green_mask_shift = 8,
        .blue_mask_size = 8,
        .blue_mask_shift = 0
    };
    assert(text_console_initialize(console, framebuffer, 0, 0, 8, 6));
    assert(text_console_write(console, "BOOT"));
}

/* Assert framebuffer row padding and surrounding guards were never touched. */
static void verify_guards(const struct guarded *memory)
{
    for (unsigned int y = 0; y < 96; ++y) {
        for (unsigned int x = 128; x < 136; ++x) {
            assert(memory->pixels[y][x] == UINT32_C(0xa5a5a5a5));
        }
    }
    for (unsigned int index = 0; index < 8; ++index) {
        assert(memory->before[index] == UINT32_C(0xa5a5a5a5));
        assert(memory->after[index] == UINT32_C(0xa5a5a5a5));
    }
}

/* Check exact width, edge+1, three rows, cross-wrap deletion and submission. */
static void test_wrapping_and_submission(void)
{
    static struct guarded memory;
    struct limine_framebuffer framebuffer;
    struct text_console console;
    struct keyboard_console input;
    unsigned char storage[64];
    struct capture capture = {0};
    initialize_console(&memory, &framebuffer, &console);
    assert(!keyboard_console_initialize(
        &input, NULL, storage, sizeof storage, capture_submit, &capture));
    assert(keyboard_console_initialize(
        &input, &console, storage, sizeof storage, capture_submit, &capture));
    assert(console.row == 1 && console.column == 0);
    for (unsigned int index = 0; index < 8; ++index) {
        assert(keyboard_console_accept(&input, 'A' + index) ==
               KEYBOARD_CONSOLE_ACCEPTED);
    }
    assert(input.line.length == 8);
    assert(console.row == 2 && console.column == 0 && capture.calls == 0);
    assert(keyboard_console_accept(&input, 'I') == KEYBOARD_CONSOLE_ACCEPTED);
    assert(console.row == 2 && console.column == 1 && capture.calls == 0);
    for (unsigned int index = 0; index < 15; ++index) {
        assert(keyboard_console_accept(&input, 'Z') ==
               KEYBOARD_CONSOLE_ACCEPTED);
    }
    assert(input.line.length == 24);
    assert(console.row == 4 && console.column == 0 && capture.calls == 0);
    assert(keyboard_console_accept(&input, '\b') ==
           KEYBOARD_CONSOLE_ACCEPTED);
    assert(input.line.length == 23 && console.row == 3 && console.column == 7);
    assert(keyboard_console_accept(&input, '\b') ==
           KEYBOARD_CONSOLE_ACCEPTED);
    assert(input.line.length == 22 && console.row == 3 && console.column == 6);
    assert(keyboard_console_accept(&input, 'Q') ==
           KEYBOARD_CONSOLE_ACCEPTED);
    assert(keyboard_console_accept(&input, 'R') ==
           KEYBOARD_CONSOLE_ACCEPTED);
    assert(input.line.length == 24 && capture.calls == 0);
    assert(keyboard_console_accept(&input, '\n') ==
           KEYBOARD_CONSOLE_SUBMITTED);
    assert(capture.calls == 1 && capture.length == 24);
    assert(memcmp(capture.bytes, "ABCDEFGHIZZZZZZZZZZZZZQR", 24) == 0);
    /* Exact-width Enter reuses the already wrapped continuation row. */
    assert(console.row == 4 && console.column == 0);
    assert(input.line.length == 0);
    assert(keyboard_console_accept(&input, '\b') ==
           KEYBOARD_CONSOLE_IGNORED);
    verify_guards(&memory);
}

/* Check explicit capacity rejection without truncation or implicit submission. */
static void test_capacity(void)
{
    static struct guarded memory, saved;
    struct limine_framebuffer framebuffer;
    struct text_console console;
    struct keyboard_console input;
    unsigned char storage[4];
    struct capture capture = {0};
    initialize_console(&memory, &framebuffer, &console);
    assert(keyboard_console_initialize(
        &input, &console, storage, sizeof storage, capture_submit, &capture));
    for (unsigned int index = 0; index < 4; ++index) {
        assert(keyboard_console_accept(&input, '1' + index) ==
               KEYBOARD_CONSOLE_ACCEPTED);
    }
    saved = memory;
    assert(keyboard_console_accept(&input, '5') == KEYBOARD_CONSOLE_FULL);
    assert(memcmp(&saved, &memory, sizeof memory) == 0);
    assert(input.line.length == 4 && memcmp(input.line.bytes, "1234", 4) == 0);
    assert(capture.calls == 0);
    assert(keyboard_console_accept(&input, '\n') ==
           KEYBOARD_CONSOLE_SUBMITTED);
    assert(capture.calls == 1 && capture.length == 4);
    assert(memcmp(capture.bytes, "1234", 4) == 0);
    verify_guards(&memory);
}

/* Type beyond viewport height, retain off-screen prefix, then submit all bytes. */
static void test_scrolling_logical_line(void)
{
    static struct guarded memory;
    struct limine_framebuffer framebuffer;
    struct text_console console;
    struct keyboard_console input;
    unsigned char storage[64];
    struct capture capture = {0};
    initialize_console(&memory, &framebuffer, &console);
    assert(keyboard_console_initialize(
        &input, &console, storage, sizeof storage, capture_submit, &capture));
    for (unsigned int index = 0; index < 48; ++index) {
        assert(keyboard_console_accept(&input, 'S') ==
               KEYBOARD_CONSOLE_ACCEPTED);
    }
    assert(console.scroll_count > 0);
    assert(input.line.length == 48 && capture.calls == 0);
    assert(keyboard_console_accept(&input, '\b') ==
           KEYBOARD_CONSOLE_ACCEPTED);
    assert(input.line.length == 47);
    assert(keyboard_console_accept(&input, 'T') ==
           KEYBOARD_CONSOLE_ACCEPTED);
    assert(keyboard_console_accept(&input, '\n') ==
           KEYBOARD_CONSOLE_SUBMITTED);
    assert(capture.calls == 1 && capture.length == 48);
    for (unsigned int index = 0; index < 47; ++index) {
        assert(capture.bytes[index] == 'S');
    }
    assert(capture.bytes[47] == 'T');
    verify_guards(&memory);
}

int main(void)
{
    test_wrapping_and_submission();
    test_capacity();
    test_scrolling_logical_line();
    puts("PASS logical input wraps, submits once, backspaces and retains scroll");
}
