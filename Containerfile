# syntax=docker/dockerfile:1.6

ARG BASE_IMAGE=ubuntu:24.04
ARG JOBS=8


#########################################
####    Stage 1: Bootstrap LLVM      ####
#########################################
FROM ${BASE_IMAGE} AS llvm
ARG JOBS
ARG NINJA_VERSION=1.13.1
ARG LLVM_VERSION=21.1.0
ENV DEBIAN_FRONTEND=noninteractive

# Seed toolchain + build deps
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        python3 \
        cmake \
        pkg-config \
        zlib1g-dev \
        libzstd-dev \
        libxml2-dev \
        libedit-dev \
        libncurses5-dev \
        libtinfo-dev \
        clang lld libc++-dev libc++abi-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp/build

# Build pinned Ninja from source
RUN curl -fL -o ninja.tar.gz \
        "https://github.com/ninja-build/ninja/archive/refs/tags/v${NINJA_VERSION}.tar.gz" \
    && tar -xzf ninja.tar.gz \
    && rm ninja.tar.gz \
    && cd "ninja-${NINJA_VERSION}" \
    && python3 configure.py --bootstrap \
    && install -m 0755 ninja /usr/bin/ninja \
    && ninja --version

# Fetch LLVM source tarball
RUN curl -fL -o llvm.tar.gz \
        "https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-${LLVM_VERSION}.tar.gz" \
    && tar -xzf llvm.tar.gz \
    && rm llvm.tar.gz \
    && mv "llvm-project-llvmorg-${LLVM_VERSION}" llvm-project

