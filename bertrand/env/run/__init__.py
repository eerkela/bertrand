"""General-purpose utilities for running commands and managing locks, which have
minimal dependencies.

Note that the structure of this subpackage is a bit odd, since it also houses Git
hooks that need to run without access to the rest of the Bertrand codebase, in order
to prevent Git from breaking when `bertrand` is not installed in the current
environment.
"""
from .bertrand_git import (
    BERTRAND_ENV,
    CONTAINER_ID_ENV,
    CONTAINER_RUNTIME_ENV,
    CONTAINER_RUNTIME_MOUNT,
    CONTAINER_SOCKET,
    CONTAINER_TMP_MOUNT,
    ENV_ID_ENV,
    IMAGE_ID_ENV,
    IMAGE_TAG_ENV,
    METADATA_DIR,
    METADATA_FILE,
    METADATA_LOCK,
    METADATA_TMP,
    PROJECT_ENV,
    PROJECT_MOUNT,
    WORKTREE_ENV,
    WORKTREE_MOUNT,
    CommandError,
    CompletedProcess,
    GitRefUpdate,
    GitRepository,
    JSONValue,
    Scalar,
    TimeoutExpired,
    User,
    atomic_write_bytes,
    atomic_write_text,
    can_escalate,
    confirm,
    inside_container,
    inside_image,
    mkdir_private,
    run,
    sanitize_name,
    sudo,
)
from .lock import (
    LOCK_TIMEOUT,
    Lock,
    lock_worktree,
)
