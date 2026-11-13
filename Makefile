# First-boot build: Clang targets a freestanding ELF, never the host runtime.
CC := clang
LD := ld.lld
HOST_CC ?= cc
CFLAGS := --target=x86_64-unknown-none-elf -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -Wall -Wextra -Werror -O2 -g -Iinclude -Ibuild
CFLAGS += $(BOOT_TEST_FLAGS)
POWERUP_CHECK_FLAGS ?= --powerup --console --workspace
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T linker.ld
KERNEL := build/spiffos-kernel.elf

# M10-B first QEMU smoke uses a dedicated test-only object set; the default
# production ISO links none of these objects and retains original behavior.
M10B_TEST_OBJECTS :=
ifneq ($(findstring -DSPIFFOS_M10_B_SWITCH_TEST,$(BOOT_TEST_FLAGS)),)
M10B_TEST_OBJECTS := build/kernel-task.o build/x86-task-context.o build/x86-task-switch.o build/m10-b-guest-smoke.o
endif

# D2 is a separate interrupt-frame diagnostic, never in the released ISO.
M10D2_TEST_OBJECTS :=
ifneq ($(findstring -DSPIFFOS_M10_D2_IRQ_FRAME_TEST,$(BOOT_TEST_FLAGS)),)
M10D2_TEST_OBJECTS := build/m10-d2-irq-frame.o build/m10-d2-probe-run.o
endif

# D3 full-frame IRQ return handoff is guest diagnostic ONLY, not a live
# scheduler or default kernel link dependency.
M10D3_TEST_OBJECTS :=
ifneq ($(findstring -DSPIFFOS_M10_D3_PREEMPT_TEST,$(BOOT_TEST_FLAGS)),)
M10D3_TEST_OBJECTS := build/m10-d3-guest-smoke.o
endif

# D4 opts into the already-tested pure D1 policy only for the real-IRQ
# diagnostic image. Normal kernel and legacy D3 tests link no D4 code.
M10D4_TEST_OBJECTS :=
ifneq ($(findstring -DSPIFFOS_M10_D4_PRIORITY_TEST,$(BOOT_TEST_FLAGS)),)
M10D4_TEST_OBJECTS := build/kernel-priority-policy.o
endif

.PHONY: all
all: $(KERNEL)

build/main.o: src/kernel/main.c src/kernel/keyboard.h src/kernel/keyboard-ps2.h src/kernel/screen-editor.h src/kernel/memory-boot.h src/kernel/workspace-manager.h src/kernel/x86-idt.h src/kernel/x86-pic.h src/kernel/acpi-discovery.h src/kernel/apic-preflight.h src/kernel/x86-page-walk.h src/kernel/mmio-page-policy.h src/kernel/x86-mmio-map.h src/kernel/apic-memory-type.h src/kernel/apic-readonly.h src/kernel/apic-irq0.h src/kernel/kernel-timer.h src/kernel/kernel-scheduler-clock.h src/kernel/logical-line.h src/kernel/text-console.h src/kernel/boot-logo.h src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/framebuffer.o: src/kernel/framebuffer.c src/kernel/framebuffer.h include/limine.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# M9-2: freestanding CPU gate builder and fixed exception-only assembly stubs.
build/x86-idt.o: src/kernel/x86-idt.c src/kernel/x86-idt.h src/kernel/x86-tss.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# End-to-end real interrupt delivery and fail-closed no-route regression.
.PHONY: test-irq0-timing test-irq0-negative test-irq0
test-irq0-timing:
	python3 scripts/test-irq0-timing.py

test-irq0-negative:
	python3 scripts/test-irq0-negative.py

test-irq0: test-apic-irq0 test-irq0-timing test-irq0-negative

build/apic-irq0.o: src/kernel/apic-irq0.c src/kernel/apic-irq0.h src/kernel/acpi-madt.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/apic-irq0.c -o $@

.PHONY: test-apic-irq0
test-apic-irq0: build/apic-irq0-test
	./build/apic-irq0-test

build/apic-irq0-test: tests/apic-irq0-test.c src/kernel/apic-irq0.c src/kernel/apic-irq0.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/apic-irq0-test.c src/kernel/apic-irq0.c -o $@

build/x86-irq0.o: src/kernel/x86-irq0.S Makefile
	mkdir -p build
	$(CC) --target=x86_64-unknown-none-elf -mno-red-zone $(BOOT_TEST_FLAGS) -c src/kernel/x86-irq0.S -o $@