# Bootstrap LLVM into /opt/llvm using seed clang explicitly
RUN set -eux; \
    SRC="/tmp/build/llvm-project/llvm"; \
    BLD="/tmp/build/llvm-build"; \
    INST="/opt/llvm"; \
    mkdir -p "$BLD"; \
    cmake -G Ninja \
        -S "$SRC" -B "$BLD" \
        -DCMAKE_INSTALL_PREFIX="$INST" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_LINKER=ld.lld \
        -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb" \
        -DLLVM_ENABLE_RUNTIMES="compiler-rt;libc;libcxx;libcxxabi;libunwind;openmp" \
        -DCLANG_ENABLE_BOOTSTRAP=ON \
        -DCLANG_BOOTSTRAP_PASSTHROUGH="CMAKE_INSTALL_PREFIX;CMAKE_BUILD_TYPE;LLVM_PARALLEL_LINK_JOBS;LLVM_INCLUDE_EXAMPLES;LLVM_INCLUDE_TESTS;LLVM_INCLUDE_BENCHMARKS;CMAKE_C_COMPILER;CMAKE_CXX_COMPILER;CMAKE_LINKER" \
        -DLLVM_PARALLEL_LINK_JOBS=1 \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        \
        # stage 1.1: build clang + runtimes using stage 0 compiler
        -DLLVM_TARGETS_TO_BUILD=Native \
        \
        # stage 1.2: rebuild using stage 1.1 clang + runtimes
        -DBOOTSTRAP_LLVM_APPEND_VC_REV=OFF \
        -DBOOTSTRAP_LLVM_TARGETS_TO_BUILD=all \
        -DBOOTSTRAP_LLVM_BUILD_LLVM_DYLIB=ON \
        -DBOOTSTRAP_LLVM_LINK_LLVM_DYLIB=ON \
        -DBOOTSTRAP_CLANG_LINK_CLANG_DYLIB=ON \
        -DBOOTSTRAP_LLVM_ENABLE_EH=ON \
        -DBOOTSTRAP_LLVM_ENABLE_PIC=ON \
        -DBOOTSTRAP_LLVM_ENABLE_FFI=ON \
        -DBOOTSTRAP_LLVM_ENABLE_RTTI=ON \
        -DBOOTSTRAP_LLVM_ENABLE_LTO=Thin \
        -DBOOTSTRAP_LLVM_ENABLE_LLD=ON \
        -DBOOTSTRAP_LLVM_ENABLE_LIBCXX=ON \
        -DBOOTSTRAP_CLANG_DEFAULT_CXX_STDLIB=libc++ \
        -DBOOTSTRAP_CLANG_DEFAULT_LINKER=lld \
        -DBOOTSTRAP_CLANG_DEFAULT_RTLIB=compiler-rt \
        -DBOOTSTRAP_CLANG_DEFAULT_UNWINDLIB=libunwind \
        -DBOOTSTRAP_COMPILER_RT_USE_BUILTINS_LIBRARY=ON \
        -DBOOTSTRAP_LIBUNWIND_USE_COMPILER_RT=YES \
        -DBOOTSTRAP_LIBCXX_USE_COMPILER_RT=YES \
        -DBOOTSTRAP_LIBCXXABI_USE_COMPILER_RT=YES \
        -DBOOTSTRAP_LIBCXXABI_USE_LLVM_UNWINDER=YES \
        -DBOOTSTRAP_LIBCXX_INSTALL_MODULES=ON; \
    \
    # build stage 1.1
    ninja -C "$BLD" clang-bootstrap-deps -j${JOBS}; \
    \
    # stage 1.1.5: forward correct paths of stage 1 runtimes to stage 2 build
    HOST_TARGET="$("$BLD/bin/llvm-config" --host-target)"; \
    STAGE1_LIB="$BLD/lib"; \
    STAGE2_LIB="$BLD/tools/clang/stage2-bins/lib"; \
    LIBS="$STAGE2_LIB/$HOST_TARGET $STAGE2_LIB $STAGE1_LIB/$HOST_TARGET $STAGE1_LIB"; \
    export LDFLAGS="$(printf '%s ' $(for d in $LIBS; do echo -n "-L$d "; done))${LDFLAGS:-}"; \
    export LD_LIBRARY_PATH="$(printf '%s:' $LIBS)${LD_LIBRARY_PATH:-}"; \
    \
    # build stage 1.2
    ninja -C "$BLD" stage2 -j${JOBS}; \
    ninja -C "$BLD" stage2-install -j${JOBS}; \
    \
    # copy std modules to standardized location
    mkdir -p "$INST/modules/std/std" "$INST/modules/std.compat/std"; \
    cp -f "$INST/share/libc++/v1/std.cppm" "$INST/modules/std/std.cppm" || true; \
    cp -rf "$INST/share/libc++/v1/std" "$INST/modules/std/std" || true; \
    cp -f "$INST/share/libc++/v1/std.compat.cppm" "$INST/modules/std.compat/std/compat.cppm" || true; \
    cp -rf "$INST/share/libc++/v1/std.compat" "$INST/modules/std.compat/std/compat" || true; \
    \
    # sanity check
    "$INST/bin/clang" --version

