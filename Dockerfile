# Base image
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    git \
    libsnappy-dev \
    zlib1g-dev \
    libbz2-dev \
    liblz4-dev \
    libzstd-dev \
    libgflags-dev \
    && rm -rf /var/lib/apt/lists/*

# Set working directory to where code will be mounted
WORKDIR /rocksdb

# Default command
CMD ["/bin/bash"]