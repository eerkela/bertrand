# pylint: disable=redefined-outer-name, unused-argument
from __future__ import annotations
import datetime
import zoneinfo

import pytz
import tzlocal


pytz.BaseTzInfo


def tz(tz: str | datetime.tzinfo | None, state: dict) -> datetime.tzinfo:
    """Convert a time zone specifier into a ``datetime.tzinfo`` object."""
    if tz is None:
        return None

    # trivial case
    if isinstance(tz, datetime.tzinfo):
        return tz

    # local specifier
    if isinstance(tz, str) and tz.lower() == "local":
        return zoneinfo.ZoneInfo(tzlocal.get_localzone_name())

    # IANA string
    return zoneinfo.ZoneInfo(tz)
