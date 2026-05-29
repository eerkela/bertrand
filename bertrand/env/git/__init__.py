# ruff: noqa: F401
"""Git hook-safe utilities shared by Bertrand's runtime.

Note that the structure of this subpackage is a bit odd, since it also houses Git
hooks that need to run without access to the rest of the Bertrand codebase, in order
to prevent Git from breaking when `bertrand` is not installed in the current
environment.
"""

from .bertrand_git import (
    BERTRAND_ENV,
    BERTRAND_NAMESPACE,
    CONTAINER_ARTIFACT_MOUNT,
    CONTAINER_ID_ENV,
    CONTAINER_RUNTIME_ENV,
    CONTAINER_RUNTIME_MOUNT,
    CONTAINER_TMP_MOUNT,
    HOST_MOUNTS,
    IMAGE_ID_ENV,
    METADATA_DIR,
    METADATA_FILE,
    METADATA_LOCK,
    METADATA_REPO_ID,
    METADATA_TMP,
    METADATA_WORKTREE_ID,
    NO_DEADLINE,
    NORMALIZE_ARCH,
    PROJECT_ENV,
    PROJECT_MOUNT,
    REPO_ID_ENV,
    ROOT_DIR,
    WORKTREE_ENV,
    WORKTREE_ID_ENV,
    WORKTREE_MOUNT,
    CommandError,
    CompletedProcess,
    Deadline,
    GitRefUpdate,
    GitRepository,
    GroupStatus,
    HostLock,
    JSONValue,
    Scalar,
    TimeoutExpired,
    User,
    abspath,
    atomic_symlink,
    atomic_write_text,
    can_escalate,
    confirm,
    ensure_worktree_id,
    inside_container,
    inside_image,
    install_packages,
    run,
    sudo,
    symlink_points_to,
)
