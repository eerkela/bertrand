from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz
import tzlocal

from pdtypes.error import error_trace
from pdtypes.util.downcast import (
    downcast_complex, downcast_float, downcast_int_dtype
)
from pdtypes.util.time import (
    _to_ns, date_to_days, datetime64_components, days_to_date, ns_since_epoch,
    time_unit, to_utc, total_nanoseconds
)


"""
Test Cases:
-   infinities
"""

#######################
####    Helpers    ####
#######################


def _to_pandas_timestamp(series: pd.Series,
                         tz: str | pytz.timezone | None,
                         min_val: int,
                         max_val: int) -> pd.Series:
    # check whether series fits within datetime64[ns] range
    if min_val < -2**63 + 1 or max_val > 2**63 - 1:
        bad = series[(series < -2**63 + 1) | (series > 2**63 - 1)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace(stack_index=2)}] series could not be "
                       f"converted to {pd.Timestamp}: values exceed 64-bit "
                       f"limit for datetime64[ns] (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace(stack_index=2)}] series could not be "
                       f"converted to {pd.Timestamp}: values exceed 64-bit "
                       f"limit for datetime64[ns] (indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace(stack_index=2)}] series could not be "
                       f"converted to {pd.Timestamp}: values exceed 64-bit "
                       f"limit for datetime64[ns] (indices: [{shortened}, ...] "
                       f"({len(bad)}))")
        raise OverflowError(err_msg)

    # initialize as utc datetimes
    series = pd.to_datetime(series.round(), unit="ns", utc=True)

    # localize to final timezone
    if tz is None:
        return series.dt.tz_localize(None)
    if tz == "local":
        tz = tzlocal.get_localzone_name()
    return series.dt.tz_convert(tz)


def _to_datetime_datetime(series: pd.Series,
                          tz: str | pytz.timezone | None,
                          min_val: int,
                          max_val: int) -> pd.Series:
    min_poss = ns_since_epoch(datetime.datetime.min)
    max_poss = ns_since_epoch(datetime.datetime.max)

    # check whether series fits within datetime.datetime range/precision
    if min_val < min_poss or max_val > max_poss:
        bad = series[(series < min_poss) | (series > max_poss)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to {datetime.datetime}: values "
                       f"exceed available range or have < us precision "
                       f"(index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to {datetime.datetime}: values "
                       f"exceed available range or have < us precision "
                       f"(indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to {datetime.datetime}: values "
                       f"exceed available range or have < us precision "
                       f"(indices: [{shortened}, ...] ({len(bad)}))")
        raise OverflowError(err_msg)

    # conversion function
    def make_dt(ns: int | None) -> datetime.datetime:
        if pd.isna(ns):
            return pd.NaT
        utc = datetime.timezone.utc
        result = datetime.datetime.fromtimestamp(ns / int(1e9), utc)
        result += datetime.timedelta(microseconds=(ns % int(1e9) / 1000))
        if tz is None:
            return result.replace(tzinfo=None)
        if tz == "local":
            return result.astimezone()  # automatically localizes
        if isinstance(tz, datetime.timezone):
            return result.astimezone(tz)
        return result.astimezone(pytz.timezone(tz))

    return series.apply(make_dt)