build/m10-d2-irq-frame.o: src/kernel/m10-d2-irq-frame.c src/kernel/m10-d2-irq-frame.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/m10-d2-irq-frame.c -o $@

build/m10-d2-probe-run.o: src/kernel/m10-d2-probe-run.S Makefile
	mkdir -p build
	$(CC) --target=x86_64-unknown-none-elf -mno-red-zone -c src/kernel/m10-d2-probe-run.S -o $@

build/m10-d3-guest-smoke.o: src/kernel/m10-d3-guest-smoke.c src/kernel/m10-d2-irq-frame.h src/kernel/memory-boot.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/m10-d3-guest-smoke.c -o $@

build/kernel-priority-policy.o: src/kernel/kernel-priority-policy.c src/kernel/kernel-priority-policy.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/kernel-priority-policy.c -o $@

.PHONY: test-m10-d4-guest
test-m10-d4-guest:
	python3 scripts/test-m10-d4-guest.py

.PHONY: test-m10-d4-four-class
test-m10-d4-four-class:
	python3 scripts/test-m10-d4-four-class.py

.PHONY: test-m10-d4-block-wakeup
test-m10-d4-block-wakeup:
	python3 scripts/test-m10-d4-block-wakeup.py

.PHONY: test-m10-d4-acceptance
test-m10-d4-acceptance: test-m10-d4-guest test-m10-d4-four-class test-m10-d4-block-wakeup test-m10-d3-acceptance
	@echo SPIFFOS_M10_D4_ACCEPTANCE_OK

.PHONY: test-m10-d3-guest
test-m10-d3-guest:
	python3 scripts/test-m10-d3-guest.py

# Full isolated D3 release gate: D3 real handoffs, D2 trap frames,
# D1 fairness and every M9/M10 regression from the previous acceptance
# suite. No physical disk, network or owner ISO side effects.
.PHONY: test-m10-d3-acceptance
test-m10-d3-acceptance: test-m10-d3-guest test-m10-d2-acceptance
	@echo SPIFFOS_M10_D3_ACCEPTANCE_OK

.PHONY: test-m10-d2-guest
test-m10-d2-guest:
	python3 scripts/test-m10-d2-guest.py

# Owner evening gate: one read-only, isolated end-to-end regression suite.
# Existing tests build throwaway QEMU images with -net none and temporary
# OVMF variables. This never installs or changes the Forge owner ISO.
.PHONY: test-m10-d2-acceptance
test-m10-d2-acceptance: test-m10-d1-priority test-m10-d2-guest test-memory test-keyboard test-irq0 test-idt-fault test-double-fault test-m10-b-host test-m10-b-guest test-m10-c-guest test-apic-readonly-all test-mmio
	@echo SPIFFOS_M10_D2_ACCEPTANCE_OK

build/apic-readonly.o: src/kernel/apic-readonly.c src/kernel/apic-readonly.h src/kernel/acpi-madt.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/apic-readonly.c -o $@

# Require identity/cache/alias proof in real QEMU at all three memory sizes.
.PHONY: test-apic-readonly-positive test-apic-readonly-all
test-apic-readonly-positive: iso
	python3 scripts/test-apic-readonly-positive.py

test-apic-readonly-all: test-apic-readonly test-apic-memory-type test-apic-readonly-positive test-apic-readonly-negative

# Negative read-only hardware gate runs only in disposable QEMU source.
.PHONY: test-apic-readonly-negative
test-apic-readonly-negative:
	python3 scripts/test-apic-readonly-negative.py

.PHONY: test-apic-readonly
test-apic-readonly: build/apic-readonly-test
	./build/apic-readonly-test

build/apic-readonly-test: tests/apic-readonly-test.c src/kernel/apic-readonly.c src/kernel/apic-readonly.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/apic-readonly-test.c src/kernel/apic-readonly.c -o $@

build/apic-memory-type.o: src/kernel/apic-memory-type.c src/kernel/apic-memory-type.h src/kernel/x86-page-walk.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/apic-memory-type.c -o $@

.PHONY: test-apic-memory-type
test-apic-memory-type: build/apic-memory-type-test
	./build/apic-memory-type-test

build/apic-memory-type-test: tests/apic-memory-type-test.c src/kernel/apic-memory-type.c src/kernel/apic-memory-type.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/apic-memory-type-test.c src/kernel/apic-memory-type.c -o $@

