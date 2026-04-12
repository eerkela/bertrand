"""Shared utility helpers for Bertrand's Kubernetes runtime assembly."""
from __future__ import annotations

import base64
import binascii
import re
from decimal import Decimal, InvalidOperation
from typing import Annotated, Self

from pydantic import BaseModel, ConfigDict, Field, StrictStr, ValidationError

from ..config.core import KubeName, NonEmpty, Trimmed
from ..run import BERTRAND_NAMESPACE, kubectl

QUANTITY_RE = re.compile(r"^([0-9]+(?:\.[0-9]+)?)([A-Za-z]{0,2})$")
STORAGE_FACTORS: dict[str, Decimal] = {
    "": Decimal(1),
    "m": Decimal("0.001"),
    "k": Decimal(10) ** 3,
    "K": Decimal(10) ** 3,
    "M": Decimal(10) ** 6,
    "G": Decimal(10) ** 9,
    "T": Decimal(10) ** 12,
    "P": Decimal(10) ** 15,
    "E": Decimal(10) ** 18,
    "Ki": Decimal(2) ** 10,
    "Mi": Decimal(2) ** 20,
    "Gi": Decimal(2) ** 30,
    "Ti": Decimal(2) ** 40,
    "Pi": Decimal(2) ** 50,
    "Ei": Decimal(2) ** 60,
}


def parse_pvc_size(value: str) -> Decimal:
    """Parse a Kubernetes PVC request size string into a Decimal value.

    Parameters
    ----------
    value : str
        Kubernetes PVC request size string (e.g., "1Gi", "500Mi").

    Returns
    -------
    Decimal
        The parsed size as a Decimal value.

    Raises
    ------
    ValueError
        If the input string is not a valid Kubernetes PVC request size.
    """
    match = QUANTITY_RE.fullmatch(value.strip())
    if not match:
        raise ValueError(f"invalid Kubernetes PVC request size: {value!r}")

    number, suffix = match.groups()
    factor = STORAGE_FACTORS.get(suffix)
    if factor is None:
        raise ValueError(
            f"invalid Kubernetes memory unit for PVC request: {suffix!r} (options are "
            f"{', '.join(repr(s) for s in STORAGE_FACTORS)})"
        )

    try:
        return Decimal(number) * factor
    except (InvalidOperation, ValueError) as err:
        raise ValueError(
            f"invalid Kubernetes memory quantity for PVC request: {value!r}"
        ) from err


class KubeMetadata(BaseModel):
    """Generic metadata subset shared by Kubernetes payload models."""
    model_config = ConfigDict(extra="ignore")
    name: Trimmed = ""
    labels: Annotated[dict[Trimmed, Trimmed], Field(default_factory=dict)]
    annotations: Annotated[dict[Trimmed, Trimmed], Field(default_factory=dict)]
    deletionTimestamp: Annotated[Trimmed | None, Field(default=None)]


class KubeSecret(BaseModel):
    """Validated subset of a Kubernetes Secret payload."""
    model_config = ConfigDict(extra="ignore")
    metadata: Annotated[KubeMetadata, Field(default_factory=KubeMetadata.model_construct)]
    data: Annotated[dict[StrictStr, StrictStr], Field(default_factory=dict)]

    class List(BaseModel):
        """Validated subset of a Kubernetes Secret list payload."""
        model_config = ConfigDict(extra="ignore")
        items: Annotated[list[KubeSecret], Field(default_factory=list)]

    @classmethod
    async def get(cls, name: KubeName, timeout: float) -> Self | None:
        """Load a Kubernetes Secret by name and validate its structure.

        Parameters
        ----------
        name : str
            The name of the Kubernetes Secret, which must be lowercase with `-` and/or
            `.` separators.
        timeout : float
            The maximum time to wait for the Kubernetes Secret to be retrieved, in
            seconds.

        Returns
        -------
        KubeSecret | None
            The validated Kubernetes Secret data, or `None` if the Secret is not found.
        """
        payload = (await kubectl(
            [
                "get",
                "secret",
                name,
                "-n", BERTRAND_NAMESPACE,
                "-o", "json",
                "--ignore-not-found=true",
            ],
            capture_output=True,
            timeout=timeout,
        )).stdout.strip()
        if not payload:
            return None
        try:
            return cls.model_validate_json(payload)
        except ValidationError as err:
            raise OSError(
                f"cluster secret {name!r} returned malformed JSON payload"
            ) from err

    def decode(self, name: KubeName) -> bytes:
        """Decode a base64-encoded value from the validated Kube Secret.

        Parameters
        ----------
        name : str
            The name of the Kubernetes Secret, which must be lowercase with `-` and/or
            `.` separators.  Used for error messages.

        Returns
        -------
        bytes
            The decoded value of the specified key, as raw bytes.

        Raises
        ------
        OSError
            If the specified key is not defined in the Secret data, or if the value is
            not valid base64-encoded data.
        """
        value = self.data.get("value")
        if value is None:
            raise OSError(
                f"cluster secret {name!r} does not define required key 'data.value'"
            )
        try:
            return base64.b64decode(value, validate=True)
        except (binascii.Error, ValueError) as err:
            raise OSError(
                f"cluster secret {name!r} contains invalid base64 data for key "
                f"'data.value'"
            ) from err


