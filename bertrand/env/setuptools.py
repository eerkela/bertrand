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

from .codegen import PyModule, CppModule
from .environment import env
from .package import Package, PackageLike
from .version import __version__


# TODO: eventually, get_include() won't be necessary, since all the headers will be
# converted into modules.


def get_include() -> str:
    """Get the path to the include directory for this package, which is necessary to
    make C++ headers available to the compiler.

    Returns
    -------
    str
        The path to the include directory for this package.
    """
    return str(Path(__file__).absolute().parent.parent.parent)


class Source(setuptools.Extension):
    """Describes an arbitrary source file that can be scanned for dependencies and used
    to generate automated bindings.  One of these is constructed for every source file
    in the project.

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
    """

    def __init__(
        self,
        path: Path,
        *,
        cpp_std: int = 0,
        traceback: bool | None = None,
        extra_cmake_args: dict[str, str] | None = None,
        **kwargs: Any
    ) -> None:
        if not path.exists():
            raise FileNotFoundError(f"source file does not exist: {path}")
        if path.is_dir():
            raise OSError(f"source file is a directory: {path}")
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
        self.module = False
        self.primary_module = False
        self.executable = False

        self.include_dirs.append(get_include())  # TODO: eventually not necessary

    def __repr__(self) -> str:
        return f"<Source {self.path}>"


# TODO: codegen takes in the JSON output of bertrand-ast and generates a corresponding
# C++ file that exports it to Python.  If working with an unresolved import, then it
# just attempts the import from Python to generate the binding file.


