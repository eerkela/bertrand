"""Run Bertrand from the command line to get include directory, version number, etc.
"""
import argparse
import subprocess
from pathlib import Path

from .env import init, env, clean
from . import __version__


class Parser:
    """Command-line parser for Bertrand utilities."""

    def __init__(self) -> None:
        self.root = argparse.ArgumentParser(
            description="Command line utilities for bertrand.",
        )
        self.commands = self.root.add_subparsers(
            dest="command",
            title="commands",
            description=(
                "Create and manage Python/C/C++ virtual environments, package "
                "managers, and streamlined build tools."
            ),
            prog="bertrand",
            metavar="(command)",
        )

    def version(self) -> None:
        """Add the 'version' query to the parser."""
        self.root.add_argument("-v", "--version", action="version", version=__version__)

    def init(self) -> None:
        """Add the 'init' command to the parser."""
        command = self.commands.add_parser(
            "init",
            help=(
                "Bootstrap a virtual environment with a full C/C++ compiler suite, "
                "toolchain, Python distribution, and associated package managers.  "
                "This can take anywhere from ~15 to ~45 minutes to complete, "
                "depending on hardware and configuration.  The resulting environment "
                "is stored in the current working directory under the specified name "
                "(defaults to '/venv/').  It can be activated by sourcing the "
                "`activate` script within the environment directory (e.g. "
                "`$ source venv/activate`), and deactivated by running the "
                "`$ deactivate` command from the command line."
            ),
        )
        command.add_argument(  # TODO: this should be required?
            "name",
            nargs=1,
            help=(
                "The name of the virtual environment to create.  This sets both the "
                "environment's directory name and the command-line prompt within it."
            ),
        )
        command.add_argument(
            "-f", "--force",
            action="store_true",
            help=(
                "Force the virtual environment to be rebuilt from the ground up.  "
                "This deletes the existing environment and all packages that are "
                "contained within it, and then reinstalls the environment from "
                "scratch."
            ),
        )
        command.add_argument(
            "-j", "--jobs",
            type=int,
            nargs=1,
            default=[0],
            help=(
                "The number of parallel workers to run when building the "
                "environment.  Defaults to 0, which uses all available CPUs.  Must be "
                "a positive integer."
            ),
            metavar="N",
        )
        command.add_argument(
            "--swap",
            type=int,
            nargs=1,
            default=[0],
            help=(
                "Allocate a temporary swap file with the specified size in GB.  This "
                "is commonly used when bootstrapping a local environment from source, "
                "as doing so is very resource intensive.  Allocating a large enough "
                "swap file can allow environments to be built on systems with limited "
                "RAM.  Defaults to 0, which disables swap file creation.  Requires "
                "root privileges if set to a non-zero value."
            )
        )
        command.add_argument(
            "--bootstrap",
            action="store_true",
            help=(
                "Bootstrap a virtual environment from source, rather than downloading "
                "a precompiled image.  This is the default behavior if no image can "
                "be found for the current system, or if manually forced through the "
                "use of this flag.  Most of the time, there's no benefit to building "
                "this way, and doing so can be time-consuming and brittle, so it's "
                "not recommended in general.  It can potentially lead to faster "
                "binaries, however, as the compiler will be built in situ and "
                "optimized for the host architecture, rather than a generic one.  "
                "Note that bootstrapping requires the host system to have a working "
                "C/C++ compiler, Python, Make, and CMake preinstalled, which can be "
                "done through the system's package manager."
            )
        )
        command.add_argument(
            "--ninja",
            nargs=1,
            default=["latest"],
            help=(
                "Set the ninja version to use within the virtual environment.  Uses "
                "the same version scheme as the compiler, and must be >=1.11."
            ),
            metavar="X.Y.Z",
        )
        command.add_argument(
            "--clang",
            nargs=1,
            default=["latest"],
            help=(
                "Use the specified Clang version as the environment's compiler.  The "
                "version must be >=18.0.0.  If set to 'latest', then the most recent "
                "release will be used."
            ),
            metavar="X.Y.Z",
        )
        command.add_argument(
            "--cmake",
            nargs=1,
            default=["latest"],
            help=(
                "Set the CMake version to use within the virtual environment.  Uses "
                "the same version scheme as the compiler, and must be >=3.28."
            ),
            metavar="X.Y.Z",
        )
        command.add_argument(
            "--mold",
            nargs=1,
            default=["latest"],
            help=(
                "Set the default linker within the virtual environment to mold.  Uses "
                "the same version scheme as the compiler.  This is the most efficient "
                "linker for C++ projects, and is recommended for most use cases."
            ),
            metavar="X.Y.Z",
        )
        command.add_argument(
            "--python",
            nargs=1,
            default=["latest"],
            help=(
                "Set the Python version to use within the virtual environment.  Uses "
                "the same version scheme as the compiler, and must be >=3.9."
            ),
            metavar="X.Y.Z",
        )
        command.add_argument(
            "--conan",
            nargs=1,
            default=["latest"],
            help=(
                "Install the Conan package manager within the virtual environment.  "
                "This is necessary for installing C++ dependencies into the "
                "environment, and will be installed by default.  Setting this option "
                "allows users to choose a specific version of Conan to install, which "
                "must be >=2.0.0.  Defaults to 'latest'."
            ),
            metavar="X.Y.Z",
        )

    def activate(self) -> None:
        """Add the 'activate' command to the parser."""
        command = self.commands.add_parser(
            "activate",
            help=(
                "Print a sequence of bash commands that will be used to activate the "
                "virtual environment when the activation script is sourced.  This "
                "method parses the environment variables from a TOML file (typically "
                "'venv/env.toml') and prints them to stdout as bash commands, which "
                "are caught in the activation script and exported into the resulting "
                "environment.  See the docs for Environment::activate() for more "
                "details."
            ),
        )
        command.add_argument(
            "file",
            type=Path,
            nargs=1,
            help="The path to the TOML file containing the environment variables."
        )

    def deactivate(self) -> None:
        """Add the 'deactivate' command to the parser."""
        self.commands.add_parser(
            "deactivate",
            help=(
                "Print a sequence of bash commands that will be used to deactivate "
                "the virtual environment when the deactivation script is sourced.  "
                "This method undoes the changes made by the activation script, "
                "restoring the environment variables to their original state."
            )
        )

    # TODO: conan has different syntax for install and uninstall compared to pip, so
    # I'll have to think about how best to handle it.

    def install(self) -> None:
        """Add the 'install' command to the parser."""
        command = self.commands.add_parser(
            "install",
            help=(
                "Install a C++ dependency into the virtual environment using the "
                "Conan package manager.  This command will download the specified "
                "package from the Conan repository and then install it into the "
                "virtual environment similar to pip."
            )
        )
        command.add_argument(
            "package",
            nargs=1,
            help=(
                "Specifies the package to install.  Must be provided in the format: "
                "`package/version@find/link`, where the 'package/version' fragment is "
                "identical to a normal `conan install` command, and the 'find/link' "
                "fragment tells CMake how to locate the package's headers and "
                "libraries.  The `find` and `link` symbols can typically be found on "
                "the package's conan.io page under the CMake `find_package` and "
                "`target_link_libraries` examples.  After installing, this "
                "information will be saved to the environment's [[packages]] array, "
                "and will be automatically inserted whenever C/C++ code is compiled "
                "within it."
            ),
        )
        command.add_argument(
            "options",
            nargs="*",
            help=(
                "Additional options to pass to the Conan install command.  These are "
                "passed directly to Conan, and can be used to specify build options, "
                "compiler flags, or other package-specific settings.  For more "
                "information, see the Conan documentation."
            ),
        )

    def uninstall(self) -> None:
        """Add the 'uninstall' command to the parser."""
        command = self.commands.add_parser(
            "uninstall",
            help=(
                "Uninstall a C++ dependency from the virtual environment using the "
                "Conan package manager.  Identical to the `conan remove` command, "
                "except that it also updates the environment's [[packages]] array to "
                "reflect the new state."
            )
        )
        command.add_argument(
            "package",
            nargs=1,
            help=(
                "Specifies the package to uninstall.  Must be provided in the format: "
                "`package/version`, where the 'package/version' fragment is "
                "identical to a normal `conan install` command, and the 'find/link' "
                "fragment tells CMake how to locate the package's headers and "
                "libraries.  The `find` and `link` symbols can typically be found on "
                "the package's conan.io page under the CMake `find_package` and "
                "`target_link_libraries` examples.  After uninstalling, this "
                "information will be removed from the environment's [[packages]] array, "
                "and will no longer be inserted whenever C/C++ code is compiled within "
                "it."
            ),
        )

    # TODO: update command

    def compile(self) -> None:
        """Add the 'compile' command to the parser."""
        command = self.commands.add_parser(
            "compile",
            help=(
                "Compile a C++ project within the virtual environment by invoking "
                "bertrand's setuptools extensions.  This automatically generates "
                "equivalent Python bindings for all exported modules and installs the "
                "products into the environment's `bin/`, `lib/`, and `modules/` "
                "directories, as well as optionally "
            ),
        )
        command.add_argument(
            "path",
            nargs=1,
            type=Path,
            help=(
                "A path to a directory containing a `setup.py` script, which will be "
                "invoked to build the project.  The script should use bertrand's "
                "setuptools extensions to compile the project, which enables "
                "fine-grained control over the build process."
            ),
        )
        command.add_argument(
            "compiler_options",
            nargs=argparse.REMAINDER,
            help=(
                "Additional options to pass to the compiler's command line, which are "
                "passed as-is.  Note that dependencies (include paths, import "
                "statements, library paths, and link symbols) are automatically "
                "resolved by the environment, so users should not need to specify "
                "them manually."
            ),
        )

    def clean(self) -> None:
        """Add the 'clean' command to the parser."""
        command = self.commands.add_parser(
            "clean",
            help=(
                "Remove a project's build artifacts from the virtual environment.  "
                "This deletes all of the installed files outside of the project's "
                "build directory.  The build directory itself is left untouched, and "
                "can be used to rebuild the project without having to recompile it "
                "from scratch."
            ),
        )
        command.add_argument(
            "path",
            nargs=1,
            type=Path,
            help=(
                "A path to a directory containing a `setup.py` script, which will be "
                "searched against the environment cache to determine which files to "
                "delete.  The script should use bertrand's setuptools extensions to "
                "allow the environment to track the project's build artifacts."
            ),
        )

    def __call__(self) -> argparse.Namespace:
        """Run the command-line parser.

        Returns
        -------
        argparse.Namespace
            The parsed command-line arguments.
        """
        # queries
        self.version()

        # commands
        self.init()
        self.activate()
        self.deactivate()
        # self.install()
        self.compile()
        self.clean()

        return self.root.parse_args()


