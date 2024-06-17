"""Build tools for bertrand-enabled C++ extensions."""
import datetime
import os
import re
import shutil
import subprocess
import sys
import sysconfig
import platform
from pathlib import Path
from typing import Any

import numpy
import pybind11
import setuptools  # type: ignore
from pybind11.setup_helpers import Pybind11Extension
from pybind11.setup_helpers import build_ext as pybind11_build_ext


ROOT: Path = Path(__file__).absolute().parent.parent


def get_include() -> str:
    """Get the path to the include directory for this package, which is necessary to
    make C++ headers available to the compiler.

    Returns
    -------
    str
        The path to the include directory for this package.
    """
    return str(ROOT)


def quick_include() -> list[str]:
    """Return the complete include and link flags necessary to build a pure-C++ project
    with bertrand as a dependency.

    Returns
    -------
    list
        A list of strings containing the various include and link libraries needed to
        build a bertrand-enabled project from the command line, as a single unit.
    """
    cpptrace = DEPS / "cpptrace-0.5.2"
    gtest = DEPS / "googletest-1.14.0"
    pcre2 = DEPS / "pcre2-10.43"
    return [
        f"-I{sysconfig.get_path('include')}",
        f"-I{get_include()}",
        f"-I{pybind11.get_include()}",
        f"-I{numpy.get_include()}",
        f"-I{str(cpptrace / 'include')}",
        f"-I{str(gtest / 'googletest' / 'include')}",
        f"-I{str(pcre2 / 'src')}",
        f"-L{str(cpptrace / 'build')}",
        f"-L{str(cpptrace / 'build' / '_deps' / 'libdwarf-build' / 'src' / 'lib' / 'libdwarf')}",
        f"-L{str(cpptrace / 'build' / '_deps' / 'zstd-build' / 'lib')}",
        f"-L{str(gtest / 'build' / 'lib')}",
        f"-L{str(pcre2 / '.libs')}",
        f"-lpython{sysconfig.get_python_version()}",
        "-lcpptrace",
        "-ldwarf",
        "-lz",
        "-lzstd",
        "-ldl",
        "-lgtest",
    ]


