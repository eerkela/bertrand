from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd
import pytz
import tzlocal

from pdtypes.error import error_trace
from pdtypes.util.parse import (
    available_range
)
from pdtypes.util.downcast import (
    downcast_complex, downcast_float, downcast_int_dtype
)
from pdtypes.util.time import (
    _to_ns, date_to_days, datetime64_components, days_to_date, ns_since_epoch,
    time_unit, to_utc, total_nanoseconds
)


# pd.Series([1.0, 2.0]).view("S8").astype("O")


"""
Test Cases:
-   infinities
"""

# TODO: allow sparse dtypes and sparse flag


#######################
####    Helpers    ####
#######################


def _to_pandas_timestamp(series: pd.Series,
                         tz: str | datetime.tzinfo | None) -> pd.Series:
    # initialize as utc datetimes
    series = pd.to_datetime(series.round(), unit="ns", utc=True)

    # localize to final timezone
    if tz is None:
        return series.dt.tz_localize(None)
    if tz == "local":
        tz = tzlocal.get_localzone_name()
    return series.dt.tz_convert(tz)


def _to_datetime_datetime(series: pd.Series,
                          tz: str | datetime.tzinfo | None) -> pd.Series:
    # conversion function
    def make_dt(ns: int | None) -> datetime.datetime:
        if pd.isna(ns):
            return pd.NaT
        utc = datetime.timezone.utc
        result = datetime.datetime.fromtimestamp(ns / int(1e9), utc)
        if tz is None:
            return result.replace(tzinfo=None)
        if tz == "local":
            return result.astimezone()  # automatically localizes
        if isinstance(tz, datetime.timezone):
            return result.astimezone(tz)
        return result.astimezone(pytz.timezone(tz))

    # construct new object series - prevents automatic coercion to pd.Timestamp.
    # This is also marginally faster than series.apply, for some reason
    return pd.Series([make_dt(ns) for ns in series], dtype="O")


def _to_numpy_datetime64_consistent_unit(series: pd.Series,
                                         min_val: int,
                                         max_val: int) -> pd.Series:
    """Helper to convert integer series to np.datetime64 w/ consistent units."""
    # TODO: 
    # try microseconds, milliseconds, seconds, minutes, hours, days, weeks
    selected = None
    for test_unit in ("us", "ms", "s", "m", "h", "D", "W"):
        scale_factor = _to_ns[test_unit]
        if (series % scale_factor).any():  # check for residuals
            break
        if (min_val / scale_factor > -2**63 + 1 and
            max_val / scale_factor < 2**63 - 1):  # within 64-bit range?
            selected = test_unit
    if selected:  # matched
        scale_factor = _to_ns[selected]
        make_dt = lambda x: np.datetime64(round(x / scale_factor), selected)
        return series.apply(lambda x: pd.NaT if pd.isna(x) else make_dt(x))

    # try months, years
    if not (series % _to_ns["D"]).any():  # integer number of days
        # convert nanoseconds to days, then days to dates
        dates = days_to_date(series / _to_ns["D"])
        if (dates["day"] == 1).all():  # integer number of months
            if (dates["month"] == 1).all():  # integer number of years
                # try years
                years = pd.Series(dates["year"] - 1970)
                min_year = years.min()
                max_year = years.max()
                if min_year >= -2**63 + 1 and max_year <= 2**63 - 1 - 1970:
                    make_dt = lambda x: np.datetime64(x, "Y")
                    return years.apply(lambda x: pd.NaT if pd.isna(x)
                                                 else make_dt(x))
            # try months
            months = pd.Series(12 * (dates["year"] - 1970) + dates["month"] - 1)
            min_month = months.min()
            max_month = months.max()
            if min_month >= -2**63 + 1 and max_month <= 2**63 - 1:
                make_dt = lambda x: np.datetime64(x, "M")
                return months.apply(lambda x: pd.NaT if pd.isna(x)
                                                else make_dt(x))

    # could not find a consistent unit to represent series -> raise ValueError
    raise ValueError("no consistent unit could be detected for series")


