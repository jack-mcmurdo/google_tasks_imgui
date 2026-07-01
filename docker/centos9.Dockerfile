FROM quay.io/centos/centos:stream9
RUN dnf install -y epel-release && \
    dnf groupinstall -y "Development Tools" && \
    dnf install -y \
    gcc-c++ \
    cmake \
    glfw-devel \
    openssl-devel \
    rpm-build \
    git && \
    dnf clean all
