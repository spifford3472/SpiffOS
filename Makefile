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

build/main.o: src/kernel/main.c src/kernel/keyboard.h src/kernel/keyboard-ps2.h src/kernel/screen-editor.h src/kernel/memory-boot.h src/kernel/workspace-manager.h src/kernel/x86-idt.h src/kernel/x86-pic.h src/kernel/acpi-discovery.h src/kernel/kernel-timer.h src/kernel/logical-line.h src/kernel/text-console.h src/kernel/boot-logo.h src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/framebuffer.o: src/kernel/framebuffer.c src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# M9-2: freestanding CPU gate builder and fixed exception-only assembly stubs.
build/x86-idt.o: src/kernel/x86-idt.c src/kernel/x86-idt.h src/kernel/x86-tss.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/acpi-discovery.o: src/kernel/acpi-discovery.c src/kernel/acpi-discovery.h src/kernel/acpi-madt.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/acpi-discovery.c -o $@

.PHONY: test-acpi-discovery
test-acpi-discovery: build/acpi-discovery-test
	./build/acpi-discovery-test

build/acpi-discovery-test: tests/acpi-discovery-test.c src/kernel/acpi-discovery.c src/kernel/acpi-madt.c src/kernel/acpi-discovery.h src/kernel/acpi-madt.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/acpi-discovery-test.c src/kernel/acpi-discovery.c src/kernel/acpi-madt.c -o $@

build/acpi-madt.o: src/kernel/acpi-madt.c src/kernel/acpi-madt.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/acpi-madt.c -o $@

.PHONY: test-acpi-madt
test-acpi-madt: build/acpi-madt-test
	./build/acpi-madt-test

build/acpi-madt-test: tests/acpi-madt-test.c src/kernel/acpi-madt.c src/kernel/acpi-madt.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/acpi-madt-test.c src/kernel/acpi-madt.c -o $@

build/kernel-timer.o: src/kernel/kernel-timer.c src/kernel/kernel-timer.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/kernel-timer.c -o $@

.PHONY: test-timer-memory
test-timer-memory: build/kernel-timer-test
	./build/kernel-timer-test

build/kernel-timer-test: tests/kernel-timer-test.c src/kernel/kernel-timer.c src/kernel/kernel-timer.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/kernel-timer-test.c src/kernel/kernel-timer.c -o $@

build/x86-pic.o: src/kernel/x86-pic.c src/kernel/x86-pic.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/x86-pic.c -o $@

.PHONY: test-pic-memory
test-pic-memory: build/x86-pic-test
	./build/x86-pic-test

build/x86-pic-test: tests/x86-pic-test.c src/kernel/x86-pic.c src/kernel/x86-pic.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/x86-pic-test.c src/kernel/x86-pic.c -o $@

build/x86-tss.o: src/kernel/x86-tss.c src/kernel/x86-tss.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/x86-tss.c -o $@

build/x86-exceptions.o: src/kernel/x86-exceptions.S Makefile
	mkdir -p build
	$(CC) --target=x86_64-unknown-none-elf -mno-red-zone -c $< -o $@

# Run fault injection ONLY in a copied isolated tree; this never modifies the
# owner/developer's normal ISO or enabling state of hardware IRQs.
.PHONY: test-idt-fault
test-idt-fault:
	python3 scripts/test-exception-fault.py

.PHONY: test-idt-memory
build/x86-idt-test: tests/x86-idt-test.c src/kernel/x86-idt.c src/kernel/x86-idt.h src/kernel/x86-tss.c src/kernel/x86-tss.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/x86-idt-test.c src/kernel/x86-idt.c src/kernel/x86-tss.c -o $@

test-idt-memory: build/x86-idt-test build/x86-tss-test
	./build/x86-idt-test
	./build/x86-tss-test

build/x86-tss-test: tests/x86-tss-test.c src/kernel/x86-tss.c src/kernel/x86-tss.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/x86-tss-test.c src/kernel/x86-tss.c -o $@

.PHONY: test-double-fault
test-double-fault:
	python3 scripts/test-double-fault.py

# Limine supplies a valid stack; the recovered start.S is intentionally unused.
$(KERNEL): build/main.o build/framebuffer.o build/text-console.o build/console-font.o build/boot-logo.o build/keyboard.o build/keyboard-ps2.o build/logical-line.o build/keyboard-console.o build/screen-editor.o build/memory-map.o build/memory-boot.o build/frame-allocator.o build/frame-allocate.o build/page-access.o build/kernel-heap.o build/memory-service.o build/workspace-manager.o build/acpi-madt.o build/acpi-discovery.o build/kernel-timer.o build/x86-idt.o build/x86-tss.o build/x86-pic.o build/x86-exceptions.o linker.ld Makefile
	$(LD) $(LDFLAGS) build/main.o build/framebuffer.o build/text-console.o build/console-font.o build/boot-logo.o build/keyboard.o build/keyboard-ps2.o build/logical-line.o build/keyboard-console.o build/screen-editor.o build/memory-map.o build/memory-boot.o build/frame-allocator.o build/frame-allocate.o build/page-access.o build/kernel-heap.o build/memory-service.o build/workspace-manager.o build/acpi-madt.o build/acpi-discovery.o build/kernel-timer.o build/x86-idt.o build/x86-tss.o build/x86-pic.o build/x86-exceptions.o -o $@

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

