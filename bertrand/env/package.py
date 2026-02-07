"""A selection of atomic package manager operations meant to be used in conjunction
with CLI pipelines.
"""
from __future__ import annotations

import os
import platform
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Literal, cast

from .pipeline import JSONValue, Pipeline, atomic
from .filesystem import WriteBytes, WriteText
from .network import Download
from .run import run, sudo_prefix

# pylint: disable=unused-argument, missing-function-docstring, broad-exception-caught


@dataclass(frozen=True)
class DetectPackageManager:
    """A result class for `detect_package_manager()`."""
    manager: str
    distro_id: str | None
    version_id: str | None
    codename: str | None


def _read_os_release() -> dict[str, str]:
    path = Path("/etc/os-release")
    data: dict[str, str] = {}
    if not path.exists():
        return data
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        data[k.strip()] = v.strip().strip('"').strip("'")
    return data


def _detect_apt_arch() -> str | None:
    """Detect the Debian/Ubuntu architecture string (e.g., amd64, arm64)."""
    if shutil.which("dpkg"):
        try:
            result = run(["dpkg", "--print-architecture"], capture_output=True)
            arch = result.stdout.strip()
            if arch:
                return arch
        except Exception:
            pass
    machine = platform.machine()
    arch_map = {
        "x86_64": "amd64",
        "amd64": "amd64",
        "aarch64": "arm64",
        "arm64": "arm64",
        "armv7l": "armhf",
        "armv6l": "armel",
        "i386": "i386",
        "i686": "i386",
        "ppc64le": "ppc64el",
        "s390x": "s390x",
    }
    return arch_map.get(machine)


def _parse_repo_key_path(manager: str, repo_path: Path) -> Path | None:
    """Best-effort parse of a key path referenced inside a repo file."""
    try:
        text = repo_path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return None

    def _clean(token: str) -> str:
        return token.strip().strip('"').strip("'")

    if manager == "apt":
        for line in text.splitlines():
            raw = line.strip()
            if not raw or raw.startswith("#"):
                continue
            key, sep, value = raw.partition(":")
            if sep and key.strip().lower() == "signed-by":
                token = _clean(value)
                if token:
                    first = token.split()[0]
                    if first.startswith("/"):
                        return Path(first)
            if raw.startswith("deb "):
                if "[" in raw and "]" in raw:
                    opts = raw[raw.find("[") + 1:raw.find("]")]
                    for opt in opts.split():
                        if opt.startswith("signed-by="):
                            token = _clean(opt.split("=", 1)[1])
                            if token.startswith("/"):
                                return Path(token)

        return None

    # RPM-style repo
    for line in text.splitlines():
        raw = line.strip()
        if not raw or raw.startswith("#"):
            continue
        if raw.lower().startswith("gpgkey="):
            value = _clean(raw.split("=", 1)[1])
            for token in value.replace(",", " ").split():
                token = _clean(token)
                if token.startswith("file://"):
                    path = token[7:]
                else:
                    path = token
                if path.startswith("/"):
                    return Path(path)
    return None


def detect_package_manager() -> DetectPackageManager:
    """Detect the system package manager and basic distro metadata.

    Returns
    -------
    DetectPackageManager
        The detected package manager and distro metadata.

    Raises
    ------
    OSError
        If the platform is unsupported or no supported package manager is found.
    """
    system = platform.system().lower()
    if system != "linux":
        raise OSError("Unsupported platform for package manager detection")

    # read /etc/os-release for distro info
    os_release = _read_os_release()
    distro_id = (os_release.get("ID") or "").lower() or None
    version_id = os_release.get("VERSION_ID") or None
    codename = os_release.get("UBUNTU_CODENAME") or os_release.get("VERSION_CODENAME")

    # detect package manager
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
        raise OSError("No supported package manager found")

    # return structured result
    return DetectPackageManager(
        manager=manager,
        distro_id=distro_id,
        version_id=version_id,
        codename=codename,
    )


@dataclass(frozen=True)
class InstallSpec:
    """A specification class for a system package manager to be used with
    `InstallPackage`.

    Attributes
    ----------
    install : list[str]
        The base command to install packages.
    remove : list[str]
        The base command to remove packages.
    refresh : list[str] | None
        The base command to refresh the package cache, or None if not supported.
    yes_install : list[str]
        The flags to assume yes for install commands.
    yes_remove : list[str]
        The flags to assume yes for remove commands.
    yes_refresh : list[str]
        The flags to assume yes for refresh commands.
    noninteractive_env : dict[str, str] | None
        Environment variables to set for non-interactive operation, or None.
    query : Callable[[str], tuple[bool, str | None]]
        A function to query if a package is installed, returning (installed, version),
        where version may be None if unknown.
    """
    install: list[str]
    remove: list[str]
    refresh: list[str] | None
    yes_install: list[str]
    yes_remove: list[str]
    yes_refresh: list[str]
    noninteractive_env: dict[str, str] | None
    query: Callable[[str], tuple[bool, str | None]]