def _to_numpy_datetime64_any_unit(series: pd.Series,
                                  min_val: int,
                                  max_val: int) -> pd.Series:
    min_poss = -2**63 + 1  # -2**63 is reserved for NaT
    max_poss = 2**63 - 1

    # attempt to select a non-ns unit based on series range
    selected = None
    for u in ("us", "ms", "s", "m", "h", "D", "W"):
        scale_factor = _to_ns[u]
        if (series % scale_factor).any():
            break
        if (min_val / scale_factor > min_poss and
            max_val / scale_factor < max_poss):
            selected = u
    if selected:
        scale_factor = _to_ns[selected]
        make_dt = lambda x: np.datetime64(round(x / scale_factor), selected)
        return series.apply(lambda x: pd.NaT if pd.isna(x) else make_dt(x))
    elif not (series % _to_ns["D"]).any():  # try months and years
        dates = days_to_date(series / _to_ns["D"])
        if (dates["day"] == 1).all():
            if (dates["month"] == 1).all():  # try years
                years = pd.Series(dates["year"] - 1970)
                min_year = years.min()
                max_year = years.max()
                if min_year >= min_poss and max_year <= max_poss - 1970:
                    make_dt = lambda x: np.datetime64(x, "Y")
                    return years.apply(lambda x: pd.NaT if pd.isna(x)
                                                 else make_dt(x))
            # try months
            months = pd.Series(12 * (dates["year"] - 1970) + dates["month"] - 1)
            min_month = months.min()
            max_month = months.max()
            if min_month >= min_poss and max_month <= max_poss:
                make_dt = lambda x: np.datetime64(x, "M")
                return months.apply(lambda x: pd.NaT if pd.isna(x)
                                                else make_dt(x))

    # if series does not fit within consistent units, return mixed
    def to_datetime64_any_precision(x):
        if pd.isna(x):
            return pd.NaT
        result = None
        for u in ("ns", "us", "ms", "s", "m", "h", "D", "W"):
            scale_factor = _to_ns[u]
            if x % scale_factor:
                break
            rescaled = x / scale_factor
            if min_poss < rescaled < max_poss:
                result = np.datetime64(rescaled, u)
        if result:
            return result
        elif not x % _to_ns["D"]:  # try months and years
            date = days_to_date(x / _to_ns["D"])
            if (date["day"] == 1).all():
                if (date["month"] == 1).all():  # try years
                    year = (date["year"] - 1970)[0]
                    if min_poss < year < max_poss - 1970:
                        return year
                # try months
                month = (12 * (date["year"] - 1970) + date["month"] - 1)[0]
                if min_poss < month < max_poss:
                    return month
        raise OverflowError()  # stop at first bad value

    try:
        return series.apply(to_datetime64_any_precision)
    except OverflowError:
        err_msg = (f"[{error_trace(stack_index=2)}] series cannot be "
                    f"converted to datetime64 at any precision: values exceed "
                    f"64-bit limit for every choice of unit")
        raise OverflowError(err_msg)


def _to_numpy_datetime64_specific_unit(series: pd.Series,
                                       dtype_unit: str,
                                       min_val: int,
                                       max_val: int) -> pd.Series:
    min_poss = -2**63 + 1
    max_poss = 2**63 - 1

    # get units and scale factor from dtype
    scale_factor = _to_ns[dtype_unit]

    # check whether series values fit within available range for dtype_unit
    if (min_val / scale_factor < min_poss or
        max_val / scale_factor > max_poss):
        bad = series[(series < min_poss * scale_factor) |
                     (series > max_poss * scale_factor)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to datetime64[{dtype_unit}]: values "
                       f"exceed 64-bit range (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to datetime64[{dtype_unit}]: values "
                       f"exceed 64-bit range (indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to datetime64[{dtype_unit}]: values "
                       f"exceed 64-bit range (indices: [{shortened}, ...] "
                       f"({len(bad)}))")
        raise OverflowError(err_msg)

    make_td = lambda x: np.datetime64(round(x / scale_factor), dtype_unit)
    return series.apply(lambda x: pd.NaT if pd.isna(x) else make_td(x))


def _to_pandas_timedelta(series: pd.Series,
                         min_val: int,
                         max_val: int) -> pd.Series:
    min_poss = -2**63 + 1
    max_poss = 2**63 - 1

    # check whether series fits within timedelta64[ns] range
    if min_val < min_poss or max_val > max_poss:
        bad = series[(series < min_poss) | (series > max_poss)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to {pd.Timedelta}: values exceed "
                       f"64-bit limit for timedelta64[ns] (index: "
                       f"{list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to {pd.Timedelta}: values exceed "
                       f"64-bit limit for timedelta64[ns] (indices: "
                       f"{list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to {pd.Timedelta}: values exceed "
                       f"64-bit limit for timedelta64[ns] (indices: "
                       f"[{shortened}, ...] ({len(bad)}))")
        raise OverflowError(err_msg)

    # convert and return
    # pd.to_timedelta can't parse pd.NA by default for some reason
    return pd.to_timedelta(series.round(), unit="ns", errors="coerce")


