"""A selection of atomic user/group operations meant to be used in conjunction
with CLI pipelines.
"""
from __future__ import annotations

import grp
import os
import pwd
import re
import shlex
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import cast

from .core import JSONValue, Pipeline, atomic
from ..run import atomic_write_text, can_escalate, confirm, run, sudo

# pylint: disable=unused-argument, missing-function-docstring, broad-exception-caught
# pylint: disable=bare-except


@dataclass(frozen=True)
class SubIDRange:
    """A range of subordinate IDs for a user.

    Attributes
    ----------
    user : str
        The username.
    start : int
        The starting subordinate ID.
    count : int
        The number of subordinate IDs in the range.
    """
    user: str
    start: int
    count: int

    @property
    def end(self) -> int:
        """
        Returns
        -------
        int
            The first subordinate ID after the end of the range.
        """
        return self.start + self.count


_SUBID_REGEX = re.compile(r"^(?P<user>[^:]+):(?P<start>\d+):(?P<count>\d+)\s*$")


def _current_user() -> str:
    return pwd.getpwuid(os.getuid()).pw_name


def _read_subid_file(path: Path) -> list[SubIDRange]:
    if not path.exists():
        return []
    out: list[SubIDRange] = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = _SUBID_REGEX.match(line)
        if not m:
            continue
        out.append(SubIDRange(
            user=m.group("user"),
            start=int(m.group("start")),
            count=int(m.group("count")),
        ))
    return out


def _has_enough(user: str, ranges: list[SubIDRange], needed: int) -> bool:
    return any(r.user == user and r.count >= needed for r in ranges)


def _choose_non_overlapping_start(ranges: list[SubIDRange], needed: int) -> int:
    base = 100000
    max_end = max((r.end for r in ranges), default=base)
    start = max(base, max_end)
    rem = start % needed
    if rem:
        start += (needed - rem)
    return start


async def _append_subid(
    path: Path,
    user: str,
    start: int,
    count: int,
    *,
    assume_yes: bool
) -> None:
    line = f"{user}:{start}:{count}"
    quoted_line = shlex.quote(line)
    quoted_path = shlex.quote(str(path))
    cmd = (
        "set -euo pipefail; "
        f"touch {quoted_path}; "
        "if command -v flock >/dev/null 2>&1; then "
        f"  exec 9>>{quoted_path}; flock -x 9; "
        "fi; "
        f"grep -F -x -q {quoted_line} {quoted_path} || echo {quoted_line} >> {quoted_path}"
    )
    await run(sudo(["sh", "-lc", cmd], non_interactive=assume_yes))


def _group_has_user(user: str, group: str) -> bool:
    gr = grp.getgrnam(group)
    if user in gr.gr_mem:
        return True
    pw = pwd.getpwnam(user)
    return pw.pw_gid == gr.gr_gid


def _group_is_empty(group: str) -> bool:
    gr = grp.getgrnam(group)
    if gr.gr_mem:
        return False
    gid = gr.gr_gid
    for pw in pwd.getpwall():
        if pw.pw_gid == gid:
            return False
    return True


def _user_info(name: str) -> pwd.struct_passwd:
    return pwd.getpwnam(name)


def _ensure_user(name: str) -> pwd.struct_passwd:
    return _user_info(name)


