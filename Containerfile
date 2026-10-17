# Build/dev environment for the MoH static recompilation.
# Use through ./dev, which mounts the repo at /work and runs as your own user.
FROM registry.fedoraproject.org/fedora:44

RUN dnf install -y --setopt=install_weak_deps=False \
        gcc gcc-c++ clang lld \
        cmake ninja-build make \
        git python3 python3-pip \
        binutils-mips64-linux-gnu gcc-mips64-linux-gnu \
        SDL3-devel \
        gdb \
    && dnf clean all

COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir --root-user-action=ignore -r /tmp/requirements.txt && rm /tmp/requirements.txt

WORKDIR /work
