"""Shared Kubernetes control-plane image configuration."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from datetime import UTC, datetime, timedelta

from bertrand.env.git import Deadline

CONTROL_PLANE_IMAGE_ENV = "BERTRAND_CONTROL_PLANE_IMAGE"


# TODO: move this into the newly-unified @api/resource.py, since it's just a shared
# utility for the actual kubernetes API wrapper classes, and we want to keep the
# top-level modules limited to the actual wrapper classes themselves, with all the
# utilities moved to @api/


def _initial_maintenance_time() -> datetime:
    return datetime.min.replace(tzinfo=UTC)


@dataclass
class MaintenanceClock:
    """Track the next due time for best-effort control-loop maintenance.

    Parameters
    ----------
    next_at : datetime
        Timestamp when the maintenance task should next run.
    """

    next_at: datetime = field(default_factory=_initial_maintenance_time)

    def due(self, now: datetime) -> bool:
        """Return whether maintenance is due.

        Parameters
        ----------
        now : datetime
            Current controller timestamp.

        Returns
        -------
        bool
            Whether `now` is at or after the scheduled maintenance time.
        """
        return now >= self.next_at

    def pass_deadline(
        self,
        now: datetime,
        *,
        deadline: Deadline,
        budget: float,
    ) -> Deadline | None:
        """Return a bounded deadline when maintenance is due.

        Parameters
        ----------
        now : datetime
            Current controller timestamp.
        deadline : Deadline
            Outer controller deadline.
        budget : float
            Maximum maintenance pass budget in seconds.

        Returns
        -------
        Deadline | None
            Deadline for this maintenance pass, or `None` when maintenance is not due
            or no runtime budget remains.
        """
        if not self.due(now):
            return None
        remaining = deadline.remaining
        if remaining <= 0:
            return None
        pass_timeout = min(budget, remaining)
        if pass_timeout <= 0:
            return None
        return Deadline(pass_timeout)

    def schedule_after(self, seconds: float) -> None:
        """Schedule maintenance after a relative delay.

        Parameters
        ----------
        seconds : float
            Delay in seconds from the current UTC time.
        """
        self.next_at = datetime.now(UTC) + timedelta(seconds=seconds)

    def schedule_at(self, when: datetime) -> None:
        """Schedule maintenance at an absolute time.

        Parameters
        ----------
        when : datetime
            Absolute timestamp for the next maintenance run.
        """
        self.next_at = when

    def schedule_now(self) -> None:
        """Schedule maintenance for the next controller tick."""
        self.next_at = _initial_maintenance_time()

    def schedule_no_later_than(self, when: datetime) -> None:
        """Move the schedule earlier when the requested time is sooner.

        Parameters
        ----------
        when : datetime
            Upper bound for the next maintenance run.
        """
        if self.next_at <= datetime.now(UTC) or when < self.next_at:
            self.next_at = when


def control_plane_image() -> str:
    """Return the configured Bertrand control-plane image.

    Returns
    -------
    str
        Container image used by in-cluster Bertrand control-plane workloads.

    Raises
    ------
    ValueError
        If the image reference is missing or empty.
    """
    raw = os.environ.get(CONTROL_PLANE_IMAGE_ENV)
    if raw is None:
        msg = f"{CONTROL_PLANE_IMAGE_ENV} must be set to a pre-existing image"
        raise ValueError(msg)
    image = raw.strip()
    if not image:
        msg = f"{CONTROL_PLANE_IMAGE_ENV} cannot be empty"
        raise ValueError(msg)
    return image
