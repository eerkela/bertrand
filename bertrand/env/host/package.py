"""Host-level package management across a variety of Linux distributions."""

from __future__ import annotations

import os
import platform
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING, Literal

from bertrand.env.git import (
    NO_DEADLINE,
    Deadline,
    can_escalate,
    confirm,
    run,
    sudo,
)

if TYPE_CHECKING:
    from collections.abc import Sequence


def read_os_release() -> dict[str, str]:
    """Read key-value pairs from the host's `/etc/os-release` file, if it exists.

    Returns
    -------
    dict[str, str]
        A dictionary containing the parsed values of every `<KEY>=<VALUE>` pair in the
        host's `/etc/os-release` file, if it exists.  If the file does not exist, an
        empty dictionary is returned.  Values are stripped of whitespace and up to 1
        layer of surrounding single or double quotes.  Lines that are empty, start with
        `#`, or do not contain `=` are ignored.
    """
    path = Path("/etc/os-release")
    data: dict[str, str] = {}
    if not path.exists() or not path.is_file():
        return data
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        data[k.strip()] = v.strip().strip('"').strip("'")
    return data


@dataclass(frozen=True)
class _PackageSpec:
    install: list[str]
    refresh: list[str] | None
    yes_install: list[str]
    yes_refresh: list[str]
    noninteractive_env: dict[str, str] | None


type PackageManagerName = Literal["apt", "dnf", "yum", "zypper", "pacman", "apk"]