def _to_numpy_datetime64_mixed_unit(series: pd.Series) -> pd.Series:
    """Helper to convert integer series to np.datetime64 w/ mixed units."""
    def to_datetime64_any_precision(element):  # elementwise conversion
        # basically elementwise version of _to_numpy_datetime64_consistent_unit
        if pd.isna(element):  # na base case
            return pd.NaT

        # try ns, us, ms, s, m, h, D by minimizing residual
        result = None
        min_residual = 1
        for test_unit in ("ns", "us", "ms", "s", "m", "h", "D", "M", "Y"):
            if test_unit == "M":
                scale_factor = 365.2425 / 12 * 24 * 60 * 60 * int(1e9)
            elif test_unit == "Y":
                scale_factor = 365.2425 * 24 * 60 * 60 * int(1e9)
            else:
                scale_factor = _to_ns[test_unit]
            # if element % scale_factor:
            #     break
            rescaled = int(np.round(element) / scale_factor)
            residual = (element % scale_factor) / element
            if -2**63 + 1 <= rescaled <= 2**63 - 1 and residual <= min_residual:
                result = np.datetime64(rescaled, test_unit)
                min_residual = residual
        if result:  # matched
            return result

        # wierd overflow behavior observed with unit="W" that makes it
        # impractical.  Values > 1317624576693540966 ((2**63 - 1) // 7 + 1565)
        # wrap to negative, and values < -1317624576693537835 (-max + 3131)
        # wrap to positive.  As a result, "W" has only 30 more years of range
        # than "D", which is unlikely to be useful (25252734927768554-07-27 vs.
        # 25252734927768524-07-25).

        # try months, years
        # TODO: do these units even make sense?  -> use avgs instead
        # if not element % _to_ns["D"]:  # integer number of days
        #     # convert nanoseconds to days, then days to dates
        #     date = days_to_date(element / _to_ns["D"])
        #     if (date["day"] == 1).all():  # integer number of months
        #         if (date["month"] == 1).all():  # integer number of years
        #             # try years
        #             year = (date["year"] - 1970)[0]
        #             if -2**63 + 1 < year < 2**63 - 1 - 1970:
        #                 return year
        #         # try months
        #         month = (12 * (date["year"] - 1970) + date["month"] - 1)[0]
        #         if -2**63 + 1 < month < 2**63 - 1:
        #             return month

        # tried all units -> stop at first bad value
        err_msg = (f"invalid nanosecond value for np.datetime64: {element}")
        raise OverflowError(err_msg)

    try:
        return series.apply(to_datetime64_any_precision)
    except OverflowError as err:
        err_msg = (f"[{error_trace(stack_index=2)}] series cannot be "
                   f"represented as pd.Timestamp, datetime.datetime, or "
                   f"np.datetime64 with any choice of unit")
        raise OverflowError(err_msg) from err


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


