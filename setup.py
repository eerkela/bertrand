"""Setup script for Bertrand."""
from pathlib import Path
import os
import subprocess
import sys
from typing import Any

from setuptools import setup  # type: ignore
from bertrand import Extension, BuildExt


# NOTE: C++ users have to execute $(python3 -m bertrand -I) to compile against bertrand.h
# g++ foo.cpp -o foo.out $(python3 -m bertrand -I)

# NOTE: Python users writing C++ extensions should use bertrand's custom Extension
# class to add the necessary linkages to the bertrand library, and to enable clangd
# support through compile_commands.json.



# NOTE: Bertrand uses C++23 features only found in GCC 14+ and Clang 18+ (MSVC is not
# yet fully supported).  Users must first install these compilers to use the library.
# Here are the steps to do so:


# GCC: If you have Ubuntu 24.04 or later, then you can install gcc-14 directly using the
# package manager:
#       sudo apt-get install build-essential gcc-14 g++-14 python3-dev


# Otherwise:

# 1. set up local build variables:
#       gcc_source=$(realpath $(pwd)/gcc-14)
#       gcc_build=$(realpath $(pwd)/gcc-build)
#       gcc_install=$(realpath $(pwd)/gcc-install)
# 2. clone the git repository to a local directory:
#       git clone git://gcc.gnu.org/git/gcc.git $gcc_source
#       cd $gcc_source
# 3. checkout the GCC 14 release branch:
#       git checkout releases/gcc-14
# 4. download prerequisites
#       ./contrib/download_prerequisites
# 5. create a local build directory and configure the build:
#       mkdir $gcc_build
#       cd $gcc_build
#       ${gcc_source}/configure --prefix=${gcc_install} --enable-shared --disable-werror \
#           --disable-bootstrap
# 6. run the build (takes about 20 minutes):
#       make -j$(nproc)
# 7. install:
#       make install
# 8. (optional) update alternatives and set as default compiler:
#       sudo update-alternatives --install /usr/bin/gcc gcc ${gcc_install}/bin/gcc 14 \
#           --slave /usr/bin/g++ g++ ${gcc_install}/bin/g++ \
#           --slave /usr/bin/c++ c++ ${gcc_install}/bin/c++ \
#           --slave /usr/bin/cpp cpp ${gcc_install}/bin/cpp \
#           --slave /usr/bin/gcc-ar gcc-ar ${gcc_install}/bin/gcc-ar \
#           --slave /usr/bin/gcc-nm gcc-nm ${gcc_install}/bin/gcc-nm \
#           --slave /usr/bin/gcc-ranlib gcc-ranlib ${gcc_install}/bin/gcc-ranlib \
#           --slave /usr/bin/gcov gcov ${gcc_install}/bin/gcov
#       sudo update-alternatives --config gcc
# 9. Check the version:
#       gcc --version



# Clang:

# 1. set up local build variables:
#       clang_source=$(realpath $(pwd)/clang-18)
#       clang_build=$(realpath $(pwd)/clang-build)
#       clang_install=$(realpath $(pwd)/clang-install)
# 1. clone the git repository to a local directory:
#       git clone https://github.com/llvm/llvm-project.git $clang_source
#       cd $clang_source
# 2. checkout the clang 18 release branch:
#       git checkout release/18.x
# 3. create a local build directory and configure the build:
#       mkdir $clang_build
#       cd $clang_build
#       cmake -DLLVM_ENABLE_PROJECTS=clang -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" \
#           -DCMAKE_INSTALL_PREFIX=${clang_install} -DBUILD_SHARED_LIBS=ON \
#           ${clang_source}/llvm
# 4. run the build (takes about 30 minutes):
#       make -j$(nproc)
# 5. install:
#       make install
# 6. (optional) update alternatives and set as default compiler:
#       sudo update-alternatives --install /usr/bin/clang clang ${clang_install}/bin/clang 18 \
#           --slave /usr/bin/clang++ clang++ ${clang_install}/bin/clang++ \
#           --slave /usr/bin/clang-cpp clang-cpp ${clang_install}/bin/clang-cpp
#       sudo update-alternatives --config clang
# 7. Check the version:
#       clang --version



