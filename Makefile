# First-boot build: Clang targets a freestanding ELF, never the host runtime.
CC := clang
LD := ld.lld
HOST_CC ?= cc
CFLAGS := --target=x86_64-unknown-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -Wall -Wextra -Werror -O2 -g -Iinclude -Ibuild
CFLAGS += $(BOOT_TEST_FLAGS)
POWERUP_CHECK_FLAGS ?= --powerup --console --workspace
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T linker.ld
KERNEL := build/spiffos-kernel.elf

.PHONY: all
all: $(KERNEL)

build/main.o: src/kernel/main.c src/kernel/text-console.h src/kernel/boot-logo.h src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/framebuffer.o: src/kernel/framebuffer.c src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# Limine supplies a valid stack; the recovered start.S is intentionally unused.
$(KERNEL): build/main.o build/framebuffer.o build/text-console.o build/console-font.o build/boot-logo.o linker.ld Makefile
	$(LD) $(LDFLAGS) build/main.o build/framebuffer.o build/text-console.o build/console-font.o build/boot-logo.o -o $@

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
	python3 scripts/check-framebuffer.py build/framebuffer.ppm $(POWERUP_CHECK_FLAGS) --serial build/serial.log

# Test fixed bitmap lettering using ordinary guarded host RAM.
.PHONY: test-text-memory test-text
build/text-test: tests/text-test.c src/kernel/framebuffer.c src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/text-test.c src/kernel/framebuffer.c -o $@

test-text-memory: build/text-test
	./build/text-test

test-text: test-text-memory test-framebuffer

build/text-console.o: src/kernel/text-console.c src/kernel/text-console.h src/kernel/framebuffer.h src/kernel/console-font.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/console-font.o: src/kernel/console-font.c src/kernel/console-font.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-console-memory test-console
build/console-test: tests/console-test.c src/kernel/text-console.c src/kernel/text-console.h src/kernel/console-font.c src/kernel/console-font.h src/kernel/framebuffer.c src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/console-test.c src/kernel/text-console.c src/kernel/console-font.c src/kernel/framebuffer.c -o $@

test-console-memory: build/console-test
	./build/console-test

test-console: test-console-memory test-text

.PHONY: test-scroll-memory test-scroll
build/scroll-test: tests/scroll-test.c src/kernel/text-console.c src/kernel/text-console.h src/kernel/console-font.c src/kernel/console-font.h src/kernel/framebuffer.c src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/scroll-test.c src/kernel/text-console.c src/kernel/console-font.c src/kernel/framebuffer.c -o $@

test-scroll-memory: build/scroll-test
	./build/scroll-test

test-scroll: test-scroll-memory test-console

build/boot-logo-data.h: assets/branding/spiffos-boot-logo.rgb assets/branding/spiffos-boot-logo.json assets/branding/spiffos-logo.svg scripts/pack-boot-logo.py
	python3 scripts/pack-boot-logo.py

build/boot-logo.o: src/kernel/boot-logo.c src/kernel/boot-logo.h src/kernel/text-console.h src/kernel/framebuffer.h build/boot-logo-data.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-logo-memory test-powerup
build/logo-test: src/kernel/text-console.c src/kernel/text-console.h src/kernel/console-font.c src/kernel/console-font.h tests/logo-test.c src/kernel/boot-logo.c src/kernel/boot-logo.h src/kernel/framebuffer.c src/kernel/framebuffer.h build/boot-logo-data.h include/limine.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel -Ibuild tests/logo-test.c src/kernel/boot-logo.c src/kernel/framebuffer.c src/kernel/text-console.c src/kernel/console-font.c -o $@

test-logo-memory: build/logo-test
	./build/logo-test

test-powerup: test-logo-memory test-scroll

.PHONY: test-workspace-memory test-workspace
build/workspace-test: tests/workspace-test.c src/kernel/boot-logo.c src/kernel/boot-logo.h src/kernel/text-console.c src/kernel/text-console.h src/kernel/console-font.c src/kernel/console-font.h src/kernel/framebuffer.c src/kernel/framebuffer.h build/boot-logo-data.h include/limine.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel -Ibuild tests/workspace-test.c src/kernel/boot-logo.c src/kernel/text-console.c src/kernel/console-font.c src/kernel/framebuffer.c -o $@

test-workspace-memory: build/workspace-test
	./build/workspace-test

test-workspace: test-workspace-memory test-powerup

.PHONY: test-workspace-scroll
test-workspace-scroll:
	python3 scripts/test-workspace-scroll.py
