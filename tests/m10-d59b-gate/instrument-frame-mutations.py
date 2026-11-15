#!/usr/bin/env python3
"""Generate a disposable, hook-instrumented copy of genuine frame-allocate.c.

Only the fixture uses the output; production source must remain untouched.
An anchor mismatch fails closed rather than silently skipping mutation points.
"""
import pathlib
import sys


def main() -> int:
    """Require exactly one occurrence of each mutation and insert a hook."""
    if len(sys.argv) != 3:
        raise SystemExit('usage: instrument-frame-mutations.py INPUT_C OUTPUT_C')
    source = pathlib.Path(sys.argv[1]).read_text()
    anchors = [
        '        allocator->owners[index] = (uint8_t)owner;',
        '        ++allocator->owned[owner];',
        '        --*free_count;',
    ]
    source = source.replace('#include "frame-allocator.h"',
        '#include "frame-allocator.h"\n'
        'extern void d59b_frame_test_hook(const struct frame_allocator *, unsigned int);', 1)
    for point, anchor in enumerate(anchors, 1):
        if source.count(anchor) != 1:
            raise SystemExit(f'unsafe source drift at mutation point {point}')
        source = source.replace(anchor, anchor +
            f'\n        d59b_frame_test_hook(allocator, {point}u);', 1)
    pathlib.Path(sys.argv[2]).write_text(source)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