_INSTALL_SPECS: dict[PackageManagerName, _PackageSpec] = {
    "apt": _PackageSpec(
        install=["apt-get", "install"],
        refresh=["apt-get", "update"],
        yes_install=["-y"],
        yes_refresh=[],
        noninteractive_env={"DEBIAN_FRONTEND": "noninteractive"},
    ),
    "dnf": _PackageSpec(
        install=["dnf", "install"],
        refresh=["dnf", "makecache"],
        yes_install=["-y"],
        yes_refresh=["-y"],
        noninteractive_env=None,
    ),
    "yum": _PackageSpec(
        install=["yum", "install"],
        refresh=["yum", "makecache"],
        yes_install=["-y"],
        yes_refresh=["-y"],
        noninteractive_env=None,
    ),
    "zypper": _PackageSpec(
        install=["zypper", "install"],
        refresh=["zypper", "refresh"],
        yes_install=["--non-interactive"],
        yes_refresh=["--non-interactive"],
        noninteractive_env=None,
    ),
    "pacman": _PackageSpec(
        install=["pacman", "-S"],
        refresh=["pacman", "-Sy"],
        yes_install=["--noconfirm"],
        yes_refresh=[],
        noninteractive_env=None,
    ),
    "apk": _PackageSpec(
        install=["apk", "add"],
        refresh=["apk", "update"],
        yes_install=["--no-interactive"],
        yes_refresh=["--no-interactive"],
        noninteractive_env=None,
    ),
}
PREREQS: dict[PackageManagerName, dict[str, str]] = {
    "apt": {
        "groupadd": "passwd",
        "usermod": "passwd",
        "curl": "curl",
        "install": "coreutils",
        "mount.ceph": "ceph-common",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
    "dnf": {
        "groupadd": "shadow-utils",
        "usermod": "shadow-utils",
        "curl": "curl",
        "install": "coreutils",
        "mount.ceph": "ceph-common",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
    "yum": {
        "groupadd": "shadow-utils",
        "usermod": "shadow-utils",
        "curl": "curl",
        "install": "coreutils",
        "mount.ceph": "ceph-common",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
    "zypper": {
        "groupadd": "shadow",
        "usermod": "shadow",
        "curl": "curl",
        "install": "coreutils",
        "mount.ceph": "ceph-common",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
    "pacman": {
        "groupadd": "shadow",
        "usermod": "shadow",
        "curl": "curl",
        "install": "coreutils",
        "mount.ceph": "ceph",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
    "apk": {
        "groupadd": "shadow",
        "usermod": "shadow",
        "curl": "curl",
        "install": "coreutils",
        "mount.ceph": "ceph",
        "pvs": "lvm2",
        "vgs": "lvm2",
        "lvs": "lvm2",
        "losetup": "util-linux",
    },
}
CHECK_PREREQS = (
    ("groupadd", ("groupadd",)),
    ("usermod", ("usermod",)),
    ("curl", ("curl",)),
    ("install", ("install",)),
    ("mount.ceph", ("mount.ceph",)),
    ("pvs", ("pvs",)),
    ("vgs", ("vgs",)),
    ("lvs", ("lvs",)),
    ("losetup", ("losetup",)),
)


@dataclass(frozen=True)
class PackageManager:
    """A distro-agnostic interface for installing packages on the host system.

    Attributes
    ----------
    name : PackageManagerName
        The name of the detected package manager (e.g. "apt", "dnf", etc.).
    """

    @staticmethod
    def _detect_package_manager() -> PackageManagerName:
        system = platform.system().lower()
        if system != "linux":
            msg = "Unsupported platform for package manager detection"
            raise OSError(msg)

        os_release = read_os_release()
        distro_id = (os_release.get("ID") or "").lower() or None
        manager: str | None = None
        if distro_id in {"debian", "ubuntu"} and shutil.which("apt-get"):
            manager = "apt"
        elif shutil.which("dnf"):
            manager = "dnf"
        elif shutil.which("yum"):
            manager = "yum"
        elif shutil.which("zypper"):
            manager = "zypper"
        elif shutil.which("pacman"):
            manager = "pacman"
        elif shutil.which("apk"):
            manager = "apk"
        if manager is None:
            msg = "No supported package manager found"
            raise OSError(msg)
        return manager

    name: PackageManagerName = field(default_factory=_detect_package_manager)
    _spec: _PackageSpec = field(init=False)

    def __post_init__(self) -> None:
        """Validate the detected package manager.

        Raises
        ------
        OSError
            If the package manager is not supported on the current system.
        ValueError
            If the detected package manager is not supported.
        """
        if os.name != "posix":
            msg = "package manager operations require a POSIX system."
            raise OSError(msg)
        spec = _INSTALL_SPECS.get(self.name)
        if spec is None:
            msg = (
                f"Unsupported package manager detected: {self.name!r} (supported: "
                f"{sorted(_INSTALL_SPECS)})"
            )
            raise ValueError(msg)
        object.__setattr__(self, "_spec", spec)

    async def install(
        self,
        packages: Sequence[str],
        *,
        deadline: Deadline = NO_DEADLINE,
        yes: bool = False,
    ) -> None:
        """Install OS packages with the requested host package manager.

        Parameters
        ----------
        packages : Sequence[str]
            The packages to install.
        deadline : Deadline, optional
            An overall deadline by which the installation must complete before raising a
            `TimeoutExpired` exception.  Defaults to an infinite deadline, which causes
            the command to block indefinitely until completion.  If the deadline is
            earlier than the time this method is called, a `TimeoutExpired` exception
            will be raised immediately.
        yes : bool, optional
            If True, automatically confirm any prompts for installation or refreshing
            package lists.  Default is False.

        Raises
        ------
        PermissionError
            If the package manager requires root privileges and they are not available.
        """
        if os.geteuid() != 0 and not can_escalate():
            msg = (
                f"package installation using '{self.name}' requires root privileges; "
                "sudo not available."
            )
            raise PermissionError(msg)

        # generate environment for non-interactive installs, if needed
        env: dict[str, str] | None = None
        if yes and self._spec.noninteractive_env:
            env = os.environ.copy()
            env.update(self._spec.noninteractive_env)

        # refresh package lists if supported
        if self._spec.refresh is not None:
            cmd = self._spec.refresh.copy()
            if yes:
                cmd.extend(self._spec.yes_refresh)
            await run(
                sudo(cmd, non_interactive=yes),
                env=env,
                deadline=deadline,
            )

        # install requested packages
        cmd = self._spec.install.copy()
        if yes:
            cmd.extend(self._spec.yes_install)
        cmd.extend(packages)
        await run(
            sudo(cmd, non_interactive=yes),
            env=env,
            deadline=deadline,
        )


async def install_prereqs(*, yes: bool, deadline: Deadline) -> None:
    """Install host bootstrap prerequisites using the detected package manager.

    Parameters
    ----------
    yes : bool
        If True, automatically confirm any prompts for installation or refreshing
        package lists.
    deadline : Deadline
        An overall deadline by which the installation must complete before raising a
        `TimeoutExpired` exception.  If the deadline is earlier than the time this
        method is called, a `TimeoutExpired` exception will be raised immediately.

    Raises
    ------
    OSError
        If the package manager is not supported on the current system, if the package
        manager's executable or its refresh command (if applicable) is not found in the
        system PATH, or if the installation fails for any reason.
    PermissionError
        If the package manager requires root privileges and they are not available, or
        if the user declines to allow Bertrand to perform the installation.
    """
    # fail fast if no escalation path is available for package installs
    if os.geteuid() != 0 and not can_escalate():
        msg = (
            "Bertrand requires root escalation to install host bootstrap dependencies, "
            "but neither 'sudo' nor 'doas' is available.  Install one of these tools "
            "or manually rerun `bertrand init` as root."
        )
        raise PermissionError(msg)

    # package mapping for bootstrap-required host tools across package managers
    package_manager = PackageManager()
    packages = PREREQS.get(package_manager.name)
    if packages is None:
        msg = (
            "Unsupported package manager for prerequisite installation: "
            f"'{package_manager.name}' (supported: {sorted(PREREQS)})"
        )
        raise OSError(msg)

    # detect missing required bootstrap tools
    missing: set[str] = set()
    for tool, package in packages.items():
        if package in missing:
            continue
        if shutil.which(tool):
            continue
        missing.add(package)
    if not missing:
        return

    # install missing tools
    if not confirm(
        "Bertrand requires host bootstrap tools to configure runtime "
        f"dependencies and shared state (missing: {', '.join(missing)}).  Would "
        "you like Bertrand to install missing packages now (requires sudo)?\n[y/N] ",
        assume_yes=yes,
    ):
        msg = "Installation declined by user."
        raise PermissionError(msg)
    await package_manager.install(
        packages=sorted(missing),
        yes=yes,
        deadline=deadline,
    )

    # verify all required tools after installation
    unresolved: list[str] = [
        name for name, cmd in CHECK_PREREQS if not any(shutil.which(c) for c in cmd)
    ]
    if unresolved:
        msg = (
            "Prerequisite installation completed, but required host bootstrap tools "
            f"are still missing: {', '.join(unresolved)}."
        )
        raise OSError(msg)