class Extension(Pybind11Extension):
    """A setuptools.Extension class that builds using CMake and supports C++20 modules.

    Parameters
    ----------
    *args, **kwargs : Any
        Arbitrary arguments passed to the Pybind11Extension constructor.
    conan : list[str], optional
        A list of Conan package names to install before building the extension.
    cxx_std : int, default 20
        The C++ standard to use when compiling the extension.  Values less than 20 will
        raise a ValueError.
    traceback : bool, default True
        If set to false, add `BERTRAND_NO_TRACEBACK` to the compile definitions, which
        will disable cross-language tracebacks for the extension.
    extra_cmake_args : dict[str, Any], optional
        Additional arguments to pass to the Extension's CMake configuration.  These are
        emitted as key-value pairs into a `set_target_properties()` block in the
        generated CMakeLists.txt file.  Some options are filled in by default,
        including `PREFIX`, `LIBRARY_OUTPUT_DIRECTORY`, `LIBRARY_OUTPUT_NAME`,
        `SUFFIX`, `CXX_STANDARD`, and `CXX_STANDARD_REQUIRED`.
    """

    MODULE_REGEX = re.compile(r"\s*export\s+module\s+(\w+).*;", re.MULTILINE)

    def __init__(
        self,
        *args: Any,
        executable: bool = False,
        cxx_std: int = 23,
        traceback: bool = True,
        extra_cmake_args: dict[str, Any] | None = None,
        **kwargs: Any
    ) -> None:
        if cxx_std < 23:
            raise ValueError(
                "C++ standard must be at least C++23 to enable bertrand features"
            )

        super().__init__(*args, **kwargs)
        self.executable = executable
        self.cxx_std = cxx_std
        self.traceback = traceback
        self.extra_cmake_args = extra_cmake_args or {}

        self.include_dirs.append(get_include())
        self.include_dirs.append(numpy.get_include())
        if self.traceback:
            self.extra_compile_args.append("-g")
            self.extra_link_args.append("-g")
        else:
            self.define_macros.append("BERTRAND_NO_TRACEBACK")

    def add_to_cmakelists(
        self,
        cmakelists: Path,
        build_dir: Path,
        module_cache: dict[str, bool],
        conan: list[tuple[str, str, str]]
    ) -> None:
        """Generate a temporary CMakeLists.txt that configures the extension for
        building with CMake.

        Parameters
        ----------
        cmakelists : Path
            The (current) path to the generated CMakeLists.txt file.
        build_dir : Path
            The path to the build directory for the extension.
        module_cache : dict[str, bool]
            A dictionary that caches whether each source file exports a module.  If the
            same source is used across multiple extensions, this will prevent duplicate
            scans.
        conan : list[tuple[str, str, str]]
            A list of Conan packages to link against when building the extension.
        """
        with cmakelists.open("a") as f:
            if self.executable:
                f.write(f"add_executable({self.name}\n")
            else:
                f.write(f"add_library({self.name} MODULE\n")
            # TODO: add sources related to built-in bertrand modules
            for source in self.sources:
                f.write(f"    {ROOT / source}\n")
            f.write(")\n")

            f.write(f"set_target_properties({self.name} PROPERTIES\n")
            f.write( "    PREFIX \"\"\n")
            f.write(f"    OUTPUT_NAME {self.name}\n")
            if not self.executable:
                f.write(f"    LIBRARY_OUTPUT_DIRECTORY {build_dir}")
                f.write(f"    SUFFIX {sysconfig.get_config_var('EXT_SUFFIX')}\n")
            f.write(f"    CXX_STANDARD {self.cxx_std}\n")
            f.write( "    CXX_STANDARD_REQUIRED ON\n")
            for key, value in self.extra_cmake_args.items():
                f.write(f"    {key} {value}\n")
            f.write(")\n")

            modules = []
            for source in self.sources:
                if source not in module_cache:
                    with Path(ROOT / source).open("r", encoding="utf_8") as s:
                        module_cache[source] = bool(self.MODULE_REGEX.search(s.read()))
                if module_cache[source]:
                    modules.append(source)

            if modules:
                f.write(f"target_sources({self.name} PRIVATE\n")
                f.write( "    FILE_SET CXX_MODULES\n")
                f.write(f"    BASE_DIRS {ROOT}\n")
                f.write( "    FILES\n")
                for source in modules:
                    f.write(f"        {ROOT / source}\n")
                f.write(")\n")

            if self.include_dirs:
                f.write(f"target_include_directories({self.name} PRIVATE\n")
                for include in self.include_dirs:
                    f.write(f"    {include}\n")
                f.write(")\n")

            if self.library_dirs:
                f.write(f"target_link_directories({self.name} PRIVATE\n")
                for lib_dir in self.library_dirs:
                    f.write(f"    {lib_dir}\n")
                f.write(")\n")

            if self.libraries or conan:
                f.write(f"target_link_libraries({self.name} PRIVATE\n")
                for package in conan:
                    f.write(f"    {package[2]}\n")
                for lib in self.libraries:
                    f.write(f"    {lib}\n")
                f.write(")\n")

            if self.extra_compile_args:
                f.write(f"target_compile_options({self.name} PRIVATE\n")
                for flag in self.extra_compile_args:
                    f.write(f"    {flag}\n")
                f.write(")\n")

            _link_options = [sysconfig.get_config_var("LDFLAGS")] + self.extra_link_args
            if _link_options:
                f.write(f"target_link_options({self.name} PRIVATE\n")
                for flag in _link_options:
                    f.write(f"    {flag}\n")
                f.write(")\n")

            if self.define_macros:
                f.write(f"target_compile_definitions({self.name} PRIVATE\n")
                for define in self.define_macros:
                    if isinstance(define, tuple):
                        f.write(f"    {define[0]}={define[1]}\n")
                    else:
                        f.write(f"    {define}\n")
                f.write(")\n")


