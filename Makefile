# First-boot build: Clang targets a freestanding ELF, never the host runtime.
CC := clang
LD := ld.lld
HOST_CC ?= cc
CFLAGS := --target=x86_64-unknown-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -Wall -Wextra -Werror -O2 -g -Iinclude
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T linker.ld
KERNEL := build/spiffos-kernel.elf

.PHONY: all
all: $(KERNEL)

build/main.o: src/kernel/main.c src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/framebuffer.o: src/kernel/framebuffer.c src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# Limine supplies a valid stack; the recovered start.S is intentionally unused.
$(KERNEL): build/main.o build/framebuffer.o linker.ld Makefile
	$(LD) $(LDFLAGS) build/main.o build/framebuffer.o -o $@

.PHONY: iso test run debug
iso: $(KERNEL)
	python3 scripts/build-image.py

test: iso
	python3 scripts/qemu.py test

run: iso
	python3 scripts/qemu.py run

debug: iso
	python3 scripts/qemu.py debug

# Host-only bounds tests: ordinary RAM, never privileged kernel instructions.
.PHONY: test-framebuffer-memory
build/framebuffer-test: tests/framebuffer-test.c src/kernel/framebuffer.c src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/framebuffer-test.c src/kernel/framebuffer.c -o $@

test-framebuffer-memory: build/framebuffer-test
	./build/framebuffer-test

.PHONY: test-framebuffer
test-framebuffer: test-framebuffer-memory iso
	python3 scripts/qemu.py test --screenshot
	python3 scripts/check-framebuffer.py build/framebuffer.ppm