@dataclass(frozen=True)
class RepositorySpec:
    """A specification class describing repository management for a system package
    manager, to be used with `AddRepository`.

    Attributes
    ----------
    repo_dir : Path
        The directory to store repository definition files.
    repo_ext : str
        The file extension for repository definition files.
    key_dir : Path
        The directory to store repository key files.
    refresh : list[str] | None
        The base command to refresh repository metadata, or None if not supported.
    yes_refresh : list[str]
        The flags to assume yes for refresh commands.
    noninteractive_env : dict[str, str] | None
        Environment variables to set for non-interactive operation, or None.
    """
    repo_dir: Path
    repo_ext: str
    key_dir: Path
    refresh: list[str] | None
    yes_refresh: list[str]
    noninteractive_env: dict[str, str] | None


@dataclass(frozen=True)
class CACertSpec:
    """A specification class describing CA trust store integration.

    Attributes
    ----------
    cert_dir : Path
        The directory to store CA certificate files.
    cert_ext : str
        The file extension for CA certificate files.
    refresh : list[str] | None
        The base command to refresh the trust store, or None if not supported.
    yes_refresh : list[str]
        The flags to assume yes for refresh commands.
    noninteractive_env : dict[str, str] | None
        Environment variables to set for non-interactive operation, or None.
    """
    cert_dir: Path
    cert_ext: str
    refresh: list[str] | None
    yes_refresh: list[str]
    noninteractive_env: dict[str, str] | None


def _query_dpkg(pkg: str) -> tuple[bool, str | None]:
    cp = run(
        ["dpkg-query", "-W", "-f=${Version}", pkg],
        check=False,
        capture_output=True,
    )
    if cp.returncode != 0:
        return False, None
    version = cp.stdout.strip()
    return True, version or None


def _query_rpm(pkg: str) -> tuple[bool, str | None]:
    cp = run(
        ["rpm", "-q", "--qf", "%{VERSION}-%{RELEASE}.%{ARCH}", pkg],
        check=False,
        capture_output=True,
    )
    if cp.returncode != 0:
        return False, None
    version = cp.stdout.strip()
    return True, version or None


def _query_pacman(pkg: str) -> tuple[bool, str | None]:
    cp = run(["pacman", "-Q", pkg], check=False, capture_output=True)
    if cp.returncode != 0:
        return False, None
    parts = cp.stdout.strip().split()
    if len(parts) >= 2:
        return True, parts[1]
    return True, None


def _query_apk(pkg: str) -> tuple[bool, str | None]:
    cp = run(["apk", "info", "-e", pkg], check=False, capture_output=True)
    if cp.returncode != 0:
        return False, None
    cp = run(["apk", "info", pkg], check=False, capture_output=True)
    if cp.returncode != 0:
        return True, None
    for line in cp.stdout.splitlines():
        if not line:
            continue
        token = line.split(maxsplit=1)[0]
        prefix = f"{pkg}-"
        if token.startswith(prefix):
            return True, token[len(prefix):]
    return True, None


INSTALL_MANAGERS: dict[str, InstallSpec] = {
    "apt": InstallSpec(
        install=["apt-get", "install"],
        remove=["apt-get", "remove"],
        refresh=["apt-get", "update"],
        yes_install=["-y"],
        yes_remove=["-y"],
        yes_refresh=[],
        noninteractive_env={"DEBIAN_FRONTEND": "noninteractive"},
        query=_query_dpkg,
    ),
    "dnf": InstallSpec(
        install=["dnf", "install"],
        remove=["dnf", "remove"],
        refresh=["dnf", "makecache"],
        yes_install=["-y"],
        yes_remove=["-y"],
        yes_refresh=["-y"],
        noninteractive_env=None,
        query=_query_rpm,
    ),
    "yum": InstallSpec(
        install=["yum", "install"],
        remove=["yum", "remove"],
        refresh=["yum", "makecache"],
        yes_install=["-y"],
        yes_remove=["-y"],
        yes_refresh=["-y"],
        noninteractive_env=None,
        query=_query_rpm,
    ),
    "zypper": InstallSpec(
        install=["zypper", "install"],
        remove=["zypper", "remove"],
        refresh=["zypper", "refresh"],
        yes_install=["--non-interactive"],
        yes_remove=["--non-interactive"],
        yes_refresh=["--non-interactive"],
        noninteractive_env=None,
        query=_query_rpm,
    ),
    "pacman": InstallSpec(
        install=["pacman", "-S"],
        remove=["pacman", "-R"],
        refresh=["pacman", "-Sy"],
        yes_install=["--noconfirm"],
        yes_remove=["--noconfirm"],
        yes_refresh=[],
        noninteractive_env=None,
        query=_query_pacman,
    ),
    "apk": InstallSpec(
        install=["apk", "add"],
        remove=["apk", "del"],
        refresh=["apk", "update"],
        yes_install=["--no-interactive"],
        yes_remove=["--no-interactive"],
        yes_refresh=["--no-interactive"],
        noninteractive_env=None,
        query=_query_apk,
    ),
}


