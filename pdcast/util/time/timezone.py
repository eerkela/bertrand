# pylint: disable=redefined-outer-name, unused-argument
from __future__ import annotations
import datetime
import zoneinfo

import tzlocal


def tz(tz: str | datetime.tzinfo | None, state: dict) -> datetime.tzinfo:
    """Convert a time zone specifier into a ``datetime.tzinfo`` object."""
    # naive
    if tz is None:
        return None

    # trivial case
    if isinstance(tz, datetime.tzinfo):
        return tz

    # string
    if tz.lower() == "local":
        return zoneinfo.ZoneInfo(tzlocal.get_localzone_name())
    return zoneinfo.ZoneInfo(tz)
