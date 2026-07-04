# =============================================================================
# Xtensa leg of the isa-portability-lab DSP arm.
#
# ONE vessel, three swappable args (see IMPLEMENTATION_SPEC / README): the open
# dc233c build here proves the harness + plumbing on a free core and is the
# portability baseline (a scalar controller core, NOT a DSP data point). The
# licensed HiFi build behind the firewall is the SAME vessel with XT_CC /
# XT_CORE / XT_RUN pointed at the HiFi toolchain and IMPL=dsp_kernels_hifi
# + HAVE=HAVE_HIFI - no structural change.
#
#   docker build -t xtensa-baremetal xtensa
#   docker run --rm -v "$PWD:/work" -w /work/xtensa xtensa-baremetal make run
# =============================================================================

# ----- Swappable toolchain args (open dc233c defaults) -----------------------
XT_CC    ?= xtensa-dc233c_zephyr-elf-gcc
XT_CORE  ?= dc233c
XT_RUN   ?= qemu-system-xtensa -M sim -cpu $(XT_CORE) -nographic -semihosting
# Under-test impl (../kernels/<IMPL>.c) + its accel macro:
#   open dc233c : dsp_kernels_baseline  (public k_* forward to the scalar _ref)
#   firewall    : IMPL=dsp_kernels_hifi HAVE=HAVE_HIFI
IMPL     ?= dsp_kernels_baseline
HAVE     ?=

# ----- GCC + picolibc semihosting recipe (open dc233c leg) -------------------
# The firewall xt-clang build overrides these with its LSP (e.g. -mlsp=sim).
XT_SPECS    ?= --specs=picolibc.specs -crt0=semihost -oslib=semihost
XT_LDSCRIPT ?= xtsim.ld

HAVE_DEF := $(if $(HAVE),-D$(HAVE),)

CFLAGS  := $(XT_SPECS) -O2 -g -ffreestanding -Wall -Wextra \
           -I../kernels -I../harness $(HAVE_DEF) $(EXTRA_CFLAGS)
LDFLAGS := $(XT_SPECS) -T $(XT_LDSCRIPT)

K := ../kernels
H := ../harness

OBJS := build/xt_rt.o build/run_kernels.o build/dsp_kernels_scalar.o build/$(IMPL).o
TARGET := build/dsp.elf

.PHONY: all run clean
all: $(TARGET)

build:
	@mkdir -p build

# golden_data.h is generated from the committed golden/*.txt (gitignored).
$(H)/golden_data.h: $(wildcard $(K)/golden/*.txt) $(H)/golden_to_header.py
	python3 $(H)/golden_to_header.py $(K)/golden $(H)/golden_data.h

# run_kernels.c is compiled with -Dmain=dsp_main; xt_rt.c supplies the real
# main() that runs it and forwards the exit code (see xt_rt.c).
build/run_kernels.o: $(H)/run_kernels.c $(H)/golden_data.h | build
	$(XT_CC) $(CFLAGS) -Dmain=dsp_main -c $< -o $@
build/xt_rt.o: xt_rt.c | build
	$(XT_CC) $(CFLAGS) -c $< -o $@
build/dsp_kernels_scalar.o: $(K)/dsp_kernels_scalar.c | build
	$(XT_CC) $(CFLAGS) -c $< -o $@
build/$(IMPL).o: $(K)/$(IMPL).c | build
	$(XT_CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(XT_LDSCRIPT)
	$(XT_CC) $(LDFLAGS) $(OBJS) -o $@
	@$(XT_CC:gcc=size) $@ 2>/dev/null || true

# Run the DSP equivalence harness; exit code 0 == every kernel bit-exact.
run: $(TARGET)
	@echo "=== DSP kernel equivalence ($(XT_CORE)) in QEMU ==="
	@timeout 20 $(XT_RUN) -kernel $(TARGET) ; \
	rc=$$? ; \
	if [ $$rc -eq 0 ]; then echo "=== $(XT_CORE) PASS (qemu exit 0) ===" ; \
	elif [ $$rc -eq 124 ]; then echo "=== TIMEOUT ===" ; exit 1 ; \
	else echo "=== $(XT_CORE) FAIL (qemu exit $$rc) ===" ; exit $$rc ; fi

clean:
	rm -rf build
