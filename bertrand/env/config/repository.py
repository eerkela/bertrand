"""Repository identity helpers for Bertrand project configuration."""

from __future__ import annotations

import uuid

from bertrand.env.git import METADATA_REPO_ID, GitRepository

REPO_ID_NAMESPACE = uuid.UUID("7b1506f4-4a3f-4b46-94bb-471e0f59d1a0")


def resolve_repo_id(repo: GitRepository) -> str:
    """Return the stable Bertrand repository identity.

    Parameters
    ----------
    repo : GitRepository
        Git repository whose identity should be resolved.

    Returns
    -------
    str
        Stable UUID hex string used to scope cluster resources for the repository.

    Notes
    -----
    Managed repository metadata takes precedence when present. Otherwise, the ID is
    deterministically derived from the repository root path so uninitialized
    repositories can still address cluster resources consistently.
    """
    repo_id_file = repo.root / METADATA_REPO_ID
    if repo_id_file.is_file():
        try:
            return uuid.UUID(repo_id_file.read_text(encoding="utf-8").strip()).hex
        except (OSError, ValueError):
            pass
    return uuid.uuid5(REPO_ID_NAMESPACE, repo.root.as_posix()).hex
