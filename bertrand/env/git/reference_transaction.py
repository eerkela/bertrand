#!/usr/bin/env python3
# bertrand-managed: reference-transaction
# NOTE: the above marker is used to allow Bertrand to automatically rewrite this file
# in-place when the packaged version is updated, rather than warning in the event of a
# mismatch.
"""A git reference-transaction hook that tracks changes to branches and mirrors them
in the project directory, so that Bertrand can avoid any manual branch management and
treat git as the sole source of truth for its isolated worktree model.
"""
from __future__ import annotations

import os
import subprocess
import sys

from pathlib import Path


HEADS_PREFIX = "refs/heads/"
STATES = {"prepared", "committed", "aborted"}


def git(
    args: list[str],
    *,
    input: str | None = None,  # pylint: disable=redefined-builtin
    env: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    """Run a git command as a subprocess.

    Note that no subprocess error will be raised for non-zero exit codes; the caller
    must check the result's `returncode` field to determine if the command succeeded.

    Parameters
    ----------
    args : list[str]
        Git command arguments, excluding the initial "git".
    input : str | None, optional
        Optional text to pass as stdin to the git process.  Defaults to None (no
        stdin).
    env : dict[str, str] | None, optional
        Optional environment variables to pass to the git process.  Defaults to None
        (inherit current environment).

    Returns
    -------
    subprocess.CompletedProcess[str]
        The completed process result, with stdout and stderr captured as text.
    """
    return subprocess.run(
        ["git", *args],
        input=input,
        text=True,
        capture_output=True,
        check=False,
        env=env,
    )


def strip_env(env: dict[str, str] | None = None) -> dict[str, str]:
    """Remove repo-local git environment variables from the given environment mapping.

    Parameters
    ----------
    env : dict[str, str] | None, optional
        The environment mapping to strip git variables from.  If None, the current
        process environment will be used.  Defaults to None.

    Returns
    -------
    dict[str, str]
        The filtered environment variables with repo-local keys removed.
    """
    env = dict(os.environ if env is None else env)
    local = git(["rev-parse", "--local-env-vars"], env=env)
    if local.returncode == 0:
        keys = [key.strip() for key in local.stdout.splitlines() if key.strip()]
    else:
        keys = ["GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR"]  # best-effort fallback
    for key in keys:
        env.pop(key, None)
    return env


class RefUpdate:
    """A single reference update received from git on stdin."""
    old: str
    new: str
    ref: str

    def __init__(self, old: str, new: str, ref: str) -> None:
        self.old = old
        self.new = new
        self.ref = ref

    @property
    def is_head(self) -> bool:
        """
        Returns
        -------
        bool
            True if this update targets `refs/heads/*`.
        """
        return self.ref.startswith(HEADS_PREFIX)

    @property
    def branch(self) -> str:
        """
        Returns
        -------
        str
            The short branch name for `refs/heads/*` updates.
        """
        return self.ref[len(HEADS_PREFIX):]

    @property
    def created(self) -> bool:
        """
        Returns
        -------
        bool
            True if this update created a new ref.
        """
        return all((c == "0" for c in self.old)) and any((c != "0" for c in self.new))

    @property
    def destroyed(self) -> bool:
        """
        Returns
        -------
        bool
            True if this update deleted an existing ref.
        """
        return any((c != "0" for c in self.old)) and all((c == "0" for c in self.new))

    @classmethod
    def parse(cls, stdin: str) -> list[RefUpdate]:
        """Parse and validate transaction update lines from stdin.

        Parameters
        ----------
        stdin : str
            The raw stdin input containing reference update lines, typically read from
            the `reference-transaction` hook's standard input stream.

        Returns
        -------
        list[RefUpdate]
            A list of parsed reference updates.

        Raises
        ------
        ValueError
            If any line is malformed.
        """
        updates: list[RefUpdate] = []
        for index, raw in enumerate(stdin.splitlines(), start=1):
            line = raw.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 3:
                raise ValueError(
                    f"malformed git transaction line {index}: expected "
                    f"'<old> <new> <ref>', got: {raw!r}"
                )
            old, new, ref = (part.strip() for part in parts)
            if not ref:
                raise ValueError(
                    f"malformed git transaction line {index}: ref must not be empty"
                )
            updates.append(cls(old=old, new=new, ref=ref))
        return updates


class Project:
    """Resolved repository context for hook operations."""
    git_dir: Path
    project_root: Path
    is_bare: bool
    branches: set[str]

    class Worktree:
        """A single entry from `git worktree list --porcelain`."""
        path: Path
        branch: str | None = None

        def __init__(self, path: Path, branch: str | None = None) -> None:
            self.path = path
            self.branch = branch

    worktrees: list[Worktree]

    def __init__(
        self,
        git_dir: Path,
        project_root: Path,
        is_bare: bool,
        branches: set[str],
        worktrees: list[Worktree],
    ) -> None:
        self.git_dir = git_dir
        self.project_root = project_root
        self.is_bare = is_bare
        self.branches = branches
        self.worktrees = worktrees

    @staticmethod
    def _supports_relative_paths() -> bool:
        result = git(["worktree", "add", "-h"])
        text = f"{result.stdout}\n{result.stderr}".lower()
        return "--relative-paths" in text

    @staticmethod
    def _supports_relative_move_paths() -> bool:
        result = git(["worktree", "move", "-h"])
        text = f"{result.stdout}\n{result.stderr}".lower()
        return "--relative-paths" in text

    @classmethod
    def load(cls) -> Project:
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
        result = git(["rev-parse", "--path-format=absolute", "--git-common-dir"])
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
        bare = git([f"--git-dir={str(git_dir)}", "rev-parse", "--is-bare-repository"])
        if bare.returncode != 0:
            raise OSError(
                f"failed to determine bare repository mode:\n"
                f"{(bare.stderr or bare.stdout).strip()}"
            )
        is_bare = bare.stdout.strip().lower() == "true"
        project_root = git_dir.parent.expanduser().resolve()
        if is_bare and not cls._supports_relative_paths():
            raise OSError(
                "git worktree relative path support is required for bare repository "
                "mode, but this git version does not support '--relative-paths' for "
                "worktree creation."
            )

        # list tracked branches from the repository
        result = git([
            f"--git-dir={str(git_dir)}",
            "for-each-ref",
            "--format=%(refname:short)",
            "refs/heads",
        ])
        if result.returncode != 0:
            raise OSError(
                f"failed to list local branches:\n{(result.stderr or result.stdout).strip()}"
            )
        branches = {line.strip() for line in result.stdout.splitlines() if line.strip()}

        # find local worktrees for checked-out branches, where applicable
        result = git([f"--git-dir={str(git_dir)}", "worktree", "list", "--porcelain"])
        if result.returncode != 0:
            raise OSError(
                f"failed to list worktrees:\n{(result.stderr or result.stdout).strip()}"
            )
        worktrees: list[Project.Worktree] = []
        curr_path: Path | None = None
        curr_branch: str | None = None
        for line in result.stdout.splitlines():
            line = line.strip()
            if not line:
                if curr_path is not None:
                    worktrees.append(Project.Worktree(path=curr_path, branch=curr_branch))
                curr_path = None
                curr_branch = None
                continue
            if line.startswith("worktree "):
                curr_path = Path(line[len("worktree "):]).expanduser().resolve()
            elif line.startswith("branch "):
                ref = line[len("branch "):]
                if ref.startswith(HEADS_PREFIX):
                    curr_branch = ref[len(HEADS_PREFIX):]
        if curr_path is not None:
            worktrees.append(Project.Worktree(path=curr_path, branch=curr_branch))

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

    def _move_worktree(self, branch: str, source: Path, target: Path) -> bool:
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
        cmd = [f"--git-dir={str(self.git_dir)}", "worktree", "move",]
        if self._supports_relative_move_paths():
            cmd.append("--relative-paths")
        cmd.extend([str(source), str(target)])
        result = git(cmd)
        if result.returncode != 0:
            print(
                f"bertrand: failed to move worktree for branch '{branch}' from "
                f"{source} to {target}:\n{(result.stderr or result.stdout).strip()}",
                file=sys.stderr
            )
            return False
        return True

    def _belongs_to_repo(self, path: Path, env: dict[str, str]) -> bool:
        result = git(["-C", str(path), "rev-parse", "--git-common-dir"], env=env)
        if result.returncode != 0:
            return False
        owner = Path(result.stdout.strip()).expanduser()
        if not owner.is_absolute():
            owner = (path / owner).resolve()
        else:
            owner = owner.resolve()
        return self.git_dir == owner

    def _destroy_worktree(self, *, branch: str, path: Path) -> bool:
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
        env = strip_env()
        if not self._belongs_to_repo(path, env):
            print(
                f"bertrand: could not remove worktree for branch '{branch}': worktree "
                f"does not belong to this repository ({path})",
                file=sys.stderr
            )
            return False
        clean = git(["-C", str(path), "status", "--porcelain"], env=env)
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
        result = git([f"--git-dir={str(self.git_dir)}", "worktree", "remove", str(path)])
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

    def _create_worktree(self, *, branch: str, target: Path) -> bool:
        if target.exists():
            print(
                f"bertrand: could not create worktree for branch '{branch}': path "
                f"already exists ({target})",
                file=sys.stderr
            )
            return False
        target.parent.mkdir(parents=True, exist_ok=True)
        result = git([
            f"--git-dir={str(self.git_dir)}",
            "worktree",
            "add",
            "--relative-paths",
            str(target),
            branch,
        ])
        if result.returncode != 0:
            print(
                f"bertrand: failed to create worktree for branch '{branch}' at "
                f"{target}:\n{(result.stderr or result.stdout).strip()}",
                file=sys.stderr
            )
            return False
        return True

    def update(self, updates: list[RefUpdate]) -> None:
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
            if source == target or self._move_worktree(new_branch, source, target):
                current.pop(old_branch, None)
                current[new_branch] = target

        # apply explicit destroys for branches still present in current state
        for branch in destroyed:
            path = current.get(branch)
            if path is None or branch in desired:
                continue
            if self._destroy_worktree(branch=branch, path=path):
                current.pop(branch, None)

        # apply explicit creates for branches that are still missing
        for branch in created:
            if branch in current or branch not in desired:
                continue
            target = desired[branch]
            if self._create_worktree(branch=branch, target=target):
                current[branch] = target

        # remove branches no longer present in the repository
        stale = set(current) - set(desired)
        for branch in sorted(stale):
            if self._destroy_worktree(branch=branch, path=current[branch]):
                current.pop(branch, None)

        # move branches that are present but located at non-canonical paths
        shared = set(current) & set(desired)
        for branch in sorted(shared):
            source = current[branch]
            target = desired[branch]
            if source == target:
                continue
            if self._move_worktree(branch, source, target):
                current[branch] = target

        # create worktrees for branches that don't yet have one
        missing = set(desired) - set(current)
        for branch in sorted(missing):
            if self._create_worktree(branch=branch, target=desired[branch]):
                current[branch] = desired[branch]


def main() -> None:
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
    project = Project.load()
    project.update(head_updates)


if __name__ == "__main__":
    main()
