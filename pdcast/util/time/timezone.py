# pylint: disable=redefined-outer-name, unused-argument
from __future__ import annotations

import pytz
import tzlocal


def tz(tz: str | pytz.BaseTzInfo | None, state: dict) -> pytz.BaseTzInfo:
    """Convert a time zone specifier into a ``datetime.tzinfo`` object."""
    if tz is None:
        return None

    # trivial case
    if isinstance(tz, pytz.BaseTzInfo):
        return tz

    # local specifier
    if isinstance(tz, str) and tz.lower() == "local":
        return pytz.timezone(tzlocal.get_localzone_name())

    # IANA string
    return pytz.timezone(tz)
