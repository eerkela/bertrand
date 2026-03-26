"""General-purpose utilities for running commands and managing locks, which have
minimal dependencies.

Note that the structure of this subpackage is a bit odd, since it also houses Git
hooks that need to run without access to the rest of the Bertrand codebase, in order
to prevent Git from breaking when `bertrand` is not installed in the current
environment.
"""
from .bertrand_git import (
    CompletedProcess,
    CommandError,
    TimeoutExpired,
    User,
    atomic_write_bytes,
    atomic_write_text,
    can_escalate,
    confirm,
    git,
    mkdir_private,
    list_branches,
    list_worktrees,
    parse_git_bool,
    run,
    sanitize_name,
    sudo,
    supports_relative_worktree_paths,
)
from .lock import (
    LOCK_TIMEOUT,
    Lock
)
