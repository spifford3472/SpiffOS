/* Host-only deterministic PIC state and register-order regression suite.
 * Does not execute privileged IO; all fake port accesses are tracked. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "x86-pic.h"

struct event { uint16_t port; uint8_t byte; bool read; };
struct mock_io {
    struct event events[160];
    size_t count;
    uint8_t master_isr, slave_isr;
};

/* Append an observed write. Test fails if driver exceeds bounded event cap. */
static void record_write(void *context, uint16_t port, uint8_t byte)
{
    struct mock_io *mock = context;
    assert(mock->count < sizeof mock->events / sizeof mock->events[0]);
    mock->events[mock->count++] = (struct event){port, byte, false};
}

/* Record ISR reads and return the mock's in-service byte. */
static uint8_t record_read(void *context, uint16_t port)
{
    struct mock_io *mock = context;
    assert(mock->count < sizeof mock->events / sizeof mock->events[0]);
    mock->events[mock->count++] = (struct event){port, 0, true};
    return port == 0x20 ? mock->master_isr : mock->slave_isr;
}

/* Assert one exact IO operation (port, data and direction). */
static void check(struct mock_io *mock, size_t index, uint16_t port,
                  uint8_t byte, bool is_read)
{
    assert(index < mock->count);
    assert(mock->events[index].port == port);
    assert(mock->events[index].read == is_read);
    if (!is_read) assert(mock->events[index].byte == byte);
}

/* Exercise all masks, cascade ordering, spurious EOIs and rejection paths. */
int main(void)
{
    struct mock_io mock = {0};
    struct x86_pic pic = {0};
    const struct x86_pic_io io = {record_read, record_write, &mock};
    assert(!x86_pic_initialize(NULL, &io));
    assert(!x86_pic_initialize(&pic, NULL));
    assert(mock.count == 0);
    assert(!x86_pic_mask_irq(&pic, 0));
    assert(!x86_pic_acknowledge(&pic, 0));
    assert(x86_pic_initialize(&pic, &io));
    assert(!x86_pic_initialize(&pic, &io));
    assert(pic.ready && pic.masked_irqs == 0xffffu);
    assert(mock.count == 24); /* Twelve command/data writes + IO delays. */
    const uint16_t ports[] = {
        0x21, 0xa1, 0x20, 0xa0, 0x21, 0xa1,
        0x21, 0xa1, 0x21, 0xa1, 0x21, 0xa1
    };
    const uint8_t bytes[] = {
        0xff, 0xff, 0x11, 0x11, 0x20, 0x28,
        0x04, 0x02, 0x01, 0x01, 0xff, 0xff
    };
    for (size_t n = 0; n < 12; ++n) {
        check(&mock, n*2, ports[n], bytes[n], false);
        check(&mock, n*2+1, 0x80, 0, false);
    }
    mock.count = 0;
    assert(!x86_pic_unmask_irq(&pic, 2));
    assert(!x86_pic_mask_irq(&pic, 16));
    assert(!x86_pic_acknowledge(&pic, 16));
    assert(mock.count == 0);
    assert(x86_pic_unmask_irq(&pic, 0));
    check(&mock, 0, 0x21, 0xfe, false);
    assert(pic.masked_irqs == 0xfffe);
    assert(x86_pic_mask_irq(&pic, 0));
    check(&mock, 2, 0x21, 0xff, false);
    assert(pic.masked_irqs == 0xffff);
    mock.count = 0;
    assert(x86_pic_unmask_irq(&pic, 8));
    check(&mock, 0, 0xa1, 0xfe, false);
    check(&mock, 2, 0x21, 0xfb, false);
    assert(pic.masked_irqs == 0xfefb);
    assert(x86_pic_mask_irq(&pic, 8));
    check(&mock, 4, 0x21, 0xff, false);
    check(&mock, 6, 0xa1, 0xff, false);
    assert(pic.masked_irqs == 0xffff);
    mock.count = 0;
    mock.master_isr = 0;
    assert(x86_pic_acknowledge(&pic, 7));
    check(&mock, 0, 0x20, 0x0b, false);
    check(&mock, 1, 0x20, 0, true);
    assert(mock.count == 2);
    mock.count = 0;
    mock.master_isr = 0x80;
    assert(x86_pic_acknowledge(&pic, 7));
    check(&mock, 2, 0x20, 0x20, false);
    assert(mock.count == 3);
    mock.count = 0;
    mock.slave_isr = 0;
    assert(x86_pic_acknowledge(&pic, 15));
    check(&mock, 0, 0xa0, 0x0b, false);
    check(&mock, 1, 0xa0, 0, true);
    check(&mock, 2, 0x20, 0x20, false);
    assert(mock.count == 3);
    mock.count = 0;
    mock.slave_isr = 0x80;
    assert(x86_pic_acknowledge(&pic, 15));
    check(&mock, 2, 0xa0, 0x20, false);
    check(&mock, 3, 0x20, 0x20, false);
    assert(mock.count == 4);
    mock.count = 0;
    assert(x86_pic_acknowledge(&pic, 0));
    check(&mock, 0, 0x20, 0x20, false);
    assert(mock.count == 1);
    return 0;
}
