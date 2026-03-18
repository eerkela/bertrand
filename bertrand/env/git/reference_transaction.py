#!/usr/bin/env python3
"""A git reference-transaction hook that tracks changes to branches and mirrors them
in the project directory, so that Bertrand can avoid any manual branch management and
treat git as the sole source of truth for its isolated worktree model.
"""
from __future__ import annotations

import os
import subprocess
import sys

from dataclasses import dataclass
from pathlib import Path
from collections.abc import Sequence
from typing import NoReturn, Self, TextIO


HEADS_PREFIX = "refs/heads/"
STATES = {"prepared", "committed", "aborted"}
ZERO_OID_CHARS = {"0"}

# standardized exit codes
EXIT_INVALID_INVOCATION = 2
EXIT_MALFORMED_TRANSACTION = 3
EXIT_CONTEXT_LOAD_FAILED = 4
EXIT_RECONCILE_FAILED = 5


def _fail(stderr: TextIO, code: int, message: str) -> NoReturn:
    """Write a diagnostic and abort hook execution with the given exit code."""
    print(f"bertrand: {message}", file=stderr)
    raise SystemExit(code)


def git(
    args: Sequence[str],
    *,
    input: str | None = None,  # pylint: disable=redefined-builtin
    env: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    """Run a git command as a subprocess.

    Note that no subprocess error will be raised for non-zero exit codes; the caller
    must check the result's `returncode` field to determine if the command succeeded.

    Parameters
    ----------
    args : Sequence[str]
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


@dataclass(frozen=True)
class RefUpdate:
    """A single reference update received from git on stdin."""
    old: str
    new: str
    ref: str

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

        Raises
        ------
        ValueError
            If this update does not target `refs/heads/*`.
        """
        if not self.is_head:
            raise ValueError(f"not a branch ref: {self.ref}")
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
    def parse(cls, stdin: str, *, stderr: TextIO) -> list[Self]:
        """Parse and validate transaction update lines from stdin.

        Parameters
        ----------
        stdin : str
            The raw stdin input containing reference update lines, typically read from
            the `reference-transaction` hook's standard input stream.
        stderr : TextIO
            The standard error stream to write diagnostics to in case of malformed
            input.

        Returns
        -------
        list[RefUpdate]
            A list of parsed reference updates.

        Raises
        ------
        SystemExit
            If any line is malformed.
        """
        updates: list[Self] = []
        for index, raw in enumerate(stdin.splitlines(), start=1):
            line = raw.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 3:
                _fail(
                    stderr,
                    EXIT_MALFORMED_TRANSACTION,
                    "malformed git transaction line "
                    f"{index}: expected '<old> <new> <ref>', got: {raw!r}",
                )
            old, new, ref = (part.strip() for part in parts)
            if not ref:
                _fail(
                    stderr,
                    EXIT_MALFORMED_TRANSACTION,
                    f"malformed git transaction line {index}: empty ref",
                )
            updates.append(cls(old=old, new=new, ref=ref))
        return updates


@dataclass(frozen=True)
class Project:
    """Resolved repository context for hook operations."""
    git_dir: Path
    project_root: Path
    branches: set[str]

    @dataclass(frozen=True)
    class Worktree:
        """A single entry from `git worktree list --porcelain`."""
        path: Path
        branch: str | None = None

    worktrees: list[Worktree]

    @classmethod
    def load(cls, *, stderr: TextIO) -> Self:
        """Resolve git context and current repository/worktree state.

        Parameters
        ----------
        stderr : TextIO
            The standard error stream to write diagnostics to in case of context load
            failures.

        Returns
        -------
        Project
            The resolved repository context, including git directory, project root, and
            current branches and worktrees.

        Raises
        ------
        SystemExit
            If repository context cannot be loaded.
        """
        result = git(["rev-parse", "--absolute-git-dir"])
        if result.returncode != 0:
            _fail(
                stderr,
                EXIT_CONTEXT_LOAD_FAILED,
                "failed to resolve absolute git dir:\n"
                f"{(result.stderr or result.stdout).strip()}",
            )
        git_dir = Path(result.stdout.strip()).expanduser().resolve()
        if not git_dir.exists() or not git_dir.is_dir():
            _fail(
                stderr,
                EXIT_CONTEXT_LOAD_FAILED,
                f"resolved git dir does not exist or is not a directory: {git_dir}",
            )

        project_root: Path | None = None
        for ancestor in (git_dir, *git_dir.parents):
            if ancestor.name.endswith(".git"):
                project_root = ancestor.parent
                break
        if project_root is None:
            _fail(
                stderr,
                EXIT_CONTEXT_LOAD_FAILED,
                f"could not derive project root from git dir: {git_dir}",
            )
        project_root = project_root.expanduser().resolve()

        result = git([
            f"--git-dir={str(git_dir)}",
            "for-each-ref",
            "--format=%(refname:short)",
            "refs/heads",
        ])
        if result.returncode != 0:
            _fail(
                stderr,
                EXIT_CONTEXT_LOAD_FAILED,
                "failed to list local branches:\n"
                f"{(result.stderr or result.stdout).strip()}",
            )
        branches = {line.strip() for line in result.stdout.splitlines() if line.strip()}

        result = git([f"--git-dir={str(git_dir)}", "worktree", "list", "--porcelain"])
        if result.returncode != 0:
            _fail(
                stderr,
                EXIT_CONTEXT_LOAD_FAILED,
                "failed to list worktrees:\n"
                f"{(result.stderr or result.stdout).strip()}",
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
            branches=branches,
            worktrees=worktrees,
        )

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

    def _destroy_worktree(self, *, branch: str, path: Path, stderr: TextIO) -> None:
        if not path.exists():
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"cannot remove worktree for branch '{branch}': missing path ({path})",
            )
        if not path.is_dir():
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"cannot remove worktree for branch '{branch}': "
                f"path is not a directory ({path})",
            )

        env = strip_env()
        if not self._belongs_to_repo(path, env):
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"cannot remove worktree for branch '{branch}': "
                f"worktree does not belong to this repository ({path})",
            )

        clean = git(["-C", str(path), "status", "--porcelain"], env=env)
        if clean.returncode != 0:
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"cannot remove worktree for branch '{branch}': failed to check "
                f"worktree status at {path}",
            )
        if clean.stdout.strip():
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"cannot remove worktree for branch '{branch}': "
                f"worktree is dirty ({path})",
            )

        result = git([f"--git-dir={str(self.git_dir)}", "worktree", "remove", str(path)])
        if result.returncode != 0:
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"failed to remove worktree for branch '{branch}' at {path}:\n"
                f"{(result.stderr or result.stdout).strip()}",
            )

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

    def _move_worktree(
        self,
        *,
        branch: str,
        source: Path,
        target: Path,
        stderr: TextIO,
    ) -> None:
        if not source.exists() or not source.is_dir():
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"cannot move worktree for branch '{branch}': invalid source ({source})",
            )
        if target.exists():
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"cannot move worktree for branch '{branch}': "
                f"destination already exists ({target})",
            )
        target.parent.mkdir(parents=True, exist_ok=True)
        result = git([
            f"--git-dir={str(self.git_dir)}",
            "worktree",
            "move",
            str(source),
            str(target),
        ])
        if result.returncode != 0:
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"failed to move worktree for branch '{branch}' from "
                f"{source} to {target}:\n{(result.stderr or result.stdout).strip()}",
            )

    def _create_worktree(self, *, branch: str, target: Path, stderr: TextIO) -> None:
        if target.exists():
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"cannot create worktree for branch '{branch}': "
                f"path already exists ({target})",
            )
        target.parent.mkdir(parents=True, exist_ok=True)
        result = git([
            f"--git-dir={str(self.git_dir)}",
            "worktree",
            "add",
            str(target),
            branch,
        ])
        if result.returncode != 0:
            _fail(
                stderr,
                EXIT_RECONCILE_FAILED,
                f"failed to create worktree for branch '{branch}' at {target}:\n"
                f"{(result.stderr or result.stdout).strip()}",
            )

    def update(self, *, stderr: TextIO) -> None:
        """Converge worktrees to the authoritative branch set using a delta pass.

        Parameters
        ----------
        stderr : TextIO
            The standard error stream to write diagnostics to in case of reconciliation
            failures.

        Raises
        ------
        SystemExit
            If reconcile operations fail.
        """
        current: dict[str, Path] = {}
        for wt in self.worktrees:
            if wt.branch is None:
                continue
            existing = current.setdefault(wt.branch, wt.path)
            if existing != wt.path:
                _fail(
                    stderr,
                    EXIT_RECONCILE_FAILED,
                    f"multiple worktrees mapped to branch '{wt.branch}': "
                    f"{existing} and {wt.path}",
                )

        desired = {branch: (self.project_root / branch) for branch in self.branches}

        # 1) remove branches no longer present in the repository
        stale = sorted(set(current) - set(desired))
        for branch in stale:
            self._destroy_worktree(branch=branch, path=current[branch], stderr=stderr)
            current.pop(branch, None)

        # 2) move branches that are present but located at non-canonical paths
        shared = sorted(set(current) & set(desired))
        for branch in shared:
            source = current[branch]
            target = desired[branch]
            if source == target:
                continue
            self._move_worktree(
                branch=branch,
                source=source,
                target=target,
                stderr=stderr,
            )
            current[branch] = target

        # 3) create worktrees for branches that don't yet have one
        missing = sorted(set(desired) - set(current))
        for branch in missing:
            self._create_worktree(branch=branch, target=desired[branch], stderr=stderr)


def main() -> None:
    """Entry point for git's `reference-transaction` hook.

    Raises
    ------
    SystemExit
        With a non-zero exit code if an error occurs during processing.
    """
    argv = list(sys.argv[1:])
    stdin = sys.stdin
    stderr = sys.stderr

    if len(argv) != 1:
        _fail(
            stderr,
            EXIT_INVALID_INVOCATION,
            "expected exactly one state argument: 'prepared', 'committed', or "
            "'aborted'",
        )

    state = argv[0].strip()
    if state not in STATES:
        _fail(stderr, EXIT_INVALID_INVOCATION, f"invalid state argument: {state!r}")
    if state != "committed":
        return  # no-op outside committed phase

    updates = RefUpdate.parse(stdin.read(), stderr=stderr)
    head_updates = [u for u in updates if u.is_head]
    if not head_updates:
        return

    project = Project.load(stderr=stderr)
    project.update(stderr=stderr)


if __name__ == "__main__":
    main()