def _to_datetime_timedelta(series: pd.Series,
                           min_val: int,
                           max_val: int) -> pd.Series:
    min_poss = total_nanoseconds(datetime.timedelta.min)
    max_poss = total_nanoseconds(datetime.timedelta.max)

    # check whether series fits within datetime.timedelta range/precision
    if min_val < min_poss or max_val > max_poss:
        bad = series[(series < min_poss) | (series > max_poss)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to {datetime.timedelta}: values "
                       f"exceed available range or have < us precision "
                       f"(index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to {datetime.timedelta}: values "
                       f"exceed available range or have < us precision "
                       f"(indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to {datetime.timedelta}: values "
                       f"exceed available range or have < us precision "
                       f"(indices: [{shortened}, ...] ({len(bad)}))")
        raise OverflowError(err_msg)

    # convert and return
    make_td = lambda x: datetime.timedelta(microseconds=float(x) / 1000)
    return series.apply(lambda x: pd.NaT if pd.isna(x) else make_td(x))


def _to_numpy_timedelta64_any_unit(series: pd.Series,
                                   min_val: int,
                                   max_val: int) -> pd.Series:
    min_poss = -2**63 + 1
    max_poss = 2**63 - 1

    # attempt to select a non-ns unit based on series range
    selected = None
    for unit in ("us", "ms", "s", "m", "h", "D", "W"):
        scale_factor = _to_ns[unit]
        if (series % scale_factor).any():
            break
        if (min_val / scale_factor > min_poss and
            max_val / scale_factor < max_poss):
            selected = unit
    if selected:
        scale_factor = _to_ns[selected]
        make_td = lambda x: np.timedelta64(round(x / scale_factor), selected)
        return series.apply(lambda x: pd.NaT if pd.isna(x) else make_td(x))

    # if series does not fit within consistent units, return mixed
    def to_timedelta64_any_precision(x):
        if pd.isna(x):
            return pd.NaT
        result = None
        for unit in ("ns", "us", "ms", "s", "m", "h", "D", "W"):
            scale_factor = _to_ns[unit]
            if x % scale_factor:
                break
            rescaled = x / scale_factor
            if min_poss < rescaled < max_poss:
                result = np.timedelta64(round(rescaled), unit)
        if result:
            return result
        raise OverflowError()  # stop at first bad value

    try:
        return series.apply(to_timedelta64_any_precision)
    except OverflowError:
        err_msg = (f"[{error_trace(stack_index=2)}] series cannot be converted "
                   f"to timedelta64 at any precision: values exceed 64-bit "
                   f"limit for every choice of unit")
        raise OverflowError(err_msg)


def _to_numpy_timedelta64_specific_unit(series: pd.Series,
                                        dtype_unit: str,
                                        min_val: int,
                                        max_val: int) -> pd.Series:
    min_poss = -2**63 + 1
    max_poss = 2**63 - 1
    scale_factor = _to_ns[dtype_unit]

    # check whether series values fit within available range for dtype_unit
    if (min_val / scale_factor < min_poss or
        max_val / scale_factor > max_poss):
        bad = series[(series < min_poss * scale_factor) |
                     (series > max_poss * scale_factor) |
                     (series % scale_factor != 0)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to timedelta64[{dtype_unit}]: values "
                       f"exceed 64-bit range (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to timedelta64[{dtype_unit}]: values "
                       f"exceed 64-bit range (indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace(stack_index=2)}] integer series could "
                       f"not be converted to timedelta64[{dtype_unit}]: values "
                       f"exceed 64-bit range (indices: [{shortened}, ...] "
                       f"({len(bad)}))")
        raise OverflowError(err_msg)

    make_td = lambda x: np.timedelta64(round(x / scale_factor), dtype_unit)
    return series.apply(lambda x: pd.NaT if pd.isna(x) else make_td(x))