REPOSITORY_MANAGERS: dict[str, RepositorySpec] = {
    "apt": RepositorySpec(
        repo_dir=Path("/etc/apt/sources.list.d"),
        repo_ext=".sources",
        key_dir=Path("/etc/apt/keyrings"),
        refresh=["apt-get", "update"],
        yes_refresh=[],
        noninteractive_env={"DEBIAN_FRONTEND": "noninteractive"},
    ),
    "dnf": RepositorySpec(
        repo_dir=Path("/etc/yum.repos.d"),
        repo_ext=".repo",
        key_dir=Path("/etc/pki/rpm-gpg"),
        refresh=["dnf", "makecache"],
        yes_refresh=["-y"],
        noninteractive_env=None,
    ),
    "yum": RepositorySpec(
        repo_dir=Path("/etc/yum.repos.d"),
        repo_ext=".repo",
        key_dir=Path("/etc/pki/rpm-gpg"),
        refresh=["yum", "makecache"],
        yes_refresh=["-y"],
        noninteractive_env=None,
    ),
    "zypper": RepositorySpec(
        repo_dir=Path("/etc/zypp/repos.d"),
        repo_ext=".repo",
        key_dir=Path("/etc/pki/rpm-gpg"),
        refresh=["zypper", "refresh"],
        yes_refresh=["--non-interactive"],
        noninteractive_env=None,
    ),
}


CA_CERT_MANAGERS: dict[str, CACertSpec] = {
    "apt": CACertSpec(
        cert_dir=Path("/usr/local/share/ca-certificates"),
        cert_ext=".crt",
        refresh=["update-ca-certificates"],
        yes_refresh=[],
        noninteractive_env={"DEBIAN_FRONTEND": "noninteractive"},
    ),
    "dnf": CACertSpec(
        cert_dir=Path("/etc/pki/ca-trust/source/anchors"),
        cert_ext=".crt",
        refresh=["update-ca-trust", "extract"],
        yes_refresh=[],
        noninteractive_env=None,
    ),
    "yum": CACertSpec(
        cert_dir=Path("/etc/pki/ca-trust/source/anchors"),
        cert_ext=".crt",
        refresh=["update-ca-trust", "extract"],
        yes_refresh=[],
        noninteractive_env=None,
    ),
    "zypper": CACertSpec(
        cert_dir=Path("/usr/local/share/ca-certificates"),
        cert_ext=".crt",
        refresh=["update-ca-certificates"],
        yes_refresh=["--non-interactive"],
        noninteractive_env=None,
    ),
}


def _valid_install_manager(manager: str) -> None:
    if manager not in INSTALL_MANAGERS:
        supported = ", ".join(sorted(INSTALL_MANAGERS))
        raise ValueError(f"Unsupported package manager '{manager}'. Supported: {supported}")


def _valid_repository_manager(manager: str) -> None:
    if manager not in REPOSITORY_MANAGERS:
        supported = ", ".join(sorted(REPOSITORY_MANAGERS))
        raise ValueError(f"Unsupported repository manager '{manager}'. Supported: {supported}")


def _valid_ca_manager(manager: str) -> None:
    if manager not in CA_CERT_MANAGERS:
        supported = ", ".join(sorted(CA_CERT_MANAGERS))
        raise ValueError(f"Unsupported CA manager '{manager}'. Supported: {supported}")


def _ensure_manager_cmd(manager: str, spec: InstallSpec) -> None:
    cmd = spec.install[0]
    if not shutil.which(cmd):
        raise FileNotFoundError(f"Package manager '{manager}' not found: {cmd}")


def _cmd_env(noninteractive_env: dict[str, str] | None, assume_yes: bool) -> dict[str, str] | None:
    if assume_yes and noninteractive_env:
        env = os.environ.copy()
        env.update(noninteractive_env)
        return env
    return None


def _build_cmd(
    base: list[str],
    yes_flags: list[str],
    packages: list[str],
    *,
    sudo: list[str],
    assume_yes: bool,
) -> list[str]:
    cmd = [*sudo, *base]
    if assume_yes and yes_flags:
        cmd.extend(yes_flags)
    cmd.extend(packages)
    return cmd


def _normalize_fingerprint(value: str) -> str:
    return "".join(value.split()).upper()


def _gpg_fingerprint(path: Path) -> str | None:
    cp = run(
        ["gpg", "--show-keys", "--with-fingerprint", str(path)],
        check=False,
        capture_output=True,
    )
    if cp.returncode != 0:
        return None
    for line in cp.stdout.splitlines():
        if "Key fingerprint" not in line:
            continue
        parts = line.split("=", 1)
        if len(parts) != 2:
            continue
        return _normalize_fingerprint(parts[1])
    return None


