from __future__ import annotations
import datetime
import decimal
from functools import cache

import numpy as np
import pandas as pd

from pdtypes.error import error_trace


def parse_dtype(dtype: type) -> str:
    """Effectively equivalent to pandas.api.types.infer_dtype, but operates on
    atomic data types and type strings rather than scalar and array-like values.
    """
    # booleans - bool, "bool", "boolean", "?"
    if pd.api.types.is_bool_dtype(dtype):
        return "boolean"

    # integers - int, np.int, "int", "integer", "i", etc
    if dtype == "integer" or pd.api.types.is_integer_dtype(dtype):
        return "integer"

    # floats - float, np.float, "float", "floating", "f", etc
    if dtype == "floating" or pd.api.types.is_float_dtype(dtype):
        return "floating"

    # complex numbers - complex, np.complex, "complex", "c", etc
    if dtype == "c" or pd.api.types.is_complex_dtype(dtype):
        return "complex"

    # arbitrary precision decimals
    if dtype in (decimal.Decimal, "decimal"):
        return "decimal"

    # dates
    if dtype in (datetime.date, "date"):
        return "date"

    # times
    if dtype in (datetime.time, "time"):
        return "time"

    # datetimes - pd.Timestamp, datetime.datetime, np.datetime64, "datetime",
    # "M8", etc.
    if (dtype in (pd.Timestamp, datetime.datetime, np.datetime64, "datetime") or
        pd.api.types.is_datetime64_any_dtype(dtype)):
        return "datetime"

    # timedeltas - pd.Timedelta, datetime.timedelta, np.timedelta64,
    # "timedelta", "m8", etc.
    if (dtype in (pd.Timedelta, datetime.timedelta, np.timedelta64,
                  "timedelta") or
        pd.api.types.is_timedelta64_dtype(dtype)):
        return "timedelta"

    # periods - pd.Period, "period", etc.
    if dtype in (pd.Period, "period") or pd.api.types.is_period_dtype(dtype):
        return "period"

    # objects - object, "object", "O", etc.
    if pd.api.types.is_object_dtype(dtype):
        return "object"

    # bytes - bytes, "bytes", "b", "B", "byte", "bytes"
    if dtype in (bytes, "b", "B", "byte", "bytes"):
        return "bytes"

    # strings - str, "str", "string", "U", etc.
    if pd.api.types.is_string_dtype(dtype):
        return "string"

    # categoricals - "category", "categorical", etc.
    if dtype == "categorical" or pd.api.types.is_categorical_dtype(dtype):
        return "categorical"

    # unrecognized dtype
    err_msg = f"[{error_trace()}] could not interpret `dtype`: {dtype}"
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


def round_to_tol(series: pd.Series, tol: float) -> pd.Series:
    # round to nearest integer if within tolerance
    residuals = abs(series - series.round())
    indices = (residuals > 0) & (residuals < tol)
    series.loc[indices] = series[indices].round()
    return series
