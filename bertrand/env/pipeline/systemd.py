"""A selection of atomic systemd operations meant to be used in conjunction
with CLI pipelines.
"""
from __future__ import annotations

import os
import shlex
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import cast

from .core import JSONValue, Pipeline, atomic
from ..run import confirm, run, sudo_prefix

# pylint: disable=unused-argument, missing-function-docstring, broad-exception-caught


def _systemctl_base(user: bool) -> list[str]:
    if user:
        return ["systemctl", "--user"]
    return ["systemctl"]


def _ensure_systemctl(user: bool) -> None:
    if not shutil.which("systemctl"):
        raise OSError("systemctl not found")
    cp = run([*_systemctl_base(user), "is-system-running"], check=False, capture_output=True)
    state = (cp.stdout or "").strip()
    if state in {"running", "degraded", "starting", "maintenance"}:
        return
    scope = "user" if user else "system"
    raise OSError(f"systemctl ({scope}) not reachable (state={state or 'unknown'})")


def _service_show(name: str, user: bool) -> dict[str, str]:
    cmd = [
        *_systemctl_base(user),
        "show",
        "--no-page",
        "-p", "ActiveState",
        "-p", "ActiveEnterTimestampMonotonic",
        name
    ]
    try:
        cp = run(cmd, check=False, capture_output=True)
    except Exception:
        return {}
    if cp.returncode != 0:
        return {}
    data: dict[str, str] = {}
    for line in cp.stdout.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        data[key.strip()] = value.strip()
    return data


def _is_enabled(name: str, user: bool) -> bool | None:
    cmd = [*_systemctl_base(user), "is-enabled", "--quiet", name]
    try:
        cp = run(cmd, check=False, capture_output=True)
    except Exception:
        return None
    if cp.returncode == 0:
        return True
    if cp.returncode == 1:
        return False
    return None


