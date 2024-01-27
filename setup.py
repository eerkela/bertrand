"""Setup script for bertrand package."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

from setuptools import Extension, setup  # type: ignore
from setuptools.command.build_ext import build_ext  # type: ignore

from Cython.Build import cythonize  # type: ignore
import numpy


# TODO: note that users have to -I ~/.local/include to compile against bertrand.h
# g++ foo.cpp -o foo.out -I ~/.local/include -I /usr/include/python3.xx -lpython3.xx

# TODO: currently, trying to import bertrand from a pure C++ file causes a segfault,
# likely due to an uninitialized python interpreter.


# NOTE: bertrand users environment variables to control the build process:
#   1.  DEBUG - if true, compile C++ extensions with debug symbols and logging enabled
#   2.  HEADERS - if true, copy C++ headers to an external location for use from C++
#   3.  HEADER_PATH - the location to copy C++ headers to (defaults to ~/.local/include)


# NOTE: See setuptools for more info on how extensions are built:
# https://setuptools.pypa.io/en/latest/userguide/ext_modules.html


TRUTHY = {"1", "true", "t", "yes", "y", "sure", "yep", "yap"}
FALSY = {"0", "false", "f", "no", "n", "nah", "nope", "nop"}


# extract debug flag from environment
DEBUG: bool
fdebug = os.environ.get("DEBUG", "0").lower()
if fdebug in TRUTHY:
    DEBUG = True
elif fdebug in FALSY:
    DEBUG = False
else:
    raise ValueError(f"DEBUG={repr(fdebug)} is not a valid boolean value")


# extract header install location from environment
INCLUDE: Path | None
fheaders = os.environ.get("HEADERS", "0").lower()
if fheaders in TRUTHY:
    fheader_path = os.environ.get("HEADER_PATH", "~/.local/include")
    INCLUDE = Path(fheader_path).expanduser().absolute()
elif fheaders in FALSY:
    INCLUDE = None
else:
    raise ValueError(f"HEADERS={repr(fheaders)} is not a valid boolean value")


class BuildExt(build_ext):
    """Customized build_ext command to build and link C++ components.  Handles the
    following:
        1.  Builds Python extensions from C++/Cython source files.
        2.  Invokes CMake to build the C++ library and link it to the C++ compiler.
        3.  Optionally enables debug symbols/logging on the C++ side.
    """

    def build_extension(self, ext: Extension) -> None:
        """Builds Python extensions from C++/Cython source files.

        Parameters
        ----------
        ext : Extension
            The extension to build.  If this is a CMakeExtension, then its
            build_cmake() method will be invoked instead.
        """
        if isinstance(ext, CMakeExtension):
            ext.build_cmake(self)
        else:
            super().build_extension(ext)


class CMakeExtension(Extension):
    """An extension that invokes a CMake build process to produce a compiled binary.

    Note that setuptools expects this to produce a shared library after invoking
    build_cmake().  Header-only extensions should be placed after the call to
    `setuptools.setup()` to avoid errors.

    NOTE: as of now, bertrand does not use any CMake-enabled extensions, so this class
    is technically unnecessary.  However, it is included here for reference, in case
    it is needed in the future.
    """

    def __init__(self, name: str, sourcedir: str) -> None:
        super().__init__(name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)

    def build_cmake(self, cmd: BuildExt) -> None:
        """Invoke a CMake script for this extension.

        Parameters
        ----------
        cmd : BuildExt
            The `build_ext` command that invoked this method.

        Raises
        ------
        RuntimeError
            If the CMake executable cannot be found.
        """
        try:
            subprocess.check_output(["cmake", "--version"])
        except OSError as err:
            raise RuntimeError("Cannot find CMake executable") from err

        cwd = Path().absolute()

        # temporary build directory
        build_temp = Path(cmd.build_temp) / self.name
        build_temp.mkdir(parents=True, exist_ok=True)

        # CMake arguments
        build_type = "Debug" if cmd.debug else "Release"
        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={build_temp.absolute()}",
            f"-DCMAKE_BUILD_TYPE={build_type}",
            f"-DPYTHON_EXECUTABLE={sys.executable}",
        ]

        # swap to temporary build directory
        os.chdir(str(build_temp))

        # CMake configure
        cmd.spawn(["cmake", str(self.sourcedir)] + cmake_args)

        # CMake build
        build_args = ["--config", build_type]
        if not cmd.dry_run:
            cmd.spawn(["cmake", "--build", ".", "--target", "install"] + build_args)

        # swap back to original directory
        os.chdir(str(cwd))


extensions = [
    Extension(
        "bertrand.structs.linked",
        sources=["bertrand/structs/linked.cpp"],
        extra_compile_args=["-O3"]
    ),
#     Extension(
#         "*",
#         sources=["bertrand/**/*.pyx"],
#         define_macros=[("NPY_NO_DEPRECATED_API", "NPY_1_7_API_VERSION")],
#     ),
]


def apply_debug_flags(extensions: list[Extension]) -> None:
    """Apply debug flags to extensions if DEBUG=True.

    Parameters
    ----------
    extensions : list[Extension]
        The list of extensions to modify.
    """
    for ext in extensions:
        ext.extra_compile_args.extend(["-g", "-DBERTRAND_DEBUG"])
        ext.extra_link_args.extend(["-g", "-DBERTRAND_DEBUG"])


if DEBUG:
    apply_debug_flags(extensions)


setup(
    long_description=Path("README.rst").read_text("utf-8"),
    ext_modules=cythonize(
        extensions,
        language_level="3",
        compiler_directives={
            "embedsignature": True,
        },
    ),
    include_dirs=[numpy.get_include()],
    zip_safe=False,
    cmdclass={"build_ext": BuildExt},
)


def copy_headers(dest: Path) -> None:
    """Copies headers to local includes for use from C++.

    Parameters
    ----------
    dest : str
        The destination directory to copy headers to.
    """
    src = Path("bertrand/")

    # clear existing contents
    (dest / "bertrand.h").unlink(missing_ok=True)
    if (dest / "bertrand").exists():
        shutil.rmtree(dest / "bertrand")

    # copy headers
    for header in src.rglob("*.h"):
        target = dest / header
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(header, target)

    # copy top-level bertrand.h
    shutil.copy(Path("bertrand.h"), dest / "bertrand.h")


if INCLUDE is not None:
    copy_headers(INCLUDE)