build/x86-mmio-map.o: src/kernel/x86-mmio-map.c src/kernel/x86-mmio-map.h src/kernel/mmio-page-policy.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/x86-mmio-map.c -o $@

# Require two real APIC MMIO translations at every supported QEMU memory size.
.PHONY: test-mmio-positive test-mmio
test-mmio-positive: iso
	python3 scripts/test-mmio-positive.py

test-mmio: test-x86-mmio-map test-mmio-page-policy test-mmio-positive test-mmio-negative

# Negative real-guest injection is built in a disposable source/ISO tree.
.PHONY: test-mmio-negative
test-mmio-negative:
	python3 scripts/test-mmio-negative.py

.PHONY: test-x86-mmio-map
test-x86-mmio-map: build/x86-mmio-map-test
	./build/x86-mmio-map-test

build/x86-mmio-map-test: tests/x86-mmio-map-test.c src/kernel/x86-mmio-map.c src/kernel/mmio-page-policy.c src/kernel/x86-page-walk.c Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/x86-mmio-map-test.c src/kernel/x86-mmio-map.c src/kernel/mmio-page-policy.c src/kernel/x86-page-walk.c -o $@

build/mmio-page-policy.o: src/kernel/mmio-page-policy.c src/kernel/mmio-page-policy.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/mmio-page-policy.c -o $@

.PHONY: test-mmio-page-policy
test-mmio-page-policy: build/mmio-page-policy-test
	./build/mmio-page-policy-test

build/mmio-page-policy-test: tests/mmio-page-policy-test.c src/kernel/mmio-page-policy.c src/kernel/mmio-page-policy.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/mmio-page-policy-test.c src/kernel/mmio-page-policy.c -o $@

build/x86-page-walk.o: src/kernel/x86-page-walk.c src/kernel/x86-page-walk.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/x86-page-walk.c -o $@

.PHONY: test-page-walk
test-page-walk: build/x86-page-walk-test
	./build/x86-page-walk-test

build/x86-page-walk-test: tests/x86-page-walk-test.c src/kernel/x86-page-walk.c src/kernel/x86-page-walk.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/x86-page-walk-test.c src/kernel/x86-page-walk.c -o $@

build/apic-preflight.o: src/kernel/apic-preflight.c src/kernel/apic-preflight.h src/kernel/acpi-madt.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/apic-preflight.c -o $@

.PHONY: test-apic-preflight
test-apic-preflight: build/apic-preflight-test
	./build/apic-preflight-test

build/apic-preflight-test: tests/apic-preflight-test.c src/kernel/apic-preflight.c src/kernel/apic-preflight.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Isrc/kernel tests/apic-preflight-test.c src/kernel/apic-preflight.c -o $@

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
$(KERNEL): build/main.o build/framebuffer.o build/text-console.o build/console-font.o build/boot-logo.o build/keyboard.o build/keyboard-ps2.o build/logical-line.o build/keyboard-console.o build/screen-editor.o build/memory-map.o build/memory-boot.o build/frame-allocator.o build/frame-allocate.o build/page-access.o build/kernel-heap.o build/memory-service.o build/workspace-manager.o build/apic-irq0.o build/apic-readonly.o build/apic-memory-type.o build/x86-mmio-map.o build/mmio-page-policy.o build/x86-page-walk.o build/apic-preflight.o build/acpi-madt.o build/acpi-discovery.o build/kernel-timer.o build/kernel-scheduler-clock.o build/x86-idt.o build/x86-tss.o build/x86-pic.o build/x86-irq0.o build/x86-exceptions.o $(M10B_TEST_OBJECTS) $(M10D2_TEST_OBJECTS) $(M10D3_TEST_OBJECTS) $(M10D4_TEST_OBJECTS) linker.ld Makefile
	$(LD) $(LDFLAGS) build/main.o build/framebuffer.o build/text-console.o build/console-font.o build/boot-logo.o build/keyboard.o build/keyboard-ps2.o build/logical-line.o build/keyboard-console.o build/screen-editor.o build/memory-map.o build/memory-boot.o build/frame-allocator.o build/frame-allocate.o build/page-access.o build/kernel-heap.o build/memory-service.o build/workspace-manager.o build/apic-irq0.o build/apic-readonly.o build/apic-memory-type.o build/x86-mmio-map.o build/mmio-page-policy.o build/x86-page-walk.o build/apic-preflight.o build/acpi-madt.o build/acpi-discovery.o build/kernel-timer.o build/kernel-scheduler-clock.o build/x86-idt.o build/x86-tss.o build/x86-pic.o build/x86-irq0.o build/x86-exceptions.o $(M10B_TEST_OBJECTS) $(M10D2_TEST_OBJECTS) $(M10D3_TEST_OBJECTS) $(M10D4_TEST_OBJECTS) -o $@

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

