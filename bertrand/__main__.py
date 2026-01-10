"""Run Bertrand from the command line to get include directory, version number, etc.
"""
import argparse

from pathlib import Path
from typing import Callable

# from .env import activate, clean, deactivate, init
from .env import (
    install_docker,
    uninstall_docker,
    add_to_docker_group,
    remove_from_docker_group,
    create_environment,
    enter_environment,
    in_environment,
    stop_environment,
    delete_environment,
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

    def version(self) -> None:
        """Add the 'version' query to the parser."""
        self.root.add_argument("-v", "--version", action="version", version=__version__)

    def init(self) -> None:
        """Add the 'init' command to the parser."""
        command = self.commands.add_parser(
            "init",
            help=
                "Install and run Docker Engine if it is not already present.  If a "
                "path (relative or absolute) is provided as the next argument, then "
                "pull a Bertrand Docker image to that path in order to create a new "
                "virtual environment.  A directory will be created at the specified "
                "path, with a .bertrand.json file that links it to a corresponding "
                "Docker container managed by the activated Docker Engine daemon.  At "
                "minimum, the container will hold a full C/C++ compiler toolchain, "
                "bootstrapped Python distribution, and Bertrand's compiler plugins, "
                "which invoke the toolchain to build C/C++ extensions on-demand.  It "
                "may also contain preinstalled developer tools, like language servers, "
                "package managers, sanitizers, and AI assistants, depending on build "
                "flags.  Advanced users may also choose to swap out toolchain "
                "components, such as linkers, build systems, the base container "
                "image, compiler/Python versions, and more.",
        )
        command.add_argument(
            "path",
            nargs="?",
            help=
                "The relative path to the virtual environment to create, starting "
                "from the current working directory.  The path must include at least "
                "one component, which is the name of the environment to create.",
        )
        command.add_argument(
            "-y", "--yes",
            action="store_true",
            help=
                "Automatically answer 'yes' to all prompts during environment "
                "creation.  This is useful for scripting and automation, where user "
                "interaction is not possible or desired.  Note that some steps (such "
                "as installing Docker if it is not already present, or using it "
                "without sudo or docker group privileges) may still prompt the user "
                "for elevated permissions, and will not be affected by this flag.",
        )
        command.add_argument(
            "-f", "--force",
            action="store_true",
            help=
                "Force the Docker container to be rebuilt from the ground up.  This "
                "deletes the existing environment and all files contained within "
                "and then reinstalls the container from scratch.",
        )
        command.add_argument(
            "-j", "--jobs",
            type=int,
            nargs=1,
            default=[0],
            help=
                "The number of parallel workers to run when building the "
                "environment.  Defaults to 0, which uses all available CPUs.  Must be "
                "a positive integer.",
            metavar="N",
        )
        command.add_argument(
            "--swap",
            type=int,
            nargs=1,
            default=[0],
            help=
                "Allocate a temporary swap file with the specified size in GB.  This "
                "is commonly used when bootstrapping a local environment from source, "
                "as doing so is very resource intensive.  Allocating a large enough "
                "swap file can allow environments to be built on systems with limited "
                "RAM.  Defaults to 0, which disables swap file creation.  Requires "
                "root privileges if set to a non-zero value.",
        )
        command.add_argument(
            "--bootstrap",
            action="store_true",
            help=
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
                "done through the system's package manager.",
        )
        command.add_argument(
            "--image",
            nargs=1,
            default=["ubuntu:latest"],
            help=
                "The base Docker image to use for the virtual environment.  This can "
                "be any valid Docker image available on Docker Hub or a custom image "
                "hosted elsewhere.  Defaults to 'ubuntu:latest', which provides a "
                "stable and widely compatible base for most development tasks.",
        )
        command.add_argument(
            "--workspace",
            nargs=1,
            default=["workspace"],
            help=
                "The path within the container to mount the environment workspace.  "
                "Defaults to 'workspace', which creates a directory at the "
                "container's root level.  This directory is where users should place "
                "their project files and source code.",
        )
        # TODO: attempt to install the given shell when I start writing dockerfiles
        command.add_argument(
            "--shell",
            nargs=1,
            default=["bash -l"],
            help=
                "The shell command to execute when entering the environment.  "
                "Defaults to 'bash -l', which starts a login shell using Bash.  This "
                "can be changed to any other shell available within the container, "
                "such as 'zsh -l' or 'fish -l'.  The indicated shell will be "
                "installed if possible, if it is not already present in the container "
                "image.",
        )

        # TODO: figure out toolchain using dockerfiles
        command.add_argument(
            "--ninja",
            nargs=1,
            default=["latest"],
            help=
                "Set the ninja version to use within the virtual environment.  Uses "
                "the same version scheme as the compiler, and must be >=1.11.",
            metavar="X.Y.Z",
        )
        command.add_argument(
            "--clang",
            nargs=1,
            default=["latest"],
            help=
                "Use the specified Clang version as the environment's compiler.  The "
                "version must be >=18.0.0.  If set to 'latest', then the most recent "
                "release will be used.",
            metavar="X.Y.Z",
        )
        command.add_argument(
            "--cmake",
            nargs=1,
            default=["latest"],
            help=
                "Set the CMake version to use within the virtual environment.  Uses "
                "the same version scheme as the compiler, and must be >=3.28.",
            metavar="X.Y.Z",
        )
        command.add_argument(
            "--mold",
            nargs=1,
            default=["latest"],
            help=
                "Set the default linker within the virtual environment to mold.  Uses "
                "the same version scheme as the compiler.  This is the most efficient "
                "linker for C++ projects, and is recommended for most use cases.",
            metavar="X.Y.Z",
        )
        command.add_argument(
            "--python",
            nargs=1,
            default=["latest"],
            help=
                "Set the Python version to use within the virtual environment.  Uses "
                "the same version scheme as the compiler, and must be >=3.9.",
            metavar="X.Y.Z",
        )
        command.add_argument(
            "--conan",
            nargs=1,
            default=["latest"],
            help=
                "Install the Conan package manager within the virtual environment.  "
                "This is necessary for installing C++ dependencies into the "
                "environment, and will be installed by default.  Setting this option "
                "allows users to choose a specific version of Conan to install, which "
                "must be >=2.0.0.  Defaults to 'latest'.",
            metavar="X.Y.Z",
        )

    def enter(self) -> None:
        """Add the 'enter' command to the parser."""
        command = self.commands.add_parser(
            "enter",
            help=
                "Start an interactive shell session within a Bertrand virtual "
                "environment at the specified path relative to the current working "
                "directory.  If the container is not already running, it will be "
                "started automatically.  Once inside the shell, users can run "
                "commands as normal, with access to all its tools and libraries, "
                "fully isolated from the host system.  To exit the shell and return "
                "to the host system, simply run the 'exit' command (without a "
                "'bertrand' prefix).",
        )
        command.add_argument(
            "path",
            type=Path,
            nargs=1,
            help="The path to the environment directory to activate."
        )

    def enabled(self) -> None:
        """Add the 'enabled' query to the parser."""
        self.commands.add_parser(
            "enabled",
            help=
                "Check if the current shell session is running within a Bertrand "
                "virtual environment.  If so, the command exits with a status code of "
                "0; otherwise, it exits with a status code of 1.  This can be useful "
                "for scripts and automation that need to verify whether they are "
                "operating within an isolated environment or on the host system.",
        )

    def stop(self) -> None:
        """Add the 'stop' command to the parser."""
        command = self.commands.add_parser(
            "stop",
            help=
                "Stop a running Bertrand virtual environment at the specified path.  "
                "This halts the associated Docker container, freeing up system "
                "resources.  Note that stopping the environment does not delete any "
                "files; all data within the environment is preserved and can be "
                "accessed again by relaunching.",
        )
        command.add_argument(
            "path",
            type=Path,
            nargs=1,
            help="The path to the environment directory to stop."
        )
        command.add_argument(
            "-f", "--force",
            action="store_true",
            help=
                "Force stop the environment, even if there are active processes "
                "running within the container.  This will terminate all processes "
                "immediately, which may lead to data loss or corruption if they are "
                "in the middle of writing to files.",
        )

    def delete(self) -> None:
        """Add the 'delete' command to the parser."""
        command = self.commands.add_parser(
            "delete",
            help=
                "Delete a Bertrand virtual environment at the specified path.  This "
                "removes the environment directory and all files contained within, as "
                "well as deleting the associated Docker container managed by Docker "
                "Engine.  Use this command with caution, as it permanently deletes "
                "all data within the environment.  Cannot be run while inside a "
                "virtual environment shell.",
        )
        command.add_argument(
            "path",
            type=Path,
            nargs="?",
            help="The path to the environment directory to delete."
        )
        command.add_argument(
            "-y", "--yes",
            action="store_true",
            help=
                "Automatically answer 'yes' to all prompts during environment "
                "deletion.  This is useful for scripting and automation, where user "
                "interaction is not possible or desired.",
        )
        command.add_argument(
            "-f", "--force",
            action="store_true",
            help=
                "Force deletion of the environment, even if the Docker container is "
                "currently running.  This will stop the container if necessary before "
                "deleting it.",
        )
        command.add_argument(
            "--docker",
            action="store_true",
            help=
                "Delete Docker Engine and all its components from the host system.  "
                "This is only meant for testing purposes, or for sandboxed "
                "environments where Docker is solely used for Bertrand and nothing "
                "else.  Use with extreme caution, as this will permanently delete all "
                "other Docker images, containers, volumes, and networks from the host "
                "system as well.",
        )

    # TODO: status <env> / ls   -> display:
    # - env path
    # - container name
    # - running/stopped
    # - image + digest
    # - last start time (from inspect)
    # - mount correctness

    # TODO: bertrand doctor   -> check:
    # - docker CLI found
    # - daemon reachable
    # - permission mode (group vs sudo vs rootless)
    # - context/DOCKER_HOST
    # - minimal version check
    # - ability to docker pull a tiny test image (optional)

    # TODO: there are race conditions under concurrent `bertrand init` or `enter`
    # -> add a lock file in the environment directory

    # TODO: set retry/timeout policies for network ops

    # TODO: think about how workspaces are managed, and see if I can reuse the
    # container name, or something.  This may also need to be subject to drift
    # detection during relocation

    # TODO: think about bind mount UX, root ownership on host files, etc.  The real
    # way to do this is to match UIDs/GIDs between host and container


    # TODO: conan has different syntax for install and uninstall compared to pip, so
    # I'll have to think about how best to handle it.

    def install(self) -> None:
        """Add the 'install' command to the parser."""
        command = self.commands.add_parser(
            "install",
            help=
                "Install a C++ dependency into the virtual environment using the "
                "Conan package manager.  This command will download the specified "
                "package from the Conan repository and then install it into the "
                "virtual environment similar to pip.",
        )
        command.add_argument(
            "package",
            nargs=1,
            help=
                "Specifies the package to install.  Must be provided in the format: "
                "`package/version@find/link`, where the 'package/version' fragment is "
                "identical to a normal `conan install` command, and the 'find/link' "
                "fragment tells CMake how to locate the package's headers and "
                "libraries.  The `find` and `link` symbols can typically be found on "
                "the package's conan.io page under the CMake `find_package` and "
                "`target_link_libraries` examples.  After installing, this "
                "information will be saved to the environment's [[packages]] array, "
                "and will be automatically inserted whenever C/C++ code is compiled "
                "within it.",
        )
        command.add_argument(
            "options",
            nargs="*",
            help=
                "Additional options to pass to the Conan install command.  These are "
                "passed directly to Conan, and can be used to specify build options, "
                "compiler flags, or other package-specific settings.  For more "
                "information, see the Conan documentation.",
        )

    def uninstall(self) -> None:
        """Add the 'uninstall' command to the parser."""
        command = self.commands.add_parser(
            "uninstall",
            help=
                "Uninstall a C++ dependency from the virtual environment using the "
                "Conan package manager.  Identical to the `conan remove` command, "
                "except that it also updates the environment's [[packages]] array to "
                "reflect the new state.",
        )
        command.add_argument(
            "package",
            nargs=1,
            help=
                "Specifies the package to uninstall.  Must be provided in the format: "
                "`package/version`, where the 'package/version' fragment is "
                "identical to a normal `conan install` command, and the 'find/link' "
                "fragment tells CMake how to locate the package's headers and "
                "libraries.  The `find` and `link` symbols can typically be found on "
                "the package's conan.io page under the CMake `find_package` and "
                "`target_link_libraries` examples.  After uninstalling, this "
                "information will be removed from the environment's [[packages]] array, "
                "and will no longer be inserted whenever C/C++ code is compiled within "
                "it.",
        )

    # TODO: update command

    def build(self) -> None:
        """Add the 'build' command to the parser."""
        command = self.commands.add_parser(
            "build",
            help=
                "Compile a C++ project within the virtual environment by invoking "
                "bertrand's setuptools extensions.  This automatically generates "
                "equivalent Python bindings for all exported modules and installs the "
                "products into the environment's `bin/`, `lib/`, and `modules/` "
                "directories, as well as optionally ",
        )
        command.add_argument(
            "path",
            nargs=1,
            type=Path,
            help=
                "A path to a directory containing a `setup.py` script, which will be "
                "invoked to build the project.  The script should use bertrand's "
                "setuptools extensions to compile the project, which enables "
                "fine-grained control over the build process.",
        )
        command.add_argument(
            "compiler_options",
            nargs=argparse.REMAINDER,
            help=
                "Additional options to pass to the compiler's command line, which are "
                "passed as-is.  Note that dependencies (include paths, import "
                "statements, library paths, and link symbols) are automatically "
                "resolved by the environment, so users should not need to specify "
                "them manually.",
        )

    def clean(self) -> None:
        """Add the 'clean' command to the parser."""
        command = self.commands.add_parser(
            "clean",
            help=
                "Remove a project's build artifacts from the virtual environment.  "
                "This deletes all of the installed files outside of the project's "
                "build directory.  The build directory itself is left untouched, and "
                "can be used to rebuild the project without having to recompile it "
                "from scratch.",
        )
        command.add_argument(
            "path",
            nargs="?",
            help=
                "A path to a directory containing a `setup.py` script, which will be "
                "searched against the environment cache to determine which files to "
                "delete.  The script should use bertrand's setuptools extensions to "
                "allow the environment to track the project's build artifacts.",
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
        self.enter()
        self.enabled()
        self.stop()
        self.delete()
        # self.install()
        # self.build()
        # self.clean()

        return self.root.parse_args()


def init_docker() -> None:
    """Ensure Docker is installed and ready to use.  This is invoked at the beginning
    of nearly every other bertrand command.

    Raises
    ------
    SystemExit
        If Docker is not installed and cannot be installed automatically, or if the
        Docker daemon is not reachable.
    """
    # ensure Docker is installed, or attempt to install it automatically
    try:
        status = install_docker(assume_yes=False)
    except ValueError as err:
        print(f"bertrand: {err}")
        raise SystemExit(1) from err

    # check that the Docker daemon is reachable
    if not status.dockerd_reachable:
        # This is usually permissions (not in docker group) or daemon not running.
        print(
            "bertrand: Docker appears installed, but the daemon is not reachable.\n"
            f"bertrand: detail: {status.detail}\n"
            "bertrand: try one of the following, then rerun:\n"
            "  - run 'sudo docker info' to verify the daemon\n"
            "  - add your user to the 'docker' group and re-login (Linux post-install)"
        )
        raise SystemExit(1)

    # if the user is not in the docker group, forcing them to use sudo to run
    # docker commands, offer to add them to the group now
    if status.sudo_required:
        added = add_to_docker_group(assume_yes=False)
        if added:
            print(
                "bertrand: Added you to the docker group.\n"
                "bertrand: You must log out and back in (or run 'newgrp docker') "
                "for this to take effect.\n"
                "bertrand: Continuing using sudo for this session."
            )


def init(args: argparse.Namespace) -> None:
    """Execute the `bertrand init` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during environment initialization.
    """
    init_docker()

    # create an environment at the specified path
    if args.path is not None:
        path = (Path.cwd() / args.path).resolve()
        try:
            spec = create_environment(
                path,
                image=args.image[0],
                workspace=args.workspace[0],
                shell=args.shell[0],
            )
        except ValueError as err:
            print(f"bertrand: {err}")
            raise SystemExit(1) from err
        print(
            f"bertrand: initialized docker env at {path}\n"
            f"bertrand: image={spec.image}\n"
            f"bertrand: container={spec.container}"
        )


def enter(args: argparse.Namespace) -> None:
    """Execute the `bertrand enter` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during environment entry.
    """
    path = (Path.cwd() / args.path[0]).resolve()
    try:
        enter_environment(path)
    except ValueError as err:
        print(f"bertrand: {err}")
        raise SystemExit(1) from err


def enabled(args: argparse.Namespace) -> None:
    """Execute the `bertrand enabled` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        With a status code of 0 if in a Bertrand environment, or 1 otherwise, for
        inter-process communication.
    """
    raise SystemExit(not in_environment())


def stop(args: argparse.Namespace) -> None:
    """Execute the `bertrand stop` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during environment stoppage.
    """
    if in_environment():
        print(
            "bertrand: cannot invoke 'bertrand stop' from inside a Bertrand "
            "environment."
        )
        raise SystemExit(1)

    path = (Path.cwd() / args.path[0]).resolve()
    try:
        stop_environment(
            path,
            force=args.force,
        )
    except ValueError as err:
        print(f"bertrand: {err}")
        raise SystemExit(1) from err


def delete(args: argparse.Namespace) -> None:
    """Execute the `bertrand delete` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during environment deletion.
    """
    if in_environment():
        print(
            "bertrand: cannot invoke 'bertrand delete' from inside a Bertrand "
            "environment."
        )
        raise SystemExit(1)

    # if a specific path is given, delete the environment at that path
    if args.path is not None:
        path = (Path.cwd() / args.path).resolve()
        try:
            delete_environment(path, assume_yes=args.yes, force=args.force)
        except ValueError as err:
            print(f"bertrand: {err}")
            raise SystemExit(1) from err

    # delete Docker altogether from the host system
    if args.docker:
        if args.path is not None:
            print(
                "bertrand: cannot specify both an environment path to delete and a "
                "global --docker flag at the same time."
            )
            raise SystemExit(1)
        try:
            uninstall_docker(assume_yes=args.yes)
        except ValueError as err:
            print(f"bertrand: {err}")
            raise SystemExit(1) from err
        try:
            remove_from_docker_group()
        except ValueError as err:
            print(f"bertrand: {err}")
            raise SystemExit(1) from err


def build(args: argparse.Namespace) -> None:
    """Execute the `bertrand build` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during project build.
    """
    #     # TODO: pass the compiler options to the compiler as an environment variable
    #     # that gets caught in the setup.py script.
    #     # -> inplace is always on by default?  If you're executing this command, then
    #     # presumably you want to be able to run executables and import modules inplace.
    #     # Use pip install if you want things to be installed globally within the
    #     # environment.
    #     # TODO: I can just append the compiler options to CFLAGS and LDFLAGS directly,
    #     # which avoids the need to modify the setup.py script at all.  That also makes
    #     # it simple to pass extra options like --inplace or --debug, etc.
    #     try:
    #         subprocess.check_call(
    #             ["python", "setup.py", "build_ext", *args.compiler_options],
    #             cwd=args.path[0]
    #         )
    #     except subprocess.CalledProcessError:
    #         raise  # TODO: delete this and rely on internal messaging
    #         pass  # error messages are already printed to stdout
    raise NotImplementedError("bertrand build is not yet implemented.")


def clean(args: argparse.Namespace) -> None:
    """Execute the `bertrand clean` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during project clean.
    """
    #     path = args.path
    #     if path:
    #         clean(Path(path))
    #     clean()
    raise NotImplementedError("bertrand clean is not yet implemented.")


commands: dict[str, Callable[[argparse.Namespace], None]] = {
    "init": init,
    "enter": enter,
    "enabled": enabled,
    "stop": stop,
    "delete": delete,
    "build": build,
    "clean": clean,
}


def main() -> None:
    """Run the Bertrand command-line interface.

    Raises
    ------
    SystemExit
        If an error occurs during command execution.
    """
    parser = Parser()
    args = parser()
    command = commands.get(args.command, None)
    if command is not None:
        command(args)
    else:
        parser.root.print_help()


if __name__ == "__main__":
    main()
