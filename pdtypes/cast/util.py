from __future__ import annotations
import datetime
import re

import numpy as np
import pandas as pd

from pdtypes.error import error_trace


_timedelta64_resolution_regex = re.compile(r'^[^\[]+\[([^\]]+)\]$')
_to_ns = {
    "as": 1e-9,
    "fs": 1e-6,
    "ps": 1e-3,
    "ns": 1,
    "nanosecond": 1,
    "nanoseconds": 1,
    "us": int(1e3),
    "microsecond": int(1e3),
    "microseconds": int(1e3),
    "ms": int(1e6),
    "millisecond": int(1e6),
    "milliseconds": int(1e6),
    "s": int(1e9),
    "sec": int(1e9),
    "second": int(1e9),
    "seconds": int(1e9),
    "m": 60 * int(1e9),
    "minute": 60 * int(1e9),
    "minutes": 60 * int(1e9),
    "h": 60 * 60 * int(1e9),
    "hour": 60 * 60 * int(1e9),
    "hours": 60 * 60 * int(1e9),
    "D": 24 * 60 * 60 * int(1e9),
    "day": 24 * 60 * 60 * int(1e9),
    "days": 24 * 60 * 60 * int(1e9),
    "W": 7 * 24 * 60 * 60 * int(1e9),
    "week": 7 * 24 * 60 * 60 * int(1e9),
    "weeks": 7 * 24 * 60 * 60 * int(1e9)
}


def total_nanoseconds(
    t: datetime.timedelta | pd.Timedelta | np.timedelta64) -> int:
    if isinstance(t, pd.Timedelta):
        return t.asm8.astype(int)
    if isinstance(t, datetime.timedelta):
        # casting to dtype="O" allows for >64-bit arithmetic
        coefficients = np.array([24 * 60 * 60 * int(1e9), int(1e9), int(1e3)],
                                dtype="O")
        components = np.array([t.days, t.seconds, t.microseconds], dtype="O")
        return np.sum(coefficients * components)
    if isinstance(t, np.timedelta64):
        unit = _timedelta64_resolution_regex.match(str(t.dtype)).group(1)
        return int(t.astype(int)) * _to_ns[unit]
    err_msg = (f"[{error_trace()}] could not interpret timedelta of type "
               f"{type(t)}")
    raise TypeError(err_msg)