class StorageClass(BaseModel):
    """Validated subset of one Kubernetes StorageClass."""
    model_config = ConfigDict(extra="ignore")
    metadata: Annotated[KubeMetadata, Field(default_factory=KubeMetadata.model_construct)]
    provisioner: Annotated[Trimmed, Field(default="")]
    allowVolumeExpansion: Annotated[bool, Field(default=False)]

    class List(BaseModel):
        """Validated subset of a Kubernetes StorageClass list payload."""
        model_config = ConfigDict(extra="ignore")
        items: Annotated[list[StorageClass], Field(default_factory=list)]


class PersistentVolumeClaim(BaseModel):
    """Validated subset of one Kubernetes PersistentVolumeClaim payload."""
    class Spec(BaseModel):
        """Validated subset of one PVC spec."""
        class Resources(BaseModel):
            """Validated subset of PVC resources."""
            class Requests(BaseModel):
                """Validated subset of PVC request resources."""
                model_config = ConfigDict(extra="ignore")
                storage: NonEmpty[Trimmed]

            model_config = ConfigDict(extra="ignore")
            requests: PersistentVolumeClaim.Spec.Resources.Requests

        model_config = ConfigDict(extra="ignore")
        accessModes: Annotated[list[Trimmed], Field(default_factory=list)]
        storageClassName: Annotated[Trimmed | None, Field(default=None)]
        resources: PersistentVolumeClaim.Spec.Resources

    model_config = ConfigDict(extra="ignore")
    metadata: Annotated[KubeMetadata, Field(default_factory=KubeMetadata.model_construct)]
    spec: PersistentVolumeClaim.Spec

    class List(BaseModel):
        """Validated subset of a Kubernetes PersistentVolumeClaim list payload."""
        model_config = ConfigDict(extra="ignore")
        items: Annotated[list[PersistentVolumeClaim], Field(default_factory=list)]


class Pod(BaseModel):
    """Validated subset of one Kubernetes Pod payload."""
    class Volume(BaseModel):
        """Validated subset of one pod volume entry."""
        class Ref(BaseModel):
            """Validated subset of one pod PVC reference."""
            model_config = ConfigDict(extra="ignore")
            claimName: Trimmed

        model_config = ConfigDict(extra="ignore")
        persistentVolumeClaim: Annotated[Pod.Volume.Ref | None, Field(default=None)]

    class Status(BaseModel):
        """Validated subset of one pod status payload."""
        model_config = ConfigDict(extra="ignore")
        phase: Annotated[Trimmed, Field(default="")]

    class Spec(BaseModel):
        """Validated subset of one pod spec."""
        model_config = ConfigDict(extra="ignore")
        volumes: Annotated[list[Pod.Volume], Field(default_factory=list)]

    model_config = ConfigDict(extra="ignore")
    metadata: Annotated[KubeMetadata, Field(default_factory=KubeMetadata.model_construct)]
    status: Annotated[Pod.Status, Field(default_factory=lambda: Pod.Status.model_construct())]
    spec: Annotated[Pod.Spec, Field(default_factory=lambda: Pod.Spec.model_construct())]

    class List(BaseModel):
        """Validated subset of a Kubernetes pod list payload."""
        model_config = ConfigDict(extra="ignore")
        items: Annotated[list[Pod], Field(default_factory=list)]