class BuildSources(setuptools_build_ext):
    """A custom build_ext command that uses CMake to build extensions with support for
    C++20 modules, parallel builds, clangd, executable targets, and bertrand's core
    dependencies without any extra configuration.
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
        cpp_std: int,
        cpp_deps: list[Package],
        include_dirs: list[str],
        define_macros: list[tuple[str, str]],
        compile_args: list[str],
        library_dirs: list[str],
        libraries: list[str],
        link_args: list[str],
        cmake_args: dict[str, str],
        runtime_library_dirs: list[str],
        export_symbols: list[str],
        workers: int,
        **kwargs: Any,
    ) -> None:
        super().__init__(*args, **kwargs)
        self.workers = workers
        self.bertrand: dict[str, Any] = {
            "cpp_std": cpp_std,
            "cpp_deps": cpp_deps,
            "include_dirs": include_dirs,
            "define_macros": define_macros,
            "compile_args": compile_args,
            "library_dirs": library_dirs,
            "libraries": libraries,
            "link_args": link_args,
            "cmake_args": cmake_args,
            "runtime_library_dirs": runtime_library_dirs,
            "export_symbols": export_symbols,
            # "source_lookup": {},  # populated in finalize_options
        }

    def finalize_options(self) -> None:
        """Parse command-line options and convert them to the appropriate types.

        Raises
        ------
        ValueError
            If the workers option is not set to a positive integer or 0.
        """
        super().finalize_options()
        self.bertrand["source_lookup"] = {s.sources[0]: s for s in self.extensions}
        if "-fdeclspec" not in self.bertrand["compile_args"]:
            self.bertrand["compile_args"].append("-fdeclspec")

        cpath = env.get("CPATH", "")
        if cpath:
            self.bertrand["include_dirs"] = (
                self.bertrand["include_dirs"] + cpath.split(os.pathsep)
            )

        ld_library_path = env.get("LD_LIBRARY_PATH", "")
        if ld_library_path:
            self.bertrand["library_dirs"] = (
                self.bertrand["library_dirs"] + ld_library_path.split(os.pathsep)
            )

        runtime_library_path = env.get("RUNTIME_LIBRARY_PATH", "")
        if runtime_library_path:
            self.bertrand["runtime_library_dirs"] = (
                self.bertrand["runtime_library_dirs"] +
                runtime_library_path.split(os.pathsep)
            )

        cxxflags = env.get("CXXFLAGS", "")
        if cxxflags:
            self.bertrand["compile_args"] = shlex.split(cxxflags) + self.bertrand["compile_args"]

        ldflags = env.get("LDFLAGS", "")
        if ldflags:
            self.bertrand["link_args"] = shlex.split(ldflags) + self.bertrand["link_args"]

        if self.workers:
            self.workers = int(self.workers)
            if self.workers == 0:
                self.workers = os.cpu_count() or 1
            elif self.workers < 0:
                raise ValueError(
                    "workers must be set to a positive integer or 0 to use all cores"
                )

    def write_conanfile(self) -> Path:
        """Emit a conanfile.txt file to the build directory with the necessary
        dependencies.

        Returns
        -------
        Path
            The path to the generated conanfile.txt.
        """
        path = Path(self.build_lib) / "conanfile.txt"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w") as f:
            f.write("[requires]\n")
            for package in self.bertrand["cpp_deps"]:
                f.write(f"{package.name}/{package.version}\n")
            f.write("\n")
            f.write("[generators]\n")
            f.write("CMakeDeps\n")
            f.write("CMakeToolchain\n")
            f.write("\n")
            f.write("[layout]\n")
            f.write("cmake_layout\n")

        return path

    def conan_install(self, conanfile: Path) -> None:
        """Invoke conan to install C++ dependencies for the project and link them
        against the environment.

        Parameters
        ----------
        conanfile : Path
            The path to the conanfile.txt to install.
        """
        subprocess.check_call(
            [
                "conan",
                "install",
                str(conanfile.absolute()),
                "--build=missing",
                "--output-folder",
                str(conanfile.parent),
                "-verror",
            ],
            cwd=conanfile.parent,
        )
        env.packages.extend(p for p in self.bertrand["cpp_deps"] if p not in env.packages)

    def _cmakelists_header(self) -> str:
        build_type = "Debug" if self.debug else "Release"
        toolchain = (
            Path(self.build_lib) / "build" / build_type / "generators" /
            "conan_toolchain.cmake"
        )
        libraries = self.bertrand["libraries"] + [p.link for p in env.packages]

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
        for k, v in self.bertrand["cmake_args"].items():
            out += f"set({k} {v})\n"
        if self.bertrand["define_macros"]:
            out += "add_compile_definitions(\n"
            for define in self.bertrand["define_macros"]:
                out += f"    {define[0]}={define[1]}\n"
            out += ")\n"
        if self.bertrand["compile_args"]:
            out += "add_compile_options(\n"
            for flag in self.bertrand["compile_args"]:
                out += f"    {flag}\n"
            out += ")\n"
        if self.bertrand["link_args"]:
            out += "add_link_options(\n"
            for flag in self.bertrand["link_args"]:
                out += f"    {flag}\n"
            out += ")\n"
        out += "\n"
        out += "# package management\n"
        out += f"include({toolchain})\n"
        out += "\n".join(f'find_package({p.find} REQUIRED)' for p in env.packages)
        out += "\n"
        if self.bertrand["include_dirs"]:
            out += "include_directories(\n"
            for include in self.bertrand["include_dirs"]:
                out += f"    {include}\n"
            out += ")\n"
        if self.bertrand["library_dirs"]:
            out += "link_directories(\n"
            for lib_dir in self.bertrand["library_dirs"]:
                out += f"    {lib_dir}\n"
            out += ")\n"
        if libraries:
            out += "link_libraries(\n"
            for lib in libraries:
                out += f"    {lib}\n"
            out += ")\n"
        if self.bertrand["runtime_library_dirs"]:
            out += "set(CMAKE_INSTALL_RPATH\n"
            for lib_dir in self.bertrand["runtime_library_dirs"]:
                out += f"    \"{lib_dir}\"\n"
            out += ")\n"
        # TODO: what the hell to do with export_symbols?
        out += "\n"
        return out

    def _cmakelists_target(self, source: Source, target: str, ast_plugin: bool) -> str:
        out = f"target_sources({target} PRIVATE\n"
        out +=  "    FILE_SET CXX_MODULES\n"
        out += f"    BASE_DIRS {Path.cwd()}\n"
        out +=  "    FILES\n"
        for s in source.sources:
            if self.bertrand["source_lookup"][s].module:
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
        if source.extra_compile_args or ast_plugin:
            out += f"target_compile_options({target} PRIVATE\n"
            if ast_plugin:
                out += f"    -fplugin={env / 'lib' / 'bertrand-ast.so'}\n"
                if source.primary_module:
                    out += f"    -fplugin-arg-export-python={Path('a/b/c.cpp')}\n"
            for flag in source.extra_compile_args:
                out += f"    {flag}\n"
            out += ")\n"
        if source.extra_link_args or source.runtime_library_dirs:
            out += f"target_link_options({target} PRIVATE\n"
            for flag in source.extra_link_args:
                out += f"    {flag}\n"
            if source.runtime_library_dirs:
                for lib_dir in source.runtime_library_dirs:
                    out += f"    \"-Wl,-rpath,{lib_dir}\"\n"
            out += ")\n"
        if source.define_macros:
            out += f"target_compile_definitions({target} PRIVATE\n"
            for define in source.define_macros:
                out += f"    {define[0]}={define[1]}\n"
            out += ")\n"
        # TODO: what the hell to do with export_symbols?
        out += "\n"
        return out

    def get_compile_commands(self, cmakelists: Path) -> Path:
        """Configure a CMakeLists.txt file to emit an associated compile_commands.json
        file that can be passed to `clang-scan-deps` and `bertrand-ast`.

        Parameters
        ----------
        cmakelists : Path
            The path to the CMakeLists.txt file to configure.

        Returns
        -------
        Path
            The path to the generated compile_commands.json file.  Since we're only
            configuring the project at this stage, the resulting file will have any
            lazily-evaluated cmake arguments (any commands that begin with `@`)
            stripped from it.
        """
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

        # filter out any lazily-evaluated arguments that might not be present at the
        # time this method is invoked, so that it can be used with clang tooling
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

    def cmake_build(self, cmakelists: Path) -> Path:
        """Invoke CMake to compile extensions and build the project using the generated
        CMakeLists.txt.

        Parameters
        ----------
        cmakelists : Path
            The path to the CMakeLists.txt file to use for the build.

        Returns
        -------
        Path
            The compile_commands.json file that was generated during the build.
        """
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

        return cmakelists.parent / "compile_commands.json"

    def write_stage1_cmakelists(self) -> Path:
        """Emit a preliminary CMakeLists.txt file that includes all sources as a single
        shared library target, which can be scanned using `clang-scan-deps` to
        determine module dependencies.

        Returns
        -------
        Path
            The path to the generated CMakeLists.txt.
        """
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
            f.write("# stage 1 shared library includes all sources\n")
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
            f.write(f"    CXX_STANDARD {self.bertrand['cpp_std']}\n")
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

    def resolve_imports(self, compile_commands: Path) -> None:
        """Generate a p1689 dependency graph to organize the sources and generate
        Python bindings for unresolved C++ imports.

        Parameters
        ----------
        compile_commands : Path
            The path to the compile_commands.json file to scan.

        Notes
        -----
        This method acts by side effect on the extensions list, updating each source's
        dependencies with the resolved imports.
        """
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

        lookup = {source.path: source for source in self.extensions}
        trunk = Path("CMakeFiles") / f"{self.distribution.get_name()}.dir"
        cwd = Path.cwd()

        generated: dict[str, str] = {}
        generated_dir = Path(self.build_lib).absolute() / "generated"
        for module in p1689["rules"]:
            path = Path(module["primary-output"]).relative_to(trunk)
            path = (cwd.root / path).relative_to(cwd).with_suffix("")
            source = lookup[path]

            # identify primary module interfaces and mark the associated source
            provides = module.get("provides", [])
            if provides:
                source.module = True
                source.primary_module = any(
                    ":" not in edge["logical-name"] for edge in provides
                )

            # add dependencies
            for edge in module.get("requires", []):
                name = edge["logical-name"]
                if "source-path" in edge:
                    source.sources.append(
                        Path(edge["source-path"]).relative_to(cwd).as_posix()
                    )
                elif name in generated:
                    source.sources.append(generated[name])
                else:
                    try:
                        py_import = importlib.import_module(name)
                    except ImportError:
                        print(f"Unresolved import '{name}' in source {source.path}")
                        sys.exit(1)

                    breakpoint()
                    generated_path = Path(*name.split(".")).with_suffix(".cpp")
                    PyModule(generated_dir / generated_path).generate(py_import)
                    posix_path = generated_path.as_posix()
                    generated[name] = posix_path
                    source.sources.append(posix_path)

    # TODO: it seems like the stage 2 build is inefficient, since it builds a separate
    # .o object for all dependencies of all sources, even if the same source is reused
    # across multiple libraries.

    def write_stage2_cmakelists(self) -> Path:
        """Emit an intermediate CMakeLists.txt file that includes all sources as
        separate shared libraries using the dependency information gathered from stage
        1.

        Returns
        -------
        Path
            The path to the generated CMakeLists.txt.

        Notes
        -----
        Configuring the resulting CMakeLists.txt will emit an updated
        compile_commands.json file that is complete enough to be used for AST parsing.
        """
        path = Path(self.build_lib).absolute() / "CMakeLists.txt"
        with path.open("w") as f:
            f.write(self._cmakelists_header())
            f.write("# stage 2 uses a unique shared library for each source\n")
            f.write("\n")
            for ext in self.extensions:
                f.write(f"# source: {ext.path.absolute()}\n")
                f.write(f"add_library({ext.name} OBJECT\n")
                for source in ext.sources:
                    f.write(f"    {PosixPath(source).absolute()}\n")
                f.write(")\n")
                f.write(self._cmakelists_target(ext, ext.name, ast_plugin=True))

        return path

    def parse_ast(self, compile_commands: Path) -> None:
        """Analyze the AST of the C++ sources to generate Python bindings, discover
        executable targets, and cull any unexported sources.

        Parameters
        ----------
        compile_commands : Path
            The path to the compile_commands.json file to supplement the AST parser.
        """

        generated: dict[Path, str] = {}
        generated_dir = Path(self.build_lib).absolute() / "generated"
        for source in self.extensions:
            raw_ast = subprocess.run(
                [
                    str(env / "bin" / "bertrand-ast"),
                    "-p",
                    str(compile_commands.parent),  # use stage 2 compile_commands.json
                    str(source.path.absolute()),
                ],
                check=True,
                stdout=subprocess.PIPE,
            ).stdout.decode("utf-8").strip()
            ast = json.loads(raw_ast)
            breakpoint()

            if ast["has_main"]:
                source.executable = True

            if source.primary_module:
                if source.path in generated:
                    source.sources.append(generated[source.path])
                else:
                    breakpoint()
                    CppModule(generated_dir / source.path).generate(ast)
                    posix_path = source.path.as_posix()
                    generated[source.path] = posix_path
                    source.sources.append(posix_path)

    # TODO: stage3 AST can use .o objects that were compiled in stage 2 to avoid
    # recompiling the world
    # https://stackoverflow.com/questions/38609303/how-to-add-prebuilt-object-files-to-executable-in-cmake
    # https://groups.google.com/g/dealii/c/HIUzF7fPyjs

    def write_stage3_cmakelists(self) -> Path:
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
                    f.write(self._cmakelists_target(ext, target, ast_plugin=False))

        return path

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
            raise RuntimeError(
                "setup.py must be run inside a bertrand virtual environment in order "
                "to compile C++ extensions"
            )

        # force the use of the coupled Source class to describe build targets
        self.check_extensions_list(self.extensions)
        incompabile_extensions = [
            ext for ext in self.extensions if not isinstance(ext, Source)
        ]
        if incompabile_extensions:
            raise TypeError(
                f"Extensions must be of type bertrand.Source: "
                f"{incompabile_extensions}"
            )

        # stage 0: install conan dependencies
        if self.bertrand["cpp_deps"]:
            self.conan_install(self.write_conanfile())

        # stage 1: determine module graph using clang-scan-deps and resolve imports
        cmakelists = self.write_stage1_cmakelists()
        compile_commands = self.get_compile_commands(cmakelists)
        self.resolve_imports(compile_commands)

        # stage 2: parse AST to generate Python bindings and categorize targets
        cmakelists = self.write_stage2_cmakelists()
        compile_commands = self.cmake_build(cmakelists)
        breakpoint()
        # compile_commands = self.get_compile_commands(cmakelists)
        self.parse_ast(compile_commands)

        # stage 3: build the final project with proper dependencies and bindings
        cmakelists = self.write_stage3_cmakelists()
        self.cmake_build(cmakelists)
        shutil.copy2(
            cmakelists.parent / "compile_commands.json",
            "compile_commands.json"
        )


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
                cpp_std=cpp_std,
                cpp_deps=deps,
                include_dirs=list(include_dirs) if include_dirs else [],
                define_macros=list(define_macros) if define_macros else [],
                compile_args=list(compile_args) if compile_args else [],
                library_dirs=list(library_dirs) if library_dirs else [],
                libraries=list(libraries) if libraries else [],
                link_args=list(link_args) if link_args else [],
                cmake_args=dict(cmake_args) if cmake_args else {},
                runtime_library_dirs=
                    list(runtime_library_dirs) if runtime_library_dirs else [],
                export_symbols=list(export_symbols) if export_symbols else [],
                workers=workers,
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
                        cpp_std=cpp_std,
                        cpp_deps=deps,
                        include_dirs=list(include_dirs) if include_dirs else [],
                        define_macros=list(define_macros) if define_macros else [],
                        compile_args=list(compile_args) if compile_args else [],
                        library_dirs=list(library_dirs) if library_dirs else [],
                        libraries=list(libraries) if libraries else [],
                        link_args=list(link_args) if link_args else [],
                        cmake_args=dict(cmake_args) if cmake_args else {},
                        runtime_library_dirs=
                            list(runtime_library_dirs) if runtime_library_dirs else [],
                        export_symbols=list(export_symbols) if export_symbols else [],
                        workers=workers,
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
