"""Kubernetes mailbox resources for Bertrand host/editor dev requests."""

from __future__ import annotations

import hashlib
import time
from datetime import UTC, datetime
from typing import TYPE_CHECKING, Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator

from bertrand.env.git import BERTRAND_NAMESPACE, REPO_ID_LABEL, Deadline
from bertrand.env.kube.custom_object import (
    CustomObjectManifest,
    CustomResource,
    custom_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.kube.api.client import Kube

DEV_GROUP = "dev.bertrand.dev"
DEV_VERSION = "v1alpha1"
CODE_OPEN_KIND = "CodeOpenRequest"
CODE_OPEN_PLURAL = "codeopenrequests"
CODE_OPEN_LABEL = "bertrand.dev/code-open"
CODE_OPEN_LABEL_VALUE = "code-open"
CODE_OPEN_SESSION_LABEL = "bertrand.dev/code-open-session"
CODE_OPEN_REQUEST_LABEL = "bertrand.dev/code-open-request"
CODE_OPEN_HOST_LABEL = "bertrand.dev/code-open-host"

type CodeOpenPhase = Literal[
    "Pending",
    "Accepted",
    "Succeeded",
    "Failed",
    "Expired",
]

_CODE_OPEN_LABELS = {
    "app.kubernetes.io/part-of": "bertrand",
    "app.kubernetes.io/component": "dev",
    CODE_OPEN_LABEL: CODE_OPEN_LABEL_VALUE,
}


class CodeOpenSpec(BaseModel):
    """Validated mailbox request payload for one editor-open operation."""

    model_config = ConfigDict(extra="forbid", frozen=True)
    session_id: str
    request_id: str
    repo_id: str
    worktree: str
    pod_name: str
    container_name: str
    workspace_path: str
    editor: str
    block: bool
    deadline: float

    @property
    def expired(self) -> bool:
        """Return whether the request deadline has elapsed.

        Returns
        -------
        bool
            `True` when the current wall clock is past `deadline`.
        """
        return time.time() >= self.deadline


class CodeOpenStatus(BaseModel):
    """Validated mailbox status payload for one editor-open operation."""

    model_config = ConfigDict(extra="ignore", frozen=True)
    phase: CodeOpenPhase = "Pending"
    host_id: str = ""
    accepted_at: str = ""
    completed_at: str = ""
    message: str = ""

    @property
    def terminal(self) -> bool:
        """Return whether the request reached a terminal phase.

        Returns
        -------
        bool
            `True` for `Succeeded`, `Failed`, or `Expired`.
        """
        return self.phase in ("Succeeded", "Failed", "Expired")


class CodeOpenManifest(CustomObjectManifest):
    """Push/pull manifest for a CodeOpenRequest mailbox entry."""

    api_version: str = Field(default=f"{DEV_GROUP}/{DEV_VERSION}", alias="apiVersion")
    kind: str = CODE_OPEN_KIND
    spec: CodeOpenSpec
    host_id: str = Field(default="", exclude=True)
    status: CodeOpenStatus = Field(default_factory=CodeOpenStatus)

    @model_validator(mode="after")
    def _validate_identity(self) -> CodeOpenManifest:
        """Validate deterministic identity labels against the request spec.

        Returns
        -------
        CodeOpenManifest
            This validated record.

        Raises
        ------
        ValueError
            If deterministic metadata does not match the request spec.
        """
        expected = code_open_request_name(
            self.spec.session_id,
            self.spec.request_id,
        )
        if self.name != expected:
            msg = (
                f"{CODE_OPEN_KIND} {self.name!r}: expected "
                f"deterministic name {expected!r}"
            )
            raise ValueError(msg)
        labels = self.metadata.labels
        if labels.get(CODE_OPEN_LABEL) != CODE_OPEN_LABEL_VALUE:
            msg = f"{CODE_OPEN_KIND} {self.name!r}: missing dev label"
            raise ValueError(msg)
        if labels.get(CODE_OPEN_SESSION_LABEL) != _label_value(self.spec.session_id):
            msg = f"{CODE_OPEN_KIND} {self.name!r}: session label does not match spec"
            raise ValueError(msg)
        if labels.get(CODE_OPEN_REQUEST_LABEL) != _label_value(self.spec.request_id):
            msg = f"{CODE_OPEN_KIND} {self.name!r}: request label does not match spec"
            raise ValueError(msg)
        return self

    @property
    def name(self) -> str:
        """Return the Kubernetes custom-object name.

        Returns
        -------
        str
            Kubernetes object name.
        """
        return self.metadata.name or code_open_request_name(
            self.spec.session_id,
            self.spec.request_id,
        )

    @property
    def namespace(self) -> str:
        """Return the namespace that owns this request.

        Returns
        -------
        str
            Kubernetes object namespace.
        """
        return self.metadata.namespace or BERTRAND_NAMESPACE

    def manifest(self) -> Mapping[str, object]:
        """Render the Kubernetes CodeOpenRequest manifest.

        Returns
        -------
        Mapping[str, object]
            Complete push-safe Kubernetes custom-object manifest.
        """
        return {
            "apiVersion": f"{DEV_GROUP}/{DEV_VERSION}",
            "kind": CODE_OPEN_KIND,
            "metadata": {
                "namespace": self.namespace,
                "name": self.name,
                "labels": code_open_request_labels(self.spec, self.host_id),
            },
            "spec": self.spec.model_dump(mode="json"),
        }


@custom_resource(
    manifest=CodeOpenManifest,
    group=DEV_GROUP,
    version=DEV_VERSION,
    kind=CODE_OPEN_KIND,
    plural=CODE_OPEN_PLURAL,
    labels=_CODE_OPEN_LABELS,
    singular="codeopenrequest",
    short_names=("cor",),
    spec_schema_overrides={
        "properties": {
            "session_id": {"type": "string", "minLength": 1},
            "request_id": {"type": "string", "minLength": 1},
            "repo_id": {"type": "string", "minLength": 1},
            "worktree": {"type": "string", "minLength": 1},
            "pod_name": {"type": "string", "minLength": 1},
            "container_name": {"type": "string", "minLength": 1},
            "workspace_path": {"type": "string", "minLength": 1},
            "editor": {"type": "string", "minLength": 1},
        },
    },
    status_schema_overrides={"properties": {"phase": {"type": "string"}}},
    default_namespace=BERTRAND_NAMESPACE,
)
class CodeOpenRequest(CustomResource[CodeOpenManifest]):
    """Wrapper around one CodeOpenRequest custom object."""

    @property
    def spec(self) -> CodeOpenSpec:
        """Return the validated CodeOpenRequest spec."""
        return self.payload.spec

    @property
    def status(self) -> CodeOpenStatus:
        """Return the validated CodeOpenRequest status."""
        return self.payload.status


async def patch_code_open_request_status(
    kube: Kube,
    *,
    record: CodeOpenRequest,
    phase: CodeOpenPhase,
    host_id: str = "",
    message: str = "",
    deadline: Deadline,
) -> CodeOpenRequest:
    """Patch one editor-open request status.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    record : CodeOpenRequest
        Existing mailbox record.
    phase : CodeOpenPhase
        New lifecycle phase.
    host_id : str, optional
        Host identity servicing the request.
    message : str, optional
        Human-readable status diagnostic.
    deadline : Deadline
        Maximum request budget in seconds.

    Returns
    -------
    CodeOpenRequest
        Updated mailbox record.
    """
    status: dict[str, object] = {
        "phase": phase,
        "message": message,
    }
    if host_id:
        status["host_id"] = host_id
    if phase == "Accepted":
        status["accepted_at"] = _now()
    if phase in ("Succeeded", "Failed", "Expired"):
        status["completed_at"] = _now()
    return await CodeOpenRequest.patch_status(
        kube,
        name=record.name,
        status=status,
        deadline=deadline,
    )


async def wait_code_open_request(
    kube: Kube,
    *,
    name: str,
    deadline: Deadline,
) -> CodeOpenRequest:
    """Wait until one editor-open request reaches a terminal phase.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        Request object name.
    deadline : Deadline
        Maximum wait budget in seconds.

    Returns
    -------
    CodeOpenRequest
        Terminal request record.

    Raises
    ------
    TimeoutError
        If the request does not complete before `timeout`.
    OSError
        If the request disappears before completion.
    """
    while True:
        remaining = deadline.remaining
        if remaining <= 0:
            msg = (
                "No host Bertrand editor bridge accepted the request before the "
                "deadline. Run `bertrand enter <project>` from your host shell, "
                "then run `bertrand code` inside that session."
            )
            raise TimeoutError(msg)
        record = await CodeOpenRequest.get(kube, name=name, deadline=deadline)
        if record is None:
            msg = f"{CODE_OPEN_KIND} {name!r} disappeared before completion"
            raise OSError(msg)
        if record.status.terminal:
            return record
        if record.spec.expired:
            return await patch_code_open_request_status(
                kube,
                record=record,
                phase="Expired",
                message="request deadline expired before a host bridge completed it",
                deadline=deadline,
            )
        await deadline.sleep(0.5)


def code_open_request_labels(spec: CodeOpenSpec, host_id: str) -> dict[str, str]:
    """Return Kubernetes labels for one editor-open request.

    Parameters
    ----------
    spec : CodeOpenSpec
        Request spec to label.
    host_id : str
        Host identity expected to service the request.

    Returns
    -------
    dict[str, str]
        Label selector values used by the host bridge and cleanup paths.
    """
    return {
        CODE_OPEN_SESSION_LABEL: _label_value(spec.session_id),
        CODE_OPEN_REQUEST_LABEL: _label_value(spec.request_id),
        CODE_OPEN_HOST_LABEL: _hash_label(host_id),
        REPO_ID_LABEL: _label_value(spec.repo_id),
    }


def code_open_request_name(session_id: str, request_id: str) -> str:
    """Return a deterministic mailbox request name.

    Parameters
    ----------
    session_id : str
        Session identifier.
    request_id : str
        Request identifier.

    Returns
    -------
    str
        DNS-label-safe Kubernetes object name.
    """
    digest = _hash_label(f"{session_id}\0{request_id}", chars=48)
    return f"bertrand-code-{digest}"


def code_open_session_labels(session_id: str) -> dict[str, str]:
    """Return labels selecting requests for one bridge session.

    Parameters
    ----------
    session_id : str
        Host bridge session identifier.

    Returns
    -------
    dict[str, str]
        Label selector mapping.
    """
    return {
        CODE_OPEN_LABEL: CODE_OPEN_LABEL_VALUE,
        CODE_OPEN_SESSION_LABEL: _label_value(session_id),
    }


def code_open_host_labels(host_id: str) -> dict[str, str]:
    """Return labels selecting requests for one Bertrand host.

    Parameters
    ----------
    host_id : str
        Host identity.

    Returns
    -------
    dict[str, str]
        Label selector mapping.
    """
    return {
        CODE_OPEN_LABEL: CODE_OPEN_LABEL_VALUE,
        CODE_OPEN_HOST_LABEL: _hash_label(host_id),
    }


def _now() -> str:
    return datetime.now(UTC).isoformat()


def _label_value(value: str) -> str:
    text = value.strip()
    if len(text) <= 63:
        return text
    return _hash_label(text)


def _hash_label(value: str, *, chars: int = 16) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:chars]
