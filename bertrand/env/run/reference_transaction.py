#!/usr/bin/env python3
# bertrand-managed: reference-transaction
"""A git reference-transaction hook that tracks changes to branches and mirrors them
in the project directory, so that Bertrand can avoid any manual branch management and
treat git as the sole source of truth for its isolated worktree model.
"""
from __future__ import annotations

import asyncio
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:  # in-tree, relative
    from .bertrand_git import (
        HEADS_PREFIX,
        STATES,
        RefUpdate,
        git,
        git_strip_env,
        list_branches,
        list_worktrees,
        parse_git_bool,
        supports_relative_worktree_paths,
    )
else:  # out-of-tree, absolute; used when the hook is installed into .git/hooks
    from bertrand_git import (
        HEADS_PREFIX,
        STATES,
        RefUpdate,
        git,
        git_strip_env,
        list_branches,
        list_worktrees,
        parse_git_bool,
        supports_relative_worktree_paths,
    )


@dataclass(frozen=True)
class Project:
    """Resolved repository context for hook operations."""
    git_dir: Path
    project_root: Path
    is_bare: bool
    branches: set[str]

    @dataclass(frozen=True)
    class Worktree:
        """A single entry from `git worktree list --porcelain`."""
        path: Path
        branch: str | None = None

    worktrees: list[Worktree]

    @classmethod
    async def load(cls) -> Project:
        """Resolve git context and current repository/worktree state.

        Returns
        -------
        Project
            The resolved repository context, including git directory, project root, and
            current branches and worktrees.

        Raises
        ------
        FileNotFoundError
            If the git directory or project root cannot be resolved.
        OSError
            If git commands fail to list branches or worktrees.
        """
        # resolve absolute path to the repository common git directory
        result = await git(
            ["rev-parse", "--path-format=absolute", "--git-common-dir"],
            check=False,
            capture_output=True,
        )
        if result.returncode != 0:
            raise FileNotFoundError(
                "failed to resolve absolute git common dir:\n"
                f"{(result.stderr or result.stdout).strip()}"
            )
        git_dir = Path(result.stdout.strip()).expanduser().resolve()
        if not git_dir.exists() or not git_dir.is_dir():
            raise FileNotFoundError(
                f"resolved git dir does not exist or is not a directory: {git_dir}"
            )

        # determine repository mode and derive project root from common-dir parent
        bare = await git(
            [f"--git-dir={str(git_dir)}", "rev-parse", "--is-bare-repository"],
            check=False,
            capture_output=True,
        )
        if bare.returncode != 0:
            raise OSError(
                "failed to determine bare repository mode:\n"
                f"{(bare.stderr or bare.stdout).strip()}"
            )
        try:
            is_bare = parse_git_bool(bare.stdout)
        except ValueError as err:
            raise OSError(
                "failed to parse --is-bare-repository output:\n"
                f"{bare.stdout!r}"
            ) from err
        project_root = git_dir.parent.expanduser().resolve()
        if is_bare and not await supports_relative_worktree_paths():
            raise OSError(
                "git worktree relative path support is required for bare repository "
                "mode, but this git version does not support '--relative-paths' for "
                "worktree creation and move operations."
            )

        # list tracked branches from the repository
        branches = set(await list_branches(git_dir))

        # find local worktrees for checked-out branches, where applicable
        worktrees = [
            Project.Worktree(path=entry.path, branch=entry.branch)
            for entry in await list_worktrees(git_dir)
        ]

        return cls(
            git_dir=git_dir,
            project_root=project_root,
            is_bare=is_bare,
            branches=branches,
            worktrees=worktrees,
        )

    def _current_branches(self) -> dict[str, Path]:
        current: dict[str, Path] = {}
        for wt in self.worktrees:
            if wt.branch is not None:
                existing = current.setdefault(wt.branch, wt.path)
                if existing != wt.path:
                    raise FileExistsError(
                        f"multiple worktrees mapped to branch '{wt.branch}': "
                        f"{existing} and {wt.path}"
                    )
        return current

    def _intended_changes(
        self,
        updates: list[RefUpdate]
    ) -> tuple[list[str], list[str], list[tuple[str, str]]]:
        # get creation/destruction intent from transaction updates
        _created: dict[str, list[str]] = {}
        _destroyed: dict[str, list[str]] = {}
        created: list[str] = []
        destroyed: list[str] = []
        for update in updates:
            if update.created:
                created.append(update.branch)
                _created.setdefault(update.new, []).append(update.branch)
            elif update.destroyed:
                destroyed.append(update.branch)
                _destroyed.setdefault(update.old, []).append(update.branch)

        # infer rename hints from unambiguous create + destroy pairs on the same object
        renamed: list[tuple[str, str]] = []
        for oid in sorted(set(_created) & set(_destroyed)):
            c = _created[oid]
            d = _destroyed[oid]
            if len(c) == 1 and len(d) == 1:
                old_branch = d[0]
                new_branch = c[0]
                renamed.append((old_branch, new_branch))
                destroyed.remove(old_branch)
                created.remove(new_branch)

        return created, destroyed, renamed

    @staticmethod
    def _branch_conflict(left: str, right: str) -> bool:
        if left == right:
            return False
        left_parts = left.split("/")
        right_parts = right.split("/")
        min_len = min(len(left_parts), len(right_parts))
        return left_parts[:min_len] == right_parts[:min_len]

    def _filter_conflicts(
        self,
        *,
        desired: dict[str, Path],
        current: dict[str, Path],
        created: list[str],
    ) -> dict[str, Path]:
        # sort branches by conflict priority, then filter out any whose slash-separated
        # paths would create a nested worktree conflict
        winners: list[str] = []
        for branch in sorted(desired, key=lambda b: (
            0 if b in current else 1,  # prioritize existing
            0 if b not in created else 1,  # ... then prioritize non-created branches
            b,  # ... then break ties lexically
        )):
            conflict = next(
                (winner for winner in winners if self._branch_conflict(branch, winner)),
                None
            )
            if conflict is None:
                winners.append(branch)
                continue
            print(
                f"bertrand: skipping branch '{branch}' due nested path conflict with "
                f"'{conflict}'",
                file=sys.stderr
            )

        return {branch: desired[branch] for branch in winners}

    async def _move_worktree(self, branch: str, source: Path, target: Path) -> bool:
        if not source.exists() or not source.is_dir():
            print(
                f"bertrand: could not move worktree for branch '{branch}': missing "
                f"source path ({source})",
                file=sys.stderr
            )
            return False
        if target.exists():
            print(
                f"bertrand: could not move worktree for branch '{branch}': destination "
                f"already exists ({target})",
                file=sys.stderr
            )
            return False
        target.parent.mkdir(parents=True, exist_ok=True)
        cmd = [f"--git-dir={str(self.git_dir)}", "worktree", "move", "--relative-paths"]
        cmd.extend([str(source), str(target)])
        result = await git(cmd, check=False, capture_output=True)
        if result.returncode != 0:
            print(
                f"bertrand: failed to move worktree for branch '{branch}' from "
                f"{source} to {target}:\n{(result.stderr or result.stdout).strip()}",
                file=sys.stderr
            )
            return False
        return True

    async def _belongs_to_repo(self, path: Path, env: dict[str, str]) -> bool:
        result = await git(
            ["-C", str(path), "rev-parse", "--git-common-dir"],
            check=False,
            capture_output=True,
            env=env,
        )
        if result.returncode != 0:
            return False
        owner = Path(result.stdout.strip()).expanduser()
        if not owner.is_absolute():
            owner = (path / owner).resolve()
        else:
            owner = owner.resolve()
        return self.git_dir == owner

    async def _destroy_worktree(self, *, branch: str, path: Path) -> bool:
        if not path.exists():
            print(
                f"bertrand: could not remove worktree for branch '{branch}': missing "
                f"path ({path})",
                file=sys.stderr
            )
            return False
        if not path.is_dir():
            print(
                f"bertrand: could not remove worktree for branch '{branch}': path is "
                f"not a directory ({path})",
                file=sys.stderr
            )
            return False
        env = await git_strip_env()
        if not await self._belongs_to_repo(path, env):
            print(
                f"bertrand: could not remove worktree for branch '{branch}': worktree "
                f"does not belong to this repository ({path})",
                file=sys.stderr
            )
            return False
        clean = await git(
            ["-C", str(path), "status", "--porcelain"],
            check=False,
            capture_output=True,
            env=env,
        )
        if clean.returncode != 0:
            print(
                f"bertrand: could not remove worktree for branch '{branch}': failed to "
                f"check worktree status at {path}",
                file=sys.stderr
            )
            return False
        if clean.stdout.strip():
            print(
                f"bertrand: could not remove worktree for branch '{branch}': worktree "
                f"is dirty ({path})",
                file=sys.stderr
            )
            return False

        # remove the worktree using git, which will also properly clean up the
        # associated gitdir and any linked metadata
        result = await git(
            [f"--git-dir={str(self.git_dir)}", "worktree", "remove", str(path)],
            check=False,
            capture_output=True,
        )
        if result.returncode != 0:
            print(
                f"bertrand: failed to remove worktree for branch '{branch}' at "
                f"{path}:\n{(result.stderr or result.stdout).strip()}",
                file=sys.stderr
            )
            return False

        # clean up now-empty parent directories up to project root to account for
        # branch names that contain path separators.
        cursor = path.parent
        while cursor != self.project_root:
            try:
                if any(cursor.iterdir()):
                    break
                cursor.rmdir()
            except OSError:
                break
            cursor = cursor.parent
        return True

    async def _create_worktree(self, *, branch: str, target: Path) -> bool:
        if target.exists():
            print(
                f"bertrand: could not create worktree for branch '{branch}': path "
                f"already exists ({target})",
                file=sys.stderr
            )
            return False
        target.parent.mkdir(parents=True, exist_ok=True)
        result = await git(
            [
                f"--git-dir={str(self.git_dir)}",
                "worktree",
                "add",
                "--relative-paths",
                str(target),
                branch,
            ],
            check=False,
            capture_output=True,
        )
        if result.returncode != 0:
            print(
                f"bertrand: failed to create worktree for branch '{branch}' at "
                f"{target}:\n{(result.stderr or result.stdout).strip()}",
                file=sys.stderr
            )
            return False
        return True

    async def update(self, updates: list[RefUpdate]) -> None:
        """Converge worktrees to the authoritative branch set using a hybrid
        intent-first delta pass.

        Parameters
        ----------
        updates : list[RefUpdate]
            The parsed reference updates received from git on stdin, which will be used
            to derive explicit intent for branch creations and destructions, as well as
            implicit hints for branch renames where creation and destruction are paired
            on the same object.
        """
        # non-bare repositories are treated as single-worktree mode; no branch-path
        # reconciliation is performed in this mode.
        if not self.is_bare:
            return

        # derive current and desired branch-to-path mappings
        current = self._current_branches()
        desired = {branch: (self.project_root / branch) for branch in self.branches}

        # derive explicit intent from transaction updates, preserving order
        created, destroyed, renamed = self._intended_changes(updates)
        desired = self._filter_conflicts(desired=desired, current=current, created=created)

        # apply rename hints first (when they map cleanly to current state)
        for old_branch, new_branch in renamed:
            source = current.get(old_branch)
            if source is None:
                continue
            target = desired.get(new_branch)
            if target is None or new_branch in current:
                continue
            if source == target or await self._move_worktree(new_branch, source, target):
                current.pop(old_branch, None)
                current[new_branch] = target

        # apply explicit destroys for branches still present in current state
        for branch in destroyed:
            path = current.get(branch)
            if path is None or branch in desired:
                continue
            if await self._destroy_worktree(branch=branch, path=path):
                current.pop(branch, None)

        # apply explicit creates for branches that are still missing
        for branch in created:
            if branch in current or branch not in desired:
                continue
            target = desired[branch]
            if await self._create_worktree(branch=branch, target=target):
                current[branch] = target

        # remove branches no longer present in the repository
        stale = set(current) - set(desired)
        for branch in sorted(stale):
            if await self._destroy_worktree(branch=branch, path=current[branch]):
                current.pop(branch, None)

        # move branches that are present but located at non-canonical paths
        shared = set(current) & set(desired)
        for branch in sorted(shared):
            source = current[branch]
            target = desired[branch]
            if source == target:
                continue
            if await self._move_worktree(branch, source, target):
                current[branch] = target

        # create worktrees for branches that don't yet have one
        missing = set(desired) - set(current)
        for branch in sorted(missing):
            if await self._create_worktree(branch=branch, target=desired[branch]):
                current[branch] = desired[branch]


async def main() -> None:
    """Entry point for git's `reference-transaction` hook.

    Raises
    ------
    TypeError
        If the hook is invoked with an invalid state argument.
    """
    argv = list(sys.argv[1:])
    if len(argv) != 1:
        raise TypeError(f"expected exactly one state argument in {sorted(STATES)}")
    state = argv[0].strip()
    if state not in STATES:
        raise TypeError(
            f"invalid state argument: {state!r} (expected one of: {sorted(STATES)})"
        )
    if state != "committed":
        return  # no-op outside committed phase

    # parse transaction and filter for branch updates
    updates = RefUpdate.parse(sys.stdin.read())
    head_updates = [u for u in updates if u.is_head]
    if not head_updates:
        return  # nothing to do

    # load project context and apply updates to branch worktrees
    project = await Project.load()
    await project.update(head_updates)


if __name__ == "__main__":
    asyncio.run(main())
