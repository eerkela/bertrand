"""Run Bertrand from the command line to get include directory, version number, etc.
"""
from __future__ import annotations

import argparse
import json as json_parser
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import cast

from .env.code import CodeError, open_editor
from .env.pipeline import (
    JSONValue,
    Pipeline,
    on_init,
    on_build,
    on_start,
    on_enter,
    on_code,
    on_run,
    on_stop,
    on_pause,
    on_resume,
    on_restart,
    on_prune,
    on_rm,
    on_ls,
    on_monitor,
    on_top,
    on_log,
)
from .env.container import Environment
from .env.config import (
    ENV_LAYOUT_KEY,
    MOUNT,
    Config,
)
from .env.run import confirm
from . import __version__

# pylint: disable=unused-argument


INTERNAL_ENV: str = "BERTRAND"
RUNTIME_TAG_KEYS: tuple[str, str, str] = (
    "BERTRAND_ENV",
    "BERTRAND_IMAGE",
    "BERTRAND_CONTAINER",
)


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


def _dedupe(values: list[str]) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for value in values:
        if value in seen:
            continue
        seen.add(value)
        out.append(value)
    return out


def _existing_layout(env_root: Path) -> Config | None:
    env_file = env_root / ".bertrand" / "env.json"
    if not env_file.exists():
        return None
    if not env_file.is_file():
        raise OSError(f"environment metadata path is not a file: {env_file}")
    try:
        data = json_parser.loads(env_file.read_text(encoding="utf-8"))
    except Exception as err:
        raise OSError(f"failed to parse environment metadata at {env_file}: {err}") from err
    if not isinstance(data, dict):
        raise OSError(f"environment metadata at {env_file} must be a JSON object")
    if ENV_LAYOUT_KEY not in data:
        return None
    return Config.load(env_root)


