# =============================================================================
# Reproducible build environment for bare-metal RV64 / RVA23U64 subset.
#
# Provides:
#   - riscv-none-elf-gcc (xPack build, bare-metal toolchain, newlib-based)
#   - qemu-system-riscv64
#   - make
#
# Build & run:
#   docker build -t riscv-baremetal .
#   docker run --rm -it -v "$PWD:/work" riscv-baremetal make
#   docker run --rm -it -v "$PWD:/work" riscv-baremetal make run
# =============================================================================

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Base utilities + QEMU + objdump for the host (occasionally handy)
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        wget \
        xz-utils \
        tar \
        gzip \
        make \
        git \
        qemu-system-misc \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# xPack riscv-none-elf-gcc. Bump XPACK_VERSION when newer releases ship:
#   https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases
ARG XPACK_VERSION=14.2.0-3
ARG XPACK_URL=https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v${XPACK_VERSION}/xpack-riscv-none-elf-gcc-${XPACK_VERSION}-linux-x64.tar.gz

RUN mkdir -p /opt/riscv \
    && wget -qO /tmp/xpack.tar.gz "${XPACK_URL}" \
    && tar -xzf /tmp/xpack.tar.gz -C /opt/riscv --strip-components=1 \
    && rm /tmp/xpack.tar.gz

ENV PATH="/opt/riscv/bin:${PATH}"

# Sanity check at build time
RUN riscv-none-elf-gcc --version | head -1 \
    && qemu-system-riscv64 --version | head -1

# The repo root is mounted at /work so the build can reach ../portable; this
# leg lives in /work/riscv (peer of /work/m7).
WORKDIR /work/riscv
CMD ["make"]
