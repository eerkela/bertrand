from __future__ import annotations
import datetime
from typing import Type

import numpy as np
import pandas as pd
import tzlocal

from pdtypes.error import error_trace


########################
####    Integers    ####
########################


def integer_to_float(series: pd.Series,
                     dtype: type = np.float64) -> pd.Series:
    if not pd.api.types.is_float_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be float-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    return series.astype(dtype)


def integer_to_complex(series: pd.Series,
                       dtype: type = np.complex128) -> pd.Series:
    if not pd.api.types.is_complex_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be complex-like "
                   f"(received: {dtype})")
        raise TypeError(err_msg)
    return series.astype(dtype)


def integer_to_string(series: pd.Series,
                      dtype: type = pd.StringDtype()) -> pd.Series:
    # pandas is not picky about what constitutes a string dtype
    if (dtype in (datetime.datetime, pd.Timestamp) or
        dtype in (datetime.timedelta, pd.Timedelta) or
        pd.api.types.is_object_dtype(dtype) or
        not pd.api.types.is_string_dtype(dtype)):
        err_msg = (f"[{error_trace()}] `dtype` must be string-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if series.hasnans:
        if pd.api.types.is_float_dtype(series):
            # # can series be converted to integer?
            # try:
            #     series = float_to_integer(series)
            # except ValueError as err:
            #     err_msg = (f"[{error_trace()}] could not interpret series as "
            #                f"containing integer data")
            #     raise ValueError(err_msg) from err

            # can series be represented as 64-bit integer?
            min_val = series.min()
            max_val = series.max()
            if min_val >= -2**63 and max_val <= 2**63 - 1:  # signed
                series = series.astype(pd.Int64Dtype())
            elif min_val >= 0 and max_val <= 2**64 - 1:  # unsigned
                series = series.astype(pd.UInt64Dtype())
            else:  # integer floats > 64 bit limit
                conv = lambda x: int(x) if not pd.isna(x) else None
                series = series.apply(conv)
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


def integer_to_boolean(series: pd.Series,
                       force: bool = False,
                       dtype: type = bool) -> pd.Series:
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if not series.isna().all():  # abs() throws error on all na
        if force:
            series = series.abs().clip(0, 1)
        else:
            adjusted = series.abs()
            if (adjusted > 1).any():
                err_msg = (f"[{error_trace()}] could not convert series to "
                           f"boolean without losing information: "
                           f"{list(series.head())}")
                raise TypeError(err_msg)
            series = adjusted
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def integer_to_datetime(series: pd.Series, unit: str = "s") -> pd.Series:
    return pd.to_datetime(series, unit=unit, utc=True)


def integer_to_timedelta(series: pd.Series, unit: str = "s") -> pd.Series:
    return pd.to_timedelta(series, unit=unit)


######################
####    Floats    ####
######################


def float_to_integer(series: pd.Series,
                     force: bool = False,
                     round: bool = False,
                     tol: float = 1e-6,
                     dtype: type = np.int64) -> pd.Series:
    if not pd.api.types.is_integer_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be int-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if not series.isna().all():  # round() and trunc() throw errors on all na
        if round:
            series = series.round()
        elif force:
            series = np.trunc(series)
        else:  # round to nearest integer iff within tolerance
            residuals = abs(series - series.round())
            if (residuals > tol).any():
                err_msg = (f"[{error_trace()}] could not convert series to "
                           f"integer without losing information: "
                           f"{list(series.head())}")
                raise ValueError(err_msg)
            if tol:
                series = series.round()
    if series.hasnans and not pd.api.types.is_extension_dtype(dtype):
        if np.issubdtype(np.unsignedinteger):
            extension_types = {
                np.uint8: pd.UInt8Dtype(),
                np.uint16: pd.UInt16Dtype(),
                np.uint32: pd.UInt32Dtype(),
                np.uint64: pd.UInt64Dtype()
            }
        else:
            extension_types = {
                np.int8: pd.Int8Dtype(),
                np.int16: pd.Int16Dtype(),
                np.int32: pd.Int32Dtype(),
                np.int64: pd.Int64Dtype()
            }
        for np_type, ext_type in extension_types.items():
            if np.issubdtype(dtype, np_type):
                dtype = ext_type
                break
    return series.astype(dtype)


def float_to_complex(series: pd.Series,
                     dtype: type = np.complex128) -> pd.Series:
    if not pd.api.types.is_complex_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be complex-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    return series.astype(dtype)


def float_to_string(series: pd.Series,
                    dtype: type = pd.StringDtype()) -> pd.Series:
    if (pd.api.types.is_object_dtype(dtype) or
        not pd.api.types.is_string_dtype(dtype)):
        err_msg = (f"[{error_trace()}] `dtype` must be string-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)
    if series.hasnans and dtype != pd.StringDtype():
        # TODO: just use StringDtype()?
        # casting straight to str will turn None/nan into 'None'/'nan'
        null_indices = series.isna()
        result = series.astype(dtype)
        result[null_indices] = None
        return result
    return series.astype(dtype)


def float_to_boolean(series: pd.Series,
                     force: bool = False,
                     round: bool = False,
                     tol: float = 1e-6,
                     dtype: type = bool) -> pd.Series:
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # all na special case
    if series.isna().all():
        return series.astype(pd.BooleanDtype())

    # handle rounding
    if round:
        transfer = series.round()
    elif tol and not force:
        rounded = series.round()
        if ((series - rounded).abs() > tol).any():
            err_msg = (f"[{error_trace()}] could not convert series to "
                       f"boolean without losing information: "
                       f"{list(series.head())}")
            raise ValueError(err_msg)
        transfer = rounded
    else:
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


def float_to_datetime(series: pd.Series,
                      unit: str = "s",
                      timezone: str | None = "local") -> pd.Series:
    if timezone == "local":
        timezone = tzlocal.get_localzone_name()
    return pd.to_datetime(series, unit=unit).dt.tz_localize(timezone)


def float_to_timedelta(series: pd.Series, unit: str = "s") -> pd.Series:
    return pd.to_timedelta(series, unit=unit)


###############################
####    Complex Numbers    ####
###############################


def complex_to_integer(series: pd.Series,
                       force: bool = False,
                       round: bool = True,
                       tol: float = 1e-6) -> pd.Series:
    series = complex_to_float(series, force=force, tol=tol)
    return float_to_integer(series, force=force, round=round, tol=tol)


def complex_to_float(series: pd.Series,
                     force: bool = False,
                     tol: float = 1e-6) -> pd.Series:
    # split series into real and imaginary components
    if series.dtype.itemsize < 8:
        real = series.astype(np.dtype(np.float32))
        imag = abs(series - real).astype(np.dtype(np.float32))
    else:
        real = series.astype(np.dtype(np.float64))
        imag = abs(series - real).astype(np.dtype(np.float64))

    # round imag to within tolerance
    residuals = abs(imag - imag.round())
    indices = (residuals > 0) & (residuals < tol)
    imag.loc[indices] = imag[indices].round()

    if force or not imag.any():
        return real
    err_msg = (f"[{error_trace()}] could not convert series to float without "
               f"losing information: {list(series.head())}")
    raise TypeError(err_msg)


def complex_to_string(series: pd.Series) -> pd.Series:
    return series.astype(pd.StringDtype())


def complex_to_boolean(series: pd.Series,
                       force: bool = False,
                       round: bool = True,
                       tol: float = 1e-6) -> pd.Series:
    series = complex_to_float(series, force=force, tol=tol)
    return float_to_boolean(series, force=force, round=round, tol=tol)


def complex_to_datetime(series: pd.Series,
                        force: bool = False,
                        tol: float = 1e-6,
                        unit: str = "s",
                        timezone: str | None = "local") -> pd.Series:
    series = complex_to_float(series, force=force, tol=tol)
    return float_to_datetime(series, unit=unit, timezone=timezone)


def complex_to_timedelta(series: pd.Series,
                         force: bool = False,
                         tol: float = 1e-6,
                         unit: str = "s") -> pd.Series:
    series = complex_to_float(series, force=force, tol=tol)
    return float_to_timedelta(series, unit=unit)


#######################
####    Strings    ####
#######################


# def string_to_integer(series: pd.Series,
#                       force: bool = False,
#                       round: bool = False,
#                       tol: float = 1e-6,
#                       dayfirst: bool = False,
#                       ) -> pd.Series:
#     try:
#         return series.astype(pd.Int64Dtype())
#     except ValueError:
#         pass

#     try:
#         return float_to_integer(series.astype(np.float64),
#                                 force=force, round=round, tol=tol)
#     except ValueError:
#         pass

#     try:
#         return complex_to_integer(series.astype(np.complex128),
#                                   force=force, round=round, tol=tol)
#     except ValueError:
#         pass

#     try:
#         return boolean_to_integer(series.astype(pd.BooleanDtype()))
#     except ValueError:
#         pass

#     try:
#         return datetime_to_integer(pd.to_datetime(series, ))


def string_to_float(series: pd.Series) -> pd.Series:
    return series.astype(np.dtype(np.float64))


def string_to_complex(series: pd.Series) -> pd.Series:
    return series.astype(np.dtype(np.complex128))


def string_to_boolean(series: pd.Series) -> pd.Series:
    return series.astype(pd.BooleanDtype())


def string_to_datetime(series: pd.Series,
                       format: str | None = None,
                       exact: bool = True,
                       infer_datetime_format: bool = False,
                       dayfirst: bool = False,
                       yearfirst: bool = False,
                       cache: bool = True,
                       timezone: str | None = "local") -> pd.Series:
    return pd.to_datetime(series,
                          format=format,
                          exact=exact,
                          infer_datetime_format=infer_datetime_format,
                          dayfirst=dayfirst,
                          yearfirst=yearfirst,
                          cache=cache)


def string_to_timedelta(series: pd.Series,
                        unit: str = "s") -> pd.Series:
    return pd.to_timedelta(series, unit=unit)


########################
####    Booleans    ####
########################


def boolean_to_integer(series: pd.Series) -> pd.Series:
    return series.astype(pd.Int64Dtype())


def boolean_to_float(series: pd.Series) -> pd.Series:
    return series.astype(np.dtype(np.float64))


def boolean_to_complex(series: pd.Series) -> pd.Series:
    return series.astype(np.dtype(np.complex128))


def boolean_to_string(series: pd.Series) -> pd.Series:
    return series.astype(pd.StringDtype())


def boolean_to_datetime(series: pd.Series,
                        unit: str = "s") -> pd.Series:
    return pd.to_datetime(series, unit=unit)


def boolean_to_timedelta(series: pd.Series,
                         unit: str = "s") -> pd.Series:
    return pd.to_timedelta(series, unit=unit)


#########################
####    Datetimes    ####
#########################


def datetime_to_integer(series: pd.Series,
                        force: bool = False,
                        round: bool = True,
                        tol: float = 1e-6,
                        unit: str = "s",
                        naive_tz: str | None = None) -> pd.Series:
    """
    Test cases:
        - extreme timestamps > 2**33.  This is a limit imposed by to_numeric
            returning nanosecond precision.  This operation is limited to
            pd.Timestamp.min, pd.Timestamp.max
    """
    timestamps = datetime_to_float(series, unit=unit, naive_tz=naive_tz)
    return float_to_integer(timestamps, force=force, round=round, tol=tol)


def datetime_to_float(series: pd.Series,
                      unit: str = "s",
                      naive_tz: str | None = None) -> pd.Series:
    unit_conversion = {
        "ns": 1,
        "nanosecond": 1,
        "nanoseconds": 1,
        "us": 1e3,
        "microsecond": 1e3,
        "microseconds": 1e3,
        "ms": 1e6,
        "millisecond": 1e6,
        "milliseconds": 1e6,
        "s": 1e9,
        "sec": 1e9,
        "second": 1e9,
        "seconds": 1e9,
        "m": 60 * 1e9,
        "minute": 60 * 1e9,
        "minutes": 60 * 1e9,
        "h": 60 * 60 * 1e9,
        "hour": 60 * 60 * 1e9,
        "hours": 60 * 60 * 1e9,
        "d": 24 * 60 * 60 * 1e9,
        "day": 24 * 60 * 60 * 1e9,
        "days": 24 * 60 * 60 * 1e9,
        "w": 7 * 24 * 60 * 60 * 1e9,
        "week": 7 * 24 * 60 * 60 * 1e9,
        "weeks": 7 * 24 * 60 * 60 * 1e9,
        "y": 365.2425 * 24 * 60 * 60 * 1e9,  # correcting for leap years
        "year": 365.2425 * 24 * 60 * 60 * 1e9,
        "years": 365.2425 * 24 * 60 * 60 * 1e9
    }
    if pd.api.types.is_object_dtype(series):  # possible mixed tz or aware/naive
        naive = series.apply(lambda x: x.tzinfo is None)
        if naive_tz is None:
            naive_tz = tzlocal.get_localzone_name()
        series.loc[naive] = pd.to_datetime(series[naive]).dt.tz_localize(naive_tz)
        series = pd.to_datetime(series, utc=True)
    return pd.to_numeric(series) / unit_conversion[unit.lower()]


def datetime_to_complex(series: pd.Series,
                        unit: str = "s",
                        naive_tz: str | None = None) -> pd.Series:
    timestamps = datetime_to_float(series, unit=unit, naive_tz=naive_tz)
    return float_to_complex(timestamps)


def datetime_to_string(series: pd.Series,
                       format: str = "") -> pd.Series:
    return series.dt.strftime(format)


def datetime_to_boolean(series: pd.Series,
                        force: bool = False,
                        round: bool = False,
                        tol: float = 1e-6,
                        unit: str = "s",
                        naive_tz: str | None = None) -> pd.Series:
    timestamps = datetime_to_float(series, unit=unit, naive_tz=naive_tz)
    return float_to_boolean(timestamps, force=force, round=round, tol=tol)


def datetime_to_timedelta(series: pd.Series,
                          naive_tz: str | None = None) -> pd.Series:
    timestamps = datetime_to_float(series, unit="s", naive_tz=naive_tz)
    return pd.to_timedelta(timestamps, unit="s")


##########################
####    Timedeltas    ####
##########################


def timedelta_to_integer(series: pd.Series,
                         force: bool = False,
                         round: bool = False,
                         tol: float = 1e-6,
                         unit: str = "s") -> pd.Series:
    series = timedelta_to_float(series, unit=unit)
    return float_to_integer(series, force=force, round=round, tol=tol)


def timedelta_to_float(series: pd.Series,
                       unit: str = "s") -> pd.Series:
    unit_conversion = {
        "ns": 1e-9,
        "nanosecond": 1e-9,
        "nanoseconds": 1e-9,
        "us": 1e-6,
        "microsecond": 1e-6,
        "microseconds": 1e-6,
        "ms": 1e-3,
        "millisecond": 1e-3,
        "milliseconds": 1e-3,
        "s": 1,
        "sec": 1,
        "second": 1,
        "seconds": 1,
        "m": 60,
        "minute": 60,
        "minutes": 60,
        "h": 60 * 60,
        "hour": 60 * 60,
        "hours": 60 * 60,
        "d": 24 * 60 * 60,
        "day": 24 * 60 * 60,
        "days": 24 * 60 * 60,
        "w": 7 * 24 * 60 * 60,
        "week": 7 * 24 * 60 * 60,
        "weeks": 7 * 24 * 60 * 60,
        "y": 365.2425 * 24 * 60 * 60,  # correcting for leap years
        "year": 365.2425 * 24 * 60 * 60,
        "years": 365.2425 * 24 * 60 * 60
    }
    return series.dt.total_seconds() / unit_conversion[unit.lower()]


def timedelta_to_complex(series: pd.Series,
                         unit: str = "s") -> pd.Series:
    series = timedelta_to_float(series, unit=unit)
    return float_to_complex(series)


def timedelta_to_string(series: pd.Series, format: str = "") -> pd.Series:
    return series.dt.strftime(format)


def timedelta_to_boolean(series: pd.Series,
                         force: bool = False,
                         round: bool = False,
                         tol: float = 1e-6,
                         unit: str = "s") -> pd.Series:
    series = timedelta_to_float(series, unit=unit)
    return float_to_boolean(series, force=force, round=round, tol=tol)


def timedelta_to_datetime(series: pd.Series) -> pd.Series:
    series = timedelta_to_float(series, unit="s")
    return pd.to_timedelta(series, unit="s")
