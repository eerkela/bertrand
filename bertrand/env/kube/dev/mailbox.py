"""Kubernetes mailbox resources for Bertrand host/editor dev requests."""

from __future__ import annotations

import asyncio
import hashlib
import time
import uuid
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import TYPE_CHECKING, Literal

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from bertrand.env.git import BERTRAND_NAMESPACE, REPO_ID_ENV, Deadline
from bertrand.env.kube.crd import CustomResourceDefinition
from bertrand.env.kube.custom_object import (
    CustomObjectClient,
    CustomObjectMetadata,
    CustomObjectSpec,
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
CODE_OPEN_WORKTREE_LABEL = "bertrand.dev/code-open-worktree"
CODE_OPEN_PHASE_LABEL = "bertrand.dev/code-open-phase"

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
_CODE_OPEN_CLIENT = CustomObjectClient(
    CustomObjectSpec(
        group=DEV_GROUP,
        version=DEV_VERSION,
        kind=CODE_OPEN_KIND,
        plural=CODE_OPEN_PLURAL,
        labels=_CODE_OPEN_LABELS,
    )
)
_CODE_OPEN_SPEC_SCHEMA: dict[str, object] = {
    "type": "object",
    "required": [
        "session_id",
        "request_id",
        "repo_id",
        "worktree",
        "pod_name",
        "container_name",
        "workspace_path",
        "editor",
        "block",
        "deadline",
    ],
    "properties": {
        "session_id": {"type": "string", "minLength": 1},
        "request_id": {"type": "string", "minLength": 1},
        "repo_id": {"type": "string", "minLength": 1},
        "worktree": {"type": "string", "minLength": 1},
        "pod_name": {"type": "string", "minLength": 1},
        "container_name": {"type": "string", "minLength": 1},
        "workspace_path": {"type": "string", "minLength": 1},
        "editor": {"type": "string", "minLength": 1},
        "block": {"type": "boolean"},
        "deadline": {"type": "number"},
    },
}
_CODE_OPEN_STATUS_SCHEMA: dict[str, object] = {
    "type": "object",
    "properties": {
        "phase": {"type": "string"},
        "host_id": {"type": "string"},
        "accepted_at": {"type": "string"},
        "completed_at": {"type": "string"},
        "message": {"type": "string"},
    },
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
            ``True`` when the current wall clock is past ``deadline``.
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
            ``True`` for ``Succeeded``, ``Failed``, or ``Expired``.
        """
        return self.phase in ("Succeeded", "Failed", "Expired")


class CodeOpenRecord(BaseModel):
    """Validated Kubernetes ``CodeOpenRequest`` custom object."""

    model_config = ConfigDict(extra="ignore", frozen=True, populate_by_name=True)
    api_version: str = Field(alias="apiVersion")
    kind: Literal["CodeOpenRequest"]
    metadata: CustomObjectMetadata
    spec: CodeOpenSpec
    status: CodeOpenStatus = Field(default_factory=CodeOpenStatus)

    @classmethod
    def from_payload(cls, payload: object) -> CodeOpenRecord:
        """Validate a Kubernetes custom-object payload.

        Parameters
        ----------
        payload : object
            Raw Kubernetes custom-object payload.

        Returns
        -------
        CodeOpenRecord
            Validated mailbox record.

        Raises
        ------
        OSError
            If the payload is malformed or labels do not match the spec.
        """
        try:
            record = cls.model_validate(payload)
        except ValidationError as err:
            msg = f"malformed {CODE_OPEN_KIND} custom object: {err}"
            raise OSError(msg) from err
        expected = code_open_request_name(
            record.spec.session_id,
            record.spec.request_id,
        )
        if record.name != expected:
            msg = (
                f"malformed {CODE_OPEN_KIND} {record.name!r}: expected "
                f"deterministic name {expected!r}"
            )
            raise OSError(msg)
        labels = record.metadata.labels
        if labels.get(CODE_OPEN_LABEL) != CODE_OPEN_LABEL_VALUE:
            msg = f"malformed {CODE_OPEN_KIND} {record.name!r}: missing dev label"
            raise OSError(msg)
        if labels.get(CODE_OPEN_SESSION_LABEL) != _label_value(record.spec.session_id):
            msg = (
                f"malformed {CODE_OPEN_KIND} {record.name!r}: session label does "
                "not match spec"
            )
            raise OSError(msg)
        if labels.get(CODE_OPEN_REQUEST_LABEL) != _label_value(record.spec.request_id):
            msg = (
                f"malformed {CODE_OPEN_KIND} {record.name!r}: request label does "
                "not match spec"
            )
            raise OSError(msg)
        return record

    @property
    def name(self) -> str:
        """Return the Kubernetes custom-object name.

        Returns
        -------
        str
            Kubernetes object name.
        """
        return self.metadata.name

    @property
    def namespace(self) -> str:
        """Return the namespace that owns this request.

        Returns
        -------
        str
            Kubernetes object namespace.
        """
        return self.metadata.namespace


@dataclass(frozen=True)
class CodeOpenIntent:
    """Intent for creating one editor-open mailbox request.

    Parameters
    ----------
    session_id : str
        Active host bridge session identifier.
    repo_id : str
        Stable Bertrand repository UUID.
    worktree : str
        Repository-relative worktree path.
    pod_name : str
        Kubernetes Pod name requesting the editor.
    container_name : str
        Container inside the Pod that owns the workspace.
    workspace_path : str
        Container path to open in the editor.
    editor : str
        Editor alias selected from Bertrand config.
    block : bool
        Whether the requester waits for editor lifetime completion.
    deadline : float
        Unix timestamp deadline for request handling.
    host_id : str
        Host identity expected to service this request.
    request_id : str
        Unique request ID.  Defaults to a random UUID hex value.
    """

    session_id: str
    repo_id: str
    worktree: str
    pod_name: str
    container_name: str
    workspace_path: str
    editor: str
    block: bool
    deadline: float
    host_id: str
    request_id: str = ""

    def __post_init__(self) -> None:
        """Normalize the request ID."""
        if not self.request_id:
            object.__setattr__(self, "request_id", uuid.uuid4().hex)

    @property
    def name(self) -> str:
        """Return the deterministic request object name.

        Returns
        -------
        str
            Kubernetes custom-object name.
        """
        return code_open_request_name(self.session_id, self.request_id)

    @property
    def spec(self) -> dict[str, object]:
        """Return this intent as a Kubernetes ``spec`` payload.

        Returns
        -------
        dict[str, object]
            Custom-resource spec payload.
        """
        return {
            "session_id": self.session_id,
            "request_id": self.request_id,
            "repo_id": self.repo_id,
            "worktree": self.worktree,
            "pod_name": self.pod_name,
            "container_name": self.container_name,
            "workspace_path": self.workspace_path,
            "editor": self.editor,
            "block": self.block,
            "deadline": self.deadline,
        }

    @property
    def labels(self) -> dict[str, str]:
        """Return Kubernetes labels for this request.

        Returns
        -------
        dict[str, str]
            Label selector values used by the host bridge and cleanup paths.
        """
        return {
            CODE_OPEN_SESSION_LABEL: _label_value(self.session_id),
            CODE_OPEN_REQUEST_LABEL: _label_value(self.request_id),
            CODE_OPEN_HOST_LABEL: _hash_label(self.host_id),
            CODE_OPEN_WORKTREE_LABEL: _hash_label(self.worktree),
            CODE_OPEN_PHASE_LABEL: "pending",
            REPO_ID_ENV: _label_value(self.repo_id),
        }


async def ensure_code_open_request_crd(kube: Kube, *, timeout: float) -> None:
    """Converge the ``CodeOpenRequest`` CRD.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum convergence budget in seconds.

    Raises
    ------
    TimeoutError
        If CRD establishment exceeds the budget.
    """
    if timeout <= 0:
        msg = "CodeOpenRequest CRD timeout must be non-negative"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(
        timeout,
        message="CodeOpenRequest CRD timeout must be non-negative",
    )
    crd = await CustomResourceDefinition.upsert(
        kube,
        group=DEV_GROUP,
        version=DEV_VERSION,
        plural=CODE_OPEN_PLURAL,
        singular="codeopenrequest",
        kind=CODE_OPEN_KIND,
        short_names=("cor",),
        spec_schema=_CODE_OPEN_SPEC_SCHEMA,
        status_schema=_CODE_OPEN_STATUS_SCHEMA,
        labels=_CODE_OPEN_LABELS,
        timeout=deadline.remaining(),
    )
    await crd.wait_established(kube, timeout=deadline.remaining())


async def create_code_open_request(
    kube: Kube,
    *,
    intent: CodeOpenIntent,
    timeout: float,
) -> CodeOpenRecord:
    """Create one editor-open mailbox request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    intent : CodeOpenIntent
        Request creation intent.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    CodeOpenRecord
        Created mailbox record.
    """
    obj = await _CODE_OPEN_CLIENT.create(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=intent.name,
        spec=intent.spec,
        labels=intent.labels,
        timeout=timeout,
    )
    return CodeOpenRecord.from_payload(obj.payload)


async def get_code_open_request(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> CodeOpenRecord | None:
    """Read one editor-open mailbox request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        Request object name.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    CodeOpenRecord | None
        Wrapped record, or ``None`` if it does not exist.
    """
    obj = await _CODE_OPEN_CLIENT.get(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=name,
        timeout=timeout,
    )
    return None if obj is None else CodeOpenRecord.from_payload(obj.payload)


async def list_code_open_requests(
    kube: Kube,
    *,
    timeout: float,
    labels: Mapping[str, str] | None = None,
) -> list[CodeOpenRecord]:
    """List editor-open mailbox requests.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    timeout : float
        Maximum request budget in seconds.
    labels : Mapping[str, str] | None, optional
        Optional label selector.

    Returns
    -------
    list[CodeOpenRecord]
        Validated mailbox records matching the selector.
    """
    objects = await _CODE_OPEN_CLIENT.list(
        kube,
        namespace=BERTRAND_NAMESPACE,
        labels=labels,
        timeout=timeout,
    )
    return [CodeOpenRecord.from_payload(obj.payload) for obj in objects]


async def patch_code_open_request_status(
    kube: Kube,
    *,
    record: CodeOpenRecord,
    phase: CodeOpenPhase,
    host_id: str = "",
    message: str = "",
    timeout: float,
) -> CodeOpenRecord:
    """Patch one editor-open request status.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    record : CodeOpenRecord
        Existing mailbox record.
    phase : CodeOpenPhase
        New lifecycle phase.
    host_id : str, optional
        Host identity servicing the request.
    message : str, optional
        Human-readable status diagnostic.
    timeout : float
        Maximum request budget in seconds.

    Returns
    -------
    CodeOpenRecord
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
    obj = await _CODE_OPEN_CLIENT.patch_status(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=record.name,
        status=status,
        timeout=timeout,
    )
    return CodeOpenRecord.from_payload(obj.payload)


async def delete_code_open_request(
    kube: Kube,
    *,
    record: CodeOpenRecord,
    timeout: float,
) -> None:
    """Delete one editor-open mailbox request.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    record : CodeOpenRecord
        Request record to delete.
    timeout : float
        Maximum request budget in seconds.
    """
    await _CODE_OPEN_CLIENT.delete_by_name(
        kube,
        namespace=BERTRAND_NAMESPACE,
        name=record.name,
        timeout=timeout,
    )


async def wait_code_open_request(
    kube: Kube,
    *,
    name: str,
    timeout: float,
) -> CodeOpenRecord:
    """Wait until one editor-open request reaches a terminal phase.

    Parameters
    ----------
    kube : Kube
        Active Kubernetes API context.
    name : str
        Request object name.
    timeout : float
        Maximum wait budget in seconds.

    Returns
    -------
    CodeOpenRecord
        Terminal request record.

    Raises
    ------
    TimeoutError
        If the request does not complete before ``timeout``.
    OSError
        If the request disappears before completion.
    """
    if timeout <= 0:
        msg = f"timed out waiting for {CODE_OPEN_KIND} {name!r}"
        raise TimeoutError(msg)
    deadline = Deadline.from_timeout(
        timeout,
        message=f"timed out waiting for {CODE_OPEN_KIND} {name!r}",
    )
    while True:
        remaining = deadline.remaining()
        if remaining <= 0:
            msg = (
                "No host Bertrand editor bridge accepted the request before the "
                "deadline. Run `bertrand enter <project>` from your host shell, "
                "then run `bertrand code` inside that session."
            )
            raise TimeoutError(msg)
        record = await get_code_open_request(kube, name=name, timeout=remaining)
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
                timeout=remaining,
            )
        await asyncio.sleep(deadline.bounded(0.5))


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