###########################
####    Conversions    ####
###########################


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

    # round if appropriate
    if round:  # always round
        series = series.round()
    elif tol:  # round if within tolerance
        indices = (series - series.round()).abs() <= tol
        series[indices] = series[indices].round()

    # check for information loss
    if force:
        series = np.ceil(series.abs().clip(0, 1))
    elif not series.dropna().isin((0, 1)).all():
        bad = series[~series.isin((0, 1))].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] could not convert float to boolean "
                       f"without losing information: non-boolean value at "
                       f"index {list(bad)}")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] could not convert float to boolean "
                       f"without losing information: non-boolean values at "
                       f"indices {list(bad)}")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] could not convert float to boolean "
                       f"without losing information: non-boolean values at "
                       f"indices [{shortened}, ...] ({len(bad)})")
        raise ValueError(err_msg)

    # convert and return
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def to_integer(series: pd.Series,
               force: bool = False,
               round: bool = False,
               tol: float = 1e-6,
               downcast: bool = False,
               dtype: type = int) -> pd.Series:
    """_summary_

    Args:
        series (pd.Series): _description_
        force (bool, optional): _description_. Defaults to False.
        round (bool, optional): _description_. Defaults to False.
        tol (float, optional): _description_. Defaults to 1e-6.
        downcast (bool, optional): _description_. Defaults to False.
        dtype (type, optional): _description_. Defaults to int.

    Raises:
        TypeError: _description_
        TypeError: _description_
        ValueError: _description_
        OverflowError: _description_

    Returns:
        pd.Series: _description_
    """
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_integer_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be int-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # round/truncate if appropriate
    if round:  # always round
        series = series.round()
    elif force:
        series = np.trunc(series)
    else:
        rounded = series.round()
        if tol:  # round if within tolerance
            close_to_integer = (series - rounded <= tol)
            series[close_to_integer] = rounded[close_to_integer]

        # check for information loss
        if (series - rounded).any():
            bad = series[series - rounded != 0].index.values
            if len(bad) == 1:  # singular
                err_msg = (f"[{error_trace()}] series contains non-integer "
                           f"value at index {list(bad)}")
            elif len(bad) <= 5:  # plural
                err_msg = (f"[{error_trace()}] series contains non-integer "
                           f"values at indices {list(bad)}")
            else:  # plural, shortened
                shortened = ", ".join(str(i) for i in bad[:5])
                err_msg = (f"[{error_trace()}] series contains non-integer "
                           f"values at indices [{shortened}, ...] ({len(bad)})")
            raise ValueError(err_msg)

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # built-in integer special case - can be arbitrarily large
    if ((min_val < -2**63 or max_val > 2**63 - 1) and
        dtype in (int, "int", "i", "integer", "integers")):
        # these special cases are unaffected by downcast
        if min_val >= 0 and max_val < 2**64:  # > int64 but < uint64
            if series.hasnans:
                return series.astype(pd.UInt64Dtype())
            return series.astype(np.uint64)
        return series.apply(lambda x: pd.NA if pd.isna(x) else int(x))

    # convert to pandas dtype to expose itemsize attribute
    dtype = pd.api.types.pandas_dtype(dtype)

    # check that series fits within specified dtype
    if pd.api.types.is_unsinged_integer_dtype(dtype):
        min_poss = 0
        max_poss = 2**(8 * dtype.itemsize) - 1
    else:
        min_poss = -2**(8 * dtype.itemsize - 1)
        max_poss = 2**(8 * dtype.itemsize - 1) - 1
    if min_val < min_poss or max_val > max_poss:
        bad = series[(series < min_poss) | (series > max_poss)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"{dtype} (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"{dtype} (indices: {list(bad)})")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"{dtype} (indices: [{shortened}, ...] ({len(bad)}))")
        raise OverflowError(err_msg)

    # attempt to downcast if applicable
    if downcast:
        dtype = downcast_int_dtype(min_val, max_val, dtype)

    # convert and return
    if (series.hasnans and
        not pd.api.types.is_extension_array_dtype(dtype)):
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
        return series.astype(extension_types[dtype])
    return series.astype(dtype)


def to_float(series: pd.Series,
             downcast: bool = False,
             dtype: type = float) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_float_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be float-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # count original infinities
    original_infs = (series == np.inf)

    # convert
    series = series.astype(dtype)

    # check for overflow - count new infs introduced by coercion
    if ((series == np.inf) ^ original_infs).any():
        pandas_dtype = pd.api.types.pandas_dtype(dtype)
        bad = series[(series == np.inf) ^ original_infs].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"{pandas_dtype} (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"{pandas_dtype} (indices: {list(bad)})")
        else:  # plural, shortened
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"{pandas_dtype} (indices: [{shortened}, ...] "
                       f"({len(bad)}))")
        raise OverflowError(err_msg)

    # attempt to downcast if applicable
    if downcast:
        return series.apply(downcast_float)

    # return
    return series.astype(dtype)