def to_boolean(
    series: pd.Series,
    force: bool = False,
    round: bool = False,
    tol: float = 1e-6,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = bool
) -> pd.Series:
    """Convert a series of floating point numbers to their boolean equivalents.

    Args:
        series (pd.Series):
            series to be converted.
        force (bool, optional):
            if True, coerces non-boolean values (i.e. not [0.0, 1.0, NA]) to
            their boolean equivalents, producing the same output as `bool(x)`.
            If False, throws a ValueError instead.  Defaults to False.
        round (bool, optional):
            if True, round series values to the nearest integer before
            converting.  Defaults to False.
        tol (float, optional):
            floating point tolerance.  Values that are within this amount from
            an integer are rounded to said integer before converting.  Can be
            used to correct for floating point rounding errors introduced
            during arithmetic, for instance.  Must be a value between 0 and 1.
            Defaults to 1e-6.
        dtype (type | str | np.dtype | pd.api.extensions.ExtensionDtype,
               optional):
            final dtype of the resulting output.  Must be bool-like.  Defaults
            to bool.

    Raises:
        TypeError: if `series` does not contain floating point data.
        TypeError: if `dtype` is not bool-like.
        ValueError: if `force=False` and `round=False`, and `series` contains
            values which would lose precision during conversion (i.e. not
            [0.0, 1.0, NA]).

    Returns:
        pd.Series: series containing boolean equivalent of input series.
    """
    # check series contains floats
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check dtype is bool-like
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # round if appropriate
    if round:  # always round
        series = series.round()
    elif tol:  # round only if within tolerance
        indices = (series - series.round()).abs() <= tol
        series[indices] = series[indices].round()

    # coerce if applicable
    if force:
        # series.abs() doesn't work on object series with None as missing value
        if series.hasnans and pd.api.types.is_object_dtype(series):
            series = series.fillna(np.nan)
        series = np.ceil(series.abs().clip(0, 1))  # coerce vals to [0, 1, nan]

    # check for information loss
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

    # TODO: allow sparse

    # convert and return
    if series.hasnans:
        return series.astype(pd.BooleanDtype())
    return series.astype(dtype)


def to_integer(
    series: pd.Series,
    force: bool = False,
    round: bool = False,
    tol: float = 1e-6,
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = int
) -> pd.Series:
    """Convert a series of floating point numbers to their integer equivalents.

    Args:
        series (pd.Series):
            series to be converted.
        force (bool, optional):
            if True, coerce non-integer values by truncating, just like
            `int(x)`.  Defaults to False.
        round (bool, optional):
            if True, coerce non-integer values by rounding to nearest integer.
            Defaults to False.
        tol (float, optional):
            floating point tolerance.  Coerce non-integer values by rounding
            to the nearest integer if the distance between the two is less than
            the given amount.  This can be used to correct for floating point
            rounding errors introduced during arithmetic, for instance.  Must
            be a value between 0 and 1.  A setting of 0 disables conditional
            rounding.  Defaults to 1e-6.
        downcast (bool, optional):
            if True, attempts to reduce the byte size of `dtype` to match
            the underlying data and save memory.  Defaults to False.
        dtype (type | str | np.dtype | pd.api.extensions.ExtensionDtype,
               optional):
            final dtype of the resulting output.  Must be integer-like.
            Defaults to int.

    Raises:
        TypeError: if `series` does not contain floating point data.
        TypeError: if `dtype` is not integer-like.
        ValueError: if `force=False` and `round=False`, and significant digits
            greater than `tol` would be lost during conversion.
        OverflowError: if values of `series` exceed available range for `dtype`.

    Returns:
        pd.Series: series containing integer equivalent of input series.
    """
    # check series contains float data
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check dtype is int-like
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

    # get min/max to evaluate range - longdouble maintains integer precision
    # for entire 64-bit range, prevents inconsistent comparison
    min_val = np.longdouble(series.min())
    max_val = np.longdouble(series.max())

    # built-in integer special case - can be arbitrarily large
    if ((min_val < -2**63 or max_val > 2**63 - 1) and
        dtype in (int, "int", "i", "integer", "integers")):
        # these special cases are unaffected by downcast
        # longdouble can't be compared to extended python integer (> 2**63 - 1)
        if min_val >= 0 and max_val < np.uint64(2**64 - 1):  # > i8 but < u8
            if series.hasnans:
                return series.astype(pd.UInt64Dtype())
            return series.astype(np.uint64)
        return series.apply(lambda x: pd.NA if pd.isna(x) else int(x))

    # convert to pandas dtype to expose itemsize attribute
    dtype = pd.api.types.pandas_dtype(dtype)

    # check that series fits within specified dtype
    min_poss, max_poss = available_range(dtype)
    if min_val < min_poss or max_val > max_poss:
        bad = series[(series < min_poss) | (series > max_poss)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                       f"index: {list(bad)}")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                       f"indices: {list(bad)}")
        else:  # plural, shortened for brevity
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                       f"indices: [{shortened}, ...] ({len(bad)})")
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


