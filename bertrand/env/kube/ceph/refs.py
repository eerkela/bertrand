"""Shared Ceph repository labels and storage preferences."""

from __future__ import annotations

CEPHFS_STORAGE_CLASS_PREFERENCES: tuple[str, ...] = ("cephfs", "rook-cephfs")
REPOSITORY_SNAPSHOT_LABEL = "bertrand.dev/ceph-repository-snapshot"
REPOSITORY_SNAPSHOT_LABEL_VALUE = "v1"
REPOSITORY_SNAPSHOT_PURPOSE_LABEL = "bertrand.dev/ceph-repository-snapshot-purpose"
REPOSITORY_SNAPSHOT_PURPOSE_RETAINED = "retained"
REPOSITORY_SNAPSHOT_PURPOSE_BUILD = "build"
REPOSITORY_BUILD_SOURCE_LABEL = "bertrand.dev/ceph-repository-build-source"
REPOSITORY_BUILD_SOURCE_LABEL_VALUE = "v1"