def to_complex(series: pd.Series,
               downcast: bool = False,
               dtype: type = complex) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if not pd.api.types.is_complex_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be complex-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # count original infinities
    original_infs = (series == np.inf)

    # convert
    if dtype in (complex, "complex", "c"):
        # preserve precision
        if pd.api.types.is_object_dtype(series):
            float_type = series.apply(lambda x: np.dtype(type(x))).max()
        else:
            float_type = series.dtype
        equivalent_complex = {
            np.dtype(np.float16): np.dtype(np.complex64),
            np.dtype(np.float32): np.dtype(np.complex64),
            np.dtype(np.float64): np.dtype(np.complex128),
            np.dtype(np.longdouble): np.dtype(np.clongdouble)
        }
        dtype = equivalent_complex[float_type]
    series = series.astype(dtype)

    # check for overflow - count new infs introduced by coercion
    if ((series == np.inf) ^ original_infs).any():
        pandas_dtype = pd.api.types.pandas_dtype(dtype)
        bad = series[(series == np.inf) ^ original_infs].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"{pandas_dtype} (index: {list(bad)})")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"{pandas_dtype} (indices: {list(bad)})")
        else:  # plural, shortened
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values do not fit within "
                       f"{pandas_dtype} (indices: [{shortened}, ...] "
                       f"({len(bad)}))")
        raise OverflowError(err_msg)

    # attempt to downcast if applicable
    if downcast:
        return series.apply(downcast_complex)

    # return
    return series.astype(dtype)


def to_decimal(series: pd.Series) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if pd.api.types.is_object_dtype(series):
        series[series.isna()] = np.nan  # fillna loses precision if longdouble
    return series.apply(decimal.Decimal)


def to_datetime(
    series: pd.Series,
    unit: str = "s",
    offset: pd.Timestamp | datetime.datetime | None = None,
    tz: str | pytz.timezone | None = "local",
    dtype: type | str | np.dtype = np.datetime64) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if offset and not isinstance(offset, (pd.Timestamp, datetime.datetime,
                                          np.datetime64)):
        err_msg = (f"[{error_trace()}] `offset` must be an instance of "
                   f"pandas.Timestamp, datetime.datetime, or "
                   f"numpy.datetime64, not {type(offset)}")
        raise TypeError(err_msg)

    # convert series to nanoseconds
    if unit in _to_ns:
        series = series * _to_ns[unit]
    elif unit in ("Y", "year", "years"):
        series = pd.Series(date_to_days(1970 + series, 1, 1)) * _to_ns["D"]
    elif unit in ("M","month", "months"):
        series = pd.Series(date_to_days(1970, 1 + series, 1)) * _to_ns["D"]
    else:  # unrecognized unit
        valid_units = (list(_to_ns) +
                       ["M", "month", "months", "Y", "year", "years"])
        err_msg = (f"[{error_trace()}] could not interpret `unit` "
                   f"{repr(unit)}.  Must be in {valid_units}")
        raise ValueError(err_msg)

    # check for whole number of nanoseconds

    # get offset in nanoseconds since epoch
    if offset:
        # convert offset to UTC, assuming local time for naive offsets
        if isinstance(offset, (pd.Timestamp, datetime.datetime)):
            # np.datetime64 assumed UTC
            offset = to_utc(offset)
        series += ns_since_epoch(offset)

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # decide how to return datetimes.  4 options:
    # (1) pd.Timestamp == np.datetime64[ns] <- preferred
    # (2) datetime.datetime
    # (3) np.datetime64[us/ms/s/m/h/D/W/M/Y] <- any unit (can be mixed)
    # (4) np.datetime64[us/ms/s/m/h/D/W/M/Y] <- specific unit

    # pd.Timestamp - preferred to enable .dt namespace
    if ((min_val > -2**63 + 1 and max_val < 2**63 - 1) or
        dtype in (pd.Timestamp, "pandas.Timestamp", "pd.Timestamp")):
        return _to_pandas_timestamp(series, tz, min_val, max_val)

    # datetime.datetime - slightly wider range than datetime64[ns]
    if dtype in (datetime.datetime, "datetime.datetime"):
        return _to_datetime_datetime(series, tz, min_val, max_val)

    # np.datetime64 - auto detect unit (no timezones)
    if dtype in (np.datetime64, "numpy.datetime64", "np.datetime64",
                 "datetime64", np.dtype(np.datetime64), "M8", "<M8", ">M8"):
        return _to_numpy_datetime64_any_unit(series, min_val, max_val)

    # np.datetime64 - dtype has specific unit (no timezones)
    if pd.api.types.is_datetime64_dtype(dtype):
        dtype_unit = time_unit(dtype)  # get unit from given dtype
        return _to_numpy_datetime64_specific_unit(series, dtype_unit, min_val,
                                                  max_val)

    # dtype is unrecognized
    err_msg = (f"[{error_trace()}] could not interpret `dtype`: {dtype}")
    raise ValueError(err_msg)


