/* Purpose: verify cursor shapes, arbitrary editing and push-down reflow.
 * Dependencies: real editor/console/font/framebuffer over guarded host RAM.
 * Invariants: static state avoids the large editor buffer on the host stack. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limine.h>
#include "text-console.h"
#include "screen-editor.h"

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

static struct guarded memory;
static struct guarded before_editor;
static struct limine_framebuffer framebuffer;
static struct text_console console;
static struct screen_editor editor;

/* Retain the last complete counted submission as deterministic evidence. */
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

/* Build an eight-column, six-row padded framebuffer and short boot line. */
static void initialize_console(struct limine_framebuffer *framebuffer,
    struct text_console *console)
{
    memset(&memory, 0xa5, sizeof memory);
    *framebuffer = (struct limine_framebuffer){
        .address = memory.pixels,
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

/* Verify every padding/guard word survives editor drawing and row movement. */
static void verify_guards(void)
{
    for (unsigned int y = 0; y < 96; ++y) {
        for (unsigned int x = 128; x < 136; ++x) {
            assert(memory.pixels[y][x] == UINT32_C(0xa5a5a5a5));
        }
    }
    for (unsigned int index = 0; index < 8; ++index) {
        assert(memory.before[index] == UINT32_C(0xa5a5a5a5));
        assert(memory.after[index] == UINT32_C(0xa5a5a5a5));
    }
}

/* The overwrite cursor inverts all 16 cell rows; Insert restores those pixels
 * and inverts only the bottom two rows, exactly as owner-approved. */
static void verify_cursor_shapes(struct capture *capture)
{
    initialize_console(&framebuffer, &console);
    before_editor = memory;
    assert(screen_editor_initialize(&editor, &console,
        capture_submit, capture));
    assert(editor.cursor_row == 1 && editor.cursor_column == 0);
    for (unsigned int y = 16; y < 32; ++y) {
        for (unsigned int x = 0; x < 16; ++x) {
            assert(memory.pixels[y][x] == ~before_editor.pixels[y][x]);
        }
    }
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_TOGGLE_INSERT, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    assert(editor.insert_mode);
    for (unsigned int y = 16; y < 30; ++y) {
        for (unsigned int x = 0; x < 16; ++x) {
            assert(memory.pixels[y][x] == before_editor.pixels[y][x]);
        }
    }
    for (unsigned int y = 30; y < 32; ++y) {
        for (unsigned int x = 0; x < 16; ++x) {
            assert(memory.pixels[y][x] == ~before_editor.pixels[y][x]);
        }
    }
}

/* Create two retained lines, grow the earlier one through a row boundary,
 * prove the later line moves down intact, then shrink and pull it back up. */
static void verify_editing_and_reflow(struct capture *capture)
{
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_TOGGLE_INSERT, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    assert(!editor.insert_mode);
    for (unsigned int index = 0; index < 7; ++index) {
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_CHARACTER, 'A') ==
               SCREEN_EDITOR_ACCEPTED);
    }
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_ENTER, 0) ==
           SCREEN_EDITOR_SUBMITTED);
    assert(capture->calls == 1 && capture->length == 7);
    assert(memcmp(capture->bytes, "AAAAAAA", 7) == 0);
    for (unsigned int index = 0; index < 4; ++index) {
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_CHARACTER, 'B') ==
               SCREEN_EDITOR_ACCEPTED);
    }
    assert(editor.lines[0].start_row == 1);
    assert(editor.lines[1].start_row == 2);

    assert(screen_editor_accept(&editor, SCREEN_EDITOR_UP, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    for (unsigned int index = 0; index < 3; ++index) {
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_RIGHT, 0) ==
               SCREEN_EDITOR_ACCEPTED);
    }
    assert(editor.cursor_line == 0 && editor.cursor_offset == 7);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_TOGGLE_INSERT, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_CHARACTER, 'X') ==
           SCREEN_EDITOR_ACCEPTED);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_CHARACTER, 'Y') ==
           SCREEN_EDITOR_ACCEPTED);
    assert(editor.lines[0].text.length == 9);
    assert(memcmp(editor.lines[0].text.bytes, "AAAAAAAXY", 9) == 0);
    assert(editor.lines[1].start_row == 3);
    assert(editor.lines[1].text.length == 4);
    assert(memcmp(editor.lines[1].text.bytes, "BBBB", 4) == 0);

    assert(screen_editor_accept(&editor, SCREEN_EDITOR_BACKSPACE, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    assert(editor.lines[0].text.length == 8);
    assert(memcmp(editor.lines[0].text.bytes, "AAAAAAAX", 8) == 0);
    assert(editor.lines[1].start_row == 2);

    assert(screen_editor_accept(&editor, SCREEN_EDITOR_RIGHT, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    assert(editor.cursor_line == 1 && editor.cursor_offset == 1);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_TOGGLE_INSERT, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_CHARACTER, 'C') ==
           SCREEN_EDITOR_ACCEPTED);
    assert(memcmp(editor.lines[1].text.bytes, "BCBB", 4) == 0);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_ENTER, 0) ==
           SCREEN_EDITOR_SUBMITTED);
    assert(capture->calls == 2 && capture->length == 4);
    assert(memcmp(capture->bytes, "BCBB", 4) == 0);

    assert(screen_editor_accept(&editor, SCREEN_EDITOR_DOWN, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    for (unsigned int index = 0; index < 3; ++index) {
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_RIGHT, 0) ==
               SCREEN_EDITOR_ACCEPTED);
    }
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_CHARACTER, 'Z') ==
           SCREEN_EDITOR_ACCEPTED);
    assert(editor.lines[editor.cursor_line].text.length == 4);
    assert(memcmp(editor.lines[editor.cursor_line].text.bytes, "   Z", 4) == 0);

    static const unsigned char boot_text[] = "BOOT";
    assert(screen_editor_seed_line(&editor, 0, boot_text,
                                   sizeof boot_text - 1));
    assert(!screen_editor_seed_line(&editor, 0, boot_text,
                                    sizeof boot_text - 1));
    while (editor.cursor_row > 0) {
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_UP, 0) ==
               SCREEN_EDITOR_ACCEPTED);
    }
    assert(editor.cursor_offset == 4);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_ENTER, 0) ==
           SCREEN_EDITOR_SUBMITTED);
    assert(capture->calls == 3 && capture->length == 4);
    assert(memcmp(capture->bytes, "BOOT", 4) == 0);
    verify_guards();
}