def _pin_version(manager: str, name: str, version: str) -> str:
    if manager in {"apt", "pacman", "apk"}:
        return f"{name}={version}"
    if manager in {"dnf", "yum", "zypper"}:
        return f"{name}-{version}"
    raise ValueError(f"Unsupported version pinning for manager '{manager}'")


@atomic
@dataclass(frozen=True)
class InstallPackage:
    """Install packages using the system package manager.

    Attributes
    ----------
    manager : str
        The package manager name (e.g., "apt", "dnf", "yum", "zypper", "pacman",
        "apk").  The manager string will be checked against `INSTALL_MANAGERS` to
        determine the corresponding commands to use.
    packages : list[str]
        The package names to install.
    assume_yes : bool, optional
        If true, use non-interactive flags to assume yes to all prompts.  Defaults to
        false.
    refresh : bool, optional
        If true, refresh the package manager's cache before installing.  Defaults to
        true.
    """
    manager: str
    packages: list[str]
    assume_yes: bool = False
    refresh: bool = True

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("Package manager operations require a POSIX system.")
        _valid_install_manager(self.manager)
        spec = INSTALL_MANAGERS[self.manager]
        _ensure_manager_cmd(self.manager, spec)

        # ensure we can elevate if needed
        sudo = sudo_prefix()
        if os.name == "posix" and os.geteuid() != 0 and not sudo:
            raise PermissionError(
                "Package installation requires root privileges; sudo not available."
            )

        # deduplicate package list while preserving order
        packages: list[str] = []
        seen: set[str] = set()
        for pkg in self.packages:
            if not pkg or pkg in seen:
                continue
            seen.add(pkg)
            packages.append(pkg)
        if not packages:
            return

        # persist intent before mutating
        payload["manager"] = self.manager
        payload["packages"] = cast(list[JSONValue], packages)
        payload["assume_yes"] = self.assume_yes
        payload["refresh"] = self.refresh
        ctx.dump()

        # snapshot preinstalled state
        preinstalled: dict[str, JSONValue] = {}
        for p in packages:
            p_installed, p_version = spec.query(p)
            if p_installed:
                preinstalled[p] = p_version
        if preinstalled:
            payload["preinstalled"] = preinstalled
            ctx.dump()

        try:
            # refresh if requested and manager supports it
            env = _cmd_env(spec.noninteractive_env, self.assume_yes)
            if self.refresh and spec.refresh:
                if not shutil.which(spec.refresh[0]):
                    raise FileNotFoundError(f"Refresh command not found: {spec.refresh[0]}")
                cmd = _build_cmd(
                    spec.refresh,
                    spec.yes_refresh,
                    [],
                    sudo=sudo,
                    assume_yes=self.assume_yes,
                )
                run(cmd, env=env)

            # install packages
            cmd = _build_cmd(
                spec.install,
                spec.yes_install,
                packages,
                sudo=sudo,
                assume_yes=self.assume_yes,
            )
            run(cmd, env=env)

        # record successfully installed packages even if installation fails
        finally:
            present: list[dict[str, JSONValue]] = []
            for pkg in packages:
                pkg_installed, pkg_version = spec.query(pkg)
                if not pkg_installed:
                    continue
                if pkg in preinstalled:
                    continue
                if pkg_version is None:
                    continue  # cannot verify version; skip for no-clobber
                present.append({"name": pkg, "version": pkg_version})
            payload["installed"] = cast(list[JSONValue], present)
            ctx.dump()

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("Package manager operations require a POSIX system.")
        manager = payload.get("manager")
        installed = payload.get("installed")
        if not isinstance(manager, str) or not isinstance(installed, list):
            return  # didn't get far enough to install packages

        # validate manager and check availability
        _valid_install_manager(manager)
        spec = INSTALL_MANAGERS[manager]
        _ensure_manager_cmd(manager, spec)

        # ensure we can elevate if needed
        sudo = sudo_prefix()
        if os.name == "posix" and os.geteuid() != 0 and not sudo:
            raise PermissionError(
                "Package removal requires root privileges; sudo not available."
            )

        # detect which packages need to be removed
        assume_yes = bool(payload.get("assume_yes", False))
        errors: list[str] = []
        to_remove: list[str] = []
        for item in installed:
            if not isinstance(item, dict):
                continue
            name = item.get("name")
            version = item.get("version")
            if not isinstance(name, str) or not isinstance(version, str):
                continue

            # ensure versions match what we installed
            installed, current = spec.query(name)
            if not installed:
                continue
            if current is None:
                errors.append(f"[{name}] cannot verify installed version")
                continue
            if current != version:
                errors.append(f"[{name}] version changed: {current} != {version}")
                continue
            to_remove.append(name)

        # remove any eligible packages (best-effort)
        if to_remove:
            try:
                cmd = _build_cmd(
                    spec.remove,
                    spec.yes_remove,
                    to_remove,
                    sudo=sudo,
                    assume_yes=assume_yes,
                )
                run(cmd, env=_cmd_env(spec.noninteractive_env, assume_yes))
            except Exception as e:
                errors.append(str(e))

        # raise if any issues occurred
        if errors:
            raise OSError(f"Errors occurred during package undo:\n{'\n'.join(errors)}")


