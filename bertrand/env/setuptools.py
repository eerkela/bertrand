"""Build tools for bertrand-enabled C++ extensions."""
from __future__ import annotations

import importlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path, PosixPath
from typing import Any, Iterable

import setuptools
from packaging.version import Version
from setuptools.command.build_ext import build_ext as setuptools_build_ext

from .codegen import PyModule
from .environment import env
from .messages import FAIL, WHITE, RED, YELLOW, GREEN, CYAN
from .package import Package, PackageLike
from .version import __version__


# TODO: module dependencies should be stored separately from sources.  They can also
# be separated when listing sources in CMakeLists.txt for clarity.


# import std;  can be supported by doing this:
# https://discourse.llvm.org/t/libc-c-23-module-installation-support/77061/9
# https://discourse.llvm.org/t/llvm-discussion-forums-libc-c-23-module-installation-support/77087/27


# TODO: eventually, get_include() won't be necessary, since all the headers will be
# converted into modules, and the python module might be imported by default.


def get_include() -> str:
    """Get the path to the include directory for this package, which is necessary to
    make C++ headers available to the compiler.

    Returns
    -------
    str
        The path to the include directory for this package.
    """
    return str(Path(__file__).absolute().parent.parent.parent)


# TODO: I have to figure out how to get the stage 3 build to use the .pcm files
# generated in stage 2, and not recompile them itself.

# https://clang.llvm.org/docs/StandardCPlusPlusModules.html#header-units

# -> Perhaps stage 0.5 can build the .pcm files for import std + import Python.h,
# which are referenced for both stage 2 and stage 3.


# TODO: Object-orientation for Conan dependencies similar to Source?  Users would
# specify dependencies as Dependency("name", "version", "find", "link"), and
# potentially extra flags as well.


class Source(setuptools.Extension):
    """Describes an arbitrary source file that can be scanned for dependencies and used
    to generate automated bindings.  One of these should be specified for every source
    file in the project.

    Parameters
    ----------
    path : Path
        The path to the managed source file.
    cpp_std : int
        The C++ standard to use when compiling the source as a target.
    cmake_args : dict[str, Any]
        Additional arguments to pass to the CMake configuration.
    traceback : bool, default True
        If set to false, add `BERTRAND_NO_TRACEBACK` to the compile definitions, which
        will disable cross-language tracebacks when the source is built.  This can
        slightly improve performance on the unhappy path, at the cost of less
        informative error messages.  It has no effect on the happy path, when no errors
        are raised.
    **kwargs : Any
        Additional arguments passed to the setuptools.Extension constructor.

    Raises
    ------
    FileNotFoundError
        If the source file does not exist.
    OSError
        If the source file is a directory.

    Notes
    -----
    Unlike `setuptools.Extension`, these objects are meant to represent only a single
    source file.  Bertrand's build system is powerful enough to automatically detect
    dependencies between files, so there is no need to specify them manually.  All the
    required information is extracted from the source file itself.

    One important thing to note regards the way extra build options are interpreted on
    a per-source basis.  If you have a source file that requires special treatment, you
    can list additional arguments in the `Source` constructor, which will be used when
    that source is built as a target.  These flags will also apply to any dependencies
    that the source has, but *not* to any sources that depend on it in turn.  So, for
    instance, if you have 3 source files, A, B, and C, where A depends on B and B
    depends on C, then adding a `-DDEBUG` flag to B will cause both B and C to be built
    with the flag, while A will be built without it.  If A requires the flag as well,
    then it should be added to the `Source` constructor for A as well.
    """

    def __init__(
        self,
        path: str | Path,
        *,
        cpp_std: int = 0,
        traceback: bool | None = None,
        extra_cmake_args: dict[str, str] | None = None,
        **kwargs: Any
    ) -> None:
        if isinstance(path, str):
            path = Path(path)
        if not path.exists():
            FAIL(f"source file does not exist: {RED}{path}{WHITE}")
        if path.is_dir():
            FAIL(f"source file is a directory: {RED}{path}{WHITE}")
        if path.is_absolute():
            path = path.relative_to(Path.cwd())

        name = ".".join([
            ".".join(p.name for p in reversed(path.parents) if p.name),
            path.stem
        ])
        super().__init__(name, [path.as_posix()], **{"language": "c++", **kwargs})
        self.path = path
        self.cpp_std = cpp_std
        self.extra_cmake_args = extra_cmake_args or {}
        self.traceback = traceback
        self.module = ""
        self.primary_module = ""
        self.executable = False
        self.imports: list[str] = []

        self.include_dirs.append(get_include())  # TODO: eventually not necessary

    def __repr__(self) -> str:
        return f"<Source {self.path}>"


