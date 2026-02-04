"""A selection of atomic systemd operations meant to be used in conjunction
with CLI pipelines.
"""
from __future__ import annotations

import os
import shutil
from dataclasses import dataclass

from .pipeline import JSONValue, Pipeline, atomic
from .run import run, sudo_prefix

# pylint: disable=unused-argument, missing-function-docstring, broad-exception-caught


def _systemctl_base(user: bool) -> list[str]:
    if user:
        return ["systemctl", "--user"]
    return ["systemctl"]


def _ensure_systemctl() -> None:
    if not shutil.which("systemctl"):
        raise OSError("systemctl not found")
    cp = run(["systemctl", "is-system-running"], check=False, capture_output=True)
    state = (cp.stdout or "").strip()
    if state in {"running", "degraded", "starting", "maintenance"}:
        return
    raise OSError(f"systemctl not reachable (state={state or 'unknown'})")


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
    """
    name: str
    user: bool = False

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        _ensure_systemctl()
        name = self.name
        user = self.user
        payload["name"] = name
        payload["user"] = user
        ctx.dump()

        # check current active state
        is_active = run([*_systemctl_base(user), "is-active", "--quiet", name], check=False)
        was_active = is_active.returncode == 0
        payload["was_active"] = was_active
        ctx.dump()
        if was_active:
            return

        # ensure we can elevate if needed
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                raise PermissionError(
                    "Starting system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]

        # start the service
        run([*cmd_prefix, "start", name])

        # record start timestamp
        info = _service_show(name, user)
        start_monotonic = info.get("ActiveEnterTimestampMonotonic")
        if start_monotonic:
            payload["start_monotonic"] = start_monotonic
            ctx.dump()

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        name = payload.get("name")
        user = payload.get("user")
        was_active = payload.get("was_active")
        start_monotonic = payload.get("start_monotonic")
        if not isinstance(name, str) or not isinstance(user, bool):
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
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                raise PermissionError(
                    "Stopping system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, "systemctl"]
        run([*cmd_prefix, "stop", name])


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
    """
    name: str
    user: bool = False

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        _ensure_systemctl()
        name = self.name
        user = self.user
        payload["name"] = name
        payload["user"] = user
        ctx.dump()

        # check current active state
        is_active = run([*_systemctl_base(user), "is-active", "--quiet", name], check=False)
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
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                raise PermissionError(
                    "Stopping system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]

        # stop the service
        run([*cmd_prefix, "stop", name])

        # record stop timestamp
        info = _service_show(name, user)
        stop_monotonic = info.get("ActiveEnterTimestampMonotonic")
        if stop_monotonic:
            payload["stop_monotonic"] = stop_monotonic
            ctx.dump()

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        name = payload.get("name")
        user = payload.get("user")
        was_active = payload.get("was_active")
        stop_monotonic = payload.get("stop_monotonic")
        if not isinstance(name, str) or not isinstance(user, bool):
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
                raise PermissionError(
                    "Starting system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, "systemctl"]
        run([*cmd_prefix, "start", name])


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
    """
    name: str
    user: bool = False

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        _ensure_systemctl()
        name = self.name
        user = self.user
        payload["name"] = name
        payload["user"] = user
        ctx.dump()

        # record pre-restart timestamp
        info = _service_show(name, user)
        pre_monotonic = info.get("ActiveEnterTimestampMonotonic")
        if pre_monotonic:
            payload["pre_restart_monotonic"] = pre_monotonic
            ctx.dump()

        # restart the service
        cmd_prefix = _systemctl_base(user)
        if not user:
            sudo = sudo_prefix()
            if os.geteuid() != 0 and not sudo:
                raise PermissionError(
                    "Restarting system services requires root privileges; no sudo available."
                )
            cmd_prefix = [*sudo, *cmd_prefix]
        run([*cmd_prefix, "restart", name])

        # record post-restart timestamp
        info = _service_show(name, user)
        post_monotonic = info.get("ActiveEnterTimestampMonotonic")
        if post_monotonic:
            payload["post_restart_monotonic"] = post_monotonic
            ctx.dump()

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
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
    """
    name: str
    user: bool = False

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        _ensure_systemctl()
        name = self.name
        user = self.user
        payload["name"] = name
        payload["user"] = user
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
        run([*cmd_prefix, "enable", name])

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        name = payload.get("name")
        user = payload.get("user")
        was_enabled = payload.get("was_enabled")
        if not isinstance(name, str) or not isinstance(user, bool):
            return
        if was_enabled is not False:
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
        try:
            run([*cmd_prefix, "disable", name])
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
    """
    name: str
    user: bool = False

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        _ensure_systemctl()
        name = self.name
        user = self.user
        payload["name"] = name
        payload["user"] = user
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
        run([*cmd_prefix, "disable", name])

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        name = payload.get("name")
        user = payload.get("user")
        was_enabled = payload.get("was_enabled")
        if not isinstance(name, str) or not isinstance(user, bool):
            return
        if was_enabled is not True:
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
        try:
            run([*cmd_prefix, "enable", name])
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
    """
    user: bool = False

    def apply(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("systemd operations require a POSIX system.")
        _ensure_systemctl()
        user = self.user
        payload["user"] = user
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
        run([*cmd_prefix, "daemon-reload"])

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        return  # no-op