@atomic
@dataclass(frozen=True)
class UninstallPackage:
    """Remove packages using the system package manager.

    Attributes
    ----------
    manager : str
        The package manager name (e.g., "apt", "dnf", "yum", "zypper", "pacman",
        "apk").  The manager string will be checked against `INSTALL_MANAGERS` to
        determine the corresponding commands to use.
    packages : list[str]
        The package names to remove.
    assume_yes : bool, optional
        If true, use non-interactive flags to assume yes to all prompts.  Defaults to
        false.
    refresh : bool, optional
        If true, refresh the package manager's cache before removing.  Defaults to
        false.
    """
    manager: str
    packages: list[str]
    assume_yes: bool = False
    refresh: bool = False

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("Package manager operations require a POSIX system.")
        _valid_install_manager(self.manager)
        spec = INSTALL_MANAGERS[self.manager]
        _ensure_manager_cmd(self.manager, spec)
        if spec.remove and not shutil.which(spec.remove[0]):
            raise FileNotFoundError(f"Remove command not found: {spec.remove[0]}")

        # ensure we can elevate if needed
        sudo = sudo_prefix()
        if os.name == "posix" and os.geteuid() != 0 and not sudo:
            raise PermissionError(
                "Package removal requires root privileges; sudo not available."
            )

        # deduplicate package list while preserving order
        packages: list[str] = []
        seen: set[str] = set()
        for pkg in self.packages:
            if not pkg or pkg in seen:
                continue
            seen.add(pkg)
            packages.append(pkg)
        if not packages:
            return

        # persist intent before mutating
        payload["manager"] = self.manager
        payload["packages"] = cast(list[JSONValue], packages)
        payload["assume_yes"] = self.assume_yes
        payload["refresh"] = self.refresh
        ctx.dump()

        # snapshot preinstalled state
        preinstalled: dict[str, JSONValue] = {}
        for p in packages:
            p_installed, p_version = spec.query(p)
            if p_installed:
                preinstalled[p] = p_version
        if preinstalled:
            payload["preinstalled"] = preinstalled
            ctx.dump()

        try:
            # refresh if requested and manager supports it
            env = _cmd_env(spec.noninteractive_env, self.assume_yes)
            if self.refresh and spec.refresh:
                if not shutil.which(spec.refresh[0]):
                    raise FileNotFoundError(f"Refresh command not found: {spec.refresh[0]}")
                cmd = _build_cmd(
                    spec.refresh,
                    spec.yes_refresh,
                    [],
                    sudo=sudo,
                    assume_yes=self.assume_yes,
                )
                run(cmd, env=env)

            # remove packages
            cmd = _build_cmd(
                spec.remove,
                spec.yes_remove,
                list(preinstalled),
                sudo=sudo,
                assume_yes=self.assume_yes,
            )
            run(cmd, env=env)

        # record successfully removed packages even if removal fails
        finally:
            removed: list[dict[str, JSONValue]] = []
            for pkg, pkg_version in preinstalled.items():
                pkg_installed, _ = spec.query(pkg)
                if pkg_installed:
                    continue
                if pkg_version is None:
                    continue  # cannot restore exact version; skip for no-clobber
                removed.append({"name": pkg, "version": pkg_version})
            payload["removed"] = cast(list[JSONValue], removed)
            ctx.dump()

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("Package manager operations require a POSIX system.")
        manager = payload.get("manager")
        removed = payload.get("removed")
        if not isinstance(manager, str) or not isinstance(removed, list):
            return

        # validate manager and check availability
        _valid_install_manager(manager)
        spec = INSTALL_MANAGERS[manager]
        _ensure_manager_cmd(manager, spec)

        # ensure we can elevate if needed
        sudo = sudo_prefix()
        if os.name == "posix" and os.geteuid() != 0 and not sudo:
            raise PermissionError(
                "Package installation requires root privileges; sudo not available."
            )

        # detect which packages need to be re-installed
        assume_yes = bool(payload.get("assume_yes", False))
        to_install: list[str] = []
        errors: list[str] = []
        expected: dict[str, str] = {}
        for item in removed:
            if not isinstance(item, dict):
                continue
            name = item.get("name")
            version = item.get("version")
            if not isinstance(name, str) or not isinstance(version, str):
                continue
            installed, _ = spec.query(name)
            if installed:
                continue
            try:
                pinned = _pin_version(manager, name, version)
            except ValueError as err:
                errors.append(str(err))
                continue
            to_install.append(pinned)
            expected[name] = version

        # install any eligible packages (best-effort)
        if to_install:
            # install packages
            try:
                cmd = _build_cmd(
                    spec.install,
                    spec.yes_install,
                    to_install,
                    sudo=sudo,
                    assume_yes=assume_yes,
                )
                run(cmd, env=_cmd_env(spec.noninteractive_env, assume_yes))
            except Exception as e:
                errors.append(str(e))

            # verify re-installation
            for name, version in expected.items():
                installed, current = spec.query(name)
                if not installed:
                    errors.append(f"[{name}] not installed after undo")
                    continue
                if current != version:
                    errors.append(f"[{name}] version mismatch after undo: {current} != {version}")

        # raise if any issues occurred
        if errors:
            raise OSError(f"Errors occurred during package undo:\n{'\n'.join(errors)}")