def to_float(
    series: pd.Series,
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype |
           pd.SparseDtype = float
) -> pd.Series:
    """Convert a series of floating point numbers to another float dtype.

    Args:
        series (pd.Series):
            series to be converted.
        downcast (bool, optional):
            if True, attempts to reduce the byte size of `dtype` to match
            the underlying data and save memory.  Defaults to False.
        dtype (type | str | np.dtype | pd.api.extensions.ExtensionDtype |
               pd.SparseDtype, optional):
            final dtype of the resulting output.  Must be float-like.
            Defaults to int.

    Raises:
        TypeError: if `series` does not contain floating point data
        TypeError: if `dtype` is not float-like
        OverflowError: if values of `series` exceed available range for `dtype`.

    Returns:
        pd.Series: copy of input series with new dtype.
    """
    # check series contains float data
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check dtype is float-like
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

    # TODO: allow sparse

    # return
    return series.astype(dtype)


def to_complex(
    series: pd.Series,
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype |
           pd.SparseDtype = complex
) -> pd.Series:
    """Convert a series of floating point numbers to their complex equivalents.

    Args:
        series (pd.Series):
            series to be converted.
        downcast (bool, optional):
            if True, attempts to reduce the byte size of `dtype` to match
            the underlying data and save memory.  Defaults to False.
        dtype (type | str | np.dtype | pd.api.extensions.ExtensionDtype |
               pd.SparseDtype, optional):
            final dtype of the resulting output.  Must be float-like.
            Defaults to int.

    Raises:
        TypeError: if `series` does not contain floating point data.
        TypeError: if `dtype` is not complex-like.
        OverflowError: if values of `series` exceed available range for `dtype`.

    Returns:
        pd.Series: series containing complex equivalent of input series.
    """
    # check series contains float data
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check dtype is complex-like
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

    # TODO: allow sparse

    # return
    return series.astype(dtype)


def to_decimal(series: pd.Series) -> pd.Series:
    """Convert a series of floating point numbers to their arbitrary precision
    `decimal.Decimal` equivalents.

    Args:
        series (pd.Series):
            series to be converted.

    Raises:
        TypeError: if `series` does not contain floating point data.

    Returns:
        pd.Series: series containing decimal equivalent of input series.
    """
    # check series contains float data
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # convert and return
    if pd.api.types.is_object_dtype(series):
        series[series.isna()] = np.nan  # fillna loses precision if longdouble
    return series.apply(decimal.Decimal)


