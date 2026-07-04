# =============================================================================
# Reproducible build environment for the Xtensa dc233c DSP-arm leg.
#
# Provides:
#   - xtensa-dc233c_zephyr-elf-gcc (Zephyr SDK toolchain, GCC 12 + picolibc)
#   - qemu-system-xtensa  (the `sim` machine speaks SIMCALL semihosting)
#   - make, python3
#
# The Zephyr SDK dc233c toolchain is the open-source-reachable way to get a
# dc233c-configured compiler without the Cadence config overlay. It ships
# picolibc with a semihosting crt0; we place its reset vector + window vectors
# ourselves (xtsim.ld) for QEMU's `-M sim` machine.
#
# Build & run (repo root mounted at /work; this leg lives in /work/xtensa):
#   docker build -t xtensa-baremetal xtensa
#   docker run --rm -v "$PWD:/work" -w /work/xtensa xtensa-baremetal make run
# =============================================================================
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates wget xz-utils tar make python3 \
        qemu-system-misc \
    && rm -rf /var/lib/apt/lists/*

# Zephyr SDK dc233c toolchain. Bump ZSDK_VERSION for newer releases:
#   https://github.com/zephyrproject-rtos/sdk-ng/releases
ARG ZSDK_VERSION=0.17.4
ARG ZSDK_URL=https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZSDK_VERSION}/toolchain_linux-x86_64_xtensa-dc233c_zephyr-elf.tar.xz

RUN mkdir -p /opt/xtensa \
    && wget -qO /tmp/xt.tar.xz "${ZSDK_URL}" \
    && tar -xf /tmp/xt.tar.xz -C /opt/xtensa \
    && rm /tmp/xt.tar.xz

ENV PATH="/opt/xtensa/xtensa-dc233c_zephyr-elf/bin:${PATH}"

# Sanity check at build time
RUN xtensa-dc233c_zephyr-elf-gcc --version | head -1 \
    && qemu-system-xtensa --version | head -1

WORKDIR /work/xtensa
CMD ["make", "run"]