@atomic
@dataclass(frozen=True)
class StartService:
    """Start a systemd service and undo only if we can verify the start.

    Attributes
    ----------
    name : str
        The name of the systemd service to start.
    user : bool, optional
        If true, operate on the user service manager rather than the system service
        manager.
    env : dict[str, str] | None, optional
        An optional environment to pass when starting the service.  Note that this will
        not have any effect if the service is already active.  Defaults to None.
    """
    name: str
    user: bool = False
    env: dict[str, str] | None = None

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        name = self.name
        user = self.user
        env = self.env or {}
        _ensure_systemctl(user)
        payload["name"] = name
        payload["user"] = user
        payload["env"] = cast(dict[str, JSONValue], env)
        ctx.dump()

        # check current active state
        cmd_prefix = _systemctl_base(user)
        is_active = run([
            *cmd_prefix,
            "is-active",
            "--quiet",
            name
        ], check=False, env=env)
        was_active = is_active.returncode == 0
        payload["was_active"] = was_active
        ctx.dump()
        if was_active:
            return

        # ensure we can elevate if needed
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                raise PermissionError(
                    "Starting system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]

        # start the service
        run([*cmd_prefix, "start", name], env=env)

        # record start timestamp
        info = _service_show(name, user)
        start_monotonic = info.get("ActiveEnterTimestampMonotonic")
        if start_monotonic:
            payload["start_monotonic"] = start_monotonic
            ctx.dump()

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        name = payload.get("name")
        user = payload.get("user")
        env = payload.get("env")
        was_active = payload.get("was_active")
        start_monotonic = payload.get("start_monotonic")
        if not isinstance(name, str) or not isinstance(user, bool) or not isinstance(env, dict):
            return
        if was_active is not False:
            return
        if not isinstance(start_monotonic, str):
            return

        # if the service is not active or has been restarted, do not stop it
        info = _service_show(name, user)
        if info.get("ActiveState") != "active":
            return
        if info.get("ActiveEnterTimestampMonotonic") != start_monotonic:
            return

        # stop the service only if we can verify identity
        # Conservative force policy: keep checks, suppress undo errors.
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                if force:
                    return
                raise PermissionError(
                    "Stopping system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]
        try:
            run([*cmd_prefix, "stop", name], env=cast(dict[str, str], env))
        except Exception:
            if not force:
                raise


@atomic
@dataclass(frozen=True)
class StopService:
    """Stop a systemd service and undo only if we can verify the stop.

    Attributes
    ----------
    name : str
        The name of the systemd service to stop.
    user : bool, optional
        If true, operate on the user service manager rather than the system service
        manager.
    env : dict[str, str] | None, optional
        An optional environment to pass when stopping the service.  Note that this will
        not have any effect if the service is already inactive.  Defaults to None.
    """
    name: str
    user: bool = False
    env: dict[str, str] | None = None

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        name = self.name
        user = self.user
        env = self.env or {}
        _ensure_systemctl(user)
        payload["name"] = name
        payload["user"] = user
        payload["env"] = cast(dict[str, JSONValue], env)
        ctx.dump()

        # check current active state
        cmd_prefix = _systemctl_base(user)
        is_active = run([
            *cmd_prefix,
            "is-active",
            "--quiet",
            name
        ], check=False, env=env)
        was_active = is_active.returncode == 0
        payload["was_active"] = was_active

        # record pre-stop timestamp (if any)
        info = _service_show(name, user)
        pre_stop = info.get("ActiveEnterTimestampMonotonic")
        if pre_stop:
            payload["pre_stop_monotonic"] = pre_stop
        ctx.dump()
        if not was_active:
            return

        # ensure we can elevate if needed
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                raise PermissionError(
                    "Stopping system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]

        # stop the service
        run([*cmd_prefix, "stop", name], env=env)

        # record stop timestamp
        info = _service_show(name, user)
        stop_monotonic = info.get("ActiveEnterTimestampMonotonic")
        if stop_monotonic:
            payload["stop_monotonic"] = stop_monotonic
            ctx.dump()

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        name = payload.get("name")
        user = payload.get("user")
        env = payload.get("env")
        was_active = payload.get("was_active")
        stop_monotonic = payload.get("stop_monotonic")
        if not isinstance(name, str) or not isinstance(user, bool) or not isinstance(env, dict):
            return
        if was_active is not True:
            return
        if not isinstance(stop_monotonic, str):
            return

        # only restart if still inactive and timestamp matches
        info = _service_show(name, user)
        if info.get("ActiveState") == "active":
            return
        if info.get("ActiveEnterTimestampMonotonic") != stop_monotonic:
            return

        # restart the service
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                if force:
                    return
                raise PermissionError(
                    "Starting system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]
        try:
            run([*cmd_prefix, "start", name], env=cast(dict[str, str], env))
        except Exception:
            if not force:
                raise


@atomic
@dataclass(frozen=True)
class RestartService:
    """Restart a systemd service. Undo is a no-op by design.

    Attributes
    ----------
    name : str
        The name of the systemd service to restart.
    user : bool, optional
        If true, operate on the user service manager rather than the system service
        manager.
    env : dict[str, str] | None, optional
        An optional environment to pass when restarting the service.  Defaults to None.
    """
    name: str
    user: bool = False
    env: dict[str, str] | None = None

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        name = self.name
        user = self.user
        env = self.env or {}
        _ensure_systemctl(user)
        payload["name"] = name
        payload["user"] = user
        payload["env"] = cast(dict[str, JSONValue], env)
        ctx.dump()

        # try-restart the service (no-op if inactive)
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                raise PermissionError(
                    "Restarting system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]
        run([*cmd_prefix, "try-restart", name], env=env)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        return  # no-op


@atomic
@dataclass(frozen=True)
class EnableService:
    """Enable a systemd service at boot.

    Attributes
    ----------
    name : str
        The name of the systemd service to enable.
    user : bool, optional
        If true, operate on the user service manager rather than the system service
        manager.
    env : dict[str, str] | None, optional
        An optional environment to pass when enabling the service.  Note that this will
        not have any effect if the service is already enabled.  Defaults to None.
    """
    name: str
    user: bool = False
    env: dict[str, str] | None = None

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        name = self.name
        user = self.user
        env = self.env or {}
        _ensure_systemctl(user)
        payload["name"] = name
        payload["user"] = user
        payload["env"] = cast(dict[str, JSONValue], env)
        ctx.dump()

        # check current enabled state
        was_enabled = _is_enabled(name, user)
        payload["was_enabled"] = was_enabled
        ctx.dump()
        if was_enabled is True:
            return

        # enable the service
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                raise PermissionError(
                    "Enabling system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]
        run([*cmd_prefix, "enable", name], env=env)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        name = payload.get("name")
        user = payload.get("user")
        env = payload.get("env")
        was_enabled = payload.get("was_enabled")
        if not isinstance(name, str) or not isinstance(user, bool) or not isinstance(env, dict):
            return
        if was_enabled is not False:
            return

        # disable the service
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                if force:
                    return
                raise PermissionError(
                    "Disabling system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]
        try:
            run([*cmd_prefix, "disable", name], env=cast(dict[str, str], env))
        except Exception:
            pass


@atomic
@dataclass(frozen=True)
class DisableService:
    """Disable a systemd service at boot.

    Attributes
    ----------
    name : str
        The name of the systemd service to disable.
    user : bool, optional
        If true, operate on the user service manager rather than the system service
        manager.
    env : dict[str, str] | None, optional
        An optional environment to pass when disabling the service.  Note that this
        will not have any effect if the service is already disabled.  Defaults to None.
    """
    name: str
    user: bool = False
    env: dict[str, str] | None = None

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        name = self.name
        user = self.user
        env = self.env or {}
        _ensure_systemctl(user)
        payload["name"] = name
        payload["user"] = user
        payload["env"] = cast(dict[str, JSONValue], env)
        ctx.dump()

        # check current enabled state
        was_enabled = _is_enabled(name, user)
        payload["was_enabled"] = was_enabled
        ctx.dump()
        if was_enabled is False:
            return

        # disable the service
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                raise PermissionError(
                    "Disabling system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]
        run([*cmd_prefix, "disable", name], env=env)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        name = payload.get("name")
        user = payload.get("user")
        env = payload.get("env")
        was_enabled = payload.get("was_enabled")
        if not isinstance(name, str) or not isinstance(user, bool) or not isinstance(env, dict):
            return
        if was_enabled is not True:
            return

        # enable the service
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                if force:
                    return
                raise PermissionError(
                    "Enabling system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]
        try:
            run([*cmd_prefix, "enable", name], env=cast(dict[str, str], env))
        except Exception:
            pass


@atomic
@dataclass(frozen=True)
class ReloadDaemon:
    """Run systemctl daemon-reload.

    Attributes
    ----------
    user : bool, optional
        If true, operate on the user service manager rather than the system service
        manager.
    env : dict[str, str] | None, optional
        An optional environment to pass when reloading the daemon.  Defaults to None.
    """
    user: bool = False
    env: dict[str, str] | None = None

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        user = self.user
        env = self.env or {}
        _ensure_systemctl(user)
        payload["user"] = user
        payload["env"] = cast(dict[str, JSONValue], env)
        ctx.dump()

        # reload the daemon
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                raise PermissionError(
                    "Reloading systemd requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]
        run([*cmd_prefix, "daemon-reload"], env=env)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        return  # no-op


@atomic
@dataclass(frozen=True)
class DelegateUserControllers:
    """Write a systemd drop-in to delegate cgroup controllers to user sessions.

    Attributes
    ----------
    controllers : list[str]
        The cgroup controllers to delegate (e.g., cpu, io, memory, pids, cpuset).
    prompt : str
        The prompt to show the user before making system-level changes.
    assume_yes : bool, optional
        If true, assume "yes" to any prompts.  Defaults to false.
    """
    controllers: list[str]
    prompt: str
    assume_yes: bool = False

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        if not shutil.which("systemctl"):
            raise OSError("systemctl not found")

        # validate controllers and record in payload
        controllers = [c for c in self.controllers if c]
        payload["controllers"] = cast(list[JSONValue], controllers)
        ctx.dump()
        if not controllers:
            return

        # ensure we can elevate if needed
        sudo = sudo_prefix(non_interactive=self.assume_yes)
        if os.geteuid() != 0 and not sudo:
            raise PermissionError(
                "Configuring controller delegation requires root privileges; no sudo available."
            )
        if not confirm(self.prompt, assume_yes=self.assume_yes):
            payload["declined"] = True
            ctx.dump()
            return

        # ensure drop-in directory exists
        dropin_dir = Path("/etc/systemd/system/user@.service.d")
        run([*sudo, "mkdir", "-p", str(dropin_dir)])

        # write delegate.conf via sudo
        delegate_path = dropin_dir / "delegate.conf"
        contents = "[Service]\nDelegate=" + " ".join(controllers) + "\n"
        quoted_path = shlex.quote(str(delegate_path))
        run([*sudo, "sh", "-lc", f"cat > {quoted_path}"], input=contents)
        payload["applied"] = True
        ctx.dump()

        # reload system daemon to pick up drop-in
        run([*sudo, "systemctl", "daemon-reload"])

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue], force: bool) -> None:
        return  # no-op
