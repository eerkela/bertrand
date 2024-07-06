"""Setup script for Bertrand."""
import os
from pathlib import Path
import subprocess

from bertrand import BuildExt, Extension, setup, env


# TODO: for headless installation, use pipx to install bertrand:

# See: https://devguide.python.org/getting-started/setup-building/#install-dependencies

# sudo apt update
# sudo apt install build-essential pipx cmake autoconf gdb lcov pkg-config \
#     libbz2-dev libffi-dev libgdbm-dev libgdbm-compat-dev liblzma-dev \
#     libncurses5-dev libreadline6-dev libsqlite3-dev libssl-dev lzma lzma-dev \
#     tk-dev uuid-dev zlib1g-dev
# sudo pipx ensurepath --global
# pipx install bertrand
# bertrand init



# sudo apt build-dep python3
# sudo apt install pkg-config






# NOTE: C++ users have to execute $(bertrand -I) to compile against bertrand.h
# g++ foo.cpp -o foo.out $(bertrand -I)

# NOTE: Python users writing C++ extensions should use bertrand's custom Extension
# class to add the necessary linkages to the bertrand library, and to enable clangd
# support through compile_commands.json.


# NOTE: Bertrand uses C++23 features only found in GCC 14+ and Clang 18+ (MSVC is not
# yet fully supported).  Users must first install these compilers to use the library.
# Here are the steps to do so:


# pylint: disable=pointless-string-statement

"""
GCC: If you have Ubuntu 24.04 or later, then you can install gcc-14 directly using the
package manager:
    sudo apt-get install build-essential gcc-14 g++-14 python3-dev

Otherwise:

1. set up local build variables:
    root=$(realpath $(pwd))
    gcc_source=${root}/gcc-source
    gcc_build=${root}/gcc-build
    gcc_install=${root}/gcc-install
    cpp_libs=${gcc_install}/lib64
2. clone the git repository to a local directory:
    git clone git://gcc.gnu.org/git/gcc.git $gcc_source
    cd $gcc_source
3. checkout a GCC 14+ release branch:
    git checkout releases/gcc-14
4. download prerequisites
    ./contrib/download_prerequisites
5. create a local build directory and configure the build:
    mkdir $gcc_build
    cd $gcc_build
    ${gcc_source}/configure --prefix=${gcc_install} --enable-languages=c,c++ \
        --enable-shared --disable-werror --disable-multilib --disable-bootstrap
6. run the build (takes ~15 minutes):
    make -j$(nproc)
7. install:
    make install
8. (optional) update alternatives and set as default compiler:
    sudo update-alternatives --install /usr/bin/gcc gcc ${gcc_install}/bin/gcc 14 \
        --slave /usr/bin/g++ g++ ${gcc_install}/bin/g++ \
        --slave /usr/bin/c++ c++ ${gcc_install}/bin/c++ \
        --slave /usr/bin/cpp cpp ${gcc_install}/bin/cpp \
        --slave /usr/bin/gcc-ar gcc-ar ${gcc_install}/bin/gcc-ar \
        --slave /usr/bin/gcc-nm gcc-nm ${gcc_install}/bin/gcc-nm \
        --slave /usr/bin/gcc-ranlib gcc-ranlib ${gcc_install}/bin/gcc-ranlib \
        --slave /usr/bin/gcov gcov ${gcc_install}/bin/gcov
9. Check the version:
    gcc --version

Clang:

1. set up local build variables:
    root=$(realpath $(pwd))
    clang_source=${root}/clang-source
    clang_build=${root}/clang-build
    clang_install=${root}/clang-install
    cpp_libs=${clang_install}/lib64  # TODO: confirm this path
1. clone the git repository to a local directory:
    git clone https://github.com/llvm/llvm-project.git $clang_source
    cd $clang_source
2. checkout the clang 18 release branch:
    git checkout release/18.x
3. create a local build directory and configure the build:
    mkdir $clang_build
    cd $clang_build
    cmake -DLLVM_ENABLE_PROJECTS=clang -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" \
        -DCMAKE_INSTALL_PREFIX=${clang_install} -DBUILD_SHARED_LIBS=ON \
        ${clang_source}/llvm
4. run the build (takes ~30 minutes):
    make -j$(nproc)
5. install:
    make install
6. (optional) update alternatives and set as default compiler:
    sudo update-alternatives --install /usr/bin/clang clang ${clang_install}/bin/clang 18 \
        --slave /usr/bin/clang++ clang++ ${clang_install}/bin/clang++ \
        --slave /usr/bin/clang-cpp clang-cpp ${clang_install}/bin/clang-cpp
7. Check the version:
    clang --version
"""


# NOTE: build ninja from source to enable C++20 modules and faster builds.


"""
1. set up local build variables:
    ninja_install=${root}/ninja-install
2. clone the git repository to a local directory:
    git clone https://github.com/ninja-build/ninja.git $ninja_install
    cd $ninja_install
3. checkout the latest release branch:
    git checkout release
4. bootstrap the build:
    ./configure.py --bootstrap
5. (optional) update alternatives and set as default ninja:
    sudo update-alternatives --install /usr/bin/ninja ninja ${ninja_install}/ninja 112
6. Check the version:
    ninja --version
"""


# NOTE: build CMake from source to enable C++20 module support.

