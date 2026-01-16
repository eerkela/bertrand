"""Run Bertrand from the command line to get include directory, version number, etc.
"""
import argparse
import subprocess

from pathlib import Path
from typing import Callable

# from .env import activate, clean, deactivate, init
from .env import (
    CommandError,
    run,
    ensure_docker,
    clean_docker,
    create_environment,
    in_environment,
    list_environments,
    find_environment,
    monitor_environment,
    enter_environment,
    start_environment,
    stop_environment,
    pause_environment,
    resume_environment,
    delete_environment,
)
from . import __version__

# pylint: disable=unused-argument



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

    def init(self) -> None:
        """Add the 'init' command to the parser."""
        command = self.commands.add_parser(
            "init",
            help=
                "Install and run Docker Engine if it is not already present.  If a "
                "path (relative or absolute) is provided as the next argument, then "
                "pull a Bertrand-enabled Docker image to that path in order to create "
                "a new virtual environment.  A directory will be created at the "
                "specified path, with a .bertrand/env.json file that links it to a "
                "corresponding Docker container managed by the Docker Engine daemon.  "
                "At minimum, the container will hold a full C/C++ compiler toolchain, "
                "bootstrapped Python distribution, and Bertrand's compiler plugins, "
                "which invoke the toolchain to build C/C++ extensions on-demand.  It "
                "may also contain preinstalled developer tools, like language "
                "servers, package managers, sanitizers, and AI assistants, unless it "
                "is built from a release image that omits these in order to reduce "
                "container size.",
        )
        command.add_argument(
            "path",
            nargs="?",
            help=
                "The path to the virtual environment directory to create, if any.  "
                "This may be an absolute or relative path starting from the current "
                "working directory, and the last component will be used as the "
                "environment name.",
        )
        command.add_argument(
            "--from",
            nargs=1,
            # TODO: replace this with `bertrand:latest` once that becomes available
            default=["ubuntu:latest"],
            help=
                "The base Docker image to use for the virtual environment.  This can "
                "be any valid Docker image available on Docker Hub or a custom image "
                "hosted elsewhere, as long as it derives from a Bertrand base image.  "
                "Defaults to 'bertrand:latest', which provides a stable and widely "
                "compatible base for most development tasks.",
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
                "stops and deletes the existing container while preserving all files "
                "within the environment directory, and then reinstalls the container "
                "from scratch, starting from a fresh base image.",
        )
        command.add_argument(
            "--swap",
            type=int,
            nargs=1,
            default=[0],
            help=
                "Allocate a temporary swap file with the specified size in GiB.  This "
                "is commonly used when bootstrapping a local environment from source, "
                "as doing so can be very resource intensive.  Allocating a large "
                "enough swap file can allow environments to be built on systems with "
                "limited RAM.  Defaults to 0, which disables swap file creation.  "
                "Requires root privileges if set to a non-zero value.",
        )
        command.add_argument(
            "build_args",
            nargs=argparse.REMAINDER,
            help=
                "Additional arguments to pass to the 'docker build' command when "
                "building the environment's Docker image.  These are passed directly "
                "to the user's Dockerfile, and can be used to specify build-time "
                "variables, proxy settings, or other customizations.  For more "
                "information, see the Docker documentation for 'docker build', as "
                "well as Dockerfile configuration more generally.",
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
        command.add_argument(
            "-y", "--yes",
            action="store_true",
            help=
                "Automatically answer 'yes' to all prompts during environment "
                "entry.  This is useful for scripting and automation, where user "
                "interaction is not possible or desired.  Note that some steps (such "
                "as installing Docker if it is not already present, or using it "
                "without sudo or docker group privileges) may still prompt the user "
                "for elevated permissions, and will not be affected by this flag.",
        )
        command.add_argument(
            "--root",
            action="store_true",
            help=
                "Enter the environment as the root user, rather than preserving "
                "UID/GID permissions from the host system.  Use with caution, as "
                "leaving a container running with root privileges can possibly leak "
                "those privileges to the host filesystem.  Bertrand ordinarily "
                "preserves user IDs to prevent users from escalating privileges "
                "unintentionally.",
        )

    def find(self) -> None:
        """Add the 'find' query to the parser."""
        command = self.commands.add_parser(
            "find",
            help=
                "Print the path(s) to all Bertrand virtual environment directories "
                "that match a given ID or name fragment.  If no argument is provided, "
                "then the current environment (if any) will be reported instead.  If "
                "multiple environments match the given name fragment, then they will "
                "be printed one per line.  This can be used by scripts and automation "
                "tools to locate specific environments on the host system, or to "
                "detect whether they are running within a Bertrand environment.",
        )
        command.add_argument(
            "container",
            type=str,
            nargs="?",
            help=
                "The name or ID of the Docker container to look up.  If omitted, "
                "the current environment (if any) will be looked up instead.  If a "
                "name is given, then it may be a partial match for one or more "
                "containers, in which case all matching containers will be printed "
                "one per line.",
        )

    def ls(self) -> None:
        """Add the 'ls' command to the parser."""
        command = self.commands.add_parser(
            "ls",
            help=
                "List the Bertrand environments installed on the host system, as well "
                "as their current status (running/stopped), image, size, and other "
                "useful information."
        )
        command.add_argument(
            "path",
            type=Path,
            nargs="?",
            help=
                "The path to the environment directory to check.  If omitted, all "
                "Bertrand environments on the host system will be listed.",
        )
        command.add_argument(
            "--name",
            type=str,
            nargs="*",
            help=
                "Filter the list of environments by their names.  Multiple names can "
                "be specified, and only environments matching one or more of the "
                "given names will be shown.  Partial matches are supported.",
        )
        command.add_argument(
            "--running",
            action="store_true",
            help=
                "Show only running Bertrand environments.  By default, all "
                "environments are displayed, regardless of their status.  In this "
                "context, 'running' covers the 'running' and 'restarting' states.",
        )
        command.add_argument(
            "--stopped",
            action="store_true",
            help=
                "Show only stopped Bertrand environments.  By default, all "
                "environments are displayed, regardless of their status.  In this "
                "context, 'stopped' covers the 'created', 'removing', 'paused', "
                "'exited', and 'dead' states.",
        )
        command.add_argument(
            "--healthy",
            action="store_true",
            help=
                "Show only environments whose Docker containers are currently "
                "healthy, according to their configured health checks.",
        )
        command.add_argument(
            "--unhealthy",
            action="store_true",
            help=
                "Show only environments whose Docker containers are currently "
                "unhealthy, according to their configured health checks.",
        )

    def top(self) -> None:
        """Add the 'top' command to the parser."""
        command = self.commands.add_parser(
            "top",
            help=
                "Display the running containers and their resource utilization "
                "statistics, or those of the processes within a specific Bertrand "
                "environment, depending on whether a path is provided."
        )
        command.add_argument(
            "path",
            type=Path,
            nargs="?",
            help=
                "The path to the environment directory to inspect.  If omitted, all "
                "running Bertrand environments on the host system will be shown.",
        )

    # TODO: logging is not well-supported atm
    def log(self) -> None:
        """Add the 'log' command to the parser."""
        command = self.commands.add_parser(
            "log",
            help=
                "Display the Docker logs for a Bertrand environment at the specified "
                "path."
        )
        command.add_argument(
            "path",
            type=Path,
            nargs=1,
            help="The path to the environment directory to view logs for."
        )

    def start(self) -> None:
        """Add the 'start' command to the parser."""
        command = self.commands.add_parser(
            "start",
            help=
                # TODO
                "Start"
        )
        command.add_argument(
            "path",
            type=Path,
            nargs=1,
            help="The path to the environment directory to start."
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
                "in the middle of writing to a file or network connection.",
        )
        command.add_argument(
            "-t", "--timeout",
            type=int,
            default=30,
            help=
                "Timeout duration in seconds to wait for the environment to stop "
                "gracefully before killing the container.",
        )

    def pause(self) -> None:
        """"Add the 'pause' command to the parser."""
        command = self.commands.add_parser(
            "pause",
            help=
                "Pause a running Bertrand virtual environment at the specified path, "
                "suspending all processes within the container without terminating "
                "them.  This is weaker than 'stop', as it allows the environment to "
                "be resumed later without a full restart, preserving its state in "
                "memory.",
        )
        command.add_argument(
            "path",
            type=Path,
            nargs=1,
            help="The path to the environment directory to pause."
        )

    def resume(self) -> None:
        """Add the 'resume' command to the parser."""
        command = self.commands.add_parser(
            "resume",
            help=
                "Resume a paused Bertrand virtual environment at the specified path, "
                "restoring all processes within the container to their previous state.  "
                "This allows users to continue working from where they left off "
                "without having to restart the environment from scratch.",
        )
        command.add_argument(
            "path",
            type=Path,
            nargs=1,
            help="The path to the environment directory to resume."
        )

    def restart(self) -> None:
        """Add the 'restart' command to the parser."""
        command = self.commands.add_parser(
            "restart",
            help=
                "Restart a running Bertrand virtual environment at the specified path.  "
                "This stops the environment if it is running, then starts it again, "
                "allowing users to apply configuration changes or recover from errors "
                "without having to manually stop and start the environment.",
        )
        command.add_argument(
            "path",
            type=Path,
            nargs=1,
            help="The path to the environment directory to restart."
        )
        command.add_argument(
            "-t", "--timeout",
            type=int,
            default=30,
            help=
                "Timeout duration in seconds to wait for the environment to stop "
                "before killing the container."
        )

    def delete(self) -> None:
        """Add the 'delete' command to the parser."""
        command = self.commands.add_parser(
            "delete",
            help=
                "Delete a Bertrand environment at the specified path.  This preserves "
                "the environment directory and its contents by default, but removes "
                "the underlying Docker container, forcing it to be rebuilt in the "
                "future.",
        )
        command.add_argument(
            "path",
            type=Path,
            nargs="?",
            help="The path to the environment that will be deleted."
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
                "deleting it, which may lead to data loss or corruption if it is in "
                "the middle of writing to a file or network connection",
        )
        command.add_argument(
            "-rm", "--remove",
            action="store_true",
            help=
                "Also delete the environment directory.  If not set, only the Docker "
                "container is removed, leaving the environment directory intact.  "
                "Note that the contents of the directory will be permanently lost.",
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

    def pack(self) -> None:
        """Add the 'pack' command to the parser."""
        command = self.commands.add_parser(
            "pack",
            help=
                # TODO: expand docs?
                "Compress a Bertrand environment into a tar archive.  "
                "`$ bertrand unpack <archive>` can be used to rebuild the environment "
                "on another machine."
        )
        command.add_argument(
            "path",
            type=Path,
            nargs=1,
            help="The path to the environment directory to compress."
        )

    def unpack(self) -> None:
        """Add the 'unpack' command to the parser."""
        command = self.commands.add_parser(
            "unpack",
            help=
                # TODO: expand docs?
                "Extract a compressed Bertrand environment into a full container, "
                "rebuilding it from scratch using the contents of the extracted "
                "environment directory."
        )
        command.add_argument(
            "path",
            type=Path,
            nargs=1,
            help="The path to the environment directory to extract."
        )

    def publish(self) -> None:
        """Add the 'publish' command to the parser."""
        command = self.commands.add_parser(
            "publish",
            help=
                # TODO: expand docs?
                "Push a Bertrand environment to Docker Hub or a custom repository "
                "of Docker images."
        )
        command.add_argument(
            "path",
            type=Path,
            nargs=1,
            help="The path to the environment directory to publish."
        )

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
        self.find()
        self.ls()
        self.start()
        self.stop()
        self.pause()
        self.resume()
        self.restart()
        self.delete()
        self.pack()
        self.unpack()
        self.build()
        self.clean()

        return self.root.parse_args()


def init_docker(*, assume_yes: bool = False) -> None:
    """Ensure Docker is installed and ready to use.  This is invoked at the beginning
    of nearly every other bertrand command.

    Parameters
    ----------
    assume_yes : bool, optional
        Whether to automatically answer 'yes' to all prompts during Docker
        installation and configuration.  Defaults to False.

    Raises
    ------
    SystemExit
        If Docker is not installed and cannot be installed automatically, or if the
        Docker daemon is not reachable.
    """
    # ensure Docker is installed, or attempt to install it automatically
    try:
        status = install_docker(assume_yes=assume_yes)
    except Exception as err:
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
        added = add_to_docker_group(assume_yes=assume_yes)
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
    if in_environment():
        print(
            "bertrand: cannot invoke 'bertrand init' from inside a Bertrand "
            "environment.  Please exit the current environment before initializing a "
            "new one."
        )
        raise SystemExit(1)

    # ensure Docker is installed and ready to use
    init_docker(assume_yes=args.yes)

    # create an environment at the specified path
    if args.path is not None:
        path = (Path.cwd() / args.path).resolve()

        # if --force is set, delete any existing environment at that path first, but
        # retain user files
        if args.force:
            try:
                delete_environment(
                    path,
                    assume_yes=args.yes,
                    force=args.force,
                    remove=False
                )
            except Exception as err:
                print(f"bertrand: {err}")
                raise SystemExit(1) from err

        # build the container
        try:
            spec = create_environment(
                path,
                image=getattr(args, "from")[0],
                swap=args.swap[0],
                shell=["bash", "-l"],
                docker_build_args=args.build_args,
            )
        except Exception as err:
            print(f"bertrand: {err}")
            raise SystemExit(1) from err
        print(
            f"bertrand: initialized docker env at {path}\n"
            f"bertrand: image={spec.image}\n"
            f"bertrand: container={path.name}-{spec.env_id}"
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
    if in_environment():
        print(
            "bertrand: cannot invoke 'bertrand enter' from inside a Bertrand "
            "environment.  Please exit the current environment before entering "
            "another one."
        )
        raise SystemExit(1)

    # ensure Docker is installed and ready to use
    init_docker(assume_yes=args.yes)

    # get a shell within the specified environment
    path = (Path.cwd() / args.path[0]).resolve()
    try:
        enter_environment(path, as_root=args.root)
    except Exception as err:
        print(f"bertrand: {err}")
        raise SystemExit(1) from err


def find(args: argparse.Namespace) -> None:
    """Execute the `bertrand find` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.
    """
    if args.container is None:
        path = find_environment()  # from environment variables
        if path is not None:
            print(str(path))
    else:
        for container_id in list_environments(args.container):
            print(str(find_environment(container_id)))


def ls(args: argparse.Namespace) -> None:
    """Execute the `bertrand ls` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during environment listing.
    """
    if in_environment():
        print(
            "bertrand: cannot invoke 'bertrand ls' from inside a Bertrand "
            "environment.  Please exit the current environment before listing "
            "available environments and their statuses."
        )
        raise SystemExit(1)

    try:
        cmd = [
            "docker", "ps",
            "--filter", "label=bertrand=1",
        ]
        if args.path is not None:
            cmd.extend(["--filter", f"volume={args.path.expanduser().resolve()}"])
        if args.running:
            cmd.extend([
                "--filter", "status=running",
                "--filter", "status=restarting",
            ])
        if args.stopped:
            cmd.extend([
                "--filter", "status=created",
                "--filter", "status=removing",
                "--filter", "status=paused",
                "--filter", "status=exited",
                "--filter", "status=dead",
            ])
        if args.name is not None:
            for name in args.name:
                cmd.extend(["--filter", f"name={name}"])
        if args.healthy:
            cmd.extend(["--filter", "health=healthy"])
        if args.unhealthy:
            cmd.extend(["--filter", "health=unhealthy"])
        cmd.extend(["--size", "--no-trunc"])
        run(cmd)

    except CommandError as err:
        if err.stdout:
            print(f"bertrand: {err.stdout}")
        if err.stderr:
            print(f"bertrand: {err.stderr}")
        raise SystemExit(1) from err


def top(args: argparse.Namespace) -> None:
    """Execute the `bertrand top` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during environment process listing.
    """
    if in_environment():
        print(
            "bertrand: cannot invoke 'bertrand top' from inside a Bertrand "
            "environment.  Please exit the current environment before listing "
            "processes."
        )
        raise SystemExit(1)

    try:
        if args.path is None:
            subprocess.check_call(["docker", "stats"])
        else:
            path = (Path.cwd() / args.path).resolve()
            monitor_environment(path)

    except subprocess.CalledProcessError as err:
        if err.stdout:
            print(f"bertrand: {err.stdout}")
        if err.stderr:
            print(f"bertrand: {err.stderr}")
        raise SystemExit(1) from err


def start(args: argparse.Namespace) -> None:
    """Execute the `bertrand start` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during environment start.
    """
    if in_environment():
        print(
            "bertrand: cannot invoke 'bertrand start' from inside a Bertrand "
            "environment.  Please exit the current environment before starting a "
            "new one."
        )
        raise SystemExit(1)

    path = (Path.cwd() / args.path[0]).resolve()
    try:
        start_environment(path)
    except Exception as err:
        print(f"bertrand: {err}")
        raise SystemExit(1) from err


def stop(args: argparse.Namespace) -> None:
    """Execute the `bertrand stop` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during environment stop.
    """
    if in_environment():
        print(
            "bertrand: cannot invoke 'bertrand stop' from inside a Bertrand "
            "environment.  Please exit the current environment before stopping one."
        )
        raise SystemExit(1)

    path = (Path.cwd() / args.path[0]).resolve()
    try:
        stop_environment(path, force=args.force,)
    except Exception as err:
        print(f"bertrand: {err}")
        raise SystemExit(1) from err


def pause(args: argparse.Namespace) -> None:
    """Execute the `bertrand pause` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during environment pause.
    """
    if in_environment():
        print(
            "bertrand: cannot invoke 'bertrand pause' from inside a Bertrand "
            "environment.  Please exit the current environment before pausing one."
        )
        raise SystemExit(1)

    path = (Path.cwd() / args.path[0]).resolve()
    try:
        pause_environment(path)
    except Exception as err:
        print(f"bertrand: {err}")
        raise SystemExit(1) from err


def resume(args: argparse.Namespace) -> None:
    """Execute the `bertrand resume` CLI command.

    Parameters
    ----------
    args : argparse.Namespace
        The parsed command-line arguments.

    Raises
    ------
    SystemExit
        If an error occurs during environment resume.
    """
    if in_environment():
        print(
            "bertrand: cannot invoke 'bertrand resume' from inside a Bertrand "
            "environment.  Please exit the current environment before resuming one."
        )
        raise SystemExit(1)

    path = (Path.cwd() / args.path[0]).resolve()
    try:
        resume_environment(path)
    except Exception as err:
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
            "environment.  Please exit the current environment before deleting one."
        )
        raise SystemExit(1)

    # if a specific path is given, delete the environment at that path
    no_args = True
    if args.path is not None:
        no_args = False
        path = (Path.cwd() / args.path).resolve()
        try:
            delete_environment(
                path,
                assume_yes=args.yes,
                force=args.force,
                remove=args.remove
            )
        except Exception as err:
            print(f"bertrand: {err}")
            raise SystemExit(1) from err

    # delete Docker altogether from the host system
    if args.docker:
        no_args = False
        if args.path is not None:
            print(
                "bertrand: cannot specify both an environment path to delete and a "
                "global --docker flag at the same time."
            )
            raise SystemExit(1)
        try:
            uninstall_docker(assume_yes=args.yes)
        except Exception as err:
            print(f"bertrand: {err}")
            raise SystemExit(1) from err
        try:
            remove_from_docker_group()
        except Exception as err:
            print(f"bertrand: {err}")
            raise SystemExit(1) from err

    if no_args:
        print(
            "bertrand: must specify either an environment path to delete, or a "
            "Bertrand component to uninstall from the host system."
        )
        raise SystemExit(1)


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
    "find": find,
    "ls": ls,
    "start": start,
    "stop": stop,
    "pause": pause,
    "resume": resume,
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