@atomic
@dataclass(frozen=True)
class AddRepository:
    """Add a package repository to the system package manager.

    Attributes
    ----------
    manager : str
        The package manager name ("apt", "dnf", "yum", "zypper").  Must be present in
        the `REPOSITORY_MANAGERS` mapping.
    name : str
        A short name for the repository (used for file names).
    url : str
        Base URL for the repository.
    repo_url : bool, optional
        If true, then the content at `url` is treated as a prebuilt repository file and
        downloaded directly to the repository path, ignoring structured fields.
        Otherwise, the repository is constructed from the structured fields.
        When `repo_url` is true and a key is provided, the key path is inferred from
        the repo file if possible. For APT, the repo file may be deb822 or deb-line
        format.
    suite : str | None, optional
        APT suite (auto-detected if omitted).  Ignored for RPM managers.
    components : list[str] | None, optional
        APT components (required for APT).  Ignored for RPM managers.
    arch : str | None, optional
        APT architecture (auto-detected if omitted).  Ignored for RPM managers.
    replace : bool, optional
        Whether to replace existing repo/key files.  Defaults to false.
    refresh : bool, optional
        Whether to refresh repository metadata after adding.  Defaults to true.
    assume_yes : bool, optional
        Whether to assume yes for prompts when supported.  Defaults to false.
    key_url : str | None, optional
        URL to the repository key (optional).
    key_ext : Literal[".asc", ".gpg"] | None, optional
        Optional key file extension (defaults to manager-specific extension).
    key_sha256 : str | None, optional
        Optional SHA-256 checksum for the key.
    key_fingerprint : str | None, optional
        Optional GPG fingerprint to verify.

    Notes
    -----
    For APT repositories, `components` is required, while `suite` and `arch` are
    auto-detected when omitted. RPM-family managers ignore `suite`, `components`,
    and `arch`.
    """
    manager: str
    name: str
    url: str
    repo_url: bool = False
    suite: str | None = None
    components: list[str] | None = None
    arch: str | None = None
    replace: bool = False
    refresh: bool = True
    assume_yes: bool = False
    key_url: str | None = None
    key_ext: Literal[".asc", ".gpg"] | None = None
    key_sha256: str | None = None
    key_fingerprint: str | None = None

    def _get_components(self) -> tuple[str | None, list[str] | None, str | None]:
        suite = self.suite
        components = self.components
        arch = self.arch
        if self.repo_url:
            return None, None, None
        if self.manager != "apt":
            return None, None, None

        if not components:
            raise ValueError("APT repository definitions require components.")
        components = list(components)

        if suite is None:
            os_release = _read_os_release()
            suite = os_release.get("VERSION_CODENAME") or os_release.get("UBUNTU_CODENAME")
            if not suite:
                raise OSError("Unable to determine APT suite from /etc/os-release.")

        if arch is None:
            arch = _detect_apt_arch()
            if not arch:
                raise OSError("Unable to determine APT architecture.")

        return suite, components, arch

    def _repo_text(
        self,
        suite: str | None,
        components: list[str] | None,
        arch: str | None,
        key_path: Path | None,
    ) -> str:
        # deb822-style repo
        if self.manager == "apt":
            assert suite is not None, "suite detection failed without raising an error"
            lines = [
                "Types: deb",
                f"URIs: {self.url}",
                f"Suites: {suite}",
                f"Components: {' '.join(components or [])}",
            ]
            if arch is not None:
                lines.append(f"Architectures: {arch}")
            if key_path is not None:
                lines.append(f"Signed-By: {key_path}")
            return "\n".join(lines) + "\n"

        # RPM-style repo
        lines = [
            f"[{self.name}]",
            f"name={self.name}",
            f"baseurl={self.url}",
            "enabled=1",
            f"gpgcheck={1 if key_path is not None else 0}",
        ]
        if key_path is not None:
            lines.append(f"gpgkey=file://{key_path}")
        return "\n".join(lines) + "\n"

    def _from_repo_url(
        self,
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        spec: RepositorySpec,
        repo_path: Path,
        key_ext: str | None,
    ) -> None:
        # download repo file directly
        repo_payload: dict[str, JSONValue] = {}
        payload["repo_download"] = repo_payload
        ctx.dump()
        Download(
            url=self.url,
            target=repo_path,
            replace=self.replace,
        ).do(ctx, repo_payload)

        # if a key URL is given, attempt to infer the key path from the repo file
        if self.key_url is not None:
            parsed = _parse_repo_key_path(self.manager, repo_path)
            if parsed is not None:
                key_path = parsed
                payload["key_path_from_repo"] = True
            else:
                key_path = spec.key_dir / f"{self.name}{key_ext}"
                payload["key_path_from_repo"] = False
            payload["key_path"] = str(key_path)
            if key_ext is not None:
                payload["key_ext"] = key_ext
            ctx.dump()
            key_dir = key_path.parent
            key_dir.mkdir(parents=True, exist_ok=True)
            os.chmod(key_dir, 0o755)  # permissive

            # download the key to that location
            download_payload = payload.get("download")
            if not isinstance(download_payload, dict):
                download_payload = {}
                payload["download"] = download_payload
                ctx.dump()
            Download(
                url=self.key_url,
                target=key_path,
                replace=self.replace,
                sha256=self.key_sha256,
            ).do(ctx, download_payload)

            # verify key fingerprint if given
            if self.key_fingerprint is not None:
                actual = _gpg_fingerprint(key_path)
                if actual is None:
                    raise OSError(f"Failed to read GPG fingerprint: {key_path}")
                expected = _normalize_fingerprint(self.key_fingerprint)
                if actual != expected:
                    raise OSError(
                        f"GPG fingerprint mismatch for {key_path}: {actual} != {expected}"
                    )

            # ensure key file is readable by package manager
            os.chmod(key_path, 0o644)
            if os.geteuid() == 0:
                os.chown(key_path, 0, 0)

    def _write_repo_file(
        self,
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        spec: RepositorySpec,
        repo_path: Path,
        suite: str | None,
        components: list[str] | None,
        arch: str | None,
        key_ext: str | None,
    ) -> None:
        if self.key_url is not None:
            key_path = spec.key_dir / f"{self.name}{key_ext}"
            payload["key_path"] = str(key_path)
            if key_ext is not None:
                payload["key_ext"] = key_ext
            ctx.dump()
            key_dir = spec.key_dir
            key_dir.mkdir(parents=True, exist_ok=True)
            os.chmod(key_dir, 0o755)

            # download key to standard location
            download_payload = payload.get("download")
            if not isinstance(download_payload, dict):
                download_payload = {}
                payload["download"] = download_payload
                ctx.dump()
            Download(
                url=self.key_url,
                target=key_path,
                replace=self.replace,
                sha256=self.key_sha256,
            ).do(ctx, download_payload)

            # verify key fingerprint if given
            if self.key_fingerprint is not None:
                actual = _gpg_fingerprint(key_path)
                if actual is None:
                    raise OSError(f"Failed to read GPG fingerprint: {key_path}")
                expected = _normalize_fingerprint(self.key_fingerprint)
                if actual != expected:
                    raise OSError(
                        f"GPG fingerprint mismatch for {key_path}: {actual} != {expected}"
                    )

            # ensure key file is readable by package manager
            os.chmod(key_path, 0o644)
            if os.geteuid() == 0:
                os.chown(key_path, 0, 0)

        # write the repository definition file
        write_payload: dict[str, JSONValue] = {}
        payload["write"] = write_payload
        ctx.dump()
        WriteText(
            repo_path,
            self._repo_text(suite, components, arch, key_path),
            replace=self.replace
        ).do(ctx, write_payload)

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("Package manager operations require a POSIX system.")
        if os.geteuid() != 0:
            raise PermissionError("Adding repositories requires root privileges.")
        _valid_repository_manager(self.manager)
        spec = REPOSITORY_MANAGERS[self.manager]
        repo_ext = spec.repo_ext
        if self.repo_url and self.manager == "apt":
            url_lower = self.url.lower()
            if url_lower.endswith(".list"):
                repo_ext = ".list"
            elif url_lower.endswith(".sources"):
                repo_ext = ".sources"
        repo_path = spec.repo_dir / f"{self.name}{repo_ext}"
        key_ext = self.key_ext
        if self.key_url:
            if key_ext is None:
                key_ext = ".asc" if self.manager == "apt" else ".gpg"
            elif key_ext not in {".asc", ".gpg"}:
                raise ValueError("key_ext must be '.asc' or '.gpg'")
        if not self.url:
            raise ValueError("Repository URL is required.")
        suite, components, arch = self._get_components()

        # persist intent before mutating
        payload["manager"] = self.manager
        payload["name"] = self.name
        payload["url"] = self.url
        if self.repo_url:
            payload["repo_url"] = self.repo_url
        if suite is not None:
            payload["suite"] = suite
        if components is not None:
            payload["components"] = list(components)
        if arch is not None:
            payload["arch"] = arch
        payload["repo_path"] = str(repo_path)
        payload["replace"] = self.replace
        payload["refresh"] = self.refresh
        payload["assume_yes"] = self.assume_yes
        if self.key_url is not None:
            payload["key_url"] = self.key_url
        if self.key_sha256 is not None:
            payload["key_sha256"] = self.key_sha256
        if self.key_fingerprint is not None:
            payload["key_fingerprint"] = self.key_fingerprint
        ctx.dump()
        env = _cmd_env(spec.noninteractive_env, self.assume_yes)
        sudo = sudo_prefix()

        # download trusted repo file and set key path from it if given
        if self.repo_url:
            self._from_repo_url(ctx, payload, spec, repo_path, key_ext)

        # write repo file from structured input and download key to standard location
        else:
            self._write_repo_file(
                ctx,
                payload,
                spec,
                repo_path,
                suite,
                components,
                arch,
                key_ext
            )

        # refresh if requested and manager supports it
        if self.refresh and spec.refresh:
            if not shutil.which(spec.refresh[0]):
                raise FileNotFoundError(f"Refresh command not found: {spec.refresh[0]}")
            cmd = _build_cmd(
                spec.refresh,
                spec.yes_refresh,
                [],
                sudo=sudo,
                assume_yes=self.assume_yes,
            )
            run(cmd, env=env)

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        manager = payload.get("manager")
        if not isinstance(manager, str):
            return

        # validate manager (best-effort)
        if manager not in REPOSITORY_MANAGERS:
            return

        # undo definition file write (best-effort)
        write_payload = payload.get("write")
        if isinstance(write_payload, dict):
            try:
                WriteText.undo(ctx, write_payload)
            except Exception:
                pass

        # undo trusted repo download (best-effort)
        repo_payload = payload.get("repo_download")
        if isinstance(repo_payload, dict):
            try:
                Download.undo(ctx, repo_payload)
            except Exception:
                pass

        # undo key download (best-effort)
        download_payload = payload.get("download")
        if isinstance(download_payload, dict):
            try:
                Download.undo(ctx, download_payload)
            except Exception:
                pass