class BuildExt(pybind11_build_ext):
    """A custom build_ext command that uses CMake to build extensions with support for
    C++20 modules, parallel builds, clangd, executable targets, and bertrand's core
    dependencies without any extra configuration.
    """

    CMAKE_MIN_VERSION = "3.28" # CMake 3.28+ is necessary for C++20 module support
    user_options = pybind11_build_ext.user_options + [
        (
            "workers=",
            "j",
            "The number of parallel workers to use when building CMake extensions"
        )
    ]

    def __init__(
        self,
        *args: Any,
        conan: list[tuple[str, str, str]] | None = None,
        workers: int | None = None,
        **kwargs: Any
    ) -> None:
        """Initialize the build extensions command.

        Parameters
        ----------
        *args : Any
            Arbitrary positional arguments passed to the parent class.
        workers : int, default 0
            The number of parallel workers to use when building the extensions.  If set
            to 0, then the build will be single-threaded.
        **kwargs : Any
            Arbitrary keyword arguments passed to the parent class.
        """
        super().__init__(*args, **kwargs)
        self.conan = conan or []
        self.workers = workers

    def finalize_options(self) -> None:
        """Extract the number of parallel workers from the setup() call.

        Raises
        ------
        ValueError
            If the workers option is not set to a positive integer.
        """
        super().finalize_options()
        if self.workers is not None:
            try:
                self.workers = int(self.workers)
            except ValueError as e:
                raise ValueError("workers must be set to an integer") from e

            if self.workers < 1:
                raise ValueError("workers must be set to a positive integer")

    def init_cmakelists(self) -> Path:
        """Create and initialize a CMakeLists.txt file for the entire project.

        Returns
        -------
        Path
            The path to the generated CMakeLists.txt file, for further processing.
        """
        file = Path(self.build_lib).absolute() / "CMakeLists.txt"
        file.parent.mkdir(parents=True, exist_ok=True)
        file.touch(exist_ok=True)

        with file.open("w") as f:
            f.write(f"cmake_minimum_required(VERSION {self.CMAKE_MIN_VERSION})\n")
            f.write(f"project({self.distribution.get_name()} LANGUAGES CXX)\n")
            f.write(f"set(CMAKE_BUILD_TYPE {'Debug' if self.debug else 'Release'})\n")
            f.write(f"set(PYTHON_EXECUTABLE {sys.executable})\n")
            f.write("set(CMAKE_COLOR_DIAGNOSTICS ON)\n")
            f.write("set(CMAKE_CXX_SCAN_FOR_MODULES ON)\n")
            f.write("set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n")

            conan_build_dir = Path(self.build_lib).absolute() / "build"
            conan_build_dir /= "Debug" if self.debug else "Release"
            # f.write(f"list(APPEND CMAKE_PREFIX_PATH {conan_build_dir})\n")
            # f.write(f"include({conan_build_dir}/generators/conan_toolchain.cmake)\n")  # TODO: why is this here?
            for package in self.conan:
                f.write(f"find_package({package[1]} REQUIRED)\n")

            if self.compiler.include_dirs:
                f.write("include_directories(\n")
                for include in self.compiler.include_dirs:
                    f.write(f"    {include}\n")
                f.write(")\n")

            if self.compiler.library_dirs:
                f.write("link_directories(\n")
                for lib_dir in self.compiler.library_dirs:
                    f.write(f"    {lib_dir}\n")
                f.write(")\n")

            f.write("link_libraries(\n")
            f.write(f"    python{sysconfig.get_python_version()}\n")
            if self.compiler.libraries:
                for lib in self.compiler.libraries:
                    f.write(f"    {lib}\n")
            f.write(")\n")

        return file

    def init_conanfile(self) -> Path | None:
        """Create and initialize a conanfile.txt file listing global C++ dependencies
        for the entire project.

        Returns
        -------
        Path
            The path to the generated conanfile.txt file, for further processing.

        Raises
        ------
        ValueError
            If the conan packages are not specified as 3-tuples of strings.
        """
        if not self.conan:
            return None

        file = Path(self.build_lib).absolute() / "conanfile.txt"
        file.parent.mkdir(parents=True, exist_ok=True)
        file.touch(exist_ok=True)
        with file.open("w") as f:
            f.write("[requires]\n")
            for package in self.conan:
                if (
                    not isinstance(package, tuple) or
                    not all(isinstance(p, str) for p in package) or
                    len(package) != 3
                ):
                    raise ValueError(
                        "conan dependencies must be specified as 3-tuples of the form:\n"
                        "    (requires, find_package, link_target)\n"
                        "e.g.:\n"
                        "    ('zlib/1.2.11', 'ZLIB', 'ZLIB::ZLIB')"
                    )
                f.write(f"{package[0]}\n")
            f.write("\n")

            f.write("[generators]\n")
            f.write("CMakeDeps\n")
            f.write("CMakeToolchain\n")
            f.write("\n")

            f.write("[layout]\n")
            f.write("cmake_layout")

        return file

    def conan_install(self, conanfile: Path) -> None:
        """Initialize a Conan profile for the current build environment, which uses the
        same compiler and settings as the Python interpreter.

        Parameters
        ----------
        conanfile : Path
            The path to the conanfile.txt file that lists the global C++ dependencies for
            the project.
        """
        timestamp = datetime.datetime.now().strftime("%Y%m%d%H%M%S")
        name = f"{self.distribution.get_name()}{timestamp}"
        profile = Path.home() / f".conan2/profiles/{name}"
        profile.parent.mkdir(parents=True, exist_ok=True)
        profile.touch(exist_ok=True)

        try:

            with profile.open("w") as f:
                f.write("[settings]\n")
                f.write(f"os={platform.system()}\n")
                f.write(f"arch={platform.machine()}\n")
                f.write(f"build_type={'Debug' if self.debug else 'Release'}\n")
                f.write(f"compiler={sysconfig.get_config_var('CC')}\n")
                f.write("compiler.cppstd=23\n")  # TODO: find a better way to get this
                f.write("compiler.libcxx=libstdc++\n")  # TODO: get libcxx version?
                f.write("compiler.version=14\n")  # TODO: get compiler version number?

                f.write("[buildenv]\n")
                f.write(f"CC={sysconfig.get_config_var('CC')}\n")
                f.write(f"CXX={sysconfig.get_config_var('CXX')}\n")
                f.write(f"CFLAGS={sysconfig.get_config_var('CFLAGS')}\n")
                f.write("CONAN_CMAKE_GENERATOR=Ninja\n")

            subprocess.check_call(
                [
                    "conan",
                    "install",
                    str(conanfile),
                    "--build=missing",
                    f"--output-folder={conanfile.parent}",
                    f"--profile={profile.name}",
                    "-verror",
                ],
                cwd=conanfile.parent
            )
        except subprocess.CalledProcessError as e:
            if e.stderr:
                print(e.stderr)
        finally:
            profile.unlink()

    def build_extensions(self) -> None:
        """Build all extensions in the project.

        Raises
        ------
        RuntimeError
            If setup.py is invoked outside of a bertrand virtual environment.
        """
        if self.extensions and not os.environ.get("BERTRAND_HOME", None):
            raise RuntimeError(
                "setup.py must be run inside a bertrand virtual environment in order "
                "to compile C++ extensions"
            )

        self.check_extensions_list(self.extensions)

        conanfile = self.init_conanfile()
        if conanfile:
            self.conan_install(conanfile)

        call_cmake = False
        cmakelists = self.init_cmakelists()
        module_cache: dict[str, bool] = {}
        for ext in self.extensions:
            if isinstance(ext, Extension):
                call_cmake = True
                build_dir = Path(ROOT / self.get_ext_fullpath(ext.name)).parent
                ext.add_to_cmakelists(cmakelists, build_dir, module_cache, self.conan)
            else:
                super().build_extension(ext)

        config_args = [
            "cmake",
            "-G",
            "Ninja",
            str(cmakelists.parent),
            # "DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake",
        ]
        build_args = [
            "cmake",
            "--build",
            ".",
            "--config",
            "Debug" if self.debug else "Release"
        ]
        if self.workers:
            build_args += ["--parallel", str(self.workers)]

        if call_cmake:
            try:
                subprocess.check_call(config_args, cwd=cmakelists.parent)
                subprocess.check_call(build_args, cwd=cmakelists.parent)
            except subprocess.CalledProcessError as e:
                if e.stderr:
                    print(e.stderr)

            shutil.move(
                cmakelists.parent / "compile_commands.json",
                ROOT / "compile_commands.json"
            )

    def get_ext_filename(self, fullname: str) -> str:
        """Format the filename of the extension module.

        Parameters
        ----------
        fullname : str
            The name of the extension module.

        Returns
        -------
        str
            The formatted filename of the build product (either a shared library or an
            executable).  If the extension is an executable, the name is returned
            directly.  Otherwise, it will be decorated with a shared library extension
            based on the platform, like normal.

        Notes
        -----
        Overriding this method is required to allow setuptools to correctly copy the
        build products out of the build directory without errors.
        """
        ext = self.ext_map[fullname]
        if getattr(ext, "executable", False):
            return ext.name
        return super().get_ext_filename(fullname)


