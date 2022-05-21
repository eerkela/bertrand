from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz
import tzlocal

from pdtypes.error import error_trace



def to_boolean(series: pd.Series,
               force: bool = False,
               round: bool = False,
               tol: float = 1e-6,
               dtype: type = bool) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # all na special case - round() throws errors unless this is handled
    if series.isna().all():
        return series.astype(pd.BooleanDtype())

    # rounding
    if round:  # always round
        transfer = series.round()
    elif tol and not force:  # round if within tolerance
        rounded = series.round()
        if ((series - rounded).abs() > tol).any():
            err_msg = (f"[{error_trace()}] could not convert series to "
                       f"boolean without losing information: "
                       f"{list(series.head())}")
            raise ValueError(err_msg)
        transfer = rounded
    else:  # do not round
        transfer = series.copy()

    # check for information loss
    if force:
        transfer = np.ceil(transfer.abs().clip(0, 1))
    elif not transfer.dropna().isin((0, 1)).all():
        err_msg = (f"[{error_trace()}] could not convert series to boolean "
                   f"without losing information: {list(series.head())}")
        raise ValueError(err_msg)

    # convert and return
    if transfer.hasnans:
        return transfer.astype(pd.BooleanDtype())
    return transfer.astype(dtype)


def to_integer(series: pd.Series,
               force: bool = False,
               round: bool = False,
               tol: float = 1e-6,
               dtype: type = int) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_integer_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be int-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

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

    # all na special case - round() and trunc() throw errors if not handled
    if series.isna().all():
        if pd.api.types.is_extension_array_dtype(dtype):
            return series.astype(dtype)
        return series.astype(extension_types[np.dtype(dtype)])

    if round:  # round
        transfer = series.round()
    elif force:  # truncate
        transfer = np.trunc(series)
    else:  # round if within tolerance
        rounded = series.round()
        if ((series - rounded).abs() > tol).any():
            bad = series[(series - rounded).abs() > tol]
            err_msg = (f"[{error_trace()}] could not convert series to "
                       f"integer without losing information: "
                       f"{list(bad.head())}")
            raise ValueError(err_msg)
        transfer = rounded

    # check series values fit within specified dtype
    min_val = transfer.min()
    max_val = transfer.max()
    if dtype == int and (min_val < -2**63 or max_val > 2**63 - 1):  # > 64-bit
        # python ints can be arbitrarily large
        return transfer.apply(lambda x: None if pd.isna(x) else int(x))
    if not pd.api.types.is_extension_array_dtype(dtype):
        # convert to explicit numpy dtype object to expose itemsize attribute
        dtype = np.dtype(dtype)
    if pd.api.types.is_unsigned_integer_dtype(dtype):
        min_possible = 0
        max_possible = 2**(8 * dtype.itemsize) - 1
    else:
        min_possible = -2**(8 * dtype.itemsize - 1)
        max_possible = 2**(8 * dtype.itemsize - 1) - 1
    if min_val < min_possible or max_val > max_possible:
        bad = transfer[(transfer < min_possible) | (transfer > max_possible)]
        err_msg = (f"[{error_trace()}] could not convert series to integer: "
                   f"values exceed available range for {dtype} "
                   f"({list(bad.head())})")
        raise ValueError(err_msg)

    # return
    if series.hasnans and not pd.api.types.is_extension_array_dtype(dtype):
        return transfer.astype(extension_types[np.dtype(dtype)])
    return transfer.astype(dtype)


def to_float(series: pd.Series, dtype: type = float) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_float_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be float-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    return series.astype(dtype)


def to_complex(series: pd.Series, dtype: type = complex) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_complex_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be complex-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if dtype in (complex, "complex", "c"):
        if pd.api.types.is_object_dtype(series):
            float_type = series.apply(lambda x: np.dtype(type(x))).max()
        else:
            float_type = series.dtype
        preserve_precision = {
            np.dtype(np.float16): np.dtype(np.complex64),
            np.dtype(np.float32): np.dtype(np.complex64),
            np.dtype(np.float64): np.dtype(np.complex128),
            np.dtype(np.longdouble): np.dtype(np.clongdouble)
        }
        return series.astype(preserve_precision[float_type])
    return series.astype(dtype)


def to_decimal(series: pd.Series) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    return series.apply(lambda x: decimal.Decimal(np.nan) if pd.isna(x)
                                  else decimal.Decimal(x))


def to_datetime(series: pd.Series,
                unit: str = "s",
                offset: pd.Timestamp | datetime.datetime | None = None,
                tz: str | pytz.timezone | None = "local") -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if isinstance(offset, datetime.datetime):
        offset = pd.Timestamp(offset)

    # convert series values to equivalent UTC timestamps
    utc_times = pd.to_datetime(series, unit=unit, utc=True)

    # shift by offset
    if offset:
        epoch = pd.Timestamp.fromtimestamp(0, "UTC")
        if not offset.tzinfo:   # naive offset -> assume UTC
            shift = offset.tz_localize("UTC") - epoch
        elif not tz and offset.tzinfo and offset.tzinfo.utcoffset(offset):
            # offset tzinfo will not be used -> just strip and use UTC instead
            shift = offset.tz_localize(None).tz_localize("UTC") - epoch
        else:  # aware offset -> use directly
            shift = offset - epoch
        utc_times = utc_times + shift

    # convert to final timezone
    if not tz:
        return utc_times.dt.tz_localize(None)
    if tz == "local":
        if offset and offset.tzinfo:
            return utc_times.dt.tz_convert(offset.tzinfo)
        return utc_times.dt.tz_convert(tzlocal.get_localzone_name())
    return utc_times.dt.tz_convert(tz)


def to_timedelta(
    series: pd.Series,
    unit: str = "s",
    offset: pd.Timedelta | datetime.timedelta | None = None) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if offset:
        return pd.to_timedelta(series, unit=unit) + offset
    return pd.to_timedelta(series, unit=unit)


def to_string(series: pd.Series, dtype: type = str) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if (pd.api.types.is_object_dtype(dtype) or
        not pd.api.types.is_string_dtype(dtype)):
        err_msg = (f"[{error_trace()}] `dtype` must be string-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)