class BuildSources(setuptools_build_ext):
    """A custom build_ext command that uses a clang plugin to automatically generate
    cross-language Python/C++ bindings via ordinary import/export semantics.

    Must be used with the coupled `Source` class to describe build targets.

    Notes
    -----
    This command is intended to be placed within the `cmdclass` dictionary of a
    `setuptools.setup` call.  The `bertrand.setup()` function will normally do this
    automatically, but if you'd like to customize the build process in any way, you can
    subclass this command and pass it in manually.  Bertrand does this itself in order
    to customize the build process based on whether the user is installing within a
    virtual environment or not.
    """

    MIN_CMAKE_VERSION = Version("3.28")

    # additional command-line options accepted by the command
    user_options = setuptools_build_ext.user_options + [
        (
            "workers=",
            "j",
            "The number of parallel workers to use when building CMake extensions"
        ),
    ]

    def __init__(
        self,
        *args: Any,
        bertrand_cpp_std: int,
        bertrand_cpp_deps: list[Package],
        bertrand_include_dirs: list[str],
        bertrand_define_macros: list[tuple[str, str]],
        bertrand_compile_args: list[str],
        bertrand_library_dirs: list[str],
        bertrand_libraries: list[str],
        bertrand_link_args: list[str],
        bertrand_cmake_args: dict[str, str],
        bertrand_runtime_library_dirs: list[str],
        bertrand_export_symbols: list[str],
        bertrand_workers: int,
        **kwargs: Any,
    ) -> None:
        super().__init__(*args, **kwargs)
        self.workers = bertrand_workers
        self._bertrand_cpp_std = bertrand_cpp_std
        self._bertrand_cpp_deps = bertrand_cpp_deps
        self._bertrand_include_dirs = bertrand_include_dirs
        self._bertrand_define_macros = bertrand_define_macros
        self._bertrand_compile_args = bertrand_compile_args
        self._bertrand_library_dirs = bertrand_library_dirs
        self._bertrand_libraries = bertrand_libraries
        self._bertrand_link_args = bertrand_link_args
        self._bertrand_cmake_args = bertrand_cmake_args
        self._bertrand_runtime_library_dirs = bertrand_runtime_library_dirs
        self._bertrand_export_symbols = bertrand_export_symbols
        self._bertrand_source_lookup: dict[str, Source] = {}
        self._bertrand_module_root: Path  # initialized in finalize_options
        self._bertrand_binding_root: Path  # initialized in finalize_options
        self._bertrand_binding_cache: Path  # initialized in finalize_options
        self._bertrand_executable_cache: Path  # initialized in finalize_options

    def finalize_options(self) -> None:
        """Parse command-line options and convert them to the appropriate types.

        Raises
        ------
        TypeError
            If any extensions are not of type bertrand.Source
        ValueError
            If the workers option is not set to a positive integer or 0.
        """
        super().finalize_options()
        self._bertrand_source_lookup = {s.sources[0]: s for s in self.extensions}
        self._bertrand_module_root = Path(self.build_lib).absolute() / "modules"
        self._bertrand_binding_root = Path(self.build_lib).absolute() / "bindings"
        self._bertrand_binding_cache = Path(self.build_lib).absolute() / ".bindings"
        self._bertrand_executable_cache = Path(self.build_lib).absolute() / ".executables"
        if "-fdeclspec" not in self._bertrand_compile_args:
            self._bertrand_compile_args.append("-fdeclspec")

        # force the use of the coupled Source class to describe build targets
        self.check_extensions_list(self.extensions)
        incompabile_extensions = [
            ext for ext in self.extensions if not isinstance(ext, Source)
        ]
        if incompabile_extensions:
            FAIL(
                f"Extensions must be of type bertrand.Source: "
                f"{YELLOW}{incompabile_extensions}{WHITE}"
            )

        # add environment variables to the build configuration
        cpath = env.get("CPATH", "")
        if cpath:
            self._bertrand_include_dirs = (
                self._bertrand_include_dirs + cpath.split(os.pathsep)
            )

        ld_library_path = env.get("LD_LIBRARY_PATH", "")
        if ld_library_path:
            self._bertrand_library_dirs = (
                self._bertrand_library_dirs + ld_library_path.split(os.pathsep)
            )

        runtime_library_path = env.get("RUNTIME_LIBRARY_PATH", "")
        if runtime_library_path:
            self._bertrand_runtime_library_dirs = (
                self._bertrand_runtime_library_dirs +
                runtime_library_path.split(os.pathsep)
            )

        cxxflags = env.get("CXXFLAGS", "")
        if cxxflags:
            self._bertrand_compile_args = shlex.split(cxxflags) + self._bertrand_compile_args

        ldflags = env.get("LDFLAGS", "")
        if ldflags:
            self._bertrand_link_args = shlex.split(ldflags) + self._bertrand_link_args

        # parse workers from command line
        if self.workers:
            self.workers = int(self.workers)
            if self.workers == 0:
                self.workers = os.cpu_count() or 1
            elif self.workers < 0:
                FAIL(
                    f"workers must be set to a positive integer or 0 to use all cores, "
                    f"not {RED}{self.workers}{WHITE}"
                )

    def conan_install(self) -> None:
        """Invoke conan to install C++ dependencies for the project and link them
        against the environment.
        """
        path = Path(self.build_lib) / "conanfile.txt"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w") as f:
            f.write("[requires]\n")
            for package in self._bertrand_cpp_deps:
                f.write(f"{package.name}/{package.version}\n")
            f.write("\n")
            f.write("[generators]\n")
            f.write("CMakeDeps\n")
            f.write("CMakeToolchain\n")
            f.write("\n")
            f.write("[layout]\n")
            f.write("cmake_layout\n")

        subprocess.check_call(
            [
                "conan",
                "install",
                str(path.absolute()),
                "--build=missing",
                "--output-folder",
                str(path.parent),
                "-verror",
            ],
            cwd=path.parent,
        )
        env.packages.extend(p for p in self._bertrand_cpp_deps if p not in env.packages)

    def _cmakelists_header(self) -> str:
        build_type = "Debug" if self.debug else "Release"
        toolchain = (
            Path(self.build_lib) / "build" / build_type / "generators" /
            "conan_toolchain.cmake"
        )
        libraries = self._bertrand_libraries + [p.link for p in env.packages]

        out = f"# CMakeLists.txt automatically generated by bertrand {__version__}\n"
        out += f"cmake_minimum_required(VERSION {self.MIN_CMAKE_VERSION})\n"
        out += f"project({self.distribution.get_name()} LANGUAGES CXX)\n"
        out += "\n"
        out += "# global config\n"
        out += f"set(CMAKE_BUILD_TYPE {build_type})\n"
        out += f"set(PYTHON_EXECUTABLE {sys.executable})\n"
        out += "set(CMAKE_CXX_SCAN_FOR_MODULES ON)\n"
        out += "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        out += "set(CMAKE_COLOR_DIAGNOSTICS ON)\n"
        for k, v in self._bertrand_cmake_args.items():
            out += f"set({k} {v})\n"
        if self._bertrand_define_macros:
            out += "add_compile_definitions(\n"
            for define in self._bertrand_define_macros:
                out += f"    {define[0]}={define[1]}\n"
            out += ")\n"
        if self._bertrand_compile_args:
            out += "add_compile_options(\n"
            for flag in self._bertrand_compile_args:
                out += f"    {flag}\n"
            out += ")\n"
        if self._bertrand_link_args:
            out += "add_link_options(\n"
            for flag in self._bertrand_link_args:
                out += f"    {flag}\n"
            out += ")\n"
        out += "\n"
        out += "# package management\n"
        out += f"include({toolchain})\n"
        out += "\n".join(f'find_package({p.find} REQUIRED)' for p in env.packages)
        out += "\n"
        if self._bertrand_include_dirs:
            out += "include_directories(\n"
            for include in self._bertrand_include_dirs:
                out += f"    {include}\n"
            out += ")\n"
        if self._bertrand_library_dirs:
            out += "link_directories(\n"
            for lib_dir in self._bertrand_library_dirs:
                out += f"    {lib_dir}\n"
            out += ")\n"
        if libraries:
            out += "link_libraries(\n"
            for lib in libraries:
                out += f"    {lib}\n"
            out += ")\n"
        if self._bertrand_runtime_library_dirs:
            out += "set(CMAKE_INSTALL_RPATH\n"
            for lib_dir in self._bertrand_runtime_library_dirs:
                out += f"    \"{lib_dir}\"\n"
            out += ")\n"
        # TODO: what the hell to do with export_symbols?
        out += "\n"
        return out

    def _cmakelists_target(
        self,
        source: Source,
        target: str,
        ast_plugin: bool,
    ) -> str:
        out = f"target_sources({target} PRIVATE\n"
        out +=  "    FILE_SET CXX_MODULES\n"
        out += f"    BASE_DIRS {Path.cwd()}\n"
        out +=  "    FILES\n"
        for s in source.imports:
            out += f"        {PosixPath(s).absolute()}\n"
        out += ")\n"
        out += f"set_target_properties({target} PROPERTIES\n"
        out +=  "    PREFIX \"\"\n"
        out += f"    OUTPUT_NAME {target}\n"
        out +=  "    SUFFIX \"\"\n"
        out += f"    CXX_STANDARD {source.cpp_std}\n"
        out +=  "    CXX_STANDARD_REQUIRED ON\n"
        for key, value in source.extra_cmake_args.items():
            out += f"    {key} {value}\n"
        out += ")\n"
        out += f"target_compile_options({target} PRIVATE\n"
        # TODO: attribute plugins mess with clangd, which can't recognize them by
        # default.  To mitigate this, we simply disable warnings about unknown
        # attributes.  That's not ideal, but until LLVM implements some way around
        # this, it's the only option.
        out +=  "    -Wno-unknown-attributes\n"
        # TODO: importing std from env/modules/ causes clang to emit a warning about
        # a reserved module name.  This is superfluous, since the std.cppm module came
        # from clang in the first place, so we can safely disable it.  When
        # `import std;` is officially supported by CMake, this can be removed.
        out +=  "    -Wno-reserved-module-identifier\n"
        out += f"    -fplugin={env / 'lib' / 'bertrand-attrs.so'}\n"
        if source.extra_compile_args or ast_plugin:
            if ast_plugin:
                out += f"    -fplugin={env / 'lib' / 'bertrand-ast.so'}\n"
                out += f"    -fplugin-arg-main-cache={self._bertrand_executable_cache}\n"
                if source.primary_module:
                    python_path = self.get_python_path(source)
                    python_path.parent.mkdir(parents=True, exist_ok=True)
                    python_module = source.primary_module.split(".")[-1]
                    out += f"    -fplugin-arg-export-module={source.path.absolute()}\n"
                    out += f"    -fplugin-arg-export-import={source.primary_module}\n"
                    out += f"    -fplugin-arg-export-export={python_module}\n"
                    out += f"    -fplugin-arg-export-python={python_path}\n"
                    out += f"    -fplugin-arg-export-cache={self._bertrand_binding_cache}\n"
            for flag in source.extra_compile_args:
                out += f"    {flag}\n"
        out += ")\n"
        if source.define_macros:
            out += f"target_compile_definitions({target} PRIVATE\n"
            for define in source.define_macros:
                out += f"    {define[0]}={define[1]}\n"
            out += ")\n"
        if source.include_dirs:
            out += f"target_include_directories({target} PRIVATE\n"
            for include in source.include_dirs:
                out += f"    {include}\n"
            out += ")\n"
        if source.library_dirs:
            out += f"target_link_directories({target} PRIVATE\n"
            for lib_dir in source.library_dirs:
                out += f"    {lib_dir}\n"
            out += ")\n"
        if source.libraries:
            out += f"target_link_libraries({target} PRIVATE\n"
            for lib in source.libraries:
                out += f"    {lib}\n"
            out += ")\n"
        if source.extra_link_args or source.runtime_library_dirs:
            out += f"target_link_options({target} PRIVATE\n"
            for flag in source.extra_link_args:
                out += f"    {flag}\n"
            if source.runtime_library_dirs:
                for lib_dir in source.runtime_library_dirs:
                    out += f"    \"-Wl,-rpath,{lib_dir}\"\n"
            out += ")\n"
        # TODO: what the hell to do with export_symbols?
        out += "\n"
        return out

    def get_python_path(self, source: Source) -> Path:
        """Given a primary module interface unit, return the path to the generated
        Python binding file within the build directory.

        Parameters
        ----------
        source : Source
            The primary module interface unit to look for.

        Returns
        -------
        Path
            The path to the generated Python binding file.
        """
        assert source.primary_module, f"source is not a primary module interface: {source}"
        return self._bertrand_binding_root / source.path.with_suffix(".python.cpp")

    def stage1(self) -> None:
        """TODO
        """
        cmakelists = self._stage1_cmakelists()
        compile_commands = self._compile_commands(cmakelists)
        self._resolve_imports(compile_commands)

    # TODO: consider using INTERFACE libraries to avoid actually compiling anything.
    # This might also emit the .pcm files needed in stage 2?  If so, then I could pass
    # them efficiently to stage 3 and compile everything else (anything that's not a
    # module) as a .o file that gets linked in at the same time.

    def _stage1_cmakelists(self) -> Path:
        path = Path(self.build_lib).absolute() / "CMakeLists.txt"
        extra_include_dirs: set[Path] = set()
        extra_library_dirs: set[Path] = set()
        extra_libraries: set[str] = set()
        for source in self.extensions:
            extra_include_dirs.update(include for include in source.include_dirs)
            extra_library_dirs.update(lib_dir for lib_dir in source.library_dirs)
            extra_libraries.update(lib for lib in source.libraries)

        with path.open("w") as f:
            f.write(self._cmakelists_header())
            f.write("# stage 1 object includes all sources\n")
            f.write("add_library(${PROJECT_NAME} OBJECT\n")
            for source in self.extensions:
                f.write(f"    {source.path.absolute()}\n")
            f.write(")\n")
            f.write("target_sources(${PROJECT_NAME} PRIVATE\n")
            f.write( "    FILE_SET CXX_MODULES\n")
            f.write(f"    BASE_DIRS {Path.cwd()}\n")
            f.write( "    FILES\n")
            for source in self.extensions:
                f.write(f"        {source.path.absolute()}\n")
            f.write(")\n")
            f.write("set_target_properties(${PROJECT_NAME} PROPERTIES\n")
            f.write( "    PREFIX \"\"\n")
            f.write( "    OUTPUT_NAME ${PROJECT_NAME}\n")
            f.write( "    SUFFIX \"\"\n")
            f.write(f"    CXX_STANDARD {self._bertrand_cpp_std}\n")
            f.write( "    CXX_STANDARD_REQUIRED ON\n")
            f.write(")\n")
            if extra_include_dirs:
                f.write("target_include_directories(${PROJECT_NAME} PRIVATE\n")
                for include in extra_include_dirs:
                    f.write(f"    {include}\n")
                f.write(")\n")
            if extra_library_dirs:
                f.write("target_link_directories(${PROJECT_NAME} PRIVATE\n")
                for lib_dir in extra_library_dirs:
                    f.write(f"    {lib_dir}\n")
                f.write(")\n")
            if extra_libraries:
                f.write("target_link_libraries(${PROJECT_NAME} PRIVATE\n")
                for lib in extra_libraries:
                    f.write(f"    {lib}\n")
                f.write(")\n")
            f.write("\n")

        return path

    def _compile_commands(self, cmakelists: Path) -> Path:
        # configuring the project (but not building it!) will generate a complete
        # enough compilation database for clang-scan-deps to use
        cmakelists.parent.mkdir(parents=True, exist_ok=True)
        subprocess.check_call(
            [
                str(env / "bin" / "cmake"),
                "-G",
                "Ninja",
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                str(cmakelists.parent),
            ],
            cwd=cmakelists.parent,
            stdout=subprocess.PIPE,  # NOTE: silences noisy CMake/Conan output
        )

        # ... but first, we have to filter out any lazily-evaluated arguments that
        # might not be present at the time this method is invoked
        path = cmakelists.parent / "compile_commands.json"
        with path.open("r+") as f:
            filtered = [
                {
                    "directory": cmd["directory"],
                    "command": " ".join(
                        c for c in cmd["command"].split() if not c.startswith("@")
                    ),
                    "file": cmd["file"],
                    "output": cmd["output"],
                }
                for cmd in json.load(f)
            ]
            f.seek(0)
            json.dump(filtered, f, indent=4)
            f.truncate()

        return path

    def _import(
        self,
        source_path: Path,
        graph: dict[str, dict[str, Any]],
        edge: dict[str, Any],
        cache: dict[str, list[Path]]
    ) -> list[Path]:
        # If this is not the first time we've seen this import, then we can return the
        # cached result immediately.
        logical_name = edge["logical-name"]
        if logical_name in cache:
            return cache[logical_name]

        # Otherwise, if a source path is detected, then the import is well-formed
        # within the build tree.  In this case, we initiate a depth-first search of the
        # p1689 graph to resolve the import along with each of its dependencies.
        if "source-path" in edge:
            result = []
            stack = [Path(edge["source-path"]).relative_to(Path.cwd())]
            while stack:
                p = stack.pop()
                for e in graph.get(p.as_posix(), {}).get("requires", []):
                    stack.extend(
                        x for x in self._import(p, graph, e, cache)
                        if x not in result and x not in stack
                    )
                result.append(p)
            cache[logical_name] = result
            return result

        # If the import cannot be resolved within the build tree, then we check for an
        # equivalent .cppm file in the env/modules/ directory.  If one exists, we copy
        # it into the build along with its dependencies.
        search = env / "modules" / f"{logical_name}.cppm"
        if search.exists():
            self._bertrand_module_root.mkdir(parents=True, exist_ok=True)
            dest = self._bertrand_module_root / search.name
            shutil.copy2(search, dest)
            result = [dest]

            # Dependencies are stored in a directory with the same name as the module,
            # which must be copied into the build tree as well.
            deps = search.with_suffix("")
            dest = self._bertrand_module_root / deps.name
            if deps.exists() and deps.is_dir():
                shutil.copytree(deps, dest, dirs_exist_ok=True)
                result.extend(dest.rglob("*.cppm"))

            cache[logical_name] = result
            return result

        FAIL(
            f"Unresolved import: '{YELLOW}{logical_name}{WHITE}' in source: "
            f"{CYAN}{source_path}{WHITE}"
        )

        # If no C++ module is provided by the environment, then we attempt the import
        # at the Python level and generate an equivalent binding to resolve it.
        try:
            breakpoint()
            module = importlib.import_module(logical_name)
            binding = (
                self._bertrand_binding_root /
                    Path(*logical_name.split(".")).with_suffix(".cppm")
            ).relative_to(Path.cwd())
            result = PyModule(module).generate(binding)  # TODO: returns a list of paths
            cache[logical_name] = result
            return result

        # Else, the import is ill-formed and we fail the build.
        except ImportError:
            FAIL(f"Unresolved import: '{YELLOW}{logical_name}{WHITE}'")

    def _resolve_imports(self, compile_commands: Path) -> None:
        # module dependencies are represented according to the p1689r5 spec, which is
        # what CMake uses internally to generate the build graph.  We need some
        # translation in order to parse this spec for our purposes.
        p1689 = json.loads(subprocess.run(
            [
                str(env / "bin" / "clang-scan-deps"),
                "-format=p1689",
                "-compilation-database",
                str(compile_commands),
            ],
            check=True,
            stdout=subprocess.PIPE,
        ).stdout.decode("utf-8").strip())

        # first of all, p1689 lists the dependencies by build *target* rather than
        # source path, so we need to cross reference that with our Source objects.
        # Luckily, CMake stores the build targets in a nested source tree, so we can
        # strip the appropriate prefix and do some path arithmetic to get a 1:1 map.
        trunk = Path("CMakeFiles") / f"{self.distribution.get_name()}.dir"
        cwd = Path.cwd()
        graph = {
            (
                (cwd.root / Path(module["primary-output"]).relative_to(trunk)) \
                    .relative_to(cwd).with_suffix("").as_posix()
            ): module
            for module in p1689["rules"]
        }

        # import bindings are cached to avoid repeated work
        bindings: dict[str, list[Path]] = {}
        for posix, module in graph.items():
            source = self._bertrand_source_lookup[posix]

            # exports are represented by a "provides" array, which - for C++ modules -
            # can only have a single entry (but possibly more as an implementation
            # detail for private partitions, etc. depending on vendor).
            provides = module.get("provides", [])
            if provides:
                # a module is a primary module interface if it exports a logical name
                # that lacks partitions
                m = provides[0]
                if m["is-interface"]:
                    source.imports.append(source.path.as_posix())
                    logical_name = m["logical-name"]
                    source.module = logical_name
                    if ":" not in logical_name:
                        source.primary_module = logical_name
                        import_path = Path(*logical_name.split("."))

                        # __init__.cpp is a valid way of exporting a directory as a
                        # Python-style subpackage.
                        if source.path.stem == "__init__":
                            import_path /= "__init__"

                        # C++ does not enforce any specific meaning for dots in the
                        # module name, so we need to force them to conform with Python
                        # semantics.  This maintains consistency between the two, and
                        # makes binding generation much more intuitive/consistent.
                        if import_path != source.path.with_suffix(""):
                            expected_path = import_path.with_suffix(source.path.suffix)
                            rename = ".".join(source.path.with_suffix("").parts)
                            FAIL(
                                f"primary module interface '{YELLOW}{logical_name}{WHITE}' "
                                f"must be exported from:\n"
                                f"    + {GREEN}{expected_path.absolute()}{WHITE}\n"
                                f"    - {RED}{source.path.absolute()}{WHITE}\n"
                                f"\n"
                                f"... or be renamed to '{YELLOW}{rename}{WHITE}' to match "
                                f"Python semantics."
                            )

            # imports are represented by a "requires" array, which has one entry for
            # each import statement in the source file.  These need special handling,
            # since they may not be fully resolved within the build tree.
            source.imports.extend(
                p.as_posix()
                for edge in module.get("requires", [])
                for p in self._import(source.path, graph, edge, bindings)
            )

    def stage2(self) -> None:
        """TODO
        """
        try:
            cmakelists = self._stage2_cmakelists()
            self._cmake_build(cmakelists)
            self._parse_ast()
        finally:
            self._bertrand_binding_cache.unlink(missing_ok=True)
            self._bertrand_executable_cache.unlink(missing_ok=True)

    def _stage2_cmakelists(self) -> Path:
        path = Path(self.build_lib).absolute() / "CMakeLists.txt"
        with path.open("w") as f:
            f.write(self._cmakelists_header())
            f.write("# stage 2 uses a unique object for each source\n")
            f.write("\n")
            for ext in self.extensions:
                if ext.module and not ext.primary_module:
                    continue
                f.write(f"# source: {ext.path.absolute()}\n")
                f.write(f"add_library({ext.name} OBJECT\n")
                for source in ext.sources:
                    f.write(f"    {PosixPath(source).absolute()}\n")
                f.write(")\n")
                f.write(self._cmakelists_target(ext, ext.name, ast_plugin=True))

        return path

    def _cmake_build(self, cmakelists: Path) -> None:
        # building the project using the AST plugin will emit the Python bindings
        # automatically as part of compilation, so we don't need to do anything
        # special to trigger it.
        subprocess.check_call(
            [
                str(env / "bin" / "cmake"),
                "-G",
                "Ninja",
                str(cmakelists.parent),
                "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            ],
            cwd=cmakelists.parent,
            stdout=subprocess.PIPE,
        )
        build_args = [
            str(env / "bin" / "cmake"),
            "--build",
            ".",
            "--config",
            "Debug" if self.debug else "Release",  # TODO: probably not necessary
        ]
        if self.workers:
            build_args += ["--parallel", str(self.workers)]
        try:
            subprocess.check_call(build_args, cwd=cmakelists.parent)
        except subprocess.CalledProcessError:
            sys.exit()  # errors are already printed to the console

    def _parse_ast(self) -> None:
        # The AST plugin dumps the Python bindings to the build directory using a
        # nested directory structure under bindings/ that mirrors the source tree.  We
        # need to add these bindings to the appropriate Source objects so that they can
        # be included in the final build.
        for source in self.extensions:
            continue  # TODO: re-enable bindings
            if source.primary_module:
                bindings = self.get_python_path(source).relative_to(Path.cwd())
                source.sources.append(bindings.as_posix())

        # Additionally, the AST plugin produces a cache that notes all of the source
        # files that possess a `main()` entry point.  We mark these in order to build
        # them as executables in stage 3.
        if self._bertrand_executable_cache.exists():
            with self._bertrand_executable_cache.open("r") as f:
                for line in f:
                    path = Path(line.strip()).relative_to(Path.cwd()).as_posix()
                    self._bertrand_source_lookup[path].executable = True

    # TODO: stage3 AST can use .o objects that were compiled in stage 2 to avoid
    # recompiling the world
    # https://stackoverflow.com/questions/38609303/how-to-add-prebuilt-object-files-to-executable-in-cmake
    # https://groups.google.com/g/dealii/c/HIUzF7fPyjs

    # -> They should also use the .pcm files generated during stage 2 to avoid repeated
    # work.

    def stage3(self) -> None:
        """TODO
        """
        cmakelists = self._stage3_cmakelists()
        self._cmake_build(cmakelists)

        # make sure to copy the compilation database to the root of the source tree so
        # that clangd can use it for code completion, etc.
        shutil.copy2(
            cmakelists.parent / "compile_commands.json",
            "compile_commands.json"
        )

    def _stage3_cmakelists(self) -> Path:
        """Emit a final CMakeLists.txt file that includes semantically-correct
        shared library and executable targets based on the AST analysis, along with
        extra Python bindings to expose the modules to the interpreter.

        Returns
        -------
        Path
            The path to the generated CMakeLists.txt.
        """
        path = Path(self.build_lib).absolute() / "CMakeLists.txt"
        with path.open("w") as f:
            f.write(self._cmakelists_header())
            for ext in self.extensions:
                if ext.primary_module:
                    target = f"{ext.name}{sysconfig.get_config_var('EXT_SUFFIX')}"
                    f.write(f"# shared library: {ext.path.absolute()}\n")
                    f.write(f"add_library({target} MODULE\n")
                    for source in ext.sources:
                        f.write(f"    {PosixPath(source).absolute()}\n")
                    f.write(")\n")
                    f.write(self._cmakelists_target(ext, target, ast_plugin=False))

                if ext.executable:
                    f.write(f"# executable: {ext.path.absolute()}\n")
                    f.write(f"add_executable({ext.name}\n")
                    for source in ext.sources:
                        f.write(f"    {PosixPath(source).absolute()}\n")
                    f.write(")\n")
                    f.write(self._cmakelists_target(ext, ext.name, ast_plugin=False))

        return path

    # TODO: after everything is built, export the module interface files and their
    # dependencies to the env/modules/ directory, with the .cppm suffix.  Those source
    # files will be discovered by any other projects that are compiled in the same
    # environment.

    def build_extensions(self) -> None:
        """Build all extensions in the project.

        Raises
        ------
        RuntimeError
            If setup.py is invoked outside of a bertrand virtual environment.
        TypeError
            If any extensions are not of type bertrand.Source.
        """
        if self.extensions and "BERTRAND_HOME" not in os.environ:
            FAIL(
                "setup.py must be run inside a bertrand virtual environment in order "
                "to compile C++ extensions."
            )

        if self._bertrand_cpp_deps:
            self.conan_install()

        self.stage1()
        self.stage2()
        self.stage3()

    # TODO: if files are copied out of the build directory, then they should be added
    # to a persistent registry so that they can be cleaned up later with a simple
    # command.  This is not necessary unless the project is being built inplace (i.e.
    # through `$ bertrand compile`), in which case the copy_extensions_to_source()
    # method will be called.  It should update a temp file in the registry with the
    # paths of all copied files, and then the clean command should read this file and
    # delete all the paths listed in it if they exist.
    # -> perhaps this is modeled as a directory within the virtual environment that
    # replicates the root directory structure.  Whenever `$ bertrand clean` is called,
    # we glob all the files in this directory and delete them, reflecting any changes
    # to the equivalent files in the source directory.  If `$ bertrand clean` is given
    # a particular file or directory, then it will only delete that file or directory
    # from the build directory.  If called without any arguments, it will clean the
    # whole environment.

    # TODO: `$ bertrand compile`` should also have an --install option that will copy
    # executables and shared libraries into the environment's bin and lib directories.
    # These should also be reflected in the `$ bertrand clean` command, which should
    # remove them if they exist, forcing a recompile.  `pip install .` will bypass
    # this by not writing them to the environment, which will disregard them from the
    # cleaning process unless they are recompiled with `$ bertrand compile`.

    def copy_extensions_to_source(self) -> None:
        """Copy executables as well as shared libraries to the source directory if
        setup.py was invoked with the --inplace option.
        """
        for ext in self.extensions:
            lib_path = Path(self.build_lib) / f"{ext.name}{sysconfig.get_config_var('EXT_SUFFIX')}"
            exe_path = Path(self.build_lib) / ext.name
            if lib_path.exists():
                self.copy_file(
                    lib_path,
                    self.get_ext_fullpath(ext.name),
                    level=self.verbose,  # type: ignore
                )
            if exe_path.exists():
                new_path = Path(self.get_ext_fullpath(ext.name)).parent
                idx = ext.name.rfind(".")
                if idx < 0:
                    new_path /= ext.name
                else:
                    new_path /= ext.name[idx + 1:]
                self.copy_file(exe_path, new_path, level=self.verbose)  # type: ignore


def setup(
    *,
    sources: list[Source] | None = None,
    cpp_std: int = 23,
    cpp_deps: Iterable[PackageLike] | None = None,
    traceback: bool = True,
    include_dirs: Iterable[str] | None = None,
    define_macros: Iterable[tuple[str, str]] | None = None,
    compile_args: Iterable[str] | None = None,
    library_dirs: Iterable[str] | None = None,
    libraries: Iterable[str] | None = None,
    link_args: Iterable[str] | None = None,
    cmake_args: dict[str, str] | None = None,
    runtime_library_dirs: Iterable[str] | None = None,
    export_symbols: Iterable[str] | None = None,
    workers: int = 0,
    cmdclass: dict[str, Any] | None = None,
    **kwargs: Any
) -> None:
    """A custom setup() function that automatically appends the BuildSources command to
    the setup commands.

    Parameters
    ----------
    sources : list[Source] | None, default None
        A list of C++ source files to build as extensions.  A separate Source should be
        given for every source file in the project, and the build targets and
        dependencies will be inferred from the AST analysis of the sources.
    cpp_deps : Iterable[PackageLike] | None, default None
        A list of C++ dependencies to install before building the extensions.  Each
        dependency should be specified as a string of the form
        `{name}/{version}@{find_package}/{target_link_libraries}`, where the
        `find_package` and `target_link_libraries` symbols are passed to the CMake
        commands of the same name.  These identifiers can typically be found by running
        `conan search ${package_name}` or by browsing conan.io.
    cmdclass : dict[str, Any] | None, default None
        A dictionary of command classes to override the default setuptools commands.
        If no setting is given for "build_ext", then it will be set to
        bertrand.setuptools.BuildSources.
    workers : int, default 0
        The number of parallel workers to use when building extensions.  If set to
        zero, then the build will use all available cores.  This can also be set
        through the command line by supplying either `--workers=n` or `-j n`.
    **kwargs : Any
        Arbitrary keyword arguments passed to the setuptools.setup() function.
    """
    deps: list[Package] = list(env.packages) if env else []
    for p in cpp_deps or []:
        package = Package(p, allow_shorthand=False)
        if package not in deps:
            deps.append(package)

    class _BuildSourcesWrapper(BuildSources):
        def __init__(self, *a: Any, **kw: Any):
            super().__init__(
                *a,
                bertrand_cpp_std=cpp_std,
                bertrand_cpp_deps=deps,
                bertrand_include_dirs=list(include_dirs) if include_dirs else [],
                bertrand_define_macros=list(define_macros) if define_macros else [],
                bertrand_compile_args=list(compile_args) if compile_args else [],
                bertrand_library_dirs=list(library_dirs) if library_dirs else [],
                bertrand_libraries=list(libraries) if libraries else [],
                bertrand_link_args=list(link_args) if link_args else [],
                bertrand_cmake_args=dict(cmake_args) if cmake_args else {},
                bertrand_runtime_library_dirs=
                    list(runtime_library_dirs) if runtime_library_dirs else [],
                bertrand_export_symbols=list(export_symbols) if export_symbols else [],
                bertrand_workers=workers,
                **kw
            )

    if cmdclass is None:
        cmdclass = {"build_ext": _BuildSourcesWrapper}
    elif "build_ext" not in cmdclass:
        cmdclass["build_ext"] = _BuildSourcesWrapper
    else:
        cmd: type = cmdclass["build_ext"]
        if issubclass(cmd, BuildSources):
            class _BuildSourcesSubclassWrapper(cmd):
                def __init__(self, *a: Any, **kw: Any):
                    super().__init__(
                        *a,
                        bertrand_cpp_std=cpp_std,
                        bertrand_cpp_deps=deps,
                        bertrand_include_dirs=
                            list(include_dirs) if include_dirs else [],
                        bertrand_define_macros=
                            list(define_macros) if define_macros else [],
                        bertrand_compile_args=
                            list(compile_args) if compile_args else [],
                        bertrand_library_dirs=
                            list(library_dirs) if library_dirs else [],
                        bertrand_libraries=
                            list(libraries) if libraries else [],
                        bertrand_link_args=
                            list(link_args) if link_args else [],
                        bertrand_cmake_args=
                            dict(cmake_args) if cmake_args else {},
                        bertrand_runtime_library_dirs=
                            list(runtime_library_dirs) if runtime_library_dirs else [],
                        bertrand_export_symbols=
                            list(export_symbols) if export_symbols else [],
                        bertrand_workers=workers,
                        **kw
                    )
            cmdclass["build_ext"] = _BuildSourcesSubclassWrapper

    sources = sources or []
    for source in sources:
        if not source.cpp_std:
            source.cpp_std = cpp_std

        if source.traceback is None:
            source.traceback = traceback

        if source.traceback:
            source.extra_compile_args.append("-g")
        else:
            source.define_macros.append(("BERTRAND_NO_TRACEBACK", None))

    setuptools.setup(
        ext_modules=sources,
        cmdclass=cmdclass,
        **kwargs
    )