# Keyboard modules remain separate so transport/decoding/editing can be tested.
build/keyboard.o: src/kernel/keyboard.c src/kernel/keyboard.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/keyboard-ps2.o: src/kernel/keyboard-ps2.c src/kernel/keyboard-ps2.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/logical-line.o: src/kernel/logical-line.c src/kernel/logical-line.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/keyboard-console.o: src/kernel/keyboard-console.c src/kernel/keyboard-console.h src/kernel/logical-line.h src/kernel/text-console.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/screen-editor.o: src/kernel/screen-editor.c src/kernel/screen-editor.h src/kernel/logical-line.h src/kernel/text-console.h src/kernel/framebuffer.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-keyboard-memory test-keyboard
build/keyboard-test: tests/keyboard-test.c src/kernel/keyboard.c src/kernel/keyboard.h src/kernel/keyboard-ps2.c src/kernel/keyboard-ps2.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/keyboard-test.c src/kernel/keyboard.c src/kernel/keyboard-ps2.c -o $@

build/logical-line-test: tests/logical-line-test.c src/kernel/logical-line.c src/kernel/logical-line.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/logical-line-test.c src/kernel/logical-line.c -o $@

build/keyboard-console-test: tests/keyboard-console-test.c src/kernel/keyboard-console.c src/kernel/keyboard-console.h src/kernel/logical-line.c src/kernel/logical-line.h src/kernel/text-console.c src/kernel/text-console.h src/kernel/framebuffer.c src/kernel/framebuffer.h src/kernel/console-font.c src/kernel/console-font.h include/limine.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/keyboard-console-test.c src/kernel/keyboard-console.c src/kernel/logical-line.c src/kernel/text-console.c src/kernel/framebuffer.c src/kernel/console-font.c -o $@

build/screen-editor-test: tests/screen-editor-test.c src/kernel/screen-editor.c src/kernel/screen-editor.h src/kernel/logical-line.c src/kernel/logical-line.h src/kernel/text-console.c src/kernel/text-console.h src/kernel/framebuffer.c src/kernel/framebuffer.h src/kernel/console-font.c src/kernel/console-font.h include/limine.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/screen-editor-test.c src/kernel/screen-editor.c src/kernel/logical-line.c src/kernel/text-console.c src/kernel/framebuffer.c src/kernel/console-font.c -o $@

test-keyboard-memory: build/keyboard-test build/logical-line-test build/keyboard-console-test build/screen-editor-test build/workspace-manager-test
	./build/keyboard-test
	./build/logical-line-test
	./build/keyboard-console-test
	./build/screen-editor-test
	./build/workspace-manager-test

test-keyboard: test-keyboard-memory iso
	python3 scripts/qemu.py test --keyboard


# M9: no-heap status vocabulary is tested before memory and QEMU gates.
.PHONY: test-kernel-result
build/kernel-result-test: tests/kernel-result-test.c src/kernel/kernel-result.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/kernel-result-test.c -o $@

test-kernel-result: build/kernel-result-test
	./build/kernel-result-test

# Read-only memory-map inventory: no physical frames are allocated or touched.
build/memory-map.o: src/kernel/memory-map.c src/kernel/memory-map.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# Keep the translation unit explicit: headers are dependencies, not compiler input.
build/memory-boot.o: src/kernel/memory-boot.c src/kernel/memory-service.h src/kernel/memory-boot.h src/kernel/memory-map.h src/kernel/frame-allocator.h src/kernel/page-access.h src/kernel/kernel-heap.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/memory-boot.c -o $@

build/memory-map-test: src/kernel/memory-service.c src/kernel/memory-service.h src/kernel/frame-allocator.c src/kernel/frame-allocate.c src/kernel/frame-allocator.h tests/memory-map-test.c src/kernel/memory-map.c src/kernel/memory-map.h src/kernel/memory-boot.c src/kernel/memory-boot.h src/kernel/page-access.c src/kernel/page-access.h src/kernel/kernel-heap.c src/kernel/kernel-heap.h include/limine.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/memory-map-test.c src/kernel/memory-service.c src/kernel/memory-map.c src/kernel/memory-boot.c src/kernel/frame-allocator.c src/kernel/frame-allocate.c src/kernel/page-access.c src/kernel/kernel-heap.c -o $@

