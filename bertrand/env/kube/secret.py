"""Wrappers for the Kubernetes Secret API and related operations."""

from __future__ import annotations

import base64
import binascii
from dataclasses import dataclass
from typing import TYPE_CHECKING

from kubernetes import client as kube_client

from .api.resource import (
    Creatable,
    Deletable,
    Listable,
    Patchable,
    Readable,
    Upsertable,
    builtin_resource,
)

if TYPE_CHECKING:
    from collections.abc import Mapping

    from bertrand.env.git import Deadline

    from .api.client import Kube


@builtin_resource(api="core", scope="namespaced")
@dataclass(frozen=True)
class Secret(
    Readable[kube_client.V1Secret],
    Listable[kube_client.V1Secret],
    Creatable[kube_client.V1Secret],
    Patchable[kube_client.V1Secret],
    Upsertable[kube_client.V1Secret],
    Deletable[kube_client.V1Secret],
):
    """General-purpose wrapper around one Kubernetes Secret object.

    Parameters
    ----------
    _obj : kube_client.V1Secret
        Typed Kubernetes Secret payload returned by the cluster API.
    """

    _obj: kube_client.V1Secret

    @classmethod
    async def upsert(
        cls,
        kube: Kube,
        *,
        namespace: str,
        name: str,
        labels: Mapping[str, str] | None,
        annotations: Mapping[str, str] | None,
        payload: bytes,
        deadline: Deadline,
    ) -> Secret:
        """Create or patch one Kubernetes Secret payload.

        Parameters
        ----------
        kube : Kube
            Active Kubernetes API context.
        namespace : str
            Namespace that owns the secret.
        name : str
            Secret name to create or patch.
        labels : Mapping[str, str] | None
            Labels to apply to `metadata.labels`.
        annotations : Mapping[str, str] | None
            Annotations to apply to `metadata.annotations`.
        payload : bytes
            Raw payload bytes to store at `data.value` (base64-encoded on write).
        deadline : Deadline
            Maximum request budget in seconds. If infinite, wait indefinitely.

        Returns
        -------
        Secret
            Wrapped created or patched Kubernetes secret.
        """
        manifest = {
            "apiVersion": "v1",
            "kind": "Secret",
            "metadata": {
                "name": name,
                "namespace": namespace,
                "labels": dict(labels or {}),
                "annotations": dict(annotations or {}),
            },
            "type": "Opaque",
            "data": {"value": base64.b64encode(payload).decode("ascii")},
        }

        return await cls.upsert_manifest(
            kube,
            namespace=namespace,
            name=name,
            manifest=manifest,
            deadline=deadline,
        )

    @property
    def value(self) -> bytes:
        """Decode the binary payload stored in the secret's `value`.

        Returns
        -------
        bytes
            Decoded payload bytes.

        Raises
        ------
        OSError
            If the secret's `value` is missing or contains invalid base64.
        """
        name = self.name
        value = (self._obj.data or {}).get("value")
        if value is None:
            msg = f"cluster secret {name!r} does not define required key 'data.value'"
            raise OSError(msg)
        try:
            return base64.b64decode(value, validate=True)
        except (binascii.Error, ValueError) as err:
            msg = (
                f"cluster secret {name!r} contains invalid base64 data for key "
                "'data.value'"
            )
            raise OSError(msg) from err
