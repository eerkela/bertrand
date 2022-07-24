from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz

from pdtypes.error import error_trace
import pdtypes.cast_old.float


def round_decimal(series: pd.Series) -> pd.Series:
    return series.apply(lambda x: decimal.Decimal(np.nan) if pd.isna(x)
                                  else decimal.Decimal(round(x)))


def truncate_decimal(series: pd.Series) -> pd.Series:
    copy = series.copy()
    non_na = copy.notna()
    copy[non_na] = np.trunc(copy[non_na])
    return copy


def to_boolean(series: pd.Series,
               force: bool = False,
               round: bool = False,
               tol: float = 1e-6,
               dtype: type = bool) -> pd.Series:
    # input error checks
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype == "decimal" or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) == "decimal")):
        err_msg = (f"[{error_trace()}] `series` must contain decimal data, "
                   f"not {repr(inferred_series_dtype)}")
        raise TypeError(err_msg)
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = f"[{error_trace()}] `dtype` must be bool-like, not {dtype}"
        raise TypeError(err_msg)

    try:
        series = to_float(series)
        return pdtypes.cast_old.float.to_boolean(series, force=force, round=round,
                                             tol=tol, dtype=dtype)
    except Exception as err:
        err_msg = (f"[{error_trace()}] could not convert decimal to boolean")
        raise type(err)(err_msg) from err


def to_integer(series: pd.Series,
               force: bool = False,
               round: bool = False,
               tol: float = 1e-6,
               dtype: type = int) -> pd.Series:
    # input error checks
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype == "decimal" or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) == "decimal")):
        err_msg = (f"[{error_trace()}] `series` must contain decimal data, "
                   f"not {repr(inferred_series_dtype)}")
        raise TypeError(err_msg)
    if not pd.api.types.is_integer_dtype(dtype):
        err_msg = f"[{error_trace()}] `dtype` must be int-like, not {dtype}"
        raise TypeError(err_msg)

    # round
    if round:  # always round
        series = round_decimal(series)
    elif tol:  # round if within tolerance
        indices = (series - round_decimal(series)).abs() <= tol
        series[indices] = round_decimal(series[indices])

    # check for information loss
    if force:
        series = truncate_decimal(series)
    elif (series - truncate_decimal(series)).any():
        bad = series[(series - truncate_decimal(series)).abs() > 0].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] could not convert decimal to "
                       f"integer without losing information: non-integer "
                       f"value at index {list(bad)}")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] could not convert decimal to "
                       f"integer without losing information: non-integer "
                       f"values at indices {list(bad)}")
        raise ValueError(err_msg)

    # converting straight to int doesn't work -> convert to object form first
    series = series.apply(lambda x: None if pd.isna(x) else int(x),
                          convert_dtype=False)

    # check whether result fits within dtype
    min_val = series.min()
    max_val = series.max()
    if dtype in (int, "int", "i") and min_val < -2**63 or max_val > 2**63 - 1:
        return series  # > 64-bit limit -> return as integer objects
    size = pd.api.types.pandas_dtype(dtype).itemsize
    if pd.api.types.is_unsigned_integer_dtype(dtype):
        min_poss = 0
        max_poss = 2**(8 * size) - 1
    else:
        min_poss = -2**(8 * size - 1)
        max_poss = 2**(8 * size - 1) - 1
    if min_val < min_poss or max_val > max_poss:
        bad = series[(series < min_poss) | (series > max_poss)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"available range for {dtype} (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"available range for {dtype} (indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(bad[:5])
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"available range for {dtype} (indices: "
                       f"[{shortened}, ...] ({len(bad)}))")
        raise ValueError(err_msg)

    # convert and return
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
    # input error checks
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype == "decimal" or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) == "decimal")):
        err_msg = (f"[{error_trace()}] `series` must contain decimal data, "
                   f"not {repr(inferred_series_dtype)}")
        raise TypeError(err_msg)
    if not pd.api.types.is_float_dtype(dtype):
        err_msg = f"[{error_trace()}] `dtype` must be float-like, not {dtype}"
        raise TypeError(err_msg)

    # convert
    return series.astype(dtype)


def to_complex(series: pd.Series, dtype: type = complex) -> pd.Series:
    # input error checks
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype == "decimal" or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) == "decimal")):
        err_msg = (f"[{error_trace()}] `series` must contain decimal data, "
                   f"not {repr(inferred_series_dtype)}")
        raise TypeError(err_msg)
    if not pd.api.types.is_complex_dtype(dtype):
        err_msg = f"[{error_trace()}] `dtype` must be complex-like, not {dtype}"
        raise TypeError(err_msg)

    return series.astype(dtype)


def to_decimal(series: pd.Series) -> pd.Series:
    # input error checks
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype == "decimal" or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) == "decimal")):
        err_msg = (f"[{error_trace()}] `series` must contain decimal data, "
                   f"not {repr(inferred_series_dtype)})")
        raise TypeError(err_msg)

    return series.apply(lambda x: decimal.Decimal(np.nan) if pd.isna(x)
                                  else decimal.Decimal(x))


def to_datetime(series: pd.Series,
                unit: str = "s",
                offset: datetime.datetime | pd.Timestamp | None = None,
                tz: str | pytz.timezone | None = "local") -> pd.Series:
    # input error checks
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype == "decimal" or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) == "decimal")):
        err_msg = (f"[{error_trace()}] `series` must contain decimal data, "
                   f"not {repr(inferred_series_dtype)}")
        raise TypeError(err_msg)


def to_timedelta(
    series: pd.Series,
    unit: str = "s",
    offset: datetime.timedelta | pd.Timedelta | None = None) -> pd.Series:
    # input error checks
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype == "decimal" or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) == "decimal")):
        err_msg = (f"[{error_trace()}] `series` must contain decimal data, "
                   f"not {repr(inferred_series_dtype)}")
        raise TypeError(err_msg)


def to_string(series: pd.Series, dtype: type = str) -> pd.Series:
    # input error checks
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype == "decimal" or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) == "decimal")):
        err_msg = (f"[{error_trace()}] `series` must contain decimal data, "
                   f"not {repr(inferred_series_dtype)}")
        raise TypeError(err_msg)


def to_categorical(series: pd.Series,
                   categories: list | np.ndarray | pd.Series | None = None,
                   ordered: bool = False) -> pd.Series:
    # input error checks
    inferred_series_dtype = pd.api.types.infer_dtype(series)
    if not (inferred_series_dtype == "decimal" or
            (inferred_series_dtype == "categorical" and
             pd.api.types.infer_dtype(series.dtype.categories) == "decimal")):
        err_msg = (f"[{error_trace()}] `series` must contain decimal data, "
                   f"not {repr(inferred_series_dtype)}")
        raise TypeError(err_msg)

    # convert
    values = pd.Categorical(series, categories=categories, ordered=ordered)
    return pd.Series(values)