def to_datetime(
    series: pd.Series,
    unit: str = "s",
    offset: pd.Timestamp | datetime.datetime | None = None,
    tz: str | pytz.timezone | None = "local") -> pd.Series:
    """_summary_

    Args:
        series (pd.Series): _description_
        unit (str, optional): _description_. Defaults to "s".
        offset (pd.Timestamp | datetime.datetime | None, optional): _description_. Defaults to None.
        tz (str | pytz.timezone | None, optional): _description_. Defaults to "local".

    Raises:
        TypeError: _description_
        TypeError: _description_
        ValueError: _description_
        ValueError: _description_

    Returns:
        pd.Series: _description_
    """
    # check series contains float data
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check offset is datetime-like, if provided
    if offset:
        if isinstance(offset, str):
            offset = np.datetime64(offset)
        if not isinstance(offset, (pd.Timestamp, datetime.datetime,
                                   np.datetime64)):
            err_msg = (f"[{error_trace()}] `offset` must be an instance of "
                       f"pandas.Timestamp, datetime.datetime, or "
                       f"numpy.datetime64, not {type(offset)}")
            raise TypeError(err_msg)

    # convert series to nanoseconds
    if unit in _to_ns:
        series = series.astype(np.longdouble) * _to_ns[unit]
    elif unit in ("Y", "year", "years"):
        series = pd.Series(date_to_days(1970 + series, 1, 1), dtype=np.longdouble) * _to_ns["D"]
    elif unit in ("M","month", "months"):
        series = pd.Series(date_to_days(1970, 1 + series, 1), dtype=np.longdouble) * _to_ns["D"]
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
    min_val = int(np.round(series.min()))  # converted to nearest ns
    max_val = int(np.round(series.max()))  # pd.to_datetime does this anyways

    # decide how to return datetimes.  4 options:
    # (1) pd.Timestamp == np.datetime64[ns] <- preferred
    # (2) datetime.datetime
    # (3) np.datetime64[us/ms/s/m/h/D/W/M/Y] <- any unit (can be mixed)
    # (4) np.datetime64[us/ms/s/m/h/D/W/M/Y] <- specific unit

    # pd.Timestamp - preferred to enable .dt namespace
    if min_val >= -2**63 + 1 and max_val <= 2**63 - 1:
        return _to_pandas_timestamp(series, tz)

    # datetime.datetime - slightly wider range than datetime64[ns]
    if (min_val >= ns_since_epoch(datetime.datetime.min) + 86400 * int(1e9) and
        max_val <= ns_since_epoch(datetime.datetime.max) - 86400 * int(1e9)):
        return _to_datetime_datetime(series, tz)

    # np.datetime64 - consistent units
    try:
        return _to_numpy_datetime64_consistent_unit(series, min_val, max_val)
    except ValueError:
        pass

    # np.datetime64 - mixed units
    return _to_numpy_datetime64_mixed_unit(series)


def to_timedelta(
    series: pd.Series,
    unit: str = "s",
    offset: pd.Timedelta | datetime.timedelta | np.timedelta64 | None = None,
    calendar_offset: pd.Timestamp | datetime.datetime | np.datetime64 | None = None,
    dtype: type | str | np.dtype = np.timedelta64) -> pd.Series:
    """_summary_

    Args:
        series (pd.Series): _description_
        unit (str, optional): _description_. Defaults to "s".
        offset (pd.Timedelta | datetime.timedelta | np.timedelta64 | None, optional): _description_. Defaults to None.
        calendar_offset (pd.Timestamp | datetime.datetime | np.datetime64 | None, optional): _description_. Defaults to None.
        dtype (type | str | np.dtype, optional): _description_. Defaults to np.timedelta64.

    Raises:
        TypeError: _description_
        TypeError: _description_
        ValueError: _description_
        ValueError: _description_

    Returns:
        pd.Series: _description_
    """
    # check series contains float data
    if pd.api.types.infer_dtype(series) != "floating":
        err_msg = (f"[{error_trace()}] `series` must contain float data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check offset is timedelta-like, if provided
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


def to_string(
    series: pd.Series,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype |
           pd.SparseDtype = str
) -> pd.Series:
    """_summary_

    Args:
        series (pd.Series): _description_
        dtype (type | str | np.dtype | pd.api.extensions.ExtensionDtype | pd.SparseDtype, optional): _description_. Defaults to str.

    Raises:
        TypeError: _description_
        TypeError: _description_

    Returns:
        pd.Series: _description_
    """
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


def to_categorical(
    series: pd.Series,
    categories: list | np.ndarray | pd.Series | None = None,
    ordered: bool = False
) -> pd.Series:
    """_summary_

    Args:
        series (pd.Series): _description_
        categories (list | np.ndarray | pd.Series | None, optional): _description_. Defaults to None.
        ordered (bool, optional): _description_. Defaults to False.

    Returns:
        pd.Series: _description_
    """
    values = pd.Categorical(series, categories=categories, ordered=ordered)
    return pd.Series(values)
