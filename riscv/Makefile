# =============================================================================
# Bare-metal RV64 / RVA23U64-subset Makefile
#
# Default cross-prefix is the bare-metal toolchain (riscv-none-elf-).
# Override on the command line for other toolchains:
#   make CROSS=riscv64-linux-gnu-       # Linux-targeted GCC (works for -nostdlib)
#   make CROSS=riscv64-unknown-elf-     # vanilla newlib
#
# Build outputs go to build/. `make run` launches QEMU.
# =============================================================================

# ----- Toolchain -------------------------------------------------------------
CROSS    ?= riscv-none-elf-
CC       := $(CROSS)gcc
OBJCOPY  := $(CROSS)objcopy
OBJDUMP  := $(CROSS)objdump
SIZE     := $(CROSS)size

# ----- Architecture ----------------------------------------------------------
# RVA23U64 practical bare-metal subset:
#   rv64i + m a f d c v          (mandatory in RVA23U64 baseline)
#   zicsr zifencei               (always-on in modern RV)
#   zba zbb zbs                  (bit-manip - mandatory in RVA23U64)
#   zfh                          (FP16 - in RVA23U64)
# Skipped vs full RVA23:
#   H, S, Sv39/Sv48              (hypervisor/supervisor/MMU - no bare-metal meaning)
#   Zicond, Zcb, Zcmop, etc.     (omit until your toolchain reliably has them)
#
# When you target real silicon, narrow or widen this as the core supports.
ARCH     ?= rv64imafdcv_zicsr_zifencei_zba_zbb_zbs_zfh
ABI      ?= lp64d
CMODEL   ?= medany

# ----- Flags -----------------------------------------------------------------
COMMON_FLAGS := -march=$(ARCH) -mabi=$(ABI) -mcmodel=$(CMODEL)

CFLAGS   := $(COMMON_FLAGS) \
            -ffreestanding -nostdlib -nostartfiles \
            -fno-builtin -fno-common \
            -fno-stack-protector \
            -fno-pic -fno-pie \
            -ffunction-sections -fdata-sections \
            -Os -g3 \
            -Wall -Wextra \
            -Iinclude \
            $(EXTRA_CFLAGS)

ASFLAGS  := $(COMMON_FLAGS) -fno-pic -fno-pie -g3

LDFLAGS  := $(COMMON_FLAGS) \
            -static -no-pie \
            -nostdlib -nostartfiles \
            -Wl,--gc-sections \
            -Wl,--build-id=none \
            -Wl,--print-memory-usage \
            -Wl,-Map=build/hello.map \
            -T link.ld

# ----- Files -----------------------------------------------------------------
SRCS_C  := main.c
SRCS_S  := crt0.S trap.S
OBJS    := $(addprefix build/, $(SRCS_C:.c=.o) $(SRCS_S:.S=.o))

TARGET  := build/hello

# ----- Rules -----------------------------------------------------------------
.PHONY: all run test test-negative portable portable-run hazards dsp dsp-run clean dump

all: $(TARGET).elf $(TARGET).bin $(TARGET).dis

build:
	@mkdir -p build

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.S | build
	$(CC) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS) link.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	@$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).dis: $(TARGET).elf
	$(OBJDUMP) -d -S $< > $@

dump: $(TARGET).dis
	@less $(TARGET).dis

# QEMU virt: bare-metal, our ELF is the firmware (no OpenSBI), one hart,
# vector unit on with VLEN=128, bit-manip extensions on, FP16 on.
# Use Ctrl-A X to exit QEMU.
run: $(TARGET).elf
	qemu-system-riscv64 \
	    -M virt \
	    -cpu rv64,v=true,vlen=128,zba=true,zbb=true,zbs=true,zfh=true \
	    -smp 1 \
	    -m 8M \
	    -bios none \
	    -kernel $(TARGET).elf \
	    -nographic