def main() -> None:
    """Run Bertrand as a command-line utility."""
    parser = Parser()
    args = parser()

    if args.command == "init":
        # TODO: generate a matching image URL
        if args.bootstrap:  # or not ping(url)
            init(
                Path.cwd(),
                args.name[0],
                clang_version=args.clang[0],
                ninja_version=args.ninja[0],
                cmake_version=args.cmake[0],
                mold_version=args.mold[0],
                python_version=args.python[0],
                conan_version=args.conan[0],
                swap=args.swap[0],
                workers=args.jobs[0],
                force=args.force,
            )
        else:
            print("Currently, environments can only be initialized by bootstrapping from")
            print("source.  Eventually, precompiled images will be available for download")
            print("from a remote server, but this has not yet been set up.  Please try")
            print("again with the --bootstrap (and possibly --swap=N) flag.")

    elif args.command == "activate":
        for command in env.activate(args.file[0]):
            print(command)

    elif args.command == "deactivate":
        for command in env.deactivate():
            print(command)

    elif args.command == "compile":
        # TODO: pass the compiler options to the compiler as an environment variable
        # that gets caught in the setup.py script.
        # -> inplace is always on by default?  If you're executing this command, then
        # presumably you want to be able to run executables and import modules inplace.
        # Use pip install if you want things to be installed globally within the
        # environment.
        # TODO: I can just append the compiler options to CFLAGS and LDFLAGS directly,
        # which avoids the need to modify the setup.py script at all.  That also makes
        # it simple to pass extra options like --inplace or --debug, etc.
        try:
            subprocess.check_call(
                ["python", "setup.py", "build_ext", *args.compiler_options],
                cwd=args.path[0]
            )
        except subprocess.CalledProcessError:
            raise  # TODO: delete this and rely on internal messaging
            pass  # error messages are already printed to stdout

    elif args.command == "clean":
        clean(args.path[0])

    else:
        parser.root.print_help()


if __name__ == "__main__":
    main()
