# =============================================================================
# Cortex-M7 leg of the M7 <-> RISC-V portability comparison.
#
# Provides:
#   - arm-none-eabi-gcc (bare-metal ARM toolchain, newlib)
#   - qemu-system-arm    (runs the QEMU mps2-an500 = Cortex-M7 machine)
#   - make
#
# Build & run (from the m7/ dir, with the repo root mounted at /work):
#   docker build -t m7-baremetal m7
#   docker run --rm -v "$PWD:/work" -w /work/m7 m7-baremetal make
#   docker run --rm -v "$PWD:/work" -w /work/m7 m7-baremetal make run
# =============================================================================
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-arm-none-eabi \
        qemu-system-arm \
        make \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN arm-none-eabi-gcc --version | head -1 \
    && qemu-system-arm --version | head -1

WORKDIR /work/m7
CMD ["make"]