# Same as `run` but expects the guest to drive QEMU's sifive_test device
# at 0x100000 for a real exit code. 10s wall-clock guard against hangs.
test: $(TARGET).elf
	@echo "=== running self-test in QEMU ==="
	@timeout 10 qemu-system-riscv64 \
	    -M virt \
	    -cpu rv64,v=true,vlen=128,zba=true,zbb=true,zbs=true,zfh=true \
	    -smp 1 \
	    -m 8M \
	    -bios none \
	    -kernel $(TARGET).elf \
	    -nographic ; \
	rc=$$? ; \
	if [ $$rc -eq 0 ]; then \
	    echo "=== TEST PASS (qemu exit 0) ===" ; \
	elif [ $$rc -eq 124 ]; then \
	    echo "=== TEST TIMEOUT (no sifive_test exit within 10s) ===" ; \
	    exit 1 ; \
	else \
	    echo "=== TEST FAIL (qemu exit $$rc) ===" ; \
	    exit $$rc ; \
	fi

# Negative-path check: rebuild with an injected failure and assert that the
# suite reports a NON-zero exit. Proves the fail->exit-code path works.
test-negative:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory EXTRA_CFLAGS=-DSELFTEST_INJECT_FAIL >/dev/null
	@echo "=== running NEGATIVE self-test (expect non-zero exit) ==="
	@timeout 10 qemu-system-riscv64 \
	    -M virt \
	    -cpu rv64,v=true,vlen=128,zba=true,zbb=true,zbs=true,zfh=true \
	    -smp 1 \
	    -m 8M \
	    -bios none \
	    -kernel $(TARGET).elf \
	    -nographic ; \
	rc=$$? ; \
	if [ $$rc -ne 0 ] && [ $$rc -ne 124 ]; then \
	    echo "=== negative-path OK: suite reported failure (qemu exit $$rc) ===" ; \
	else \
	    echo "=== negative-path BROKEN: expected non-zero, got $$rc ===" ; \
	    $(MAKE) --no-print-directory clean ; \
	    exit 1 ; \
	fi
	@$(MAKE) --no-print-directory clean