"""
1. set up local build variables:
    cmake_source=${root}/cmake-source
    cmake_build=${root}/cmake-build
    cmake_install=${root}/cmake-install
2. clone the git repository to a local directory:
    git clone https://github.com/Kitware/CMake.git $cmake_source
    cd $cmake_source
3. checkout the latest release branch:
    git checkout release
4. create a local build directory and configure the build (takes ~5 minutes):
    mkdir $cmake_build
    cd $cmake_build
    ${cmake_source}/bootstrap --prefix=${cmake_install} --generator="Ninja" \
        LDFLAGS="-Wl,-rpath=${cpp_libs} -L${cpp_libs}"
5. run the build:
    ninja -j$(nproc)
6. install:
    ninja install
7. (optional) update alternatives and set as default cmake:
    sudo update-alternatives --install /usr/bin/cmake cmake ${cmake_install}/bin/cmake 328 \
        --slave /usr/bin/ctest ctest ${cmake_install}/bin/ctest
8. Check the version:
    cmake --version
"""


# NOTE: after building the C++ compiler, it's a good idea to build Python from source
# as well to prevent any ABI incompatibilities and get the best possible performance.
# Here's how to do that:

"""
1. set up local build variables:
    python_source=${root}/python-source
    python_build=${root}/python-build
    python_install=${root}/python-install
2. clone the git repository to a local directory:
    git clone https://github.com/python/cpython.git $python_source
    cd $python_source
3. checkout the latest release branch:
    git checkout 3.12
4. download prerequisites and install dependencies
    sudo apt-get install build-essential gdb lcov pkg-config \
        libbz2-dev libffi-dev libgdbm-dev libgdbm-compat-dev liblzma-dev \
        libncurses5-dev libreadline6-dev libsqlite3-dev libssl-dev \
        lzma lzma-dev tk-dev uuid-dev zlib1g-dev
5. create a local build directory and configure the build:
    mkdir $python_build
    cd $python_build
    ${python_source}/configure --prefix=${python_install} --with-ensurepip=upgrade \
        --enable-shared --enable-optimizations --with-lto \
        LDFLAGS="-Wl,-rpath=${python_install}/lib:${cpp_libs}" \
        CFLAGS="-DBERTRAND_TRACEBACK_EXCLUDE_PYTHON=${python_source}:${python_build}:${python_install}"
6. run the build (takes ~30 minutes):
    make -s -j$(nproc)
7. install:
    make install
8. (optional) update alternatives and set as default Python:
    sudo update-alternatives --install /usr/bin/python python ${python_install}/bin/python3.12 312 \
        --slave /usr/bin/python3 python3 ${python_install}/bin/python3.12 \
        --slave /usr/bin/pip pip ${python_install}/bin/pip3.12 \
        --slave /usr/bin/pip3 pip3 ${python_install}/bin/pip3.12
9. Check the version:
    python --version
"""


# NOTE: You might want to add additional configuration flags to either the C++ compiler
# or the Python build process to enable more warnings, enhanced security, or specific
# optimizations.  Here are some to consider:
#
#   GCC:
#
#   Clang
#
#   Python:
#       --with-valgrind:  enable valgrind support for Python


# Extra:
# -fno-strict-overflow

# Missing:
# -fwrapv
# -fstack-protector-strong
# -Wformat
# -Werror=format-security
# -Wdate-time
# -D_FORTIFY_SOURCE=2



class BuildExtHeadless(BuildExt):
    """A modification of the standard BuiltExt command that skips building C++
    extensions if not in a virtual environment, rather than failing.
    """

    def build_extensions(self) -> None:
        """Build if in a virtual environment, otherwise skip."""
        if env:
            # build clang AST parser first
            build = Path.cwd() / "bertrand" / "ast" / "build"
            build.mkdir(exist_ok=True)
            subprocess.check_call(
                [
                    str(env / "bin" / "cmake"),
                    "-G",
                    "Ninja",
                    f"-DCMAKE_INSTALL_PREFIX={env.dir}",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "..",
                ],
                cwd=build,
            )
            subprocess.check_call(["ninja"], cwd=build)
            subprocess.check_call(["ninja", "install"], cwd=build)

            # then build extensions using it
            super().build_extensions()




# setup(
#     conan=[
#         "pcre2/10.43@PCRE2/pcre2::pcre2",
#         "cpptrace/0.6.1@cpptrace/cpptrace::cpptrace",
#     ],
#     ext_modules=[
#         Extension(
#             "example",
#             ["bertrand/example.cpp", "bertrand/example_module.cpp"],
#             extra_compile_args=["-fdeclspec"]
#         ),
#         # Extension(
#         #     "bertrand.env.ast",
#         #     ["bertrand/env/ast.cpp"],
#         #     extra_compile_args=["-std=c++17"],
#         #     extra_link_args=["-lclang-cpp"]
#         # ),
#     ],
#     cmdclass={"build_ext": BuildExtHeadless},
# )




setup(
    cpp_deps=[
        "pcre2/10.43@PCRE2/pcre2::pcre2",
        "cpptrace/0.6.1@cpptrace/cpptrace::cpptrace",
    ],
    sources=[
        Extension(Path("bertrand") / "example.cpp"),
        Extension(Path("bertrand") / "example_module.cpp"),
    ],
    cmdclass={"build_ext": BuildExtHeadless},
)
