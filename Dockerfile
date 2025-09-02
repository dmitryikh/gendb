FROM ubuntu:24.04

# Environment
ENV DEBIAN_FRONTEND=noninteractive

# Install essentials
RUN apt-get update && apt-get install -y \
    build-essential \
    clang \
    clang-tidy \
    clang-format \
    clangd \
    lldb \
    cmake \
    ninja-build \
    git \
    curl \
    python3 \
    python3-pip \
    python3-jinja2 \
    pipx \
    unzip \
    wget \
    pkg-config \
    sudo \
    fish \
    && rm -rf /var/lib/apt/lists/*

# FlatBuffers
RUN apt-get update && apt-get install -y libflatbuffers-dev flatbuffers-compiler

# User (so you don’t run as root inside container)
RUN useradd -m dev && echo "dev ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
USER dev

# Install pipx packages for the dev user
RUN pipx ensurepath \
    && pipx install conan

WORKDIR /workspace