# NOTE: after building the C++ compiler, it's a good idea to build Python from source
# as well to prevent any ABI incompatibilities and get the best possible performance.
# Here's how to do that:
#
# 1. set up local build variables:
#       python_source=$(realpath $(pwd)/python-3.12)
#       python_build=$(realpath $(pwd)/python-build)
#       python_install=$(realpath $(pwd)/python-install)
#
#   if building with gcc:
#       cpp_libs=$(gcc -print-search-dirs | grep 'libraries' | sed 's/libraries: =//')
#
#   if building with clang:
#       cpp_libs=$(clang -print-search-dirs | grep 'libraries' | sed 's/libraries: =//')
#
# 2. clone the git repository to a local directory:
#       git clone https://github.com/python/cpython.git $python_source
#       cd $python_source
# 3. checkout the latest release branch:
#       git checkout 3.12
# 4. download prerequisites and install dependencies
#       sudo apt-get install build-essential gdb lcov pkg-config \
#         libbz2-dev libffi-dev libgdbm-dev libgdbm-compat-dev liblzma-dev \
#         libncurses5-dev libreadline6-dev libsqlite3-dev libssl-dev \
#         lzma lzma-dev tk-dev uuid-dev zlib1g-dev
# 5. create a local build directory and configure the build:
#       mkdir $python_build
#       cd $python_build
#       ${python_source}/configure --enable-shared --enable-optimizations --with-lto \
#           --with-ensurepip=upgrade --prefix=${python_install} \
#           LDFLAGS="-Wl,-rpath=${python_install}/lib:${cpp_libs}" \
#           CFLAGS="-DBERTRAND_TRACEBACK_EXCLUDE_PYTHON=${python_source}:${python_build}:${python_install}"
# 6. run the build (takes about 30 minutes):
#       make -s -j$(nproc)
# 7. install:
#       make install
# 8. (optional) update alternatives and set as default Python:
#       sudo update-alternatives --install /usr/bin/python python ${python_install}/bin/python3.12 312 \
#           --slave /usr/bin/python3 python3 ${python_install}/bin/python3.12 \
#           --slave /usr/bin/pip pip ${python_install}/bin/pip3.12 \
#           --slave /usr/bin/pip3 pip3 ${python_install}/bin/pip3.12
#       sudo update-alternatives --config python
# 10. Check the version:
#       python --version



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



# NOTE: See setuptools for more info on how extensions are built:
# https://setuptools.pypa.io/en/latest/userguide/ext_modules.html