@atomic
@dataclass(frozen=True)
class InstallCACert:
    """Install a CA certificate into the system trust store.

    Attributes
    ----------
    manager : str
        The package manager name ("apt", "dnf", "yum", "zypper").
    source : Path
        The local PEM/CRT file to install.
    name : str | None, optional
        Optional override for the target filename.
    replace : bool, optional
        Whether to replace an existing cert at the target path. Defaults to false.
    refresh : bool, optional
        Whether to refresh the trust store after installing. Defaults to true.
    """
    manager: str
    source: Path
    name: str | None = None
    replace: bool = False
    refresh: bool = True

    def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("Package manager operations require a POSIX system.")
        if os.geteuid() != 0:
            raise PermissionError("Installing CA certificates requires root privileges.")
        _valid_ca_manager(self.manager)
        spec = CA_CERT_MANAGERS[self.manager]
        source = self.source.absolute()
        name = self.name

        # determine target path
        if name:
            target_name = name
            if not target_name.endswith(spec.cert_ext):
                target_name = f"{target_name}{spec.cert_ext}"
        else:
            target_name = source.name
            if not target_name.endswith(spec.cert_ext):
                target_name = f"{target_name}{spec.cert_ext}"
        target = spec.cert_dir / target_name

        # persist intent before mutating
        payload["manager"] = self.manager
        payload["source"] = str(source)
        payload["target"] = str(target)
        payload["replace"] = self.replace
        payload["refresh"] = self.refresh
        ctx.dump()

        # read PEM/CRT data and write to target
        data = source.read_bytes()
        write_payload: dict[str, JSONValue] = {}
        payload["write"] = write_payload
        ctx.dump()
        WriteBytes(target, data, replace=self.replace).do(ctx, write_payload)

        # refresh trust store if requested and supported
        if self.refresh and spec.refresh:
            if not shutil.which(spec.refresh[0]):
                raise FileNotFoundError(f"Refresh command not found: {spec.refresh[0]}")
            cmd = _build_cmd(
                spec.refresh,
                spec.yes_refresh,
                [],
                sudo=sudo_prefix(),
                assume_yes=False,
            )
            run(cmd, env=_cmd_env(spec.noninteractive_env, False))

    @staticmethod
    def undo(ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        manager = payload.get("manager")
        if not isinstance(manager, str):
            return
    
        # validate manager (best-effort)
        if manager not in CA_CERT_MANAGERS:
            return

        # undo cert write (best-effort)
        write_payload = payload.get("write")
        if isinstance(write_payload, dict):
            try:
                WriteBytes.undo(ctx, write_payload)
            except Exception:
                pass

        # refresh trust store if supported (best-effort)
        spec = CA_CERT_MANAGERS[manager]
        if payload.get("refresh", True) and spec.refresh:
            try:
                cmd = _build_cmd(
                    spec.refresh,
                    spec.yes_refresh,
                    [],
                    sudo=sudo_prefix(),
                    assume_yes=False,
                )
                run(cmd, env=_cmd_env(spec.noninteractive_env, False))
            except Exception:
                pass
