"""Run Bertrand from the command line to get include directory, version number, etc.
"""
from __future__ import annotations

import argparse
import asyncio
import os
import shutil
import subprocess
import sys
import time

from datetime import datetime
from pathlib import Path
from typing import cast

from .env.rpc import CodeOpen, rpc
from .env.config import inside_image, inside_container
from .env.pipeline import (
    JSONValue,
    Pipeline,
    on_init,
)
from .env.container import (
    TIMEOUT,
    Environment,
    _recover_spec,
    podman_build,
    podman_code,
    podman_enter,
    podman_log,
    podman_ls,
    podman_monitor,
    podman_pause,
    podman_publish,
    podman_restart,
    podman_resume,
    podman_rm,
    podman_start,
    podman_stop,
    podman_top,
)
from .env.config import (
    CONTAINER_ARTIFACT_DIR,
    IMAGE_TAG_ENV,
    WORKTREE_MOUNT,
    Config,
)
from .env.run import TimeoutExpired, atomic_write_text, confirm
from . import __version__

# pylint: disable=unused-argument


# # create swap memory for large builds
# swapfile = env_root / "swapfile"
# sudo = sudo_prefix()
# if swap:
#     run([*sudo, "fallocate", "-l", f"{swap}G", str(swapfile)])
#     run([*sudo, "chmod", "600", str(swapfile)])
#     run([*sudo, "mkswap", str(swapfile)])
#     run([*sudo, "swapon", str(swapfile)])

# try:

# # clear swap memory
# finally:
#     if swapfile.exists():
#         print("Cleaning up swap file...")
#         run([*sudo, "swapoff", str(swapfile)], check=False)
#         swapfile.unlink(missing_ok=True)


def _parse(path: str | None) -> tuple[str | None, str, str]:
    if path is None:
        return (None, "", "")
    else:
        return Environment.parse(path)


def _require_active_image_tag() -> str:
    tag = os.environ.get(IMAGE_TAG_ENV, "").strip()
    if not tag:
        raise OSError(
            f"missing active image tag in container environment: '{IMAGE_TAG_ENV}'"
        )
    return tag


