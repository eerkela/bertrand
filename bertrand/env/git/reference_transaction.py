#!/usr/bin/env python3
# bertrand-managed: reference_transaction.py
"""A git reference-transaction hook that tracks changes to branches and mirrors them
in the project directory, so that Bertrand can avoid any manual branch management and
treat git as the sole source of truth for its isolated worktree model.
"""
from __future__ import annotations

import asyncio
import sys
from typing import TYPE_CHECKING

if TYPE_CHECKING:  # in-tree, relative
    from .bertrand_git import (
        GIT_REF_STATES,
        GitRefUpdate,
        GitRepository,
    )
else:  # out-of-tree, absolute; used when the hook is installed into .git/hooks
    from bertrand_git import (
        GIT_REF_STATES,
        GitRefUpdate,
        GitRepository,
    )


async def main() -> None:
    """Entry point for git's `reference-transaction` hook.

    Raises
    ------
    TypeError
        If the hook is invoked with an invalid state argument.
    """
    argv = list(sys.argv[1:])
    if len(argv) != 1:
        raise TypeError(f"expected exactly one state argument in {sorted(GIT_REF_STATES)}")
    state = argv[0].strip()
    if state not in GIT_REF_STATES:
        raise TypeError(
            f"invalid state argument: {state!r} (expected one of: {sorted(GIT_REF_STATES)})"
        )
    if state != "committed":
        return  # no-op outside committed phase

    # parse transaction and filter for branch updates, then converge worktrees
    updates = GitRefUpdate.parse(sys.stdin.read())
    head_updates = [u for u in updates if u.is_head]
    if head_updates:
        repo = await GitRepository.discover()
        if repo is not None:
            await repo.sync_worktrees(head_updates)


if __name__ == "__main__":
    asyncio.run(main())
