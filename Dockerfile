FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ============================================================
# SYSTEM BUILD DEPENDENCIES
# ============================================================

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    ca-certificates \
    autoconf \
    automake \
    libtool \
    python3 \
    perl \
    && rm -rf /var/lib/apt/lists/*


# ============================================================
# VCPKG
# ============================================================

WORKDIR /opt

RUN git clone --depth 1 https://github.com/microsoft/vcpkg.git

RUN /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

ENV VCPKG_ROOT=/opt/vcpkg


# ============================================================
# MX CENTRAL SOURCE
# ============================================================

WORKDIR /app

COPY . /app


# ============================================================
# BUILD
# ============================================================

RUN rm -rf \
    /app/build \
    /app/vcpkg_installed

RUN cmake \
    -S /app \
    -B /app/build/railway \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake

RUN cmake \
    --build /app/build/railway \
    --parallel


# ============================================================
# RUNTIME DIRECTORIES
# ============================================================

RUN mkdir -p \
    /app/data/database \
    /app/data/backups \
    /app/data/exports \
    /app/logs


# ============================================================
# START MX CENTRAL
# ============================================================

WORKDIR /app

CMD ["./build/railway/bin/MX_Central"]