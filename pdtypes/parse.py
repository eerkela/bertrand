from __future__ import annotations
from datetime import datetime, timedelta, timezone
from functools import cache

import pandas as pd
from tzlocal import get_localzone, get_localzone_name


from pdtypes.error import error_trace


def parse_dtype(dtype: type) -> type:
    if pd.api.types.is_integer_dtype(dtype):
        return int
    if pd.api.types.is_float_dtype(dtype):
        return float
    if pd.api.types.is_complex_dtype(dtype):
        return complex
    if pd.api.types.is_bool_dtype(dtype):
        return bool
    if (pd.api.types.is_datetime64_any_dtype(dtype) or
        dtype in (datetime, pd.Timestamp)):
        return datetime
    if (pd.api.types.is_timedelta64_dtype(dtype) or 
        dtype in (timedelta, pd.Timedelta)):
        return timedelta
    if pd.api.types.is_object_dtype(dtype):
        return object
    if pd.api.types.is_string_dtype(dtype):
        return str
    err_msg = (f"[{error_trace()}] unrecognized dtype: {dtype}")
    raise TypeError(err_msg)


@cache
def parse_string(
    string: str,
    format: str | None = None
) -> int | float | complex | pd.Timedelta | pd.Timestamp | bool | str:
    """If an arbitrary string can be interpreted as an atomic data type,
    convert it to that data type and return the result.

    Examples
    ----------
    ```
    parse_string("1") == 1
    parse_string("1.0") == 1.0
    parse_string("(1+0j)") == complex(1, 0)
    parse_string("1970-01-01 00:00:00+00:00") == pd.Timestamp.fromtimestamp(0, "utc")
    parse_string("1 day, 0:00:10") == pd.Timedelta(days=1, seconds=10)
    parse_string("True") == True
    parse_string("a") == "a"
    ```
    """
    try:  # integer string
        return int(string)
    except (TypeError, ValueError):
        pass

    try:  # float string
        return float(string)
    except (TypeError, ValueError):
        pass

    try:  # complex string
        return complex(string)
    except (TypeError, ValueError):
        pass

    try:  # timedelta string
        return pd.to_timedelta(string)
    except ValueError:
        pass

    try:  # datetime string
        return pd.to_datetime(string, format=format)
    except ValueError:
        pass

    # boolean string
    lower = string.strip().lower()
    if lower in ("t", "true"):
        return True
    if lower in ("f", "false"):
        return False

    return string


def to_utc(dt: datetime | pd.Timestamp) -> datetime | pd.Timestamp:
    if isinstance(dt, pd.Timestamp):
        if dt.tzinfo is None:
            dt = dt.tz_localize(get_localzone_name())
        return dt.tz_convert("UTC")
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=get_localzone())
    return dt.astimezone(timezone.utc)


def round_to_tol(series: pd.Series, tol: float) -> pd.Series:
    # round to nearest integer if within tolerance
    residuals = abs(series - series.round())
    indices = (residuals > 0) & (residuals < tol)
    series.loc[indices] = series[indices].round()
    return series


def localize_mixed_timezone(series: pd.Series,
                            naive_tz: str | None = None) -> pd.Series:
    # TODO: change default to 'local' and have None just strip away the tzinfo
    naive = series.apply(lambda x: x.tzinfo is None)
    if naive_tz is None:
        naive_tz = get_localzone_name()
    series.loc[naive] = pd.to_datetime(series[naive]).dt.tz_localize(naive_tz)
    return pd.to_datetime(series, utc=True)