/* Fill a logical line beyond the visible viewport, reject an insertion at
 * capacity without changing its bytes, then submit the retained full prefix.
 * Navigation clamps at both screen corners and moving away restores pixels. */
static void verify_scroll_capacity_and_boundaries(void)
{
    struct capture capture = {0};
    initialize_console(&framebuffer, &console);
    assert(screen_editor_initialize(&editor, &console,
        capture_submit, &capture));
    for (unsigned int index = 0; index < SCREEN_EDITOR_LINE_CAPACITY; ++index) {
        unsigned char byte = (unsigned char)('A' + index % 26);
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_CHARACTER, byte) ==
               SCREEN_EDITOR_ACCEPTED);
    }
    int line_index = editor.cursor_line;
    assert(editor.lines[line_index].text.length == SCREEN_EDITOR_LINE_CAPACITY);
    assert(console.scroll_count > 0);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_CHARACTER, 'Z') ==
           SCREEN_EDITOR_FULL);
    assert(editor.capacity_full && capture.calls == 0);
    for (unsigned int index = 0; index < SCREEN_EDITOR_LINE_CAPACITY; ++index) {
        assert(editor.lines[line_index].text.bytes[index] ==
               (unsigned char)('A' + index % 26));
    }
    /* Move to the top-left visible fragment and delete its preceding byte.
     * This must retain a visible cursor even though the old prefix is hidden. */
    while (editor.cursor_row > 0) {
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_UP, 0) ==
               SCREEN_EDITOR_ACCEPTED);
    }
    while (editor.cursor_column > 0) {
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_LEFT, 0) ==
               SCREEN_EDITOR_ACCEPTED);
    }
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_UP, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    assert(editor.cursor_row == 0 && editor.cursor_column == 0);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_BACKSPACE, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    assert(editor.cursor_row == 0 && editor.cursor_column == 0);
    assert(!editor.capacity_full);
    assert(editor.lines[line_index].text.length ==
           SCREEN_EDITOR_LINE_CAPACITY - 1);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_ENTER, 0) ==
           SCREEN_EDITOR_SUBMITTED);
    assert(capture.calls == 1 && capture.length == SCREEN_EDITOR_LINE_CAPACITY - 1);
    assert(memcmp(capture.bytes, editor.lines[line_index].text.bytes,
                  capture.length) == 0);
    for (unsigned int index = 0; index < 100; ++index) {
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_DOWN, 0) ==
               SCREEN_EDITOR_ACCEPTED);
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_RIGHT, 0) ==
               SCREEN_EDITOR_ACCEPTED);
    }
    assert(editor.cursor_row == console.rows - 1 &&
           editor.cursor_column == console.columns - 1);
    verify_guards();
}

int main(void)
{
    struct capture capture = {0};
    verify_cursor_shapes(&capture);
    verify_editing_and_reflow(&capture);
    verify_scroll_capacity_and_boundaries();
    initialize_console(&framebuffer, &console);
    assert(screen_editor_initialize(&editor, &console,
        capture_submit, &capture));
    /* A cursor visiting existing artwork/text restores the exact stored bits. */
    before_editor = memory;
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_UP, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_DOWN, 0) ==
           SCREEN_EDITOR_ACCEPTED);
    assert(memcmp(&before_editor, &memory, sizeof memory) == 0);
    for (unsigned int index = 0; index < 8; ++index) {
        assert(screen_editor_accept(&editor, SCREEN_EDITOR_CHARACTER, 'A') ==
               SCREEN_EDITOR_ACCEPTED);
    }
    assert(editor.cursor_row == 2 && editor.cursor_column == 0);
    assert(screen_editor_accept(&editor, SCREEN_EDITOR_ENTER, 0) ==
           SCREEN_EDITOR_SUBMITTED);
    assert(editor.cursor_row == 2 && editor.cursor_column == 0);
    assert(capture.length == 8 && memcmp(capture.bytes, "AAAAAAAA", 8) == 0);
    verify_guards();
    puts("PASS block/underscore cursor, arrows, insert/overwrite and reflow");
}
