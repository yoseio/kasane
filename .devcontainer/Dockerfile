FROM debian:bookworm

ARG DEBIAN_FRONTEND=noninteractive
ARG SOUFFLE_VERSION=2.5
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=${USER_UID}

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash-completion \
    bison \
    build-essential \
    ca-certificates \
    clang \
    clangd \
    clang-format \
    clang-tidy \
    ccache \
    cmake \
    curl \
    doxygen \
    file \
    flex \
    git \
    jq \
    less \
    libclang-cpp-dev \
    libclang-dev \
    libclang-rt-dev \
    libffi-dev \
    libncurses-dev \
    libomp-dev \
    libsqlite3-dev \
    llvm-dev \
    mcpp \
    ninja-build \
    pkg-config \
    procps \
    python-is-python3 \
    python3 \
    python3-pip \
    ripgrep \
    sqlite3 \
    sudo \
    unzip \
    wget \
    zip \
    zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

RUN groupadd --gid "${USER_GID}" "${USERNAME}" \
 && useradd --uid "${USER_UID}" --gid "${USER_GID}" -m -s /bin/bash "${USERNAME}" \
 && usermod -aG sudo "${USERNAME}" \
 && echo "${USERNAME} ALL=(root) NOPASSWD:ALL" > "/etc/sudoers.d/${USERNAME}" \
 && chmod 0440 "/etc/sudoers.d/${USERNAME}" \
 && install -d -o "${USERNAME}" -g "${USERNAME}" \
      "/home/${USERNAME}/.cache/ccache" \
      "/home/${USERNAME}/.cargo" \
      "/home/${USERNAME}/.rustup"

RUN set -eux; \
    mkdir -p /usr/local/lib/cmake; \
    llvm_cmake_dir="$(llvm-config --cmakedir)"; \
    clang_cmake_dir="$(dirname "$(find /usr/lib -name ClangConfig.cmake -print -quit)")"; \
    ln -s "${llvm_cmake_dir}" /usr/local/lib/cmake/LLVM; \
    ln -s "${clang_cmake_dir}" /usr/local/lib/cmake/Clang; \
    printf 'export LLVM_DIR=%s\nexport Clang_DIR=%s\n' "${llvm_cmake_dir}" "${clang_cmake_dir}" > /etc/profile.d/kasane-llvm-clang.sh

RUN git clone --depth 1 --branch "${SOUFFLE_VERSION}" https://github.com/souffle-lang/souffle.git /tmp/souffle \
 && cmake -S /tmp/souffle -B /tmp/souffle/build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DSOUFFLE_DOMAIN_64BIT=ON \
      -DSOUFFLE_SWIG=OFF \
 && cmake --build /tmp/souffle/build --parallel \
 && cmake --install /tmp/souffle/build \
 && rm -rf /tmp/souffle \
 && ldconfig

USER ${USERNAME}

ENV CARGO_HOME=/home/${USERNAME}/.cargo \
    RUSTUP_HOME=/home/${USERNAME}/.rustup \
    PATH=/home/${USERNAME}/.cargo/bin:${PATH}

RUN curl https://sh.rustup.rs -sSf | sh -s -- -y --profile minimal --default-toolchain stable \
 && rustup component add rust-analyzer rust-src rustfmt clippy
