"""User group control for Bertrand's narrow, privileged host operations."""

from __future__ import annotations

import contextlib
import grp
import os
import pwd
from dataclasses import dataclass, field
from pathlib import Path

from bertrand.env.git import (
    BERTRAND_GROUP,
    CommandError,
    Deadline,
    can_escalate,
    confirm,
    run,
    sudo,
    warn,
)


@dataclass(frozen=True)
class User:
    """A simple structure representing a user identity by user ID and group ID.

    Attributes
    ----------
    uid : int
        The numeric user ID.
    gid : int
        The numeric group ID.
    name : str
        The username.
    home : Path
        The path to the user's home directory.
    """

    uid: int = field(init=False)
    gid: int = field(init=False)
    name: str = field(init=False)
    home: Path = field(init=False)

    def __post_init__(self) -> None:
        """Resolve the effective host user identity."""
        euid = os.geteuid()
        sudo_uid = os.environ.get("SUDO_UID")
        sudo_user = os.environ.get("SUDO_USER")
        if euid == 0 and sudo_uid:
            object.__setattr__(self, 'uid', int(sudo_uid))
            pw = pwd.getpwuid(self.uid)
            object.__setattr__(self, 'gid', pw.pw_gid)
            object.__setattr__(self, 'name', sudo_user or pw.pw_name)
            object.__setattr__(self, 'home', Path(pw.pw_dir))
        else:
            object.__setattr__(self, 'uid', os.getuid())
            pw = pwd.getpwuid(self.uid)
            object.__setattr__(self, 'gid', pw.pw_gid)
            object.__setattr__(self, 'name', pw.pw_name)
            object.__setattr__(self, 'home', Path(pw.pw_dir))


@dataclass(frozen=True)
class UserGroup:
    """A simple struct representing a user's membership status in a host group.

    Attributes
    ----------
    user : str
        The username to which this status applies.
    group : str
        The group name to which this status applies.
    configured : bool
        Whether the user is configured as a member of the group (either as a primary or
        secondary member).
    active : bool
        Whether the user's current session is active in the group (i.e. whether the
        group is included in the user's current group list, which is determined at
        login and may need to be refreshed via `newgrp` or a logout/login cycle).
    """

    user: str
    group: str
    configured: bool = field(init=False)
    active: bool = field(init=False)

    def __post_init__(self) -> None:
        """Return (configured, active) for user membership in a host group."""
        try:
            group_info = grp.getgrnam(self.group)
        except KeyError:
            object.__setattr__(self, 'configured', False)
            object.__setattr__(self, 'active', False)
            return

        try:
            primary_gid = pwd.getpwnam(self.user).pw_gid
        except KeyError:
            primary_gid = None

        configured = self.user in group_info.gr_mem or primary_gid == group_info.gr_gid
        active = (
            group_info.gr_gid in os.getgroups() or os.getegid() == group_info.gr_gid
        )
        object.__setattr__(self, 'configured', configured)
        object.__setattr__(self, 'active', active)

    async def activate(self, *, assume_yes: bool) -> None:
        """Ensure host group membership.

        Parameters
        ----------
        assume_yes : bool
            If True, automatically confirm any prompts for fixing group membership.

        Raises
        ------
        PermissionError
            If the user declines to update their group membership, or if the update
            requires root privileges and they are not available.
        OSError
            If group membership still isn't properly configured after attempting to
            update it.
        """
        status = self
        if status.configured and status.active:
            return

        # ensure membership
        if not status.configured:
            if not confirm(
                f"Bertrand must add user {status.user!r} to the {status.group!r} "
                f"group in order to avoid future root escalation.  Would you like "
                "Bertrand to add this membership now (requires sudo)?\n[y/N] ",
                assume_yes=assume_yes,
            ):
                msg = f"{status.group} group membership update declined by user."
                raise PermissionError(msg)
            if os.geteuid() != 0 and not can_escalate():
                msg = (
                    f"Updating {status.group} group membership requires root "
                    "privileges; "
                    "sudo not available."
                )
                raise PermissionError(msg)
            await run(
                sudo(
                    ["usermod", "-a", "-G", status.group, status.user],
                    non_interactive=assume_yes,
                )
            )
            status = UserGroup(user=status.user, group=status.group)
            if not status.configured:
                msg = f"failed to add user '{status.user}' to group '{status.group}'"
                raise OSError(msg)

        # warn if group membership is not active in current session
        if not status.active:
            warn(
                f"added {status.user!r} to the {status.group!r} group, but "
                "sudo is still required for this session.  Run "
                f"`newgrp {status.group}` "
                "or log out and back in to pick up the new group privileges before "
                "proceeding."
            )


async def ensure_bertrand_group(
    *,
    deadline: Deadline,
    assume_yes: bool,
) -> grp.struct_group:
    """Ensure Bertrand's shared host group exists.

    Parameters
    ----------
    deadline : Deadline
        Maximum time in seconds to wait for required host commands.
    assume_yes : bool
        Whether to automatically confirm prompts for creating the shared group.

    Returns
    -------
    grp.struct_group
        The shared Bertrand group information.

    Raises
    ------
    PermissionError
        If group creation requires root privileges, but elevation is unavailable or
        declined.
    OSError
        If the shared group cannot be created or verified.
    CommandError
        If the host group creation command fails unexpectedly.
    """
    try:
        return grp.getgrnam(BERTRAND_GROUP)
    except KeyError:
        pass

    if not confirm(
        f"Bertrand uses a shared host group named {BERTRAND_GROUP!r} for unprivileged "
        "access to global runtime state.  Create this system group now "
        "(requires sudo)?\n"
        "[y/N] ",
        assume_yes=assume_yes,
    ):
        msg = "Bertrand shared-group bootstrap declined by user."
        raise PermissionError(msg)
    if os.geteuid() != 0 and not can_escalate():
        msg = (
            f"Creating group {BERTRAND_GROUP!r} requires root privileges; sudo not "
            "available."
        )
        raise PermissionError(msg)
    try:
        await run(
            sudo(
                ["groupadd", "--system", BERTRAND_GROUP],
                non_interactive=assume_yes,
            ),
            capture_output=True,
            deadline=deadline,
        )
    except CommandError:
        with contextlib.suppress(KeyError):
            return grp.getgrnam(BERTRAND_GROUP)
        raise

    try:
        return grp.getgrnam(BERTRAND_GROUP)
    except KeyError as err:
        msg = f"Failed to create shared Bertrand group {BERTRAND_GROUP!r}."
        raise OSError(msg) from err


def require_bertrand_group(*, group: UserGroup) -> None:
    """Raise an error if the user is not an active member of the Bertrand user group.

    Parameters
    ----------
    group : UserGroup
        The user's group membership status to check.

    Raises
    ------
    OSError
        If the user is not configured as a member of the Bertrand group, or if they are
        configured but their membership is not active in the current session.
    """
    if not group.configured:
        msg = (
            f"user {group.user!r} is not in the {BERTRAND_GROUP!r} group.  Rerun "
            "`bertrand init` to establish permissions for this user."
        )
        raise OSError(msg)
    if not group.active:
        msg = (
            f"user {group.user!r} is in the {BERTRAND_GROUP!r} group, but membership "
            f"has not yet been activated for the current session.  Run "
            f"`newgrp {BERTRAND_GROUP}` or log out and back in, then rerun "
            "`bertrand init` to continue."
        )
        raise OSError(msg)