test-memory: test-apic-irq0 test-apic-readonly test-apic-memory-type test-x86-mmio-map test-mmio-page-policy test-page-walk test-apic-preflight test-acpi-discovery test-acpi-madt test-timer-memory test-pic-memory test-idt-memory test-kernel-result test-build-rules test-memory-service test-memory-map test-frame-allocator test-page-access test-kernel-heap iso
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

# M10-D1 isolated priority selection and starvation tests: hosted only.
# These pure policy objects are not linked into the default QEMU image.
.PHONY: test-m10-d1-priority
test-m10-d1-priority: build/m10-d1-priority-test
	./build/m10-d1-priority-test

build/m10-d1-priority-test: tests/kernel-priority-policy-test.c src/kernel/kernel-priority-policy.c src/kernel/kernel-priority-policy.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc/kernel tests/kernel-priority-policy-test.c src/kernel/kernel-priority-policy.c -o $@

# M10-A standalone scheduler state/policy: no IRQ or kernel linkage yet.
.PHONY: test-kernel-task
test-kernel-task: build/kernel-task-test
	./build/kernel-task-test

build/kernel-task-test: tests/kernel-task-test.c src/kernel/kernel-task.c src/kernel/kernel-task.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc/kernel tests/kernel-task-test.c src/kernel/kernel-task.c -o $@

# A's pure hosted checks run as part of the existing memory regression suite.
test-memory: test-kernel-task

# M10-B isolated first-run context proof. This is deliberately NOT part of
# default kernel linkage or M10-A's release gate yet.
build/x86-task-context.o: src/kernel/x86-task-context.c src/kernel/x86-task-context.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/x86-task-context.c -o $@

build/x86-task-switch.o: src/kernel/x86-task-switch.S Makefile
	mkdir -p build
	$(CC) --target=x86_64-unknown-none-elf -mno-red-zone -c src/kernel/x86-task-switch.S -o $@

build/m10-b-guest-smoke.o: src/kernel/m10-b-guest-smoke.c src/kernel/x86-task-context.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/m10-b-guest-smoke.c -o $@

.PHONY: test-m10-b-host
test-m10-b-host: build/m10-b-switch-host-test
	./build/m10-b-switch-host-test

build/m10-b-switch-host-test: tests/x86-task-switch-test.c tests/x86-task-register-check.S src/kernel/x86-task-context.c src/kernel/x86-task-context.h src/kernel/x86-task-switch.S Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O1 -g -mno-red-zone -fno-omit-frame-pointer -Isrc/kernel tests/x86-task-switch-test.c tests/x86-task-register-check.S src/kernel/x86-task-context.c src/kernel/x86-task-switch.S -o $@

# Pure task policy is linked only into the M10-B QEMU smoke candidate.
build/kernel-task.o: src/kernel/kernel-task.c src/kernel/kernel-task.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/kernel-task.c -o $@

# Build and run the M10-B guest only in a disposable child checkout/ISO.
.PHONY: test-m10-b-guest
test-m10-b-guest:
	python3 scripts/test-m10-b-guest.py

# M10-C bounded tick and pending-request policy. The ordinary kernel links
# only IRQ accounting and retains cooperative-only task execution.
build/kernel-scheduler-clock.o: src/kernel/kernel-scheduler-clock.c src/kernel/kernel-scheduler-clock.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel/kernel-scheduler-clock.c -o $@

.PHONY: test-scheduler-clock
test-scheduler-clock: build/kernel-scheduler-clock-test
	./build/kernel-scheduler-clock-test

build/kernel-scheduler-clock-test: tests/kernel-scheduler-clock-test.c src/kernel/kernel-scheduler-clock.c src/kernel/kernel-scheduler-clock.h Makefile
	mkdir -p build
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc/kernel tests/kernel-scheduler-clock-test.c src/kernel/kernel-scheduler-clock.c -o $@

test-memory: test-scheduler-clock

# Real APIC IRQ0 and safe-point reschedule request in disposable guests.
.PHONY: test-m10-c-guest
test-m10-c-guest:
	python3 scripts/test-m10-c-guest.py