def _check_init_layout_drift(
    env_root: Path,
    *,
    profile: str,
    capabilities: list[str],
) -> None:
    existing = _existing_layout(env_root)
    if existing is None:
        return

    existing_profile = existing.manifest.profile
    existing_capabilities = list(existing.manifest.capabilities)
    if existing_profile == profile and existing_capabilities == capabilities:
        return

    raise OSError(
        f"init layout mismatch for existing environment at {env_root}: requested "
        f"profile='{profile}', capabilities={capabilities}; existing "
        f"profile='{existing_profile}', capabilities={existing_capabilities}. "
        "Use matching init options or migrate/recreate the environment."
    )


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
            self.start()
            self.enter()
            self.code()
            self.run()
            self.stop()
            self.pause()
            self.resume()
            self.restart()
            self.prune()
            self.rm()
            self.ls()
            self.monitor()
            self.top()
            self.log()
            self.journal()
            self.clean()

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
                    "and then initialize a new project at the specified path (relative "
                    "or absolute).  This will create a directory at that path with a "
                    "template Containerfile, .containerignore, and pyproject.toml, "
                    "which the user can edit if needed.",
            )
            command.add_argument(
                "path",
                metavar="ENV",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must not point to an existing "
                    "file.  The last component will be used as the project name in the "
                    "generated pyproject.toml.",
            )
            command.add_argument(
                "--profile",
                choices=("flat", "src"),
                default="src",
                help=
                    "Layout profile to apply for environment structure and resource "
                    "placement (default: src).",
            )
            command.add_argument(
                "--lang",
                action="append",
                choices=("python", "cpp"),
                default=None,
                help=
                    "Language capability to include (repeatable).  If omitted, defaults "
                    "to python and cpp.",
            )
            command.add_argument(
                "--code",
                choices=("vscode", "none"),
                default="vscode",
                help=
                    "Editor integration capability to include.  Use 'none' to disable "
                    "editor capability entirely (default: vscode).",
            )
            command.set_defaults(handler=External.init)

        def build(self) -> None:
            """Add the 'build' command to the parser."""
            command = self.commands.add_parser(
                "build",
                help=
                    "Compile an image of a Bertrand environment at the specified "
                    "path, passing any additional build arguments directly to its "
                    "Containerfile.",
            )
            command.add_argument(
                "path",
                metavar="ENV[:IMAGE]",
                help=
                    "A path to the specified environment directory.  This may be an "
                    "absolute or relative path, and must point to an environment "
                    "directory produced by 'bertrand init'.  The path may include an "
                    "optional image tag (e.g. '/path/to/env:image'), in which case the "
                    "tag will be assigned to the resulting image and can be used to "
                    "reference it in other commands.  If no tag is given, then the "
                    "default image for the environment will be built, which uses the "
                    "default values specified in the Containerfile.",                
            )
            command.add_argument(
                "args",
                nargs=argparse.REMAINDER,
                metavar="COMPILE_ARGS",
                help=
                    "Containerfile arguments to use when compiling the the image.  If "
                    "none are given, then the image will be built with the default "
                    "values specified in the Containerfile, and the image tag may be "
                    "omitted from path.  Otherwise, an image tag must be given, which "
                    "can serve as a stable identifier for the specified arguments, "
                    "even if the resulting image is rebuilt in the future.",
            )
            command.set_defaults(handler=External.build)

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
                    "If an image or environment scope is given, then all containers "
                    "matching that scope will be started.  If no path is given, then "
                    "all Bertrand containers on the host system will be started, after "
                    "prompting the user to confirm.",
            )
            command.add_argument(
                "args",
                nargs=argparse.REMAINDER,
                metavar="CONTAINER_ARGS",
                help=
                    "Additional arguments to pass to the 'podman create' command when "
                    "building the container.  If given, then a (possibly new) "
                    "container scope must be listed in the path, and the container tag "
                    "will serve as a stable identifier for the specified arguments, "
                    "even if the resulting container is rebuilt in the future.",
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
                    "must correspond to a previous 'bertrand start' command.",
            )
            command.set_defaults(handler=External.enter)

        def run(self) -> None:
            """Add the 'run' command to the parser."""
            command = self.commands.add_parser(
                "run",
                help=
                    "Run a one-off command within a Bertrand virtual environment at "
                    "the specified path.  This is similar to 'bertrand enter' followed "
                    "by the command to run, but does not require an interactive shell "
                    "session.",
            )
            command.add_argument(
                "path",
                metavar="ENV[:IMAGE[:CONTAINER]]",
                help=
                    "A path to the specified environment directory.  If no image or "
                    "container tag is given, then the default container for the parent "
                    "environment or image will be used.  Otherwise, the container tag "
                    "must correspond to a previous 'bertrand start' command.",
            )
            command.add_argument(
                "cmd",
                nargs=argparse.REMAINDER,
                metavar="CMD",
                help=
                    "The command to run within the environment context.  Note that "
                    "Bertrand makes no attempt to parse, validate, or sanitize the "
                    "command; it is passed directly to the container as-is.  WARNING: "
                    "never pass untrusted input to this command, as it may lead to "
                    "arbitrary code execution within the container context.  "
                    "Bertrand's containers are rootless, so the host system should "
                    "remain insulated, but this is not guaranteed, and should not be "
                    "trusted.",
            )
            command.set_defaults(handler=External.run)

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

        def prune(self) -> None:
            """Add the 'prune' command to the parser."""
            command = self.commands.add_parser(
                "prune",
                help=
                    "Remove stopped Bertrand containers at the specified path, scoping "
                    "to specific images or containers if desired.  This is similar to "
                    "'bertrand rm', but only applies to stopped containers, and never "
                    "affects images.  Note that the environment directory is "
                    "unaffected, but any data stored in a stopped container's writable "
                    "layer will be permanently lost.",
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
                    "If an image or environment scope is given, then all stopped "
                    "containers matching that scope will be pruned.  If no path is "
                    "given, then all stopped Bertrand containers on the host system "
                    "will be pruned, after prompting the user to confirm.",
            )
            command.set_defaults(handler=External.prune)

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
                "--images",
                action="store_true",
                help=
                    "Show Bertrand images in the current scope instead of containers "
                    "(which is the default).",
            )
            status = command.add_mutually_exclusive_group()
            status.add_argument(
                "--running",
                action="store_true",
                help=
                    "Show only running Bertrand images or containers.  By default, all "
                    "images or containers are displayed, regardless of their status.  "
                    "In this context, 'running' covers the 'running', 'paused', and "
                    "'restarting' states.  An image is considered to be running if any "
                    "running container references it.",
            )
            status.add_argument(
                "--stopped",
                action="store_true",
                help=
                    "Show only stopped Bertrand images or containers.  By default, all "
                    "images or containers are displayed, regardless of their status.  "
                    "In this context, 'stopped' covers the 'created', 'removing', "
                    "'exited', and 'dead' states.  An image is considered to be "
                    "stopped if no running container references it.",
            )
            command.add_argument(
                "--json",
                action="store_true",
                help=
                    "Output the list in indented JSON format.  If omitted, the list "
                    "will be printed as a human-readable table.",
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
                "-i", "--interval",
                type=int,
                default=0,
                help=
                    "Interval (in seconds) between updates to the displayed "
                    "statistics.  If set to 0 (the default), then the statistics will "
                    "not be updated after the initial display.",
            )
            command.add_argument(
                "--json",
                action="store_true",
                help=
                    "Output the statistics in indented JSON format.  If omitted, the "
                    "statistics will be printed as a human-readable table.  Not "
                    "compatible with the '--interval' option, which must be set to 0 "
                    "if '--json' is used.",
            )
            command.set_defaults(handler=External.monitor)

        def top(self) -> None:
            """Add the 'top' command to the parser."""
            command = self.commands.add_parser(
                "top",
                help=
                    "Display the active processes within a running Bertrand container "
                    "at the specified path."
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
                "--images",
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
                    "option has no effect if used in conjunction with '--images'.",
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
                    "option has no effect if used in conjunction with '--images'.",
            )

        def code(self) -> None:
            """Add the 'code' command to the parser."""
            command = self.commands.add_parser(
                "code",
                help=
                    "Launch a text editor rooted at a Bertrand environment directory "
                    "and mount its local toolchain using remote development "
                    "extensions.  Note that the editor is launched on the host system "
                    "rather than within the container, and no editor is actually "
                    "bundled inside the container.  This necessitates an RPC service "
                    "to communicate between the container and host contexts, which is "
                    "managed via systemd.  Bertrand performs a strict startup/probe "
                    "before launching, and fails fast if the RPC service is "
                    "unreachable.  Currently only supports vscode and its Remote "
                    "Containers extension, but other editors may be added in the "
                    "future.",
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
                    "must correspond to a previous 'bertrand start' command.",
            )
            command.set_defaults(handler=External.code)

        # TODO: freeze()
        # TODO: unfreeze()
        # TODO: import_()
        # TODO: export()
        # TODO: publish()

        def journal(self) -> None:
            """Add the 'journal' command to the parser."""
            command = self.commands.add_parser(
                "journal",
                help=
                    "Run a predefined pipeline of commands within a Bertrand virtual "
                    "environment at the specified path.  This is similar to 'bertrand "
                    "run', but executes a sequence of commands defined in a JSON file "
                    "within the environment directory, rather than a single arbitrary "
                    "command passed directly from the command line.  This allows for "
                    "more complex workflows and automation, while still providing some "
                    "level of abstraction and safety compared to 'bertrand run'.",
            )
            command.add_argument(
                "subcommand",
                metavar="COMMAND",
                help=
                    "The command to dump the journal of.  This should correspond to "
                    "another 'bertrand' command (e.g. 'build', 'start', 'enter', etc.) "
                    "which records a journal of its activity.  Such journals allow "
                    "Bertrand to safely resume or undo the effects of a command, and "
                    "provide a limited form of logging in order to debug issues with "
                    "Bertrand itself."
            )
            command.set_defaults(handler=External.journal)

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

        def _validate_args(self, args: argparse.Namespace) -> None:
            command = args.command

            if command == "build":
                _, image_tag, container_tag = _parse(args.path)
                if container_tag:
                    self.root.error("build path cannot include a container tag")
                if args.args and not image_tag:
                    self.root.error(
                        "build passthrough args require an explicit image tag in PATH "
                        "(use ENV:IMAGE)"
                    )
                return

            if command == "start":
                _, _, container_tag = _parse(args.path)
                if args.args and not container_tag:
                    self.root.error(
                        "start passthrough args require an explicit container tag in "
                        "PATH (use ENV:IMAGE:CONTAINER)"
                    )
                return

            if command == "monitor":
                if args.json and args.interval != 0:
                    self.root.error("monitor --json requires --interval=0")
                return

            if command == "log":
                if args.images and (args.since is not None or args.until is not None):
                    self.root.error("log --images cannot be combined with --since/--until")
                return

            if command == "journal":
                if args.subcommand not in External.pipelines:
                    self.root.error(
                        f"invalid journal subcommand '{args.subcommand}'; must be one of: "
                        f"{', '.join(sorted(External.pipelines))}"
                    )

        def __call__(self) -> argparse.Namespace:
            """Run the command-line parser.

            Returns
            -------
            argparse.Namespace
                The parsed command-line arguments.
            """
            args = self.root.parse_args()
            self._validate_args(args)
            return args

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
            layout options differ from an existing manifest.
        """
        env, image_tag, container_tag = _parse(args.path)
        if env is None:
            raise OSError("environment path is required for init")
        if image_tag or container_tag:
            raise OSError(
                "cannot specify image or container tag when initializing an environment "
                "directory"
            )

        # resolve profile + capabilities
        profile = args.profile
        langs = list(args.lang) if args.lang is not None else ["python", "cpp"]
        code_capability = args.code
        capabilities = _dedupe(
            langs + ([] if code_capability == "none" else [code_capability])
        )
        if not capabilities:
            raise OSError("init capabilities must not be empty")

        # resolve environment path and check for drift from manifest
        root = Path(env).expanduser().resolve()
        _check_init_layout_drift(
            root,
            profile=profile,
            capabilities=capabilities,
        )

        on_init.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            profile=profile,
            capabilities=cast(JSONValue, capabilities),
        )

    @staticmethod
    def build(args: argparse.Namespace) -> None:
        """Execute the `bertrand build` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_build.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            args=args.args,
        )

    @staticmethod
    def start(args: argparse.Namespace) -> None:
        """Execute the `bertrand start` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_start.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            args=args.args,
        )

    @staticmethod
    def enter(args: argparse.Namespace) -> None:
        """Execute the `bertrand enter` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_enter.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
        )

    @staticmethod
    def run(args: argparse.Namespace) -> None:
        """Execute the `bertrand run` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_run.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            cmd=args.cmd,
        )

    @staticmethod
    def stop(args: argparse.Namespace) -> None:
        """Execute the `bertrand stop` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_stop.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            timeout=args.timeout,
        )

    @staticmethod
    def pause(args: argparse.Namespace) -> None:
        """Execute the `bertrand pause` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_pause.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
        )

    @staticmethod
    def resume(args: argparse.Namespace) -> None:
        """Execute the `bertrand resume` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_resume.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
        )

    @staticmethod
    def restart(args: argparse.Namespace) -> None:
        """Execute the `bertrand restart` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_restart.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            timeout=args.timeout,
        )

    @staticmethod
    def prune(args: argparse.Namespace) -> None:
        """Execute the `bertrand prune` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_prune.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
        )

    @staticmethod
    def rm(args: argparse.Namespace) -> None:
        """Execute the `bertrand rm` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_rm.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            force=args.force,
        )

    @staticmethod
    def ls(args: argparse.Namespace) -> None:
        """Execute the `bertrand ls` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_ls.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            images=args.images,
            running=args.running,
            stopped=args.stopped,
            json=args.json,
        )

    @staticmethod
    def monitor(args: argparse.Namespace) -> None:
        """Execute the `bertrand monitor` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_monitor.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            interval=args.interval,
            json=args.json,
        )

    @staticmethod
    def top(args: argparse.Namespace) -> None:
        """Execute the `bertrand top` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_top.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
        )

    @staticmethod
    def log(args: argparse.Namespace) -> None:
        """Execute the `bertrand log` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_log.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
            images=args.images,
            since=args.since,
            until=args.until,
        )

    @staticmethod
    def code(args: argparse.Namespace) -> None:
        """Execute the `bertrand code` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.
        """
        env, image_tag, container_tag = _parse(args.path)
        on_code.do(
            env=env,
            image_tag=image_tag,
            container_tag=container_tag,
        )

    pipelines: dict[str, Pipeline] = {
        "init": on_init,
        "build": on_build,
        "start": on_start,
        "enter": on_enter,
        "code": on_code,
        "run": on_run,
        "stop": on_stop,
        "pause": on_pause,
        "resume": on_resume,
        "restart": on_restart,
        "prune": on_prune,
        "rm": on_rm,
        "ls": on_ls,
        "monitor": on_monitor,
        "top": on_top,
        "log": on_log,
    }

    @staticmethod
    def journal(args: argparse.Namespace) -> None:
        """Execute the `bertrand journal` CLI command.

        Parameters
        ----------
        args : argparse.Namespace
            The parsed command-line arguments.

        Raises
        ------
        KeyError
            If the specified command is invalid or not recognized.
        """
        pipe = External.pipelines.get(args.subcommand, None)
        if pipe is None:
            raise KeyError(f"Invalid subcommand '{args.subcommand}'.")

        # load journal for the specified pipeline and dump it to stdout in JSON format
        with pipe:
            journal = pipe.state_dir / "journal.json"
            if not journal.exists():
                print(
                    f"bertrand: no journal found for '{args.subcommand}'",
                    file=sys.stderr
                )
                return
            data = json_parser.loads(journal.read_text())
            print(json_parser.dumps(data, indent=2))

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

        def _clean(pipe: Pipeline) -> None:
            try:
                pipe.undo(force=True)
                shutil.rmtree(pipe.state_dir, ignore_errors=True)
            except Exception as e:  # pylint: disable=broad-exception-caught
                print(f"bertrand: error during cleanup: {e}", file=sys.stderr)

        # NOTE: a specific ordering is necessary to ensure that pipelines are undone
        # in a safe manner, and never leave the system in a broken state.
        for pipe in (
            # on_publish,
            # on_export,
            # on_import,
            on_top,
            on_log,
            on_monitor,
            on_ls,
            on_rm,
            on_prune,
            on_stop,
            on_pause,
            on_resume,
            on_restart,
            on_run,
            on_code,
            on_enter,
            on_start,
            on_build,
            on_init,
        ):
            _clean(pipe)

    def __call__(self) -> None:
        parser = External.Parser()
        args = parser()
        if args.command is None:
            parser.root.print_help()
            return
        args.handler(args)


def inside_image() -> bool:
    """Check if we're currently running inside an image build context (Containerfile
    or container instance).

    Returns
    -------
    bool
        True if we're either building an image or running inside a container process.
        False otherwise.

    Notes
    -----
    If this is true and `inside_container()` is false, then it means the CLI was
    invoked from a Containerfile during an image build, which corresponds to AoT
    compilation for statically-typed languages.  The `build` command takes advantage
    of this to differentiate between normal and editable installs, for example.
    """
    return os.environ.get(INTERNAL_ENV, "") == "1"


def inside_container() -> bool:
    """Check if we're currently running inside a container instance.

    Returns
    -------
    bool
        True if we're running inside a container process, False otherwise.
    """
    return all(key in os.environ for key in RUNTIME_TAG_KEYS)


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
                    "development extensions.  Note that the editor choice is "
                    "determined by the environment configuration, and the editor "
                    "process is owned by the host system, not this container.  An RPC "
                    "service managed by systemd enables this communication.  Currently "
                    "only supports vscode and its Remote Containers extension, but "
                    "other editors may be added in the future.",
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
        CodeError
            If there is an error with the RPC communication or the editor
            configuration.
        """
        if not inside_container():
            raise CodeError(
                "invalid_service_environment",
                "`bertrand code` requires a live container context.  Run "
                "`bertrand enter` first."
            )
        warnings = open_editor()
        for warning in warnings:
            print(f"bertrand: warning: {warning}", file=sys.stderr)

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
        SystemExit
            If the build process fails with a non-zero exit code.
        """
        with Config.load(MOUNT) as config:
            config.sync()
        cmd: list[str] = ["uv", "pip", "install"]
        if inside_container():
            cmd.append("-e")
        cmd.append(".")
        result = subprocess.run(cmd, check=False, cwd=MOUNT)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

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
        with Config.load(MOUNT) as config:
            config.sync()
            files = config.sources()

        # Python static checks
        for cmd in (["ruff", "check", "."], ["ty", "check", "."]):
            result = subprocess.run(cmd, check=False, cwd=MOUNT)
            if result.returncode != 0:
                raise SystemExit(result.returncode)

        # C++ static checks over resolved compilation sources.
        for source in files:
            result = subprocess.run(
                ["clang-tidy", "-p", str(MOUNT), str(source)],
                check=False,
                cwd=MOUNT,
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
        with Config.load(MOUNT) as config:
            config.sync()
            files = config.sources()

        # Python formatting
        result = subprocess.run(["ruff", "format", "."], check=False, cwd=MOUNT)
        if result.returncode != 0:
            raise SystemExit(result.returncode)

        # C++ formatting over resolved compilation sources.
        for source in files:
            result = subprocess.run(
                ["clang-format", "-i", str(source)],
                check=False,
                cwd=MOUNT
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
        result = subprocess.run(["pytest", "-q"], check=False, cwd=MOUNT)
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
