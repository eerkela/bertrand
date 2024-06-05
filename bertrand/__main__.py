"""Run Bertrand from the command line to get include directory, version number, etc.
"""
import argparse
from pathlib import Path

from .cli.env import (
    Environment, current_env, get_bin, get_include, get_lib, get_link
)
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

    def init(self) -> None:
        """Add the 'init' command to the parser."""
        command = self.commands.add_parser(
            "init",
            help=(
                "Bootstrap a virtual environment with a full C/C++ compiler suite, "
                "toolchain, Python distribution, and associated package managers.  "
                "This can take approximately 15-20 minutes to complete, and the "
                "resulting environment is stored in the current working directory "
                "under the specified name (defaults to '/venv/').  It can be "
                "activated by sourcing the `activate` script in the environment "
                "directory (e.g. `$ source venv/activate`), and deactivated by "
                "running the `$ deactivate` command from the command line."
            ),
        )
        command.add_argument(
            "name",
            nargs="?",
            default="venv",
            help=(
                "The name of the virtual environment to create.  This sets both the "
                "environment's directory name and the command-line prompt within the "
                "virtual environment.  Defaults to 'venv'."
            ),
        )
        command.add_argument(
            "-f", "--force",
            action="store_true",
            help=(
                "Force the virtual environment to be rebuilt from the ground up.  "
                "This deletes the existing environment and all packages that are "
                "contained within it, and then reinstalls the environment from scratch."
            ),
        )
        command.add_argument(
            "-j", "--jobs",
            type=int,
            nargs=1,
            default=0,
            help=(
                "The number of parallel workers to run when building the "
                "environment.  Defaults to 0, which uses all available CPUs.  Must be "
                "a positive integer."
            ),
            metavar="N",
        )

        # compiler options
        compilers = command.add_argument_group(title="compilers").add_mutually_exclusive_group()
        compilers.add_argument(
            "--gcc",
            nargs=1,
            default="latest",
            help=(
                "[DEFAULT] Use the specified GCC version as the environment's "
                "compiler.  The version must be >=14.1.0.  If no version is "
                "specified, then the most recent release will be used.  "
            ),
            metavar="X.Y.Z",
        )
        compilers.add_argument(
            "--clang",
            nargs=1,
            default="latest",
            help=(
                "Use the specified Clang version as the environment's compiler.  The "
                "version must be >=18.0.0.  If no version is specified, then the most "
                "recent release will be used."
            ),
            metavar="X.Y.Z",
        )

        # generator options
        generators = command.add_argument_group(title="generators").add_mutually_exclusive_group()
        generators.add_argument(
            "--ninja",
            nargs=1,
            default="latest",
            help=(
                "[DEFAULT] Set the build system generator within the virtual "
                "environment to Ninja.  Uses the same version scheme as the compiler, "
                "and must be >=1.11."
            ),
            metavar="X.Y.Z",
        )

        # build systems
        build_systems = command.add_argument_group(title="build systems").add_mutually_exclusive_group()
        build_systems.add_argument(
            "--cmake",
            nargs=1,
            default="latest",
            help=(
                "[DEFAULT] Set the CMake version to use within the virtual "
                "environment.  Uses the same version scheme as the compiler, and must "
                "be >=3.28."
            ),
            metavar="X.Y.Z",
        )

        # python options
        python = command.add_argument_group(title="python").add_mutually_exclusive_group()
        python.add_argument(
            "--python",
            nargs=1,
            default="latest",
            help=(
                "[DEFAULT] Set the Python version to use within the virtual "
                "environment.  Uses the same version scheme as the compiler, and must "
                "be >=3.12."
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

    def version(self) -> None:
        """Add the 'version' query to the parser."""
        self.root.add_argument("-v", "--version", action="version", version=__version__)

    def binaries(self) -> None:
        """Add the 'binaries' query to the parser."""
        self.root.add_argument(
            "-b", "--binaries",
            action="store_true",
            help=(
                "List the path to the virtual environment's binaries directory.  This "
                "includes the path to the C++ compiler, Python interpreter, and any "
                "other binaries that are installed within the environment."
            )
        )

    def include(self) -> None:
        """Add the 'include' query to the parser."""
        self.root.add_argument(
            "-I", "--include",
            action="store_true",
            help=(
                "List all the include paths needed to compile a pure-C++ project that "
                "relies on Bertrand as a dependency.  This includes the path to the "
                "C++ standard library, Python development headers, as well as those "
                "of any C++ dependency installed through conan.  Users can quickly "
                "include all of these in a single command by adding `$(bertrand -I)` "
                "to their compilation flags."
            )
        )

    def libraries(self) -> None:
        """Add the 'libraries' query to the parser."""
        self.root.add_argument(
            "-L", "--libraries",
            action="store_true",
            help=(
                "List all the library paths needed to compile a pure-C++ project that "
                "relies on Bertrand as a dependency.  This includes the C++ standard "
                "library, Python standard library, as well as those of any C++ "
                "dependency installed through conan.  Users can quickly link all of "
                "these in a single command by adding `$(bertrand -L)` to their "
                "compilation flags."
            )
        )

    def link(self) -> None:
        """Add the 'link' query to the parser."""
        self.root.add_argument(
            "-l", "--link",
            action="store_true",
            help=(
                "List all the link symbols needed to compile a pure-C++ project that "
                "relies on Bertrand as a dependency.  This includes the C++ standard "
                "library, Python standard library, as well as those of any C++ "
                "dependency installed through conan.  Users can quickly link all of "
                "these in a single command by adding `$(bertrand -l)` to their "
                "compilation flags."
            )
        )

    def __call__(self) -> argparse.Namespace:
        """Run the command-line parser.

        Returns
        -------
        argparse.Namespace
            The parsed command-line arguments.
        """
        # commands
        self.init()
        self.activate()
        self.deactivate()

        # queries
        self.version()
        self.binaries()
        self.link()
        self.include()
        self.libraries()

        return self.root.parse_args()


def main() -> None:
    """Run Bertrand as a command-line utility."""
    parser = Parser()
    args = parser()

    if args.command == "init":
        compiler = next((k for k in ["gcc", "clang"] if getattr(args, k)), "gcc")
        compiler_version = getattr(args, compiler)
        generator = next((k for k in ["ninja"] if getattr(args, k)), "ninja")
        generator_version = getattr(args, generator)
        Environment(
            Path.cwd(),
            name=args.name or "venv",
            compiler=compiler,
            compiler_version=compiler_version,
            generator=generator,
            generator_version=generator_version,
            cmake_version=args.cmake,
            python_version=args.python,
            workers=args.jobs,
        ).create()

    elif args.command == "activate":
        Environment.activate(args.file)

    elif args.command == "deactivate":
        Environment.deactivate()

    elif args.binaries:
        print(get_bin())

    elif args.include:
        print(" ".join(f"-I{path}" for path in get_include()))

    elif args.libraries:
        print(" ".join(f"-L{path}" for path in get_lib()))

    elif args.link:
        print(" ".join(f"-l{symbol}" for symbol in get_link()))

    else:
        parser.root.print_help()


if __name__ == "__main__":
    main()