.PHONY: test-memory-map test-memory
test-memory-map: build/memory-map-test
	./build/memory-map-test

test-memory: test-acpi-discovery test-acpi-madt test-timer-memory test-pic-memory test-idt-memory test-kernel-result test-build-rules test-memory-service test-memory-map test-frame-allocator test-page-access test-kernel-heap iso
	python3 scripts/test-memory.py


# M2 page ownership accounting; returned physical addresses are never touched.
build/frame-allocator.o: src/kernel/frame-allocator.c src/kernel/frame-allocator.h src/kernel/memory-map.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/frame-allocate.o: src/kernel/frame-allocate.c src/kernel/frame-allocator.h src/kernel/memory-map.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/frame-allocator-test: tests/frame-allocator-test.c src/kernel/frame-allocator.c src/kernel/frame-allocate.c src/kernel/frame-allocator.h src/kernel/memory-map.c src/kernel/memory-map.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/frame-allocator-test.c src/kernel/frame-allocator.c src/kernel/frame-allocate.c src/kernel/memory-map.c -o $@

.PHONY: test-frame-allocator
test-frame-allocator: build/frame-allocator-test
	./build/frame-allocator-test


# M3 pure checked access; only the injected host backend touches guarded RAM.
build/page-access.o: src/kernel/page-access.c src/kernel/page-access.h src/kernel/frame-allocator.h src/kernel/memory-map.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/page-access-test: tests/page-access-test.c src/kernel/page-access.c src/kernel/page-access.h src/kernel/frame-allocator.c src/kernel/frame-allocate.c src/kernel/frame-allocator.h src/kernel/memory-map.c src/kernel/memory-map.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/page-access-test.c src/kernel/page-access.c src/kernel/frame-allocator.c src/kernel/frame-allocate.c src/kernel/memory-map.c -o $@

.PHONY: test-page-access
test-page-access: build/page-access-test
	./build/page-access-test


# M4 bounded heap over caller-supplied, M3-validated ordinary backing pages.
build/kernel-heap.o: src/kernel/kernel-heap.c src/kernel/kernel-heap.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel-heap-test: tests/kernel-heap-test.c src/kernel/kernel-heap.c src/kernel/kernel-heap.h src/kernel/page-access.c src/kernel/page-access.h src/kernel/frame-allocator.c src/kernel/frame-allocate.c src/kernel/frame-allocator.h src/kernel/memory-map.c src/kernel/memory-map.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/kernel-heap-test.c src/kernel/kernel-heap.c src/kernel/page-access.c src/kernel/frame-allocator.c src/kernel/frame-allocate.c src/kernel/memory-map.c -o $@

build/kernel-heap-model-test: tests/kernel-heap-model-test.c src/kernel/kernel-heap.c src/kernel/kernel-heap.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel tests/kernel-heap-model-test.c src/kernel/kernel-heap.c -o $@

.PHONY: test-kernel-heap
test-kernel-heap: build/kernel-heap-test build/kernel-heap-model-test
	./build/kernel-heap-test
	./build/kernel-heap-model-test

# M6 pure service core only; not linked into the boot image in this checkpoint.
build/memory-service-test: tests/memory-service-test.c src/kernel/memory-service.c src/kernel/memory-service.h src/kernel/kernel-heap.c src/kernel/kernel-heap.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/memory-service-test.c src/kernel/memory-service.c src/kernel/kernel-heap.c -o $@

.PHONY: test-memory-service
test-memory-service: build/memory-service-test
	./build/memory-service-test

build/memory-service.o: src/kernel/memory-service.c src/kernel/memory-service.h src/kernel/kernel-heap.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-build-rules
test-build-rules:
	python3 scripts/test-build-rules.py

# M7 independent editor state and screen snapshot manager.
build/workspace-manager.o: src/kernel/workspace-manager.c src/kernel/workspace-manager.h src/kernel/screen-editor.h src/kernel/boot-logo.h src/kernel/memory-boot.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# M7 hosted screen image and independent editor state tests.
.PHONY: test-workspace-manager-memory
build/workspace-manager-test: tests/workspace-manager-test.c src/kernel/workspace-manager.c src/kernel/workspace-manager.h src/kernel/boot-logo.c src/kernel/text-console.c src/kernel/console-font.c src/kernel/screen-editor.c src/kernel/logical-line.c src/kernel/framebuffer.c build/boot-logo-data.h include/limine.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Iinclude -Isrc/kernel -Ibuild tests/workspace-manager-test.c src/kernel/workspace-manager.c src/kernel/boot-logo.c src/kernel/text-console.c src/kernel/console-font.c src/kernel/screen-editor.c src/kernel/logical-line.c src/kernel/framebuffer.c -o $@

test-workspace-manager-memory: build/workspace-manager-test
	./build/workspace-manager-test