def _read_proc_sys(path: str) -> int | None:
    p = Path("/proc/sys") / path
    try:
        return int(p.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return None


def _require_root_or_user(uid: int, message: str) -> bool:
    if os.geteuid() == 0:
        return True
    if os.geteuid() != uid:
        raise PermissionError(message)
    return False


def _ssh_home(user: str) -> Path:
    return Path(_ensure_user(user).pw_dir)


async def _ensure_ssh_dir(path: Path, uid: int, gid: int) -> None:
    if path.exists():
        if not path.is_dir():
            raise FileExistsError(f"Path exists and is not a directory: {path}")
        return

    is_root = _require_root_or_user(
        uid,
        "Creating .ssh directories requires root privileges or the target user."
    )

    await run(["mkdir", "-p", str(path)])
    if is_root:
        await run(["chown", f"{uid}:{gid}", str(path)])
    await run(["chmod", "700", str(path)])


def _read_authorized_keys(path: Path) -> list[str]:
    if not path.exists():
        return []
    text = path.read_text(encoding="utf-8", errors="ignore")
    return [line.strip() for line in text.splitlines() if line.strip()]


def _normalize_key(key: str, comment: str | None) -> str:
    parts = shlex.split(key, posix=True)
    if len(parts) < 2:
        raise ValueError("Invalid SSH public key format")
    key_types = {
        "ssh-ed25519",
        "ssh-rsa",
        "ecdsa-sha2-nistp256",
        "ecdsa-sha2-nistp384",
        "ecdsa-sha2-nistp521",
        "sk-ssh-ed25519@openssh.com",
        "sk-ecdsa-sha2-nistp256@openssh.com",
    }

    if parts[0] in key_types and len(parts) >= 2:
        options = None
        base_tokens = [parts[0], parts[1]]
        tail = parts[2:]
    elif len(parts) >= 3 and parts[1] in key_types:
        options = parts[0]
        base_tokens = [parts[1], parts[2]]
        tail = parts[3:]
    else:
        raise ValueError("Invalid SSH public key format")

    keydata = base_tokens[1]
    if len(keydata) < 32 or not re.fullmatch(r"[A-Za-z0-9+/=]+", keydata):
        raise ValueError("Invalid SSH public key format")

    base = " ".join(base_tokens)
    if options:
        base = f"{options} {base}"
    if comment is not None:
        extra = comment.strip()
        return f"{base} {extra}" if extra else base
    if tail:
        return f"{base} {' '.join(tail)}"
    return base


def _write_authorized_keys(path: Path, lines: list[str]) -> None:
    content = "\n".join(lines)
    if content:
        content += "\n"
    atomic_write_text(path, content, encoding="utf-8", private=True)


@atomic
@dataclass(frozen=True)
class EnsureSubIDs:
    """Ensure subordinate UID/GID ranges exist for a user."""
    user: str | None = None
    needed: int = 65536
    subuid_path: Path = Path("/etc/subuid")
    subgid_path: Path = Path("/etc/subgid")
    prompt: str = (
        "Provisioning subordinate UID/GID ranges requires modifying system files.  "
        "This may require root privileges.\nDo you want to proceed? [y/N] "
    )
    assume_yes: bool = False

    async def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("User management operations require a POSIX system.")
        user = self.user or _current_user()
        payload["user"] = user
        payload["needed"] = self.needed
        payload["subuid_path"] = str(self.subuid_path)
        payload["subgid_path"] = str(self.subgid_path)
        ctx.dump()

        # check whether there are enough subuids/subgids already
        uid_ranges = _read_subid_file(self.subuid_path)
        gid_ranges = _read_subid_file(self.subgid_path)
        if (
            _has_enough(user, uid_ranges, self.needed) and
            _has_enough(user, gid_ranges, self.needed)
        ):
            return

        if os.geteuid() != 0 and not can_escalate():
            raise PermissionError(
                "Subuid/subgid provisioning requires root privileges; no sudo available."
            )
        if not confirm(self.prompt, assume_yes=self.assume_yes):
            raise OSError("User declined to provision subordinate UID/GID ranges.")

        # choose non-overlapping ranges and append to files
        start_uid = _choose_non_overlapping_start(uid_ranges, self.needed)
        start_gid = _choose_non_overlapping_start(gid_ranges, self.needed)
        await _append_subid(
            self.subuid_path,
            user,
            start_uid,
            self.needed,
            assume_yes=self.assume_yes
        )
        await _append_subid(
            self.subgid_path,
            user,
            start_gid,
            self.needed,
            assume_yes=self.assume_yes
        )

        # verify
        uid_ranges = _read_subid_file(self.subuid_path)
        gid_ranges = _read_subid_file(self.subgid_path)
        if (
            not _has_enough(user, uid_ranges, self.needed) or
            not _has_enough(user, gid_ranges, self.needed)
        ):
            raise OSError("Failed to provision subuid/subgid ranges correctly.")

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        return  # no-op


@atomic
@dataclass(frozen=True)
class EnsureUserNamespaces:
    """Ensure unprivileged user namespaces are enabled.

    Attributes
    ----------
    needed : int, optional
        The minimum number of user namespaces that should be supported.  Defaults to
        15000.
    prompt : str
        The prompt to show the user when requesting sudo permission to enable user
        namespaces.
    assume_yes : bool, optional
        If true, assume "yes" to any prompts.  Defaults to false.
    """
    needed: int = 15000
    prompt: str = (
        "Enabling unprivileged user namespaces requires modifying system settings.  "
        "This may require root privileges.\nDo you want to proceed? [y/N] "
    )
    assume_yes: bool = False

    async def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("User management operations require a POSIX system.")
        payload["needed"] = self.needed
        ctx.dump()

        # check current status
        unpriv = _read_proc_sys("kernel/unprivileged_userns_clone")
        maxns = _read_proc_sys("user/max_user_namespaces")
        if (
            (unpriv is None or unpriv != 0) and
            (maxns is None or maxns >= self.needed)
        ):
            return

        # prompt for sudo if needed
        if os.geteuid() != 0 and not can_escalate():
            raise PermissionError(
                "Enabling user namespaces requires root privileges; no sudo available."
            )
        if not confirm(self.prompt, assume_yes=self.assume_yes):
            raise OSError("User declined to enable unprivileged user namespaces.")

        # enable unprivileged user namespaces
        if unpriv == 0:
            await run(sudo(
                ["sysctl", "-w", "kernel.unprivileged_userns_clone=1"],
                non_interactive=self.assume_yes
            ))
        if maxns is not None and maxns < self.needed:
            await run(sudo(
                ["sysctl", "-w", f"user.max_user_namespaces={self.needed}"],
                non_interactive=self.assume_yes
            ))

        # verify
        unpriv = _read_proc_sys("kernel/unprivileged_userns_clone")
        maxns = _read_proc_sys("user/max_user_namespaces")
        if (unpriv == 0) or (maxns is not None and maxns < self.needed):
            raise OSError("Failed to enable unprivileged user namespaces.")

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        return  # no-op


@atomic
@dataclass(frozen=True)
class AddUserToGroup:
    """Add a user to a group."""
    user: str
    group: str

    async def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("User management operations require a POSIX system.")
        user = self.user
        group = self.group
        _user_info(user)
        grp.getgrnam(group)

        payload["user"] = user
        payload["group"] = group
        was_member = _group_has_user(user, group)
        payload["was_member"] = was_member
        ctx.dump()
        if was_member:
            return

        if os.name == "posix" and os.geteuid() != 0 and not can_escalate():
            raise PermissionError(
                "Modifying group membership requires root privileges; no sudo available."
            )
        await run(sudo(["usermod", "-aG", group, user]))

        if not _group_has_user(user, group):
            raise OSError(f"Failed to add user '{user}' to group '{group}'.")

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        user = payload.get("user")
        group = payload.get("group")
        was_member = payload.get("was_member")
        if not isinstance(user, str) or not isinstance(group, str):
            return
        if was_member is not False:
            return
        if not _group_has_user(user, group):
            return

        if os.name == "posix" and os.geteuid() != 0 and not can_escalate():
            raise PermissionError(
                "Modifying group membership requires root privileges; no sudo available."
            )
        try:
            await run(sudo(["gpasswd", "-d", user, group]), check=False)
        except:
            pass


@atomic
@dataclass(frozen=True)
class RemoveUserFromGroup:
    """Remove a user from a group."""
    user: str
    group: str

    async def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("User management operations require a POSIX system.")
        user = self.user
        group = self.group
        _user_info(user)
        grp.getgrnam(group)

        payload["user"] = user
        payload["group"] = group
        was_member = _group_has_user(user, group)
        payload["was_member"] = was_member
        ctx.dump()
        if not was_member:
            return

        if os.name == "posix" and os.geteuid() != 0 and not can_escalate():
            raise PermissionError(
                "Modifying group membership requires root privileges; no sudo available."
            )
        await run(sudo(["gpasswd", "-d", user, group]))

        if _group_has_user(user, group):
            raise OSError(f"Failed to remove user '{user}' from group '{group}'.")

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        user = payload.get("user")
        group = payload.get("group")
        was_member = payload.get("was_member")
        if not isinstance(user, str) or not isinstance(group, str):
            return
        if was_member is not True:
            return
        if _group_has_user(user, group):
            return

        if os.name == "posix" and os.geteuid() != 0 and not can_escalate():
            raise PermissionError(
                "Modifying group membership requires root privileges; no sudo available."
            )
        try:
            await run(sudo(["usermod", "-aG", group, user]))
        except:
            pass


@atomic
@dataclass(frozen=True)
class EnableLinger:
    """Enable systemd linger for a user.

    Attributes
    ----------
    user : str | None, optional
        The username to enable linger for.  Defaults to the current user.
    prompt : str
        The prompt to show the user when requesting sudo permission to disable linger.
    assume_yes : bool, optional
        If true, assume "yes" to any prompts.  Defaults to false.
    """
    user: str | None = None
    prompt: str = (
        "Enabling systemd linger requires modifying system settings.  "
        "This may require root privileges.\nDo you want to proceed? [y/N] "
    )
    assume_yes: bool = False

    async def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("User management operations require a POSIX system.")
        if not shutil.which("loginctl"):
            raise OSError("loginctl not found")

        user = self.user or _current_user()
        payload["user"] = user
        ctx.dump()

        # check current linger status
        cp = await run(
            ["loginctl", "show-user", user, "-p", "Linger"],
            check=False,
            capture_output=True,
        )
        if "Linger=yes" in (cp.stdout or ""):
            payload["was_enabled"] = True
            ctx.dump()
            return
        payload["was_enabled"] = False
        ctx.dump()

        # enable linger
        if os.geteuid() != 0 and not can_escalate():
            raise PermissionError(
                "Enabling linger requires root privileges; no sudo available."
            )
        if not confirm(self.prompt, assume_yes=self.assume_yes):
            raise OSError("User declined to enable systemd linger.")
        await run(sudo(["loginctl", "enable-linger", user]))

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        return  # no-op


@atomic
@dataclass(frozen=True)
class DisableLinger:
    """Disable systemd linger for a user.

    Attributes
    ----------
    user : str | None, optional
        The username to disable linger for.  Defaults to the current user.
    prompt : str
        The prompt to show the user when requesting sudo permission to disable linger.
    assume_yes : bool, optional
        If true, assume "yes" to any prompts.  Defaults to false.
    """
    user: str | None = None
    prompt: str = (
        "Disabling systemd linger requires modifying system settings.  "
        "This may require root privileges.\nDo you want to proceed? [y/N] "
    )
    assume_yes: bool = False

    async def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("User management operations require a POSIX system.")
        if not shutil.which("loginctl"):
            raise OSError("loginctl not found")

        user = self.user or _current_user()
        payload["user"] = user
        ctx.dump()

        # check current linger status
        cp = await run(
            ["loginctl", "show-user", user, "-p", "Linger"],
            check=False,
            capture_output=True,
        )
        if "Linger=yes" not in (cp.stdout or ""):
            payload["was_enabled"] = False
            ctx.dump()
            return
        payload["was_enabled"] = True
        ctx.dump()

        # disable linger
        if os.geteuid() != 0 and not can_escalate():
            raise PermissionError(
                "Disabling linger requires root privileges; no sudo available."
            )
        if not confirm(self.prompt, assume_yes=self.assume_yes):
            raise OSError("User declined to disable systemd linger.")
        await run(sudo(["loginctl", "disable-linger", user]))

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        return  # no-op


@atomic
@dataclass(frozen=True)
class CreateGroup:
    """Create a group.

    Attributes
    ----------
    name : str
        The group name.
    system : bool, optional
        If true, create a system group.  Defaults to false.  System groups typically
        have lower GIDs and are used for system services.
    """
    name: str
    system: bool = False

    async def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("User management operations require a POSIX system.")
        name = self.name
        payload["name"] = name
        payload["system"] = self.system

        # check if group exists
        try:
            existing = grp.getgrnam(name)
            payload["was_present"] = True
            payload["gid"] = existing.gr_gid
            ctx.dump()
            return
        except KeyError:
            pass
        payload["was_present"] = False
        ctx.dump()

        if os.name == "posix" and os.geteuid() != 0 and not can_escalate():
            raise PermissionError(
                "Creating groups requires root privileges; no sudo available."
            )

        # create group
        cmd = sudo(["groupadd"])
        if self.system:
            cmd.append("--system")
        cmd.append(name)
        await run(cmd)

        # record created group info
        payload["gid"] = grp.getgrnam(name).gr_gid
        ctx.dump()

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        name = payload.get("name")
        was_present = payload.get("was_present")
        gid = payload.get("gid")
        if not isinstance(name, str):
            return
        if was_present is not False:
            return
        if not isinstance(gid, int):
            return

        # verify group still exists and has same GID
        try:
            current = grp.getgrnam(name)
        except KeyError:
            return
        if current.gr_gid != gid:
            return
        if not _group_is_empty(name):
            return

        # delete group
        if os.name == "posix" and os.geteuid() != 0 and not can_escalate():
            if force:
                return
            raise PermissionError(
                "Removing groups requires root privileges; no sudo available."
            )
        try:
            await run(sudo(["groupdel", name]))
        except:
            if not force:
                raise


@atomic
@dataclass(frozen=True)
class InstallSSHKey:
    """Install a public SSH key in a user's authorized_keys file.

    Attributes
    ----------
    user : str
        The username.
    key : str
        The public SSH key to install.
    replace : bool, optional
        If true, replace the authorized_keys file with only the provided key.  Defaults
        to false.
    comment : str | None, optional
        An optional comment to append to the key.
    """
    user: str
    key: str
    replace: bool = False
    comment: str | None = None

    async def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("SSH authorized_keys management requires a POSIX system.")
        user = self.user
        info = _ensure_user(user)
        key = _normalize_key(self.key, self.comment)
        ssh_dir = _ssh_home(user) / ".ssh"
        auth_keys = ssh_dir / "authorized_keys"
        payload["user"] = user
        payload["key"] = key
        payload["authorized_keys_path"] = str(auth_keys)
        payload["replace"] = self.replace
        await _ensure_ssh_dir(ssh_dir, info.pw_uid, info.pw_gid)

        # check for existing key
        existing = _read_authorized_keys(auth_keys)
        if key in existing:
            payload["was_present"] = True
            ctx.dump()
            return

        # add key
        payload["was_present"] = False
        if self.replace:
            payload["previous_contents"] = cast(list[JSONValue], existing)
            new_lines = [key]
            payload["installed_lines"] = cast(list[JSONValue], new_lines)
        else:
            new_lines = [*existing, key]
        ctx.dump()

        # set correct ownership and permissions
        is_root = _require_root_or_user(
            info.pw_uid,
            "Modifying authorized_keys requires root privileges or the target user."
        )
        _write_authorized_keys(auth_keys, new_lines)
        if is_root:
            await run(["chown", f"{info.pw_uid}:{info.pw_gid}", str(auth_keys)])
        await run(["chmod", "600", str(auth_keys)])

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        user = payload.get("user")
        key = payload.get("key")
        path = payload.get("authorized_keys_path")
        was_present = payload.get("was_present")
        replace = payload.get("replace")
        previous = payload.get("previous_contents")
        installed = payload.get("installed_lines")
        if not isinstance(user, str) or not isinstance(key, str) or not isinstance(path, str):
            return
        if was_present is True:
            return

        # if replaced, restore previous contents
        auth_keys = Path(path)
        if replace is True and isinstance(previous, list) and isinstance(installed, list):
            current = _read_authorized_keys(auth_keys)
            if current != [str(p) for p in installed]:
                return
            try:
                info = _ensure_user(user)
                _require_root_or_user(
                    info.pw_uid,
                    "Modifying authorized_keys requires root privileges or the target user."
                )
                _write_authorized_keys(auth_keys, [str(p) for p in previous])
            except:
                if not force:
                    raise
            return

        # otherwise, just remove the key line
        if not auth_keys.exists():
            return
        current = _read_authorized_keys(auth_keys)
        if key not in current:
            return
        new_lines = [line for line in current if line != key]
        try:
            info = _ensure_user(user)
            _require_root_or_user(
                info.pw_uid,
                "Modifying authorized_keys requires root privileges or the target user."
            )
            _write_authorized_keys(auth_keys, new_lines)
        except:
            if not force:
                raise


@atomic
@dataclass(frozen=True)
class RemoveSSHKey:
    """Remove a public SSH key from a user's authorized_keys file.

    Attributes
    ----------
    user : str
        The username.
    key : str
        The public SSH key to remove.
    """
    user: str
    key: str

    async def do(self, ctx: Pipeline.InProgress, payload: dict[str, JSONValue]) -> None:
        if os.name != "posix":
            raise OSError("SSH authorized_keys management requires a POSIX system.")
        user = self.user
        _ensure_user(user)
        key = _normalize_key(self.key, None)
        ssh_dir = _ssh_home(user) / ".ssh"
        auth_keys = ssh_dir / "authorized_keys"
        payload["user"] = user
        payload["key"] = key
        payload["authorized_keys_path"] = str(auth_keys)

        # check for existing key
        existing = _read_authorized_keys(auth_keys)
        if key not in existing:
            payload["was_present"] = False
            ctx.dump()
            return

        # remove key
        payload["was_present"] = True
        ctx.dump()
        new_lines = [line for line in existing if line != key]
        info = _ensure_user(user)
        _require_root_or_user(
            info.pw_uid,
            "Modifying authorized_keys requires root privileges or the target user."
        )
        _write_authorized_keys(auth_keys, new_lines)

    @staticmethod
    async def undo(
        ctx: Pipeline.InProgress,
        payload: dict[str, JSONValue],
        force: bool
    ) -> None:
        user = payload.get("user")
        key = payload.get("key")
        path = payload.get("authorized_keys_path")
        was_present = payload.get("was_present")
        if not isinstance(user, str) or not isinstance(key, str) or not isinstance(path, str):
            return
        if was_present is not True:
            return

        # recreate .ssh directory if needed
        auth_keys = Path(path)
        ssh_dir = auth_keys.parent
        try:
            info = _ensure_user(user)
            await _ensure_ssh_dir(ssh_dir, info.pw_uid, info.pw_gid)

            # re-add the key if not present
            current = _read_authorized_keys(auth_keys)
            if key in current:
                return
            _require_root_or_user(
                info.pw_uid,
                "Modifying authorized_keys requires root privileges or the target user."
            )
            _write_authorized_keys(auth_keys, [*current, key])
        except:
            if not force:
                raise
