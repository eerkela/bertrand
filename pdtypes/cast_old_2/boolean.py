from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz

from pdtypes.cast_old_2.helpers import (
    _validate_boolean_dtype, _validate_boolean_series, _validate_complex_dtype,
    _validate_decimal_dtype, _validate_dtype_is_scalar,
    _validate_integer_dtype, _validate_float_dtype,
)
from pdtypes.check import extension_type, is_dtype
from pdtypes.error import error_trace
from pdtypes.util.array import vectorize
from pdtypes.util.type_hints import array_like, dtype_like


def boolean_to_boolean(
    series: bool | array_like,
    dtype: dtype_like = bool,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series to another boolean dtype."""
    # vectorize input
    series = pd.Series(series)

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_boolean_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def boolean_to_integer(
    series: bool | array_like,
    dtype: dtype_like = np.int64,
    downcast: bool = False,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series to integer"""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_integer_dtype(dtype)

    # if series has missing values, convert to extension types
    hasnans = series.hasnans
    if hasnans and pd.api.types.is_object_dtype(series):
        series = series.astype(pd.BooleanDtype())

    # if `downcast=True`, return as 8-bit integer dtype
    if downcast:
        if is_dtype(dtype, "unsigned"):
            if hasnans:
                return series.astype(extension_type(np.uint8))
            return series.astype(np.uint8)
        if hasnans:
            return series.astype(extension_type(np.int8))
        return series.astype(np.int8)

    # built-in integer dtype
    if is_dtype(dtype, int, exact=True):
        return (series + 0).astype("O")

    # numpy integer dtype
    if hasnans:
        dtype = extension_type(dtype)
    return series.astype(dtype)


def boolean_to_float(
    series: bool | array_like,
    dtype: dtype_like = float,
    downcast: bool = False,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series to a float series."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_float_dtype(dtype)

    # if `downcast=True`, return as float16
    if downcast:
        return series.astype(np.float16)

    # return as specified dtype
    return series.astype(dtype)


def boolean_to_complex(
    series: bool | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series to a complex series."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_complex_dtype(dtype)

    # astype() is unstable when converting missing values to complex types
    not_na = series.notna()
    subset = series[not_na]

    # if `downcast=True`
    if downcast:
        series = pd.Series(np.full(series.shape, None, dtype=np.complex64))
        series[not_na] = subset.astype(np.complex64)
        return series

    series = pd.Series(np.full(series.shape, None, dtype=dtype))
    series[not_na] = subset.astype(dtype)
    return series


def boolean_to_decimal(
    series: bool | array_like,
    dtype: dtype_like = decimal.Decimal,
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series to a decimal series."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_decimal_dtype(dtype)

    # subsetting is marginally faster than backfilling with pd.NA
    not_na = series.notna()
    subset = series[not_na]
    series = pd.Series(np.full(series.shape, pd.NA, dtype="O"))
    series[not_na] = subset + decimal.Decimal(0)
    return series


# def _boolean_to_pandas_timestamp(
#     series: bool | array_like,
#     unit: str | array_like = "ns",
#     since: str | datetime_like | array_like = "1970-01-01 00:00:00+0000",
#     tz: str | datetime.tzinfo | None = "local",
#     *,
#     validate: bool = True
# ) -> pd.Series:
#     """Convert a boolean series into `pandas.Timestamp` objects."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     if validate:
#         _validate_boolean_series(series)





def boolean_to_string(
    series: bool | array_like,
    dtype: dtype_like = pd.StringDtype(),
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a boolean series into strings."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_boolean_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_string_dtype(dtype)

    # use pandas string extension type, if applicable
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


# def boolean_to_bytes(
#     series: bool | array_like,
#     *,
#     validate: bool = True
# ) -> pd.Series:
#     """Convert a boolean series into a series of `bytes` objects."""
#     # vectorize input
#     series = pd.Series(vectorize(series))

#     # validate input
#     if validate:
#         _validate_boolean_series(series)

#     # subset to avoid missing values
#     not_na = series.notna()
#     subset = series[not_na]

#     # convert subset to bytes by changing memory view
#     if pd.api.types.is_object_dtype(subset):
#         subset = subset.astype(bool)
#     subset = subset.view("S1")

#     # reintroduce missing values
#     series = pd.Series(np.full(series.shape, pd.NA, dtype="O"))
#     series[not_na] = subset
#     return series




# def to_boolean(series: pd.Series, dtype: type = bool) -> pd.Series:
#     if pd.api.types.infer_dtype(series) != "boolean":
#         err_msg = (f"[{error_trace()}] `series` must contain boolean data "
#                    f"(received: {pd.api.types.infer_dtype(series)})")
#         raise TypeError(err_msg)
#     if not pd.api.types.is_bool_dtype(dtype):
#         err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
#                    f"{dtype})")
#         raise TypeError(err_msg)
#     if series.hasnans:
#         return series.astype(pd.BooleanDtype())
#     return series.astype(dtype)


# def to_integer(series: pd.Series, dtype: type = int) -> pd.Series:
#     if pd.api.types.infer_dtype(series) != "boolean":
#         err_msg = (f"[{error_trace()}] `series` must contain boolean data "
#                    f"(received: {pd.api.types.infer_dtype(series)})")
#         raise TypeError(err_msg)
#     if not pd.api.types.is_integer_dtype(dtype):
#         err_msg = (f"[{error_trace()}] `dtype` must be int-like (received: "
#                    f"{dtype})")
#         raise TypeError(err_msg)

#     # pd.Series([True, False, None]) will not automatically convert to
#     # extension type, and will throw errors if casted directly to int
#     if series.hasnans and pd.api.types.is_object_dtype(series):
#         series = series.astype(pd.BooleanDtype())

#     if series.hasnans and not pd.api.types.is_extension_array_dtype(dtype):
#         extension_types = {
#             np.dtype(np.uint8): pd.UInt8Dtype(),
#             np.dtype(np.uint16): pd.UInt16Dtype(),
#             np.dtype(np.uint32): pd.UInt32Dtype(),
#             np.dtype(np.uint64): pd.UInt64Dtype(),
#             np.dtype(np.int8): pd.Int8Dtype(),
#             np.dtype(np.int16): pd.Int16Dtype(),
#             np.dtype(np.int32): pd.Int32Dtype(),
#             np.dtype(np.int64): pd.Int64Dtype()
#         }
#         return series.astype(extension_types[np.dtype(dtype)])
#     return series.astype(dtype)


# def to_float(series: pd.Series, dtype: type = float) -> pd.Series:
#     if pd.api.types.infer_dtype(series) != "boolean":
#         err_msg = (f"[{error_trace()}] `series` must contain boolean data "
#                    f"(received: {pd.api.types.infer_dtype(series)})")
#         raise TypeError(err_msg)
#     if not pd.api.types.is_float_dtype(dtype):
#         err_msg = (f"[{error_trace()}] `dtype` must be float-like (received: "
#                    f"{dtype})")
#         raise TypeError(err_msg)
#     return series.astype(dtype)


# def to_complex(series: pd.Series, dtype: type = complex) -> pd.Series:
#     if pd.api.types.infer_dtype(series) != "boolean":
#         err_msg = (f"[{error_trace()}] `series` must contain boolean data "
#                    f"(received: {pd.api.types.infer_dtype(series)})")
#         raise TypeError(err_msg)
#     if not pd.api.types.is_complex_dtype(dtype):
#         err_msg = (f"[{error_trace()}] `dtype` must be complex-like (received: "
#                    f"{dtype})")
#         raise TypeError(err_msg)
#     return series.astype(dtype)


# def to_decimal(series: pd.Series) -> pd.Series:
#     if pd.api.types.infer_dtype(series) != "boolean":
#         err_msg = (f"[{error_trace()}] `series` must contain boolean data "
#                    f"(received: {pd.api.types.infer_dtype(series)})")
#         raise TypeError(err_msg)
#     return series.apply(lambda x: decimal.Decimal(np.nan) if pd.isna(x)
#                                   else decimal.Decimal(x))


# def to_datetime(series: pd.Series,
#                 unit: str = "s",
#                 offset: datetime.datetime | pd.Timestamp | None = None,
#                 tz: str | pytz.timezone | None = "local") -> pd.Series:
#     if pd.api.types.infer_dtype(series) != "boolean":
#         err_msg = (f"[{error_trace()}] `series` must contain boolean data "
#                    f"(received: {pd.api.types.infer_dtype(series)})")
#         raise TypeError(err_msg)
#     try:
#         return pdtypes.cast_old.float.to_datetime(to_float(series),
#                                               unit=unit, offset=offset, tz=tz)
#     except Exception as err:
#         err_msg = (f"[{error_trace()}] could not convert series to datetime")
#         raise type(err)(err_msg) from err


# def to_timedelta(
#     series: pd.Series,
#     unit: str = "s",
#     offset: datetime.timedelta | pd.Timedelta | None = None) -> pd.Series:
#     if pd.api.types.infer_dtype(series) != "boolean":
#         err_msg = (f"[{error_trace()}] `series` must contain boolean data "
#                    f"(received: {pd.api.types.infer_dtype(series)})")
#         raise TypeError(err_msg)
#     try:
#         return pdtypes.cast_old.float.to_timedelta(to_float(series),
#                                                unit=unit, offset=offset)
#     except Exception as err:
#         err_msg = (f"[{error_trace()}] could not convert series to timedelta")
#         raise type(err)(err_msg) from err


# def to_string(series: pd.Series, dtype: type = str) -> pd.Series:
#     if pd.api.types.infer_dtype(series) != "boolean":
#         err_msg = (f"[{error_trace()}] `series` must contain boolean data "
#                    f"(received: {pd.api.types.infer_dtype(series)})")
#         raise TypeError(err_msg)
#     # pandas is not picky about what constitutes a string dtype
#     if (pd.api.types.is_object_dtype(dtype) or
#         not pd.api.types.is_string_dtype(dtype)):
#         err_msg = (f"[{error_trace()}] `dtype` must be string-like (received: "
#                    f"{dtype})")
#         raise TypeError(err_msg)
#     if series.hasnans:
#         return series.astype(pd.StringDtype())
#     return series.astype(dtype)