# Configure dynamic linker to find LLVM libraries
RUN set -eux; \
    { \
        echo "/opt/llvm/lib"; \
        for d in /opt/llvm/lib/*; do \
            [ -d "$d" ] && echo "$d"; \
        done; \
    } > /etc/ld.so.conf.d/bertrand-llvm.conf; \
    ldconfig; \
    /opt/llvm/bin/clang --version


##############################
####    Stage 2: CMake    ####
##############################
FROM llvm AS cmake
ARG JOBS
ARG CMAKE_VERSION=4.2.1
ENV DEBIAN_FRONTEND=noninteractive

# Add build deps required by CMake bootstrap (OpenSSL headers)
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp/build

# Download and build CMake from source using the newly built LLVM toolchain
RUN set -eux; \
    curl -fL -o cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}.tar.gz"; \
    tar -xzf cmake.tar.gz; \
    rm cmake.tar.gz; \
    cd "cmake-${CMAKE_VERSION}"; \
    mkdir -p build; \
    cd build; \
    CC=/opt/llvm/bin/clang CXX=/opt/llvm/bin/clang++ \
        ../bootstrap \
            --prefix=/opt/cmake \
            --generator=Ninja; \
    ninja -j${JOBS}; \
    ninja install; \
    /opt/cmake/bin/cmake --version


###############################
####    Stage 3: Python    ####
###############################
FROM cmake AS python
ARG JOBS
ARG PYTHON_VERSION=3.12.4
ENV DEBIAN_FRONTEND=noninteractive

# Ensure toolchain is discoverable by configure probes (e.g., llvm-ar)
ENV PATH="/opt/llvm/bin:/opt/cmake/bin:${PATH}"

# Python build deps (CPython will silently disable modules if deps are missing)
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        # toolchain helpers
        make \
        perl \
        xz-utils \
        # core libs for stdlib modules
        libssl-dev \
        libffi-dev \
        zlib1g-dev \
        libbz2-dev \
        liblzma-dev \
        libreadline-dev \
        libsqlite3-dev \
        libgdbm-dev \
        libgdbm-compat-dev \
        libncursesw5-dev \
        uuid-dev \
        tk-dev \
        libexpat1-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp/build

# Fetch + build CPython using the bootstrapped LLVM + CMake toolchain
RUN set -eux; \
    curl -fL -o python.tar.xz \
        "https://www.python.org/ftp/python/${PYTHON_VERSION}/Python-${PYTHON_VERSION}.tar.xz"; \
    tar -xJf python.tar.xz; \
    rm python.tar.xz; \
    cd "Python-${PYTHON_VERSION}"; \
    mkdir -p build; \
    cd build; \
    \
    # Use the LLVM toolchain explicitly; keep link behavior consistent with the rest of the image
    export CC=/opt/llvm/bin/clang; \
    export CXX=/opt/llvm/bin/clang++; \
    export AR=/opt/llvm/bin/llvm-ar; \
    export RANLIB=/opt/llvm/bin/llvm-ranlib; \
    export LD=/opt/llvm/bin/ld.lld; \
    export LDFLAGS="-fuse-ld=lld"; \
    \
    ../configure \
        --prefix=/opt/python \
        --with-ensurepip=upgrade \
        --enable-optimizations \
        --with-lto \
        # --with-tail-call-interp \
        --enable-shared; \
    \
    make -j${JOBS}; \
    make install; \
    \
    # Configure dynamic linker to find Python libraries
    printf "%s\n" "/opt/python/lib" > /etc/ld.so.conf.d/bertrand-python.conf; \
    ldconfig; \
    \
    # Friendly entrypoints inside /opt/python
    ln -sf /opt/python/bin/python3 /opt/python/bin/python; \
    ln -sf /opt/python/bin/pip3 /opt/python/bin/pip; \
    /opt/python/bin/python --version; \
    /opt/python/bin/pip --version


#######################################
####    Stage 4: Python Tooling    ####
#######################################
FROM python AS python_tools
ARG UV_VERSION=0.5.31
ARG RUFF_VERSION=0.8.4
ARG TY_VERSION=0.0.1a1
ARG PYTEST_VERSION=8.3.4
ENV DEBIAN_FRONTEND=noninteractive
ENV UV_PYTHON=/opt/python/bin/python

# Install pinned Python tooling into /opt/python using uv as the primary installer.
RUN --mount=type=cache,target=/root/.cache/uv,sharing=locked \
    set -eux; \
    /opt/python/bin/pip install --no-cache-dir "uv==${UV_VERSION}"; \
    /opt/python/bin/uv --version; \
    /opt/python/bin/uv pip install --no-cache-dir \
        "ruff==${RUFF_VERSION}" \
        "ty==${TY_VERSION}" \
        "pytest==${PYTEST_VERSION}"; \
    /opt/python/bin/ruff --version; \
    /opt/python/bin/ty --version; \
    /opt/python/bin/pytest --version; \
    /opt/python/bin/python -m pip check


####################################
####    Stage 5: C++ Tooling    ####
####################################
FROM python_tools AS cpp_tools
ARG CONAN_VERSION=2.24.0
ARG CXX_STD=23
ENV DEBIAN_FRONTEND=noninteractive

# Ensure toolchain is discoverable by configure probes (e.g., llvm-ar)
ENV PATH="/opt/llvm/bin:/opt/cmake/bin:/opt/python/bin:${PATH}"

# Conan cache location (make it usable for arbitrary users later)
ENV CONAN_HOME=/opt/conan
ENV CONAN_USER_HOME=/opt/conan

# Install Conan into /opt/python using uv.
RUN --mount=type=cache,target=/root/.cache/uv,sharing=locked \
    set -eux; \
    /opt/python/bin/uv pip install --no-cache-dir \
        "conan==${CONAN_VERSION}"; \
    /opt/python/bin/conan --version; \
    /opt/python/bin/python -m pip check

# Create a sane default profile for clang/libc++ and Ninja (Conan 2.x)
RUN set -eux; \
    mkdir -p "$CONAN_HOME"; \
    chmod 1777 "$CONAN_HOME"; \
    \
    export CC=/opt/llvm/bin/clang; \
    export CXX=/opt/llvm/bin/clang++; \
    \
    /opt/python/bin/conan profile detect --force; \
    PROFILE="$(/opt/python/bin/conan profile path default)"; \
    echo "Conan default profile: $PROFILE"; \
    test -f "$PROFILE"; \
    \
    python3 - "$PROFILE" "${CXX_STD}" <<'PY' \
import re  # not actually imported for some reason
import sys
import pathlib

profile_path = sys.argv[1]
cxx_std = sys.argv[2].strip()

p = pathlib.Path(profile_path)
txt = p.read_text().splitlines()

def ensure_section(lines, name):
    hdr = f"[{name}]"
    if any(l.strip() == hdr for l in lines):
        return lines
    if lines and lines[-1].strip() != "":
        lines.append("")
    lines.append(hdr)
    return lines

def set_kv(lines, section, key, value):
    import re  # needed here for regex usage
    hdr = f"[{section}]"
    out = []
    in_sec = False
    saw = False

    for l in lines:
        s = l.strip()
        if s.startswith("[") and s.endswith("]"):
            if in_sec and not saw:
                out.append(f"{key}={value}")
                saw = True
            in_sec = (s == hdr)
            out.append(l)
            continue

        if in_sec and re.match(rf"^{re.escape(key)}\s*=", s):
            out.append(f"{key}={value}")
            saw = True
        else:
            out.append(l)

    if in_sec and not saw:
        out.append(f"{key}={value}")
    return out

txt = ensure_section(txt, "settings")
txt = ensure_section(txt, "conf")
txt = ensure_section(txt, "buildenv")
txt = set_kv(txt, "settings", "compiler.libcxx", "libc++")
txt = set_kv(txt, "settings", "build_type", "Release")
txt = set_kv(txt, "settings", "compiler.cppstd", cxx_std)
txt = set_kv(txt, "conf", "tools.cmake.cmaketoolchain:generator", "Ninja")
txt = set_kv(txt, "conf", "tools.build:compiler_executables",
             '{"c":"/opt/llvm/bin/clang","cpp":"/opt/llvm/bin/clang++"}')
txt = set_kv(txt, "buildenv", "CC", "ccache /opt/llvm/bin/clang")
txt = set_kv(txt, "buildenv", "CXX", "ccache /opt/llvm/bin/clang++")

p.write_text("\n".join(txt) + "\n")
print(p.read_text())
PY


#################################
####    Stage 6: Bertrand    ####
#################################
FROM cpp_tools AS bertrand_install
USER root

# Make user-installs impossible/ignored during image build
ENV HOME=/root \
    PYTHONNOUSERSITE=1 \
    PIP_USER=0 \
    PIP_DISABLE_PIP_VERSION_CHECK=1

WORKDIR /src
COPY --link pyproject.toml /src/
COPY --link bertrand /src/bertrand

RUN --mount=type=cache,target=/root/.cache/uv,sharing=locked \
    set -eux; \
    # install into /opt/python
    /opt/python/bin/uv pip install --no-cache-dir /src; \
    /opt/python/bin/python -m pip check; \
    \
    # Verify: console script must exist in the image
    test -x /opt/python/bin/bertrand; \
    /opt/python/bin/bertrand --help >/dev/null


###########################
####    Final Image    ####
###########################
FROM ${BASE_IMAGE} AS bertrand
ENV DEBIAN_FRONTEND=noninteractive

# Bertrand runs rootless containers on the host; container root does not imply
# host root escalation
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libedit2 \
        libtinfo6 \
        zlib1g \
        libzstd1 \
        libxml2 \
        libatomic1 \
        libssl3 \
        libffi8 \
        libbz2-1.0 \
        liblzma5 \
        libreadline8 \
        libsqlite3-0 \
        libgdbm6 \
        libgdbm-compat4 \
        libncursesw6 \
        tk \
        libexpat1 \
        ripgrep \
        git \
        ccache \
    && rm -rf /var/lib/apt/lists/*

# Bring in final toolchain
COPY --link --from=llvm /usr/bin/ninja /usr/bin/ninja
COPY --link --from=llvm /opt/llvm /opt/llvm
COPY --link --from=cmake /opt/cmake /opt/cmake
COPY --link --from=bertrand_install /opt/python /opt/python
COPY --link --from=cpp_tools /opt/conan /opt/conan
ENV CONAN_HOME=/opt/conan
ENV CONAN_USER_HOME=/opt/conan
ENV BERTRAND=1
ENV UV_PYTHON=/opt/python/bin/python
ENV UV_CACHE_DIR=/tmp/.cache/uv
ENV BERTRAND_CACHE=/tmp/.cache/bertrand
ENV CCACHE_DIR=/tmp/.cache/ccache
ENV PIP_DISABLE_PIP_VERSION_CHECK=1
ENV PATH="/opt/llvm/bin:/opt/cmake/bin:/opt/python/bin:${PATH}"

# Configure dynamic linker and add compatibility aliases for generic tool names
RUN set -eux; \
    # Dynamic linker search paths (LLVM + Python)
    { \
        echo "/opt/llvm/lib"; \
        for d in /opt/llvm/lib/*; do [ -d "$d" ] && echo "$d"; done; \
    } > /etc/ld.so.conf.d/bertrand-llvm.conf; \
    { \
        echo "/opt/python/lib"; \
        for d in /opt/python/lib/*; do [ -d "$d" ] && echo "$d"; done; \
    } > /etc/ld.so.conf.d/bertrand-python.conf; \
    ldconfig; \
    \
    # Generic compiler/linker aliases for ecosystem compatibility
    ln -sf /opt/llvm/bin/clang   /usr/bin/cc; \
    ln -sf /opt/llvm/bin/clang++ /usr/bin/c++; \
    ln -sf /opt/llvm/bin/ld.lld  /usr/bin/ld; \
    ln -sf /opt/llvm/bin/llvm-ar /usr/bin/ar; \
    ln -sf /opt/llvm/bin/llvm-nm /usr/bin/nm; \
    ln -sf /opt/llvm/bin/llvm-ranlib /usr/bin/ranlib

# Sanity check
RUN clang --version \
    && cc --version \
    && c++ --version \
    && clang-format --version \
    && clang-tidy --version \
    && (clangd --version || true) \
    && ccache --version \
    && ninja --version \
    && cmake --version \
    && python --version \
    && pip --version \
    && conan --version \
    && uv --version \
    && ruff --version \
    && ty --version \
    && pytest --version

# sleep infinity allows `bertrand enter` to keep the container alive indefinitely
ENTRYPOINT ["sleep", "infinity"]