def to_timedelta(
    series: pd.Series,
    unit: str = "s",
    offset: pd.Timedelta | datetime.timedelta | np.timedelta64 | None = None,
    calendar_offset: pd.Timestamp | datetime.datetime | np.datetime64 | None = None,
    dtype: type | str | np.dtype = np.timedelta64) -> pd.Series:
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)
    if offset and not isinstance(offset, (pd.Timedelta, datetime.timedelta,
                                          np.timedelta64)):
        err_msg = (f"[{error_trace()}] `offset` must be an instance of "
                   f"pandas.Timedelta, datetime.timedelta, or "
                   f"numpy.timedelta64, not {type(offset)}")
        raise TypeError(err_msg)

    # convert series to nanoseconds
    if unit in _to_ns:
        series = series * _to_ns[unit]
    elif unit in ("M", "month", "months", "Y", "year", "years"):
        # account for leap years, unequal month lengths
        if calendar_offset:
            if isinstance(calendar_offset, np.datetime64):
                components = datetime64_components(calendar_offset)
                Y = components["year"]
                M = components["month"]
                D = components["day"]
            else:
                Y = calendar_offset.year
                M = calendar_offset.month
                D = calendar_offset.day
        else:
            Y = 1970
            M = 1
            D = 1
        if unit in ("Y", "year", "years"):
            series = pd.Series(date_to_days(Y + series, M, D) -
                               date_to_days(Y, M, D)) * _to_ns["D"]
        else:  # unit in ("M", "month", "months")
            series = pd.Series(date_to_days(Y, M + series, D) -
                               date_to_days(Y, M, D)) * _to_ns["D"]
    else:  # unrecognized unit
        valid_units = (list(_to_ns) +
                       ["M", "month", "months", "Y", "year", "years"])
        err_msg = (f"[{error_trace()}] could not interpret `unit` "
                   f"{repr(unit)}.  Must be in {valid_units}")
        raise ValueError(err_msg)

    # apply offset in nanoseconds
    if offset:
        series += total_nanoseconds(offset)

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # decide how to return timedeltas.  4 options:
    # (1) pd.Timedelta == np.timedelta64[ns] <- preferred
    # (2) datetime.timedelta
    # (3) np.timedelta64[us/ms/s/m/h/D/W/M/Y] <- any unit (can be mixed)
    # (4) np.timedelta64[us/ms/s/m/h/D/W/M/Y] <- specific unit

    # pd.Timedelta - preferred to enable .dt namespace
    if ((min_val > -2**63 + 1 and max_val < 2**63 - 1) or 
        dtype in (pd.Timedelta, "pandas.Timedelta", "pd.Timedelta")):
        return _to_pandas_timedelta(series, min_val, max_val)

    # datetime.timedelta - slightly wider range than timedelta64[ns]
    if dtype in (datetime.timedelta, "datetime.timedelta"):
        return _to_datetime_timedelta(series, min_val, max_val)

    # np.timedelta64 - auto detect unit (no timezones)
    if dtype in (np.timedelta64, "numpy.timedelta64", "np.timedelta64",
                 "timedelta64", np.dtype(np.timedelta64), "m8", "<m8", ">m8"):
        return _to_numpy_timedelta64_any_unit(series, min_val, max_val)

    # np.timedelta64 - dtype has specific unit (no timezones)
    if pd.api.types.is_timedelta64_dtype(dtype):
        dtype_unit = time_unit(dtype)  # get units from dtype
        return _to_numpy_timedelta64_specific_unit(series, dtype_unit, min_val,
                                                   max_val)

    # dtype is unrecognized
    err_msg = (f"[{error_trace()}] could not interpret `dtype`: {dtype}")
    raise ValueError(err_msg)


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

    if pd.api.types.is_object_dtype(series):
        series[series.isna()] = np.nan  # fillna loses precision if longdouble
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


def to_categorical(series: pd.Series,
                   categories: list | np.ndarray | pd.Series | None = None,
                   ordered: bool = False) -> pd.Series:
    values = pd.Categorical(series, categories=categories, ordered=ordered)
    return pd.Series(values)