class External:
    """External CLI for Bertrand."""

    class Parser:
        """External command-line parser for Bertrand's CLI.

        This parser is only used when Bertrand is invoked from outside a containerized
        environment, and is responsible for managing the environments themselves.  A
        separate parser is used within a containerized environment to control its
        internal toolchain.
        """

        def __init__(self) -> None:
            self.root = argparse.ArgumentParser(
                description="Command line utilities for bertrand.",
            )
            self.commands = self.root.add_subparsers(
                dest="command",
                title="commands",
                description=(
                    "Create and manage Python/C/C++ virtual environments and "
                    "containerized toolchains."
                ),
                prog="bertrand",
                metavar="(command)",
            )
            self.version()
            self.init()
            self.build()
            self.publish()
            self.start()
            self.enter()
            self.code()
            self.stop()
            self.pause()
            self.resume()
            self.restart()
            self.rm()
            self.ls()
            self.monitor()
            self.top()
            self.log()
            self.clean()

        # TODO: path arguments should be updated to new workload/tag syntax
        # TODO: zero timeouts should wait indefinitely?

        def version(self) -> None:
            """Add the 'version' query to the parser."""
            self.root.add_argument(
                "-v", "--version",
                action="version",
                version=__version__
            )

        def init(self) -> None:
            """Add the 'init' command to the parser."""
            command = self.commands.add_parser(
                "init",
                help=
                    "Install Bertrand's container engine if it is not already present, "
                    "and optionally initialize a new project at the specified path "
                    "(relative or absolute).  If an environment path is provided, this "
                    "will create a directory at that path with a template Containerfile, "
                    ".containerignore, and pyproject.toml.  If omitted, this command "
                    "only bootstraps host prerequisites for containerized workflows.",
            )
            command.add_argument(
                "path",
                metavar="ENV",
                nargs="?",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must not point to an existing "
                    "file.  The last component will be used as the project name.  If "
                    "omitted, only host bootstrap steps are performed.",
            )
            command.add_argument(
                "-y", "--yes",
                action="store_true",
                help=
                    "Skip confirmation prompts when installing the container engine "
                    "and/or initializing the project.  This is mainly intended for "
                    "non-interactive use, such as in CI/CD workflows.",
            )
            command.add_argument(
                "--profile",
                choices=("flat", "src"),
                default=None,
                help=
                    "Layout profile to apply for environment structure and resource "
                    "placement.  Requires ENV.  Defaults to src when ENV is provided.",
            )
            command.add_argument(
                "--lang",
                action="append",
                choices=("python", "cpp"),
                default=None,
                help=
                    "Language capability to include (repeatable).  Requires ENV.  If "
                    "omitted, defaults to python and cpp when ENV is provided.",
            )
            command.add_argument(
                "--code",
                choices=("vscode", "none"),
                default=None,
                help=
                    "Editor integration capability to include.  Use 'none' to disable "
                    "editor capability entirely.  Requires ENV.  Defaults to vscode "
                    "when ENV is provided.",
            )
            command.set_defaults(handler=External.init)

        def build(self) -> None:
            """Add the 'build' command to the parser."""
            command = self.commands.add_parser(
                "build",
                help=
                    "Build and materialize declared Bertrand images/containers at the "
                    "specified path without starting them.  Tags and arguments are "
                    "declared by modifying project metadata according to the '--lang' "
                    "options chosen during 'bertrand init'.  See the generated "
                    "configuration files for details.",
            )
            command.add_argument(
                "path",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  The path may include "
                    "optional image and container tags (e.g. "
                    "'/path/to/env:image:container').  If no tags are given, all "
                    "declared images/containers are materialized.  If an image tag is "
                    "given, only that image and its declared containers are "
                    "materialized.  If both image and container tags are given, only "
                    "that declared container is materialized.",
            )
            command.set_defaults(handler=External.build)

        def publish(self) -> None:
            """Add the 'publish' command to the parser."""
            command = self.commands.add_parser(
                "publish",
                help=
                    "Build and publish declared Bertrand images in the specified "
                    "environment to a remote OCI registry.  This is meant to be used "
                    "in CI workflows triggered by git tags, and usually does not need "
                    "to be invoked by the user directly.",
            )
            command.add_argument(
                "path",
                metavar="ENV",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  Publish always targets "
                    "the entire environment and all declared tags."
            )
            command.add_argument(
                "--repo",
                metavar="OCI_REPO",
                default=None,
                help=
                    "Remote OCI repository where arch-specific image tags and final "
                    "manifests will be published (for example: 'ghcr.io/owner/repo').",
            )
            command.add_argument(
                "--version",
                metavar="VERSION",
                default=None,
                help=
                    "Optional release version to enforce.  Accepts both 'X.Y.Z' and "
                    "'vX.Y.Z', and must match the current project version after "
                    "normalization.",
            )
            command.add_argument(
                "--manifest",
                action="store_true",
                help=
                    "Publish manifests only from existing arch-specific refs.  This "
                    "skips image builds and pushes no new arch tags, and is meant to "
                    "be used as a second stage in CI workflows after a successful "
                    "build-and-publish stage with the same version and repo "
                    "parameters.",
            )
            command.add_argument(
                "--manifest-arches",
                metavar="CSV",
                default=None,
                help=
                    "Comma-separated architecture list for --manifest mode (for "
                    "example: 'amd64,arm64').  Required when --manifest is set.",
            )
            command.add_argument(
                "--arch-out",
                metavar="PATH",
                default=None,
                help=
                    "Optional output path to write the normalized host architecture "
                    "detected during build mode.  This is intended for CI artifact "
                    "handoff and is invalid with --manifest.",
            )
            command.set_defaults(handler=External.publish)

        def start(self) -> None:
            """Add the 'start' command to the parser."""
            command = self.commands.add_parser(
                "start",
                help=
                    "Start Bertrand containers at the specified path, scoping to "
                    "specific images or containers if desired.",
            )
            command.add_argument(
                "path",
                nargs="?",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  The path may include "
                    "optional image and container tags (e.g. "
                    "'/path/to/env:image:container'), which can be used to scope the "
                    "command to specific images or containers within the environment.  "
                    "If an image or environment scope is given, then all declared "
                    "containers matching that scope will be started.  If no path is "
                    "given, then all Bertrand containers on the host system will be "
                    "started, after prompting the user to confirm.",
            )
            command.add_argument(
                "cmd",
                nargs=argparse.REMAINDER,
                help=
                    "The command to run inside the container context after it starts.  "
                    "If omitted, the default command declared in the project's build "
                    "matrix will be used instead.  The resulting container will run "
                    "this command at PID 1, and will exit when the command finishes.",
                metavar="CMD...",
            )
            command.set_defaults(handler=External.start)

        def enter(self) -> None:
            """Add the 'enter' command to the parser."""
            command = self.commands.add_parser(
                "enter",
                help=
                    "Launch an interactive shell session within a Bertrand virtual "
                    "environment at the specified path.  If the container is not "
                    "already running, it will be started automatically.  If the host "
                    "code RPC service is unavailable, Bertrand will warn on entrance "
                    "and disable the `bertrand code` command within the shell context.  "
                    "Use 'exit' (without a 'bertrand' prefix) to leave the shell and "
                    "return to the host system.",
            )
            command.add_argument(
                "path",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  If no image or "
                    "container tag is given, then the default container for the parent "
                    "environment or image will be used.  Otherwise, the container tag "
                    "must be declared in the project metadata according to the "
                    "'--lang' options chosen during 'bertrand init'.",
            )
            command.add_argument(
                "--shell",
                default=None,
                metavar="SHELL",
                help=
                    "Override the default shell for this enter session.  Validation "
                    "is performed at runtime by `podman_enter` against the configured "
                    "shell map.",
            )
            command.set_defaults(handler=External.enter)

        def code(self) -> None:
            """Add the 'code' command to the parser."""
            command = self.commands.add_parser(
                "code",
                help=
                    "Launch a text editor rooted at a Bertrand environment directory "
                    "and mount its local toolchain using remote development "
                    "extensions.  The editor runs on the host system and is selected "
                    "from project configuration unless overridden.",
            )
            command.add_argument(
                "path",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  The path may include "
                    "optional image and container tags (e.g. "
                    "'/path/to/env:image:container'), which determine the exact "
                    "container whose toolchain will be mounted.  If no image or "
                    "container tag is given, then the default container for the parent "
                    "environment or image will be used.  Otherwise, the container tag "
                    "must be declared in the project metadata according to the "
                    "'--lang' options chosen during 'bertrand init'.",
            )
            command.add_argument(
                "--editor",
                default=None,
                metavar="EDITOR",
                help=
                    "Override the configured host editor alias for this command.  "
                    "Validation is performed at runtime by RPC/config resolution.",
            )
            command.set_defaults(handler=External.code)

        def stop(self) -> None:
            """Add the 'stop' command to the parser."""
            command = self.commands.add_parser(
                "stop",
                help=
                    "Stop Bertrand containers at the specified path, scoping to "
                    "specific images or containers if desired.  Note that stopping the "
                    "environment does not delete it or any files contained within it; "
                    "it simply halts the container process and renders it dormant.  As "
                    "long as the container is not invalidated before the next "
                    "'bertrand start' command, it will preserve its current "
                    "(persistent) state.",
            )
            command.add_argument(
                "path",
                nargs="?",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  The path may include "
                    "optional image and container tags (e.g. "
                    "'/path/to/env:image:container'), which can be used to scope the "
                    "command to specific images or containers within the environment.  "
                    "If an image or environment scope is given, then all containers "
                    "matching that scope will be stopped.  If no path is given, then "
                    "all Bertrand containers on the host system will be stopped, after "
                    "prompting the user to confirm.",
            )
            command.add_argument(
                "-t", "--timeout",
                type=int,
                default=30,
                help=
                    "Duration (in seconds) to wait for a container to stop before "
                    "forcefully killing it (usually via a SIGKILL signal).",
            )
            command.set_defaults(handler=External.stop)

        def pause(self) -> None:
            """Add the 'pause' command to the parser."""
            command = self.commands.add_parser(
                "pause",
                help=
                    "Pause running Bertrand containers at the specified path, scoping "
                    "to specific images or containers if desired.  This is similar to "
                    "bertrand stop', but is ligher weight and faster, as it suspends "
                    "the container process rather than fully stopping it.  This allows "
                    "the process to resume where it left off, rather than fully "
                    "restarting, rebuilding invalid containers, and losing volatile "
                    "state.",
            )
            command.add_argument(
                "path",
                nargs="?",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help="The path to the environment directory to pause.",
            )
            command.set_defaults(handler=External.pause)

        def resume(self) -> None:
            """Add the 'resume' command to the parser."""
            command = self.commands.add_parser(
                "resume",
                help=
                    "Resume paused Bertrand containers at the specified path, scoping "
                    "to specific images or containers if desired.  This is the inverse "
                    "of 'bertrand pause'.",
            )
            command.add_argument(
                "path",
                nargs="?",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  The path may include "
                    "optional image and container tags (e.g. "
                    "'/path/to/env:image:container'), which can be used to scope the "
                    "command to specific images or containers within the environment.  "
                    "If an image or environment scope is given, then all containers "
                    "matching that scope will be resumed.  If no path is given, then "
                    "all paused Bertrand containers on the host system will be "
                    "resumed, after prompting the user to confirm.",
            )
            command.set_defaults(handler=External.resume)

        def restart(self) -> None:
            """Add the 'restart' command to the parser."""
            command = self.commands.add_parser(
                "restart",
                help=
                    "Restart (and potentially rebuild) running Bertrand containers at "
                    "the specified path, scoping to specific images or containers if "
                    "desired.  This is similar to 'bertrand stop' followed by "
                    "'bertrand start', but only applies to running containers, and may "
                    "be more efficient if the container does not need to be rebuilt.",
            )
            command.add_argument(
                "path",
                nargs="?",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  The path may include "
                    "optional image and container tags (e.g. "
                    "'/path/to/env:image:container'), which can be used to scope the "
                    "command to specific images or containers within the environment.  "
                    "If an image or environment scope is given, then all containers "
                    "matching that scope will be restarted.  If no path is given, then "
                    "all running Bertrand containers on the host system will be "
                    "restarted, after prompting the user to confirm.",
            )
            command.add_argument(
                "-t", "--timeout",
                type=int,
                default=30,
                help=
                    "Duration (in seconds) to wait for a container to stop before "
                    "forcefully killing it (usually via a SIGKILL signal).",
            )
            command.set_defaults(handler=External.restart)

        def rm(self) -> None:
            """Add the 'rm' command to the parser."""
            command = self.commands.add_parser(
                "rm",
                help=
                    "Delete Bertrand images and/or containers at the specified path, "
                    "scoping to specific images or containers if desired.  Note that "
                    "the environment directory is unaffected, but any data stored in "
                    "a container's writable layer will be permanently lost.",
            )
            command.add_argument(
                "path",
                nargs="?",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  The path may include "
                    "optional image and container tags (e.g. "
                    "'/path/to/env:image:container'), which can be used to scope the "
                    "command to specific images or containers within the environment.  "
                    "If an image or environment scope is given, then all images and/or "
                    "containers matching that scope will be deleted.  If no path is "
                    "given, then all Bertrand images and containers on the host system "
                    "will be deleted, after prompting the user to confirm.",
            )
            command.add_argument(
                "-t", "--timeout",
                type=int,
                default=TIMEOUT,
                help=
                    "Maximum duration (in seconds) to wait for this command.  May be "
                    "rounded up to the nearest second depending on the underlying "
                    "implementation.",
            )
            command.add_argument(
                "-f", "--force",
                action="store_true",
                help=
                    "Force deletion of containers, even if they are currently "
                    "running.  This will stop the container if necessary before "
                    "deleting it, which may lead to data loss or corruption if it is "
                    "in the middle of writing to a file or network connection.  If "
                    "omitted, Bertrand will prevent deletion and exit with an error "
                    "instead.  Use with extreme caution.",
            )
            command.set_defaults(handler=External.rm)

        def ls(self) -> None:
            """Add the 'ls' command to the parser."""
            command = self.commands.add_parser(
                "ls",
                help=
                    "List Bertrand images or containers with basic diagnostic "
                    "information at the specified path, scoping to specific images or "
                    "containers if desired.",
            )
            command.add_argument(
                "path",
                nargs="?",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  The path may include "
                    "optional image and container tags (e.g. "
                    "'/path/to/env:image:container'), which can be used to scope the "
                    "command to specific images or containers within the environment.  "
                    "If an image or environment scope is given, then only images or "
                    "containers matching that scope will be listed.  If no path is "
                    "given, then all images or containers on the host system will be "
                    "listed.",
            )
            command.add_argument(
                "-t", "--timeout",
                type=int,
                default=TIMEOUT,
                help=
                    "Maximum duration (in seconds) to wait for this command.  May be "
                    "rounded up to the nearest second depending on the underlying "
                    "implementation.",
            )
            command.add_argument(
                "--image",
                action="store_true",
                help=
                    "Show Bertrand images in the current scope instead of containers "
                    "(which is the default).",
            )
            command.add_argument(
                "--format",
                type=str,
                default="table",
                metavar="FORMAT",
                help=
                    "Output format for list results.  Accepted values: 'id', 'json', "
                    "'table', or 'table <template>' where <template> is a single "
                    "quoted Go template string forwarded directly to podman.  "
                    "Defaults to 'table'.",
            )
            command.set_defaults(handler=External.ls)

        def monitor(self) -> None:
            """Add the 'monitor' command to the parser."""
            command = self.commands.add_parser(
                "monitor",
                help=
                    "Monitor the resource utilization statistics of running Bertrand "
                    "containers at the specified path, scoping to specific images or "
                    "containers if desired.",
            )
            command.add_argument(
                "path",
                nargs="?",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  The path may include "
                    "optional image and container tags (e.g. "
                    "'/path/to/env:image:container'), which can be used to scope the "
                    "command to specific images or containers within the environment.  "
                    "If an image or environment scope is given, then only running "
                    "containers matching that scope will be monitored.  If no path is "
                    "given, then all running Bertrand containers on the host system "
                    "will be monitored.",
            )
            command.add_argument(
                "-t", "--timeout",
                type=int,
                default=TIMEOUT,
                help=
                    "Maximum duration (in seconds) to wait for this command.  May be "
                    "rounded up to the nearest second depending on the underlying "
                    "implementation.  If 'interval' is positive, then this timeout "
                    "will not disregard the live updates to the statistics.",
            )
            command.add_argument(
                "-i", "--interval",
                type=int,
                default=0,
                help=
                    "Interval (in seconds) between updates to the displayed "
                    "statistics.  If set to 0 (the default), then the statistics will "
                    "not be updated after the initial display.",
            )
            command.add_argument(
                "--format",
                type=str,
                default="table",
                metavar="FORMAT",
                help=
                    "Output format for monitor results.  Accepted values: 'json', "
                    "'table', or 'table <template>' where <template> is a single "
                    "quoted Go template string forwarded directly to podman.  "
                    "Defaults to 'table'.  JSON mode requires '--interval=0'.",
            )
            command.set_defaults(handler=External.monitor)

        def top(self) -> None:
            """Add the 'top' command to the parser."""
            command = self.commands.add_parser(
                "top",
                help=
                    "Display the active processes running within Bertrand containers "
                    "associated with a given worktree and/or workload"
            )
            command.add_argument(
                "path",
                metavar="ENV[@WORKLOAD][:TAG]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "worktree produced by Bertrand's git hooks, or a root repository "
                    "created by 'bertrand init', in which case the repository's "
                    "current HEAD branch will be targeted.  If a kubernetes workload "
                    "is specified, then it will be started before proceeding.  If a "
                    "tag is specified, then it will target a specific member of the "
                    "kubernetes workload, or a specific image if no workload is "
                    "given.  If no tag is given, then the default tag for the parent "
                    "workload or environment will be chosen."
            )
            command.add_argument(
                "-t", "--timeout",
                type=int,
                default=TIMEOUT,
                help=
                    "Maximum duration (in seconds) to wait for this command.  May be "
                    "rounded up to the nearest second depending on the underlying "
                    "implementation.",
            )
            command.set_defaults(handler=External.top)

        def log(self) -> None:
            """Add the 'log' command to the parser."""
            command = self.commands.add_parser(
                "log",
                help=
                    "View the internal logs of a Bertrand image or container at the "
                    "specified path."
            )
            command.add_argument(
                "path",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  If no image or container "
                    "tag is given, then the default container for the parent "
                    "environment or image will be used.  Otherwise, the container tag "
                    "must correspond to a previous bertrand start' command.",
            )
            command.add_argument(
                "-t", "--timeout",
                type=int,
                default=TIMEOUT,
                help=
                    "Maximum duration (in seconds) to wait for this command.  May be "
                    "rounded up to the nearest second depending on the underlying "
                    "implementation.",
            )
            command.add_argument(
                "--image",
                action="store_true",
                help=
                    "Show logs for Bertrand images instead of containers (which is the "
                    "default).  Note that image logs are only generated during the "
                    "build process, and will not include any output from running "
                    "containers.  This flag effectively toggles between 'podman "
                    "container logs' and 'podman image history', respectively.",
            )
            command.add_argument(
                "--since",
                type=str,
                help=
                    "Show only logs generated since the specified timestamp or "
                    "human-readable duration.  For more details, see the documentation "
                    "for the 'since' parameter of the 'podman container logs' command, "
                    "which supports both absolute timestamps (e.g. "
                    "'2024-01-01T00:00:00') and relative timestamps (e.g. '5m').  This "
                    "option has no effect if used in conjunction with '--image'.",
            )
            command.set_defaults(handler=External.log)
            command.add_argument(
                "--until",
                type=str,
                help=
                    "Show only logs generated until the specified timestamp or "
                    "human-readable duration.  For more details, see the documentation "
                    "for the 'until' parameter of the 'podman container logs' command, "
                    "which supports both absolute timestamps (e.g. "
                    "'2024-01-01T00:00:00') and relative timestamps (e.g. '5m').  This "
                    "option has no effect if used in conjunction with '--image'.",
            )

        def clean(self) -> None:
            """Add the 'clean' command to the parser."""
            command = self.commands.add_parser(
                "clean",
                help=
                    "Completely remove all traces of Bertrand from the host system, "
                    "including all images and containers it is managing, but leaving "
                    "environment directories intact.  This command will also attempt "
                    "to uninstall Bertrand's container engine (if it was installed by "
                    "'bertrand init') and replace any configurations it overwrote, "
                    "making a best-effort attempt to restore the system to its "
                    "previous state.  The container engine and configuration changes "
                    "will be reinstalled by a future 'bertrand init' command if "
                    "needed.",
            )
            command.add_argument(
                "-y", "--yes",
                action="store_true",
                help=
                    "Bypass confirmation prompts and proceed with cleaning all "
                    "Bertrand images, containers, and the container engine itself.  "
                    "Use with caution.",
            )
            command.set_defaults(handler=External.clean)

        def __call__(self) -> argparse.Namespace:
            """Run the command-line parser.

            Returns
            -------
            argparse.Namespace
                The parsed command-line arguments.
            """
            return self.root.parse_args()

    @staticmethod
    def version(args: argparse.Namespace) -> None:
        """Execute the `bertrand --version` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        print(__version__)

    @staticmethod
    def init(args: argparse.Namespace) -> None:
        """Execute the `bertrand init` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If the specified path includes an image or container tag, which is not
            allowed when initializing an environment directory, or if requested
            layout options differ from an existing manifest.  In host-only mode (no
            path), layout options are rejected.
        """
        env, image_tag, container_tag = _parse(args.path)
        if env is None:
            if args.profile is not None or args.lang is not None or args.code is not None:
                raise OSError(
                    "init layout options (--profile/--lang/--code) require an "
                    "environment path"
                )
            on_init.do(
                env=None,
                image_tag=image_tag,
                container_tag=container_tag,
                profile=None,
                capabilities=None,
                yes=args.yes,
            )
            return
        if image_tag or container_tag:
            raise OSError(
                "cannot specify image or container tag when initializing an environment "
                "directory"
            )

        # resolve profile + capabilities
        profile = args.profile if args.profile is not None else "src"
        capabilities = list(args.lang) if args.lang is not None else ["python", "cpp"]
        if args.code is not None:
            capabilities.append(args.code)
        else:
            capabilities.append("vscode")
        seen: set[str] = set()
        deduped: list[str] = []
        for cap in capabilities:
            if cap in seen:
                continue
            seen.add(cap)
            deduped.append(cap)
        capabilities = deduped
        if not capabilities:
            raise OSError("init capabilities must not be empty")

        on_init.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            profile=profile,
            capabilities=cast(JSONValue, capabilities),
            yes=args.yes,
        )

    @staticmethod
    def build(args: argparse.Namespace) -> None:
        """Execute the `bertrand build` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If a nested command times out while building the container.  This should
            never occur under normal circumstances, and the 'build' command
            intentionally does not accept a timeout argument, so this can only be
            surfaced from an internal error.
        """
        now = time.time()
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_build(
                    worktree,
                    workload,
                    tag,
                    quiet=False,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                cmd = ["bertrand", "build", _recover_spec(worktree, workload, tag)]
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=0.0,  # indefinite
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now()}\n"
                ) from err

    @staticmethod
    def publish(args: argparse.Namespace) -> None:
        """Execute the `bertrand publish` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If the path includes workload/tag targeting, or if no repository is
            provided.
        TimeoutExpired
            If a nested command times out while publishing.  This should never occur
            under normal circumstances, and the 'publish' command intentionally does
            not accept a timeout argument, so this can only be surfaced from an
            internal error.
        """
        now = time.time()
        repo = args.repo
        if repo is None or not repo.strip():
            raise OSError("must specify --repo when publishing")
        arch_out = args.arch_out
        if arch_out is not None:
            arch_out = arch_out.strip()
            if not arch_out:
                raise OSError("--arch-out must not be empty")
            if args.manifest:
                raise OSError("--arch-out is only valid in build mode (without --manifest)")

        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            if workload is not None or tag is not None:
                raise OSError(
                    "publish supports ENV scope only.  Omit workload (@...) and tag "
                    "(:...) selectors."
                )
            try:
                arch = runner.run(podman_publish(
                    worktree,
                    repo=repo,
                    version=args.version,
                    manifest=args.manifest,
                    manifest_arches=args.manifest_arches,
                ))
                if arch_out is not None:
                    if arch is None:
                        raise OSError(
                            "internal publish error: architecture output requested "
                            "but publish ran in manifest mode"
                        )
                    atomic_write_text(
                        Path(arch_out).expanduser().resolve(),
                        f"{arch}\n",
                        encoding="utf-8",
                    )
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                cmd = ["bertrand", "publish", str(worktree), "--repo", repo]
                if args.version:
                    cmd.extend(["--version", args.version])
                if args.manifest:
                    cmd.append("--manifest")
                if args.manifest_arches:
                    cmd.extend(["--manifest-arches", args.manifest_arches])
                if arch_out:
                    cmd.extend(["--arch-out", arch_out])
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=0.0,  # indefinite
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now()}\n"
                ) from err

    @staticmethod
    def start(args: argparse.Namespace) -> None:
        """Execute the `bertrand start` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If a nested command times out while starting the container.  This should
            never occur under normal circumstances, and the 'start' command
            intentionally does not accept a timeout argument, so this can only be
            surfaced from an internal error.
        """
        now = time.time()
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_start(
                    worktree,
                    workload,
                    tag,
                    cmd=args.cmd or None,  # empty list -> None
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                cmd = ["bertrand", "start", _recover_spec(worktree, workload, tag)]
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=0.0,  # indefinite
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now()}\n"
                ) from err

    @staticmethod
    def enter(args: argparse.Namespace) -> None:
        """Execute the `bertrand enter` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If a nested command times out while entering the container.  This should
            never occur under normal circumstances, and the 'enter' command
            intentionally does not accept a timeout argument, so this can only be
            surfaced from an internal error.
        """
        now = time.time()
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_enter(
                    worktree,
                    workload,
                    tag,
                    shell=args.shell or None,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                cmd = ["bertrand", "enter", _recover_spec(worktree, workload, tag)]
                if args.shell:
                    cmd.extend(["--shell", args.shell])
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=0.0,  # indefinite
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now()}\n"
                ) from err

    @staticmethod
    def code(args: argparse.Namespace) -> None:
        """Execute the `bertrand code` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If a nested command times out while launching the editor.  This should
            never occur under normal circumstances, and the 'code' command
            intentionally does not accept a timeout argument, so this can only be
            surfaced from an internal error.
        """
        now = time.time()
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_code(
                    worktree,
                    workload,
                    tag,
                    editor=args.editor or None,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                cmd = ["bertrand", "code", _recover_spec(worktree, workload, tag)]
                if args.editor:
                    cmd.extend(["--editor", args.editor])
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=0.0,  # indefinite
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now()}\n"
                ) from err

    @staticmethod
    def stop(args: argparse.Namespace) -> None:
        """Execute the `bertrand stop` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        now = time.time()
        deadline = now + args.timeout
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_stop(
                    worktree,
                    workload,
                    tag,
                    deadline=deadline,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                stop = datetime.fromtimestamp(deadline)
                cmd = ["bertrand", "stop", _recover_spec(worktree, workload, tag)]
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {stop}\n"
                ) from err

    @staticmethod
    def pause(args: argparse.Namespace) -> None:
        """Execute the `bertrand pause` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        now = time.time()
        deadline = now + args.timeout
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_pause(
                    worktree,
                    workload,
                    tag,
                    deadline=deadline,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                stop = datetime.fromtimestamp(deadline)
                cmd = ["bertrand", "pause", _recover_spec(worktree, workload, tag)]
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {stop}\n"
                ) from err

    @staticmethod
    def resume(args: argparse.Namespace) -> None:
        """Execute the `bertrand resume` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        now = time.time()
        deadline = now + args.timeout
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_resume(
                    worktree,
                    workload,
                    tag,
                    deadline=deadline,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                stop = datetime.fromtimestamp(deadline)
                cmd = ["bertrand", "resume", _recover_spec(worktree, workload, tag)]
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {stop}\n"
                ) from err

    @staticmethod
    def restart(args: argparse.Namespace) -> None:
        """Execute the `bertrand restart` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        now = time.time()
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_restart(
                    worktree,
                    workload,
                    tag,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                cmd = ["bertrand", "restart", _recover_spec(worktree, workload, tag)]
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=0.0,  # indefinite
                    output=None,
                    stderr=f"started: {start}\nstopped: {datetime.now()}\n"
                ) from err

    @staticmethod
    def rm(args: argparse.Namespace) -> None:
        """Execute the `bertrand rm` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        now = time.time()
        deadline = now + args.timeout
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_rm(
                    worktree,
                    workload,
                    tag,
                    deadline=deadline,
                    force=args.force,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                stop = datetime.fromtimestamp(deadline)
                cmd = ["bertrand", "rm", _recover_spec(worktree, workload, tag)]
                if args.force:
                    cmd.append("--force")
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {stop}\n"
                ) from err

    @staticmethod
    def ls(args: argparse.Namespace) -> None:
        """Execute the `bertrand ls` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        now = time.time()
        deadline = now + args.timeout
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_ls(
                    worktree,
                    workload,
                    tag,
                    deadline=deadline,
                    image=args.image,
                    format=args.format,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                stop = datetime.fromtimestamp(deadline)
                cmd = ["bertrand", "ls", _recover_spec(worktree, workload, tag)]
                if args.image:
                    cmd.append("--image")
                cmd.extend(["--format", args.format])
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {stop}\n"
                ) from err

    @staticmethod
    def monitor(args: argparse.Namespace) -> None:
        """Execute the `bertrand monitor` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        now = time.time()
        deadline = now + args.timeout
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_monitor(
                    worktree,
                    workload,
                    tag,
                    deadline=deadline,
                    interval=args.interval,
                    format=args.format,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                stop = datetime.fromtimestamp(deadline)
                cmd = ["bertrand", "monitor", _recover_spec(worktree, workload, tag)]
                if args.interval:
                    cmd.append(f"--interval={args.interval}")
                cmd.extend(["--format", args.format])
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {stop}\n"
                ) from err

    @staticmethod
    def top(args: argparse.Namespace) -> None:
        """Execute the `bertrand top` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        now = time.time()
        deadline = now + args.timeout
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_top(
                    worktree,
                    workload,
                    tag,
                    deadline=deadline,
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                stop = datetime.fromtimestamp(deadline)
                cmd = ["bertrand", "top", _recover_spec(worktree, workload, tag)]
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {stop}\n"
                ) from err

    @staticmethod
    def log(args: argparse.Namespace) -> None:
        """Execute the `bertrand log` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        TimeoutExpired
            If the command does not complete within the specified timeout.
        """
        now = time.time()
        deadline = now + args.timeout
        with asyncio.Runner() as runner:
            worktree, workload, tag = runner.run(Environment.parse(args.path))
            try:
                runner.run(podman_log(
                    worktree,
                    workload,
                    tag,
                    deadline=deadline,
                    image=args.image,
                    since=args.since,
                    until=args.until
                ))
            except (TimeoutError, TimeoutExpired) as err:
                start = datetime.fromtimestamp(now)
                stop = datetime.fromtimestamp(deadline)
                cmd = ["bertrand", "log", _recover_spec(worktree, workload, tag)]
                if args.image:
                    cmd.append("--image")
                if args.since:
                    cmd.append(f"--since={args.since}")
                if args.until:
                    cmd.append(f"--until={args.until}")
                raise TimeoutExpired(
                    cmd=cmd,
                    timeout=args.timeout,
                    output=None,
                    stderr=f"started: {start}\nstopped: {stop}\n"
                ) from err

    # NOTE: order is important here, as it defines the order in which pipelines are
    # undone during cleanup, which must be done in a safe ordering to avoid leaving
    # the system in an inconsistent state.
    pipelines: dict[str, Pipeline] = {"init": on_init}

    @staticmethod
    def clean(args: argparse.Namespace) -> None:
        """Execute the `bertrand clean` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If the user declines the prompt.
        """
        if not confirm(
            "This will permanently delete all Bertrand images, containers, and the "
            "container engine itself from the host system.\nAre you sure you want to "
            "proceed? [y/N] ",
            assume_yes=args.yes,
        ):
            raise OSError("clean declined by user.")

        with asyncio.Runner() as runner:
            for pipe in External.pipelines.values():
                try:
                    runner.run(pipe.undo(force=True))
                    shutil.rmtree(pipe.state_dir, ignore_errors=True)
                except Exception as e:  # pylint: disable=broad-exception-caught
                    print(f"bertrand: error during cleanup: {e}", file=sys.stderr)

    def __call__(self) -> None:
        parser = External.Parser()
        args = parser()
        if args.command is None:
            parser.root.print_help()
            return
        args.handler(args)


class Internal:
    """Internal CLI for Bertrand."""

    class Parser:
        """Internal command-line parser for Bertrand's CLI.

        This parser is only used when Bertrand is invoked from within a containerized
        environment, and is responsible for managing the internal toolchain and
        development environment.  A separate parser is used outside a containerized
        environment to manage the environments themselves.
        """

        def __init__(self) -> None:
            self.root = argparse.ArgumentParser(
                description="Command line utilities for bertrand.",
            )
            self.commands = self.root.add_subparsers(
                dest="command",
                title="commands",
                description=(
                    "Control the internal toolchain and development environment within "
                    "a Bertrand container."
                ),
                prog="bertrand",
                metavar="(command)",
            )
            self.version()
            self.code()
            self.build()
            self.check()
            self.test()
            self.format()

        def version(self) -> None:
            """Add the 'version' query to the parser."""
            self.root.add_argument(
                "-v", "--version",
                action="version",
                version=__version__
            )

        def code(self) -> None:
            """Add the 'code' command to the parser."""
            command = self.commands.add_parser(
                "code",
                help=
                    "Launch a text editor rooted at this container's environment "
                    "directory and mount its internal toolchain using remote "
                    "development extensions over the host RPC sidecar.",
            )
            command.add_argument(
                "--editor",
                default=None,
                metavar="EDITOR",
                help=
                    "Override the configured host editor alias for this request.  "
                    "Validation is performed at runtime by RPC/config resolution.",
            )
            command.add_argument(
                "--block",
                action="store_true",
                help=
                    "Block until the launched host editor exits.  If omitted, this "
                    "command returns after the editor launch request is submitted.",
            )
            command.set_defaults(handler=Internal.code)

        def build(self) -> None:
            """Add the 'build' command to the parser."""
            command = self.commands.add_parser(
                "build",
                help=
                    "Build and install the current workspace into this container "
                    "using Bertrand's default 'uv install' command.",
            )
            command.set_defaults(handler=Internal.build)

        def check(self) -> None:
            """Add the 'check' command to the parser."""
            command = self.commands.add_parser(
                "check",
                help=
                    "Run cross-language static checks for the current workspace: "
                    "Ruff, Ty, and clang-tidy (requires compile_commands.json).",
            )
            command.set_defaults(handler=Internal.check)

        def test(self) -> None:
            """Add the 'test' command to the parser."""
            command = self.commands.add_parser(
                "test",
                help="Run the workspace test suite with pytest.",
            )
            command.set_defaults(handler=Internal.test)

        def format(self) -> None:
            """Add the 'format' command to the parser."""
            command = self.commands.add_parser(
                "format",
                help=
                    "Run cross-language formatting for the current workspace: "
                    "Ruff and clang-format (requires compile_commands.json).",
            )
            command.set_defaults(handler=Internal.format)

        def __call__(self) -> argparse.Namespace:
            """Run the command-line parser.

            Returns
            -------
            argparse.Namespace
                The parsed command-line arguments.
            """
            return self.root.parse_args()

    @staticmethod
    def version(args: argparse.Namespace) -> None:
        """Execute the `bertrand --version` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        print(__version__)

    @staticmethod
    def code(args: argparse.Namespace) -> None:
        """Execute the `bertrand code` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        RuntimeError
            If not invoked from within a containerized environment.
        """
        if not inside_container():
            raise RuntimeError(
                "`bertrand code` requires a live container context.  Run "
                "`bertrand enter` first."
            )
        with asyncio.Runner() as runner:
            runner.run(rpc(CodeOpen(
                editor=args.editor or None,
                block=args.block,
            )))

    @staticmethod
    def build(args: argparse.Namespace) -> None:
        """Execute the `bertrand build` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If artifact sync or Python dependency/build orchestration fails.
        """
        tag = os.environ.get(IMAGE_TAG_ENV, "").strip()
        if not tag:
            raise OSError(
                "could not determine active image tag in container environment: "
                f"'{IMAGE_TAG_ENV}'"
            )
        with Config.load(WORKTREE_MOUNT) as config:
            config.sync(tag)
            config.build(tag)

    @staticmethod
    def check(args: argparse.Namespace) -> None:
        """Execute the `bertrand check` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If source discovery fails, e.g. due to malformed `compile_commands.json`.
        SystemExit
            If any check command exits non-zero.
        """
        tag = _require_active_image_tag()
        with Config.load(WORKTREE_MOUNT) as config:
            config.sync(tag)
            files = config.sources()
        artifact_root = str(CONTAINER_ARTIFACT_DIR)
        clang_tidy_config = CONTAINER_ARTIFACT_DIR / ".clang-tidy"

        # Python static checks
        for cmd in (["ruff", "check", "."], ["ty", "check", "."]):
            result = subprocess.run(cmd, check=False, cwd=WORKTREE_MOUNT)
            if result.returncode != 0:
                raise SystemExit(result.returncode)

        # C++ static checks over resolved compilation sources.
        for source in files:
            result = subprocess.run(
                [
                    "clang-tidy",
                    "-p", artifact_root,
                    f"--config-file={clang_tidy_config}",
                    str(source),
                ],
                check=False,
                cwd=WORKTREE_MOUNT,
            )
            if result.returncode != 0:
                raise SystemExit(result.returncode)

    @staticmethod
    def format(args: argparse.Namespace) -> None:
        """Execute the `bertrand format` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        OSError
            If source discovery fails, e.g. due to malformed `compile_commands.json`.
        SystemExit
            If formatting exits non-zero.
        """
        tag = _require_active_image_tag()
        with Config.load(WORKTREE_MOUNT) as config:
            config.sync(tag)
            files = config.sources()
        clang_format_config = CONTAINER_ARTIFACT_DIR / ".clang-format"

        # Python formatting
        result = subprocess.run(["ruff", "format", "."], check=False, cwd=WORKTREE_MOUNT)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

        # C++ formatting over resolved compilation sources.
        for source in files:
            result = subprocess.run(
                ["clang-format", f"--style=file:{clang_format_config}", "-i", str(source)],
                check=False,
                cwd=WORKTREE_MOUNT
            )
            if result.returncode != 0:
                raise SystemExit(result.returncode)

    @staticmethod
    def test(args: argparse.Namespace) -> None:
        """Execute the `bertrand test` CLI command from within a containerized
        environment.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        SystemExit
            If the test process exits non-zero.
        """
        result = subprocess.run(["pytest", "-q"], check=False, cwd=WORKTREE_MOUNT)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

    def __call__(self) -> None:
        parser = Internal.Parser()
        args = parser()
        if args.command is None:
            parser.root.print_help()
            return
        args.handler(args)


def main() -> None:
    """Entry point for the Bertrand CLI."""
    if inside_image():
        Internal()()
    else:
        External()()
