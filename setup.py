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

# GCC:

# NOTE: if you have Ubuntu 24.04 or later, then you can install gcc-14 directly using
# the package manager:
#       sudo apt-get install build-essential

# Otherwise:

# 1. clone the git repository to a local directory:
#       git clone git://gcc.gnu.org/git/gcc.git some/local/directory
#       cd some/local/directory
# 2. checkout the GCC 14 release branch:
#       git checkout releases/gcc-14
# 3. download prerequisites and install dependencies
#       ./contrib/download_prerequisites
#       sudo apt-get install g++-multilib flex bison
# 4. create a build directory and configure the build:
#       mkdir build
#       cd build
#       ../configure --disable-werror --disable-bootstrap
# 5. run the build (takes about 20 minutes):
#       make -j$(nproc)
# 6. install:
#       sudo make install
# 7. (optional) add as default compiler:
#       sudo update-alternatives --install /usr/bin/c++ c++ /usr/local/bin/c++ 100
#       sudo update-alternatives --install /usr/bin/g++ g++ /usr/local/bin/g++ 100
#       sudo update-alternatives --install /usr/bin/gcc gcc /usr/local/bin/gcc 100
#       sudo update-alternatives --install /usr/bin/cpp cpp /usr/local/bin/cpp 100
# 8. Check the version:
#       gcc --version


# Clang:

# 1. clone the git repository to a local directory:
#       git clone https://github.com/llvm/llvm-project.git some/local/directory
#       cd some/local/directory
# 2. checkout the clang 18 release branch:
#       git checkout release/18.x
# 3. create a build directory and configure the build:
#       mkdir build
#       cd build
#       cmake -DLLVM_ENABLE_PROJECTS=clang -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" ../llvm
# 4. run the build (takes about 30 minutes):
#       make -j$(nproc)
# 5. (optional) add as default compiler:
#       sudo update-alternatives --install /usr/bin/clang clang $(pwd)/bin/clang 100
#       sudo update-alternatives --install /usr/bin/clang++ clang++ $(pwd)/bin/clang++ 100
#       sudo update-alternatives --install /usr/bin/clang-cpp clang-cpp $(pwd)/bin/clang-cpp 100
# 6. Check the version:
#       clang --version


# NOTE: after building the C++ compiler, it's a good idea to build Python from source
# as well to prevent any ABI incompatibilities and get the best possible performance.
# Here's how to do that:
#
# 1. clone the git repository to a local directory:
#       git clone https://github.com/python/cpython.git some/local/directory
#       cd some/local/directory
# 2. checkout the latest release branch:
#       git checkout 3.12
# 3. download prerequisites and install dependencies
#       sudo apt-get install build-essential gdb lcov pkg-config \
#         libbz2-dev libffi-dev libgdbm-dev libgdbm-compat-dev liblzma-dev \
#         libncurses5-dev libreadline6-dev libsqlite3-dev libssl-dev \
#         lzma lzma-dev tk-dev uuid-dev zlib1g-dev
# 4. create a build directory and configure the build:
#       mkdir build
#       cd build
#       ../configure --enable-shared --enable-optimizations --with-lto \
#           --with-ensurepip=upgrade --prefix=/usr/local LDFLAGS="-Wl,-rpath=usr/local"
# 5. run the build (takes about 30 minutes):
#       make -s -j$(nproc)
# 6. install:  # TODO: the rest of these might not be necessary.  Check later with a fresh install of ubuntu
#       sudo make altinstall
# 7. Add shared libraries to the system path:
#       echo $(pwd) | sudo tee -a /etc/ld.so.conf.d/local.conf
#       sudo ldconfig
# 8. (optional) add as default Python:
#       sudo update-alternatives --install /usr/bin/python python /usr/local/bin/python3.12 100
#       sudo update-alternatives --install /usr/bin/python3 python3 /usr/local/bin/python3.12 100



# NOTE: building Python from source allows integration with other C++ tools, like
# valgrind (./configure --with-valgrind).  I can also manually configure the CFLAGS and
# LDFLAGS to enable more warnings and optimizations.



# NOTE: See setuptools for more info on how extensions are built:
# https://setuptools.pypa.io/en/latest/userguide/ext_modules.html



# os.environ["CC"] = "g++"  # NOTE: use GCC latest.  Set to clang-18 for Clang.



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
