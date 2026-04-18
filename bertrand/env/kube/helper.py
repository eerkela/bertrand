"""Shared utility helpers for Bertrand's Kubernetes runtime assembly."""
from __future__ import annotations

import asyncio
import base64
import binascii
import json
import re
from decimal import Decimal, InvalidOperation
from typing import Annotated, Self

from pydantic import BaseModel, ConfigDict, Field, StrictStr, ValidationError

from ..config.core import KubeName, NonEmpty, Trimmed
from ..run import CommandError, JSONValue, kubectl, run

PVC_GROW_RETRIES = 4
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


async def enable_addon(name: str, *, timeout: float | None) -> None:
    """Enable a Kubernetes addon by name, if not already enabled.

    Parameters
    ----------
    name : str
        The name of the addon to enable.
    timeout : float | None
        Maximum runtime command timeout in seconds.  If None, wait indefinitely.

    Raises
    ------
    CommandError
        If the addon cannot be enabled.
    """
    try:
        await run(
            ["microk8s", "enable", name],
            capture_output=True,
            timeout=timeout,
        )
    except CommandError as err:
        detail = f"{err.stdout}\n{err.stderr}".strip().lower()
        if "already enabled" not in detail and "alreadyenabled" not in detail:
            raise


class KubeMetadata(BaseModel):
    """Generic metadata subset shared by Kubernetes payload models."""
    model_config = ConfigDict(extra="ignore")
    name: Trimmed = ""
    namespace: Trimmed = ""
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
        async def get(
            cls,
            labels: dict[str, str],
            *,
            namespace: str,
            timeout: float | None
        ) -> Self:
            """Load all Kubernetes Secrets and validate their structure.

            Parameters
            ----------
            labels : dict[str, str]
                The labels to filter the Kubernetes Secrets by.
            namespace : str
                The namespace to search within.
            timeout : float | None
                The maximum time to wait for the Kubernetes Secrets to be retrieved, in
                seconds.  If None, wait indefinitely.

            Returns
            -------
            KubeSecret.List
                The validated list of Kubernetes Secrets.

            Raises
            ------
            CommandError
                If the `kubectl get` command fails to execute or returns an error.
            ValueError
                If the retrieved Kubernetes Secret list payload does not conform to the
                expected structure, or if the specified Secret is not found in the list.
            """
            cmd = ["get", "secret", "-n", namespace]
            if labels:
                selector = ",".join(f"{k}={v}" for k, v in labels.items())
                cmd.extend(["-l", selector])
            cmd.extend(["-o", "json"])
            result = await kubectl(
                cmd,
                capture_output=True,
                timeout=timeout
            )
            return cls.model_validate_json(result.stdout.strip())

    @classmethod
    async def get(
        cls,
        name: KubeName,
        *,
        namespace: str,
        timeout: float | None
    ) -> Self | None:
        """Load a Kubernetes Secret by name and validate its structure.

        Parameters
        ----------
        name : str
            The name of the Kubernetes Secret, which must be lowercase with `-` and/or
            `.` separators.
        namespace : str
            The namespace to search within.
        timeout : float | None
            The maximum time to wait for the Kubernetes Secret to be retrieved, in
            seconds.  If None, wait indefinitely.

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
                "-n", namespace,
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

        @classmethod
        async def get(
            cls,
            labels: dict[str, str],
            *,
            timeout: float | None
        ) -> StorageClass.List:
            """Load all Kubernetes StorageClasses and validate their structure.

            Parameters
            ----------
            labels : dict[str, str]
                The labels to filter the Kubernetes StorageClasses by.
            timeout : float | None
                The maximum time to wait for the Kubernetes StorageClasses to be
                retrieved, in seconds.  If None, wait indefinitely.

            Returns
            -------
            StorageClass.List
                The validated list of Kubernetes StorageClasses.

            Raises
            ------
            CommandError
                If the `kubectl get` command fails to execute or returns an error.
            ValueError
                If the retrieved Kubernetes StorageClass list payload does not conform to
                the expected structure, or if the specified StorageClass is not found in
                the list.
            """
            cmd = ["get", "storageclass"]
            if labels:
                selector = ",".join(f"{k}={v}" for k, v in labels.items())
                cmd.extend(["-l", selector])
            cmd.extend(["-o", "json"])
            result = await kubectl(
                cmd,
                capture_output=True,
                timeout=timeout
            )
            return cls.model_validate_json(result.stdout.strip())

    @classmethod
    async def get(cls, name: KubeName, timeout: float | None) -> Self | None:
        """Load a Kubernetes StorageClass by name and validate its structure.

        Parameters
        ----------
        name : str
            The name of the Kubernetes StorageClass, which must be lowercase with `-`
            and/or `.` separators.
        timeout : float | None
            The maximum time to wait for the Kubernetes StorageClass to be retrieved, in
            seconds.  If None, wait indefinitely.

        Returns
        -------
        StorageClass | None
            The validated Kubernetes StorageClass data, or `None` if the StorageClass is
            not found.

        Raises
        ------
        CommandError
            If the `kubectl get` command fails to execute or returns an error.
        ValueError
            If the retrieved Kubernetes StorageClass payload does not conform to the
            expected structure.
        """
        payload = (await kubectl(
            [
                "get",
                "storageclass",
                name,
                "-o", "json",
                "--ignore-not-found=true",
            ],
            capture_output=True,
            timeout=timeout,
        )).stdout.strip()
        if payload:
            return cls.model_validate_json(payload)
        return None


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
        volumeName: Annotated[Trimmed | None, Field(default=None)]
        resources: PersistentVolumeClaim.Spec.Resources

    model_config = ConfigDict(extra="ignore")
    metadata: Annotated[KubeMetadata, Field(default_factory=KubeMetadata.model_construct)]
    spec: PersistentVolumeClaim.Spec

    class List(BaseModel):
        """Validated subset of a Kubernetes PersistentVolumeClaim list payload."""
        model_config = ConfigDict(extra="ignore")
        items: Annotated[list[PersistentVolumeClaim], Field(default_factory=list)]

        @classmethod
        async def get(
            cls,
            labels: dict[str, str],
            *,
            namespace: str,
            timeout: float | None,
        ) -> Self:
            """Load all Kubernetes PersistentVolumeClaims and validate their structure.

            Parameters
            ----------
            labels : dict[str, str]
                A dictionary of label key-value pairs to filter the
                PersistentVolumeClaims by.  Only PVCs that have all of the specified
                labels with matching values will be included in the results.
            namespace : str
                The namespace to search within.
            timeout : float | None
                The maximum time to wait for the Kubernetes PersistentVolumeClaims to
                be retrieved, in seconds.  If None, wait indefinitely.

            Returns
            -------
            PersistentVolumeClaim.List
                The validated list of Kubernetes PersistentVolumeClaims.

            Raises
            ------
            CommandError
                If the `kubectl get` command fails to execute or returns an error.
            ValueError
                If the retrieved Kubernetes PersistentVolumeClaim list payload does not
                conform to the expected structure, or if the specified
                PersistentVolumeClaim is not found in the list.
            """
            cmd = ["get", "pvc", "-n", namespace]
            if labels:
                selector = ",".join(f"{k}={v}" for k, v in labels.items())
                cmd.extend(["-l", selector])
            cmd.extend(["-o", "json"])
            result = await kubectl(
                cmd,
                capture_output=True,
                timeout=timeout
            )
            return cls.model_validate_json(result.stdout.strip())

    @classmethod
    async def get(
        cls,
        name: KubeName,
        *,
        namespace: str,
        timeout: float | None,
    ) -> Self | None:
        """Load a Kubernetes PersistentVolumeClaim by name and validate its structure.

        Parameters
        ----------
        name : str
            The name of the Kubernetes PersistentVolumeClaim, which must be lowercase
            with `-` and/or `.` separators.
        namespace : str
            The namespace to search within.
        timeout : float | None
            The maximum time to wait for the Kubernetes PersistentVolumeClaim to be
            retrieved, in seconds.  If None, wait indefinitely.

        Returns
        -------
        PersistentVolumeClaim | None
            The validated Kubernetes PersistentVolumeClaim data, or `None` if the PVC
            is not found.

        Raises
        ------
        CommandError
            If the `kubectl get` command fails to execute or returns an error.
        ValueError
            If the retrieved Kubernetes PersistentVolumeClaim payload does not conform
            to the expected structure.
        """
        payload = (await kubectl(
            [
                "get",
                "pvc",
                name,
                "-n", namespace,
                "-o", "json",
                "--ignore-not-found=true",
            ],
            capture_output=True,
            timeout=timeout,
        )).stdout.strip()
        if payload:
            return cls.model_validate_json(payload)
        return None

    @classmethod
    async def create(
        cls,
        data: dict[str, JSONValue],
        *,
        timeout: float | None,
    ) -> Self:
        """Create a Kubernetes PersistentVolumeClaim with the specified parameters.

        Parameters
        ----------
        data : dict[str, JSONValue]
            The payload to send to the Kubernetes PersistentVolumeClaim API.
        timeout : float | None
            The maximum time to wait for the Kubernetes PersistentVolumeClaim to be
            created, in seconds.  If None, wait indefinitely.

        Returns
        -------
        PersistentVolumeClaim
            The validated Kubernetes PersistentVolumeClaim data for the newly created PVC.

        Raises
        ------
        CommandError
            If the `kubectl create` command fails to execute or returns an error.
        ValueError
            If the retrieved Kubernetes PersistentVolumeClaim payload does not conform
            to the expected structure.
        """
        metadata = data.get("metadata")
        name = ""
        namespace = ""
        if isinstance(metadata, dict):
            model = KubeMetadata.model_validate(metadata)
            name = model.name
            namespace = model.namespace

        # attempt to create PVC and parse returned payload
        loop = asyncio.get_event_loop()
        deadline = None if timeout is None else loop.time() + timeout
        try:
            payload = (await kubectl(
                ["create", "-o", "json", "-f", "-"],
                input=json.dumps(data, separators=(",", ":"), ensure_ascii=False),
                capture_output=True,
                timeout=None if deadline is None else deadline - loop.time(),
            )).stdout.strip()
            if payload:
                return cls.model_validate_json(payload)
        except CommandError as err:
            detail = f"{err.stdout}\n{err.stderr}".lower()
            if (
                "already exists" not in detail and
                "alreadyexists" not in detail and
                "conflict" not in detail
            ):
                raise

        # race condition; attempt to retrieve existing PVC
        if name and namespace:
            created = await cls.get(
                name,
                namespace=namespace,
                timeout=None if deadline is None else deadline - loop.time()
            )
            if created is not None:
                return created
        raise OSError(
            "kubernetes accepted PVC creation, but no valid PVC payload was returned"
        )

    async def grow(
        self,
        requested: str,
        *,
        timeout: float | None,
    ) -> None:
        """Resize the PVC if the current size is smaller than the requested size.

        Parameters
        ----------
        requested : str
            The requested size for the PVC, as a Kubernetes PVC request size string
            (e.g., "1Gi", "500Mi").
        timeout : float | None
            The maximum time to wait for the Kubernetes PersistentVolumeClaim to be
            resized, in seconds.  If None, wait indefinitely.

        Raises
        ------
        CommandError
            If the `kubectl patch` command fails to execute or returns an error during
            resizing.
        OSError
            If the PVC disappears during resize, if metadata is incomplete, or if size
            convergence could not be confirmed within retry limits.
        """
        new_size = parse_pvc_size(requested)
        name = self.metadata.name
        namespace = self.metadata.namespace
        if not name:
            raise OSError("cannot resize PVC with missing metadata.name")
        if not namespace:
            raise OSError(f"cannot resize PVC {name!r} with missing metadata.namespace")
        patch = json.dumps(
            {"spec": {"resources": {"requests": {"storage": requested}}}},
            separators=(",", ":"),
            ensure_ascii=False,
        )

        loop = asyncio.get_event_loop()
        deadline = None if timeout is None else loop.time() + timeout
        for attempt in range(PVC_GROW_RETRIES):
            live = await type(self).get(
                name,
                namespace=namespace,
                timeout=None if deadline is None else deadline - loop.time()
            )
            if live is None:
                raise OSError(
                    f"PVC {name!r} disappeared during resize lifecycle"
                )

            current_size = parse_pvc_size(live.spec.resources.requests.storage)
            if current_size >= new_size:
                return

            try:
                await kubectl(
                    [
                        "patch",
                        "pvc",
                        name,
                        "-n", namespace,
                        "--type", "merge",
                        "-p", patch,
                    ],
                    capture_output=True,
                    timeout=None if deadline is None else deadline - loop.time(),
                )
            except CommandError as err:
                detail = f"{err.stdout}\n{err.stderr}".lower()
                if "not found" in detail or "notfound" in detail:
                    raise OSError(
                        f"PVC {name!r} disappeared during resize lifecycle"
                    ) from err
                if (
                    "conflict" in detail or
                    "the object has been modified" in detail
                ) and attempt + 1 < PVC_GROW_RETRIES:
                    continue
                raise

            live = await type(self).get(
                name,
                namespace=namespace,
                timeout=None if deadline is None else deadline - loop.time()
            )
            if live is None:
                raise OSError(
                    f"PVC {name!r} disappeared during resize lifecycle"
                )
            current_size = parse_pvc_size(live.spec.resources.requests.storage)
            if current_size >= new_size:
                return
            if attempt + 1 < PVC_GROW_RETRIES:
                continue
            raise OSError(
                f"PVC {name!r} did not converge to requested size {requested!r} "
                f"after {PVC_GROW_RETRIES} attempts"
            )

    async def delete(self, *, timeout: float | None) -> None:
        """Delete the PVC from the cluster.

        Parameters
        ----------
        timeout : float | None
            The maximum time to wait for the Kubernetes PersistentVolumeClaim to be
            deleted, in seconds.  If None, wait indefinitely.

        Raises
        ------
        CommandError
            If the `kubectl delete` command fails to execute or returns an error during
            deletion.
        """
        await kubectl(
            [
                "delete",
                "pvc",
                self.metadata.name,
                "-n", self.metadata.namespace,
                "--ignore-not-found=true",
            ],
            capture_output=True,
            timeout=timeout,
        )


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

        @classmethod
        async def get(
            cls,
            labels: dict[str, str],
            *,
            namespace: str,
            timeout: float | None,
        ) -> Self:
            """Load all Kubernetes pods and validate their structure.

            Parameters
            ----------
            labels : dict[str, str]
                The labels to filter the Kubernetes pods by.
            namespace : str
                The namespace to search within.
            timeout : float | None
                The maximum time to wait for the Kubernetes pods to be retrieved, in
                seconds.  If None, wait indefinitely.

            Returns
            -------
            Pod.List
                The validated list of Kubernetes pods.

            Raises
            ------
            CommandError
                If the `kubectl get` command fails to execute or returns an error.
            ValueError
                If the retrieved Kubernetes pod list payload does not conform to the
                expected structure, or if the specified pod is not found in the list.
            """
            cmd = ["get", "pod", "-n", namespace]
            if labels:
                selector = ",".join(f"{k}={v}" for k, v in labels.items())
                cmd.extend(["-l", selector])
            cmd.extend(["-o", "json"])
            result = await kubectl(
                cmd,
                capture_output=True,
                timeout=timeout,
            )
            return cls.model_validate_json(result.stdout.strip())


class PersistentVolume(BaseModel):
    """Validated subset of one Kubernetes PersistentVolume payload."""
    class Spec(BaseModel):
        """Validated subset of one PV spec."""
        class CSI(BaseModel):
            """Validated subset of one CSI-backed PV source."""
            model_config = ConfigDict(extra="ignore")
            driver: Annotated[Trimmed, Field(default="")]
            volumeAttributes: Annotated[dict[Trimmed, Trimmed], Field(default_factory=dict)]

        model_config = ConfigDict(extra="ignore")
        csi: Annotated[PersistentVolume.Spec.CSI | None, Field(default=None)]

    model_config = ConfigDict(extra="ignore")
    metadata: Annotated[KubeMetadata, Field(default_factory=KubeMetadata.model_construct)]
    spec: PersistentVolume.Spec

    @classmethod
    async def get(cls, name: KubeName, *, timeout: float | None) -> Self | None:
        """Load a Kubernetes PersistentVolume by name and validate its structure.

        Parameters
        ----------
        name : str
            The name of the Kubernetes PersistentVolume.
        timeout : float | None
            The maximum time to wait for the Kubernetes PersistentVolume to be
            retrieved, in seconds.  If None, wait indefinitely.

        Returns
        -------
        PersistentVolume | None
            The validated Kubernetes PersistentVolume data, or `None` if not found.
        """
        payload = (await kubectl(
            [
                "get",
                "pv",
                name,
                "-o", "json",
                "--ignore-not-found=true",
            ],
            capture_output=True,
            timeout=timeout,
        )).stdout.strip()
        if payload:
            return cls.model_validate_json(payload)
        return None


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
