FROM mcr.microsoft.com/devcontainers/base:ubuntu-22.04

ARG DEBIAN_FRONTEND=noninteractive
ARG PICO_SDK_VERSION
ARG TINYUSB_VERSION

RUN if [ -z "$PICO_SDK_VERSION" ]; then \
        echo "PICO_SDK_VERSION is required. Set it in .devcontainer/devcontainer.json under build.args."; \
        exit 1; \
    fi

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        git \
        ca-certificates \
        cmake \
        ninja-build \
        build-essential \
        gdb-multiarch \
        python3 \
        python3-pip \
        pkg-config \
        libusb-1.0-0-dev \
        wget \
        xz-utils \
        libncurses5 \
    && if [ "$(dpkg --print-architecture)" = "amd64" ]; then \
        apt-get install -y --no-install-recommends libc6-i386; \
    fi \
    && rm -rf /var/lib/apt/lists/*

# Install ARM GCC toolchain 14.2.Rel1 to match typical macOS setup
RUN if [ "$(dpkg --print-architecture)" = "arm64" ]; then \
        TOOLCHAIN_URL="https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-aarch64-arm-none-eabi.tar.xz"; \
    else \
        TOOLCHAIN_URL="https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz"; \
    fi \
    && wget -q "$TOOLCHAIN_URL" -O /tmp/arm-toolchain.tar.xz \
    && mkdir -p /opt/arm-toolchain \
    && tar -xf /tmp/arm-toolchain.tar.xz -C /opt/arm-toolchain --strip-components=1 \
    && rm /tmp/arm-toolchain.tar.xz

ENV PATH="/opt/arm-toolchain/bin:${PATH}"

RUN git clone --depth 1 --branch ${PICO_SDK_VERSION} https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk \
    && git -C /opt/pico-sdk submodule update --init --recursive

# Pin TinyUSB to a known-good tag for Workshop_Computer releases
RUN if [ -n "$TINYUSB_VERSION" ]; then \
        git -C /opt/pico-sdk/lib/tinyusb fetch --tags \
        && git -C /opt/pico-sdk/lib/tinyusb checkout ${TINYUSB_VERSION}; \
    fi

# Make the SDK path available during the picotool build.
ENV PICO_SDK_PATH=/opt/pico-sdk

# Note: the MAKEFILES auto-include (scripts/pico_auto_make.mk) is set via
# devcontainer.json's remoteEnv using ${containerWorkspaceFolder}, so the path
# adapts to the workspace folder name instead of being baked into the image.

# Install picotool so the Pico SDK build doesn't need to fetch it.
# Match the SDK version by default.
RUN git clone --depth 1 --branch ${PICO_SDK_VERSION} https://github.com/raspberrypi/picotool.git /tmp/picotool \
    && cmake -S /tmp/picotool -B /tmp/picotool/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/picotool/build \
    && cmake --install /tmp/picotool/build \
    && rm -rf /tmp/picotool
