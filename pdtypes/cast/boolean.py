from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz

from pdtypes.error import error_trace
import pdtypes.cast.float


def to_boolean(series: pd.Series, dtype: type = bool) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "boolean":
        err_msg = (f"[{error_trace()}] `series` must contain boolean data "
                   f"(received: {series.dtype})")
        raise TypeError(err_msg)
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def to_integer(series: pd.Series, dtype: type = int) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "boolean":
        err_msg = (f"[{error_trace()}] `series` must contain boolean data "
                   f"(received: {series.dtype})")
        raise TypeError(err_msg)
    if not pd.api.types.is_integer_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be int-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # pd.Series([True, False, None]) will not automatically convert to
    # extension type, and will throw errors if casted directly to int
    if series.hasnans and pd.api.types.is_object_dtype(series):
        series = series.astype(pd.BooleanDtype())

    if series.hasnans and not pd.api.types.is_extension_array_dtype(dtype):
        extension_types = {
            np.dtype(np.uint8): pd.UInt8Dtype(),
            np.dtype(np.uint16): pd.UInt16Dtype(),
            np.dtype(np.uint32): pd.UInt32Dtype(),
            np.dtype(np.uint64): pd.UInt64Dtype(),
            np.dtype(np.int8): pd.Int8Dtype(),
            np.dtype(np.int16): pd.Int16Dtype(),
            np.dtype(np.int32): pd.Int32Dtype(),
            np.dtype(np.int64): pd.Int64Dtype()
        }
        return series.astype(extension_types[np.dtype(dtype)])
    return series.astype(dtype)


def to_float(series: pd.Series, dtype: type = float) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "boolean":
        err_msg = (f"[{error_trace()}] `series` must contain boolean data "
                   f"(received: {series.dtype})")
        raise TypeError(err_msg)
    if not pd.api.types.is_float_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be float-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    return series.astype(dtype)


def to_complex(series: pd.Series, dtype: type = complex) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "boolean":
        err_msg = (f"[{error_trace()}] `series` must contain boolean data "
                   f"(received: {series.dtype})")
        raise TypeError(err_msg)
    if not pd.api.types.is_complex_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be complex-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    return series.astype(dtype)


def to_decimal(series: pd.Series) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "boolean":
        err_msg = (f"[{error_trace()}] `series` must contain boolean data "
                   f"(received: {series.dtype})")
        raise TypeError(err_msg)
    return series.apply(lambda x: decimal.Decimal(np.nan) if pd.isna(x)
                                  else decimal.Decimal(x))


def to_datetime(series: pd.Series,
                unit: str = "s",
                offset: datetime.datetime | pd.Timestamp | None = None,
                tz: str | pytz.timezone | None = "local") -> pd.Series:
    if pd.api.types.infer_dtype(series) != "boolean":
        err_msg = (f"[{error_trace()}] `series` must contain boolean data "
                   f"(received: {series.dtype})")
        raise TypeError(err_msg)
    try:
        return pdtypes.cast.float.to_datetime(to_float(series),
                                              unit=unit, offset=offset, tz=tz)
    except Exception as err:
        err_msg = (f"[{error_trace()}] could not convert series to datetime")
        raise type(err)(err_msg) from err


def to_timedelta(
    series: pd.Series,
    unit: str = "s",
    offset: datetime.timedelta | pd.Timedelta | None = None) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "boolean":
        err_msg = (f"[{error_trace()}] `series` must contain boolean data "
                   f"(received: {series.dtype})")
        raise TypeError(err_msg)
    try:
        return pdtypes.cast.float.to_timedelta(to_float(series),
                                               unit=unit, offset=offset)
    except Exception as err:
        err_msg = (f"[{error_trace()}] could not convert series to timedelta")
        raise type(err)(err_msg) from err


def to_string(series: pd.Series, dtype: type = str) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "boolean":
        err_msg = (f"[{error_trace()}] `series` must contain boolean data "
                   f"(received: {series.dtype})")
        raise TypeError(err_msg)
    # pandas is not picky about what constitutes a string dtype
    if (dtype in (datetime.datetime, pd.Timestamp) or
        dtype in (datetime.timedelta, pd.Timedelta) or
        pd.api.types.is_object_dtype(dtype) or
        not pd.api.types.is_string_dtype(dtype)):
        err_msg = (f"[{error_trace()}] `dtype` must be string-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)