def setup(
    *args: Any,
    cmdclass: dict[str, Any] | None = None,
    conan: list[tuple[str, str, str]] | None = None,
    workers: int | None = None,
    **kwargs: Any
) -> None:
    """A custom setup() function that automatically appends the BuildExt command to the
    setup commands.

    Parameters
    ----------
    *args : Any
        Arbitrary positional arguments passed to the setuptools.setup() function.
    cmdclass : dict[str, Any] | None, default None
        A dictionary of command classes to override the default setuptools commands.
        If no setting is given for "build_ext", then it will be set to
        bertrand.setuptools.BuildExt.
    conan : list[tuple[str, str, str]] | None, default None
        A list of C++ dependencies to install before building the extensions.  Each
        dependency should be specified as a 3-tuple of strings, where the first is the
        package name and optional version number, the second is the name passed to the
        CMake find_package() command, and the third is the link target name, which is
        supplied to the target_link_libraries() command in CMake.  These identifiers
        can be found by running `conan search ${package_name}` or by browsing
        ConanCenter.io.
    workers : int | None, default None
        The number of parallel workers to use when building extensions.  If set to
        None, then the build will be single-threaded.  This can also be set through
        the command line by supplying either `--workers=NUM` or `-j NUM`.
    **kwargs : Any
        Arbitrary keyword arguments passed to the setuptools.setup() function.
    """
    class BuildExtWrapper(BuildExt):
        """A private subclass of BuildExt that captures the number of workers to use
        from the setup() call.
        """
        def __init__(self, *a: Any, **kw: Any):
            super().__init__(*a, conan=conan, workers=workers, **kw)

    if cmdclass is None:
        cmdclass = {"build_ext": BuildExtWrapper}
    elif "build_ext" not in cmdclass:
        cmdclass["build_ext"] = BuildExtWrapper
    else:
        cmd: type = cmdclass["build_ext"]
        if issubclass(cmd, BuildExt):

            class BuildExtSubclassWrapper(cmd):
                """A private subclass of BuildExt that captures the number of workers
                to use from the setup() call.
                """
                def __init__(self, *a: Any, **kw: Any):
                    super().__init__(*a, conan=conan, workers=workers, **kw)

        cmdclass["build_ext"] = BuildExtSubclassWrapper

    setuptools.setup(*args, cmdclass=cmdclass, **kwargs)
