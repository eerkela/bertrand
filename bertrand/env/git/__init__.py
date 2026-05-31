"""Git hook-safe utilities shared by Bertrand's runtime.

Note that the structure of this subpackage is a bit odd, since it also houses Git
hooks that need to run without access to the rest of the Bertrand codebase, in order
to prevent Git from breaking when `bertrand` is not installed in the current
environment.
"""

# ruff: noqa: F401
from .bertrand_git import (
    BERTRAND_LABEL,
    BERTRAND_LABEL_CONTAINER,
    BERTRAND_LABEL_IMAGE,
    BERTRAND_LABEL_MANAGED,
    BERTRAND_NAMESPACE,
    CONTAINER_TMP,
    HOST_MOUNTS,
    METADATA_DIR,
    METADATA_ID,
    NO_DEADLINE,
    NORMALIZE_ARCH,
    REPO_ID_LABEL,
    REPO_MOUNT,
    ROOT_DIR,
    WORKTREE_ID_LABEL,
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
    inside_container,
    inside_image,
    install_packages,
    run,
    sudo,
    symlink_points_to,
    until,
)
