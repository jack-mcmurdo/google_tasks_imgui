FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libglfw3-dev \
    libssl-dev \
    git \
    && rm -rf /var/lib/apt/lists/*