# ----- Portable cross-target probe (shares ../portable/ with the M7 leg) -----
# Built at -O2 (matching m7/Makefile) so the comparison isolates target/ABI
# differences rather than optimization-flag differences. Probe sets are globbed
# from ../portable/probes (clean) and ../portable/hazards: drop a new .c into
# either dir and it is auto-discovered via the REGISTER_PROBE registry.
# (Cross-target orchestration lives in the repo-root Makefile / compare/.)
PORTABLE_CFLAGS := $(CFLAGS) -O2 -I../portable
PROBE_SRCS  := $(wildcard ../portable/probes/*.c)
HAZARD_SRCS := $(wildcard ../portable/hazards/*.c)
PROBE_OBJS  := $(patsubst ../portable/probes/%.c,build/probes_%.o,$(PROBE_SRCS))
HAZARD_OBJS := $(patsubst ../portable/hazards/%.c,build/hazards_%.o,$(HAZARD_SRCS))

PORTABLE_ELF := build/portable.elf

build/portable_riscv.o: portable_riscv.c | build
	$(CC) $(PORTABLE_CFLAGS) -c $< -o $@
build/probe_runner.o: ../portable/probe_runner.c | build
	$(CC) $(PORTABLE_CFLAGS) -c $< -o $@
build/probes_%.o: ../portable/probes/%.c | build
	$(CC) $(PORTABLE_CFLAGS) -c $< -o $@
build/hazards_%.o: ../portable/hazards/%.c | build
	$(CC) $(PORTABLE_CFLAGS) -c $< -o $@

$(PORTABLE_ELF): build/portable_riscv.o build/probe_runner.o $(PROBE_OBJS) build/crt0.o build/trap.o link.ld
	$(CC) $(LDFLAGS) build/portable_riscv.o build/probe_runner.o $(PROBE_OBJS) build/crt0.o build/trap.o -o $@
	@$(SIZE) $@

portable: $(PORTABLE_ELF)

HAZARD_ELF := build/hazards.elf

$(HAZARD_ELF): build/portable_riscv.o build/probe_runner.o $(HAZARD_OBJS) build/crt0.o build/trap.o link.ld
	$(CC) $(LDFLAGS) build/portable_riscv.o build/probe_runner.o $(HAZARD_OBJS) build/crt0.o build/trap.o -o $@
	@$(SIZE) $@

hazards: $(HAZARD_ELF)

portable-run: $(PORTABLE_ELF)
	@timeout 10 qemu-system-riscv64 \
	    -M virt \
	    -cpu rv64,v=true,vlen=128,zba=true,zbb=true,zbs=true,zfh=true \
	    -smp 1 \
	    -m 8M \
	    -bios none \
	    -kernel $(PORTABLE_ELF) \
	    -nographic

# ----- DSP kernel equivalence harness (the DSP arm, RVV side) ----------------
# Runs the shared ../harness/run_kernels.c against ../kernels/golden/*.txt with
# the RVV intrinsic kernels (-DHAVE_RVV). This is the open-source-reachable
# accelerator side of the DSP arm (HiFi is the same harness behind the firewall).
# Bare-metal: crt0_dsp.S forwards main()'s exit code to sifive_test, and dsp_rt.c
# supplies a tiny printf-over-UART so run_kernels.c stays unmodified + neutral.
DSP_ARCH   ?= $(ARCH)
DSP_CFLAGS := -march=$(DSP_ARCH) -mabi=$(ABI) -mcmodel=$(CMODEL) \
              -ffreestanding -nostdlib -nostartfiles \
              -fno-builtin -fno-common -fno-stack-protector -fno-pic -fno-pie \
              -ffunction-sections -fdata-sections \
              -O2 -g3 -Wall -Wextra \
              -I../kernels -I../harness -DHAVE_RVV $(EXTRA_CFLAGS)
DSP_LDFLAGS := -march=$(DSP_ARCH) -mabi=$(ABI) -mcmodel=$(CMODEL) \
              -static -no-pie -nostdlib -nostartfiles \
              -Wl,--gc-sections -Wl,--build-id=none \
              -Wl,-Map=build/dsp.map -T link.ld

DSP_OBJS := build/crt0_dsp.o build/trap.o build/dsp_rt.o \
            build/dsp_run_kernels.o build/dsp_kernels_scalar.o build/dsp_kernels_rvv.o

# golden_data.h is generated from the committed golden/*.txt (gitignored).
../harness/golden_data.h: $(wildcard ../kernels/golden/*.txt) ../harness/golden_to_header.py
	python3 ../harness/golden_to_header.py ../kernels/golden ../harness/golden_data.h

build/dsp_rt.o: dsp_rt.c | build
	$(CC) $(DSP_CFLAGS) -c $< -o $@
build/dsp_run_kernels.o: ../harness/run_kernels.c ../harness/golden_data.h | build
	$(CC) $(DSP_CFLAGS) -c $< -o $@
build/dsp_kernels_scalar.o: ../kernels/dsp_kernels_scalar.c | build
	$(CC) $(DSP_CFLAGS) -c $< -o $@
build/dsp_kernels_rvv.o: ../kernels/dsp_kernels_rvv.c | build
	$(CC) $(DSP_CFLAGS) -c $< -o $@

build/dsp.elf: $(DSP_OBJS) link.ld
	$(CC) $(DSP_LDFLAGS) $(DSP_OBJS) -o $@
	@$(SIZE) $@

dsp: build/dsp.elf

# Run the DSP equivalence harness; exit code 0 == every RVV kernel bit-exact.
dsp-run: build/dsp.elf
	@echo "=== DSP kernel equivalence (RVV) in QEMU ==="
	@timeout 15 qemu-system-riscv64 \
	    -M virt \
	    -cpu rv64,v=true,vlen=128,zba=true,zbb=true,zbs=true,zfh=true \
	    -smp 1 -m 8M -bios none -kernel build/dsp.elf -nographic ; \
	rc=$$? ; \
	if [ $$rc -eq 0 ]; then echo "=== DSP RVV PASS (qemu exit 0) ===" ; \
	elif [ $$rc -eq 124 ]; then echo "=== DSP TIMEOUT ===" ; exit 1 ; \
	else echo "=== DSP RVV FAIL (qemu exit $$rc) ===" ; exit $$rc ; fi

clean:
	rm -rf build
