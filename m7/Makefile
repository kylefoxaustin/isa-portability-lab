# =============================================================================
# Cortex-M7 build for the portability comparison. Compiles the SHARED
# ../portable/kernels.c (identical source to the RISC-V leg) at -O2, links a
# tiny bare-metal image for QEMU's mps2-an500 (Cortex-M7) machine, and runs it
# with semihosting for stdout + exit.
# =============================================================================
CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
SIZE    := $(CROSS)size

# Cortex-M7 with the FPv5 double-precision FPU, hard float ABI.
MCPU    := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard

CFLAGS  := $(MCPU) \
           -ffreestanding -nostdlib -nostartfiles \
           -fno-builtin -fno-common -fno-stack-protector \
           -ffunction-sections -fdata-sections \
           -O2 -g3 -Wall -Wextra \
           -I. -I../portable

LDFLAGS := $(MCPU) \
           -nostdlib -nostartfiles \
           -Wl,--gc-sections \
           -T link.ld

# Probe sets globbed from the shared portable/ tree (auto-discovers new files).
PROBE_SRCS  := $(wildcard ../portable/probes/*.c)
HAZARD_SRCS := $(wildcard ../portable/hazards/*.c)
PROBE_OBJS  := $(patsubst ../portable/probes/%.c,build/probes_%.o,$(PROBE_SRCS))
HAZARD_OBJS := $(patsubst ../portable/hazards/%.c,build/hazards_%.o,$(HAZARD_SRCS))

OBJS := build/startup.o build/main.o build/probe_runner.o $(PROBE_OBJS)
TARGET := build/m7.elf

.PHONY: all run test hazards clean
all: $(TARGET)

build:
	@mkdir -p build

build/startup.o: startup.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build/main.o: main.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build/probe_runner.o: ../portable/probe_runner.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build/probes_%.o: ../portable/probes/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build/hazards_%.o: ../portable/hazards/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) link.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	@$(SIZE) $@

# Hazard probe leg (shares ../portable/hazards/ with the RISC-V build).
build/m7_hazards.elf: build/startup.o build/main.o build/probe_runner.o $(HAZARD_OBJS) link.ld
	$(CC) $(LDFLAGS) build/startup.o build/main.o build/probe_runner.o $(HAZARD_OBJS) -o $@
	@$(SIZE) $@

hazards: build/m7_hazards.elf

# mps2-an500 = Cortex-M7. Semihosting routes SYS_WRITEC to stdout and lets the
# guest exit with a real status code.
run: $(TARGET)
	qemu-system-arm -M mps2-an500 -semihosting -nographic -kernel $(TARGET)

test: $(TARGET)
	@timeout 10 qemu-system-arm -M mps2-an500 -semihosting -nographic -kernel $(TARGET) ; \
	rc=$$? ; echo "qemu exit: $$rc" ; exit $$rc

clean:
	rm -rf build