class build_ext(BuildExt):
    """A custom build_ext command that installs third-party C++ packages and builds C++
    unit tests as part of pip install.
    """

    def extract(self, cwd: Path, tarball: str) -> None:
        """Extract a tarball in the current working directory.

        Parameters
        ----------
        cwd : Path
            A path to the directory where the tarball is located.
        tarball : str
            The name of the tarball to extract.
        """
        try:
            subprocess.check_call(["tar", "-xzf", tarball], cwd=str(cwd))
        except subprocess.CalledProcessError as exc:
            print(f"failed to extract {tarball}:", exc)
            sys.exit(1)

    def install_cpptrace(self, cwd: Path) -> None:
        """Build cpptrace from source after extracting it.

        Parameters
        ----------
        cwd : Path
            A path to the directory where the tarball was extracted.
        """
        cpptrace_dir = "cpptrace-0.5.2"
        if (cwd / cpptrace_dir).exists():
            cwd = cwd / cpptrace_dir
        else:
            self.extract(cwd, f"{cpptrace_dir}.tar.gz")
            subprocess.check_call(["mv", "cpptrace", cpptrace_dir], cwd=str(cwd))
            cwd = cwd / cpptrace_dir / "build"
            cwd.mkdir(parents=True, exist_ok=True)

            try:
                subprocess.check_call(
                    ["cmake", "..", "-DCMAKE_BUILD_TYPE=Release"],
                    cwd=str(cwd)
                )
                subprocess.check_call(["make"], cwd=str(cwd))
            except subprocess.CalledProcessError as exc:
                print("failed to build cpptrace:", exc)
                sys.exit(1)

            cwd = cwd.parent  # back out to root dir

        # add headers to include path and link against binary
        self.include_dirs.append(str(cwd / "include"))
        self.library_dirs.append(str(cwd / "build"))
        self.library_dirs.append(str(cwd / "build/_deps/libdwarf-build/src/lib/libdwarf"))
        self.library_dirs.append(str(cwd / "build/_deps/zstd-build/lib"))
        self.libraries.extend(["cpptrace", "dwarf", "z", "zstd", "dl"])

    def install_gtest(self, cwd: Path) -> None:
        """Build GoogleTest from source after extracting it.

        Parameters
        ----------
        cwd : Path
            A path to the directory where the tarball was extracted.
        """
        gtest_dir = "googletest-1.14.0"
        if (cwd / gtest_dir).exists():
            cwd = cwd / gtest_dir
        else:
            self.extract(cwd, f"{gtest_dir}.tar.gz")
            cwd = cwd / gtest_dir / "build"
            cwd.mkdir(parents=True, exist_ok=True)

            try:
                subprocess.check_call(
                    ["cmake", "..", "-DCMAKE_CXX_FLAGS=-fPIC"],
                    cwd=str(cwd)
                )
                subprocess.check_call(["make"], cwd=str(cwd))
            except subprocess.CalledProcessError as exc:
                print("failed to build GoogleTest:", exc)
                sys.exit(1)

            cwd = cwd.parent  # back out to root dir

        # add headers to include path and link against binary
        self.include_dirs.append(str(cwd / "googletest/include"))
        self.library_dirs.append(str(cwd / "build/lib"))
        self.libraries.append("gtest")

    def install_pcre2(self, cwd: Path) -> None:
        """Build PCRE2 from source after extracting it.

        Parameters
        ----------
        cwd : Path
            A path to the directory where the tarball was extracted.
        """
        pcre_dir = "pcre2-10.43"
        if (cwd / pcre_dir).exists():
            cwd = cwd / pcre_dir
        else:
            self.extract(cwd, f"{pcre_dir}.tar.gz")
            cwd = cwd / pcre_dir
            cwd.mkdir(parents=True, exist_ok=True)

            try:
                subprocess.check_call(["./configure", "--enable-jit=auto"], cwd=str(cwd))
                subprocess.check_call(["make"], cwd=str(cwd))
            except subprocess.CalledProcessError as exc:
                print("failed to build PCRE2:", exc)
                sys.exit(1)

        # add headers to include path and link against binary
        self.include_dirs.append(str(cwd / "src"))
        self.library_dirs.append(str(cwd / ".libs"))
        self.libraries.append("pcre2-8")

    def run(self, *args: Any, **kwargs: Any) -> None:
        """Build third-party libraries from source before installing any extensions.

        Parameters
        ----------
        *args : Any
            Arbitrary positional arguments passed to the superclass.
        **kwargs : Any
            Arbitrary keyword arguments passed to the superclass.
        """
        try: # check for cmake
            subprocess.check_call(
                ["cmake", "--version"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
        except subprocess.CalledProcessError:
            print("CMake not installed")
            sys.exit(1)

        # install third-party C++ packages
        cwd = Path(os.getcwd()) / "third_party"
        self.install_cpptrace(cwd)
        self.install_pcre2(cwd)
        self.install_gtest(cwd)

        # compile Python extensions
        super().run(*args, **kwargs)

        # save compiler flags to test/ so that we can use the same configuration when
        # building the test suite
        cwd = cwd.parent / "test"
        with (cwd / ".compile_flags").open("w") as file:
            file.write(" ".join(
                self.compiler.compiler_so[1:] +  # remove the compiler name
                ["-std=c++20", "-fvisibility=hidden", "-g0"]  # added by Pybind11Extension
            ))


EXTENSIONS = [
    # Pybind11Extension(
    #     "bertrand.structs.linked",
    #     sources=["bertrand/structs/linked.cpp"],
    #     extra_compile_args=["-O3"]
    # ),
    # Extension(
    #     "*",
    #     sources=["bertrand/**/*.pyx"],
    #     define_macros=[("NPY_NO_DEPRECATED_API", "NPY_1_7_API_VERSION")],
    # ),
    Extension("example", ["example.cpp"]),
    # Extension("bertrand.regex", ["bertrand/regex.cpp"])
]


setup(
    # long_description=Path("README.rst").read_text("utf-8"),
    ext_modules=EXTENSIONS,
    # include_dirs=[
    #     "bertrand/",
    #     numpy.get_include(),
    # ],
    # zip_safe=False,  # TODO: maybe true without cython?
    cmdclass={"build_ext": build_ext},
)
