FROM ubuntu:22.04

LABEL maintainer="group49"
LABEL description=" Cross-Platform Embedded"

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    python3 \
    build-essential \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    gcc-arm-linux-gnueabi \
    gdb-multiarch \
    qemu-user \
    qemu-system-arm \
    make \
    ca-certificates \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* /usr/share/doc /usr/share/man

WORKDIR /app

#CMD ["make"]
