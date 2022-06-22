"""This module contains conversion functions to allow the conversion of
any pandas series containing integer data to any other data type recognized
by `pandas.api.types.infer_dtype`.
"""
from __future__ import annotations
import datetime
import decimal
import warnings

import numpy as np
import pandas as pd
import tzlocal
import pytz
import zoneinfo

from pdtypes.error import error_trace
from pdtypes.util.parse import available_range
from pdtypes.util.downcast import (
    downcast_complex, downcast_float, downcast_int_dtype, to_sparse
)
from pdtypes.util.time import (
    _to_ns, convert_datetime_type, convert_unit, date_to_days, datetime64_components, days_to_date, ns_since_epoch,
    to_utc, total_nanoseconds, total_units, units_since_epoch, to_unit,
    datetime_like, timedelta_like, convert_iso_string
)


"""
TODO: Test Cases:
-   integer series as object
-   greater than 64-bit
-   integer series that fit within uint64, but not int64
-   integer object series with None instead of nan or pd.NA
"""

# TODO: allow automatic conversion to SparseDtype if frequently occuring val


#######################
####    Helpers    ####
#######################


def _to_pandas_timestamp(series: pd.Series,
                         tz: str | datetime.tzinfo | None) -> pd.Series:
    """Helper to convert integer series to pd.Timestamp w/ given tz."""
    # initialize as utc timestamps
    series = pd.to_datetime(series, unit="ns", utc=True)

    # localize to final timezone
    if tz is None:
        return series.dt.tz_localize(None)
    if tz == "local":
        tz = tzlocal.get_localzone_name()
    return series.dt.tz_convert(pytz.timezone(tz))


def _to_datetime_datetime(series: pd.Series,
                          tz: str | datetime.tzinfo | None) -> pd.Series:
    """Helper to convert integer series to datetime.datetime w/ given tz."""
    # conversion function
    def make_dt(ns: int | None | np.nan | pd.NA,
                tz: str | datetime.tzinfo | None) -> datetime.datetime:
        if pd.isna(ns):
            return pd.NaT
        utc = datetime.timezone.utc
        result = datetime.datetime.fromtimestamp(ns // int(1e9), utc)
        result += datetime.timedelta(microseconds=(ns % int(1e9) // 1000))
        if tz is None:
            return result.replace(tzinfo=None)
        if isinstance(tz, datetime.tzinfo):
            return result.astimezone(tz)
        if tz == "local":
            tz = tzlocal.get_localzone_name()
        return result.astimezone(pytz.timezone(tz))

    # construct new object series - prevents automatic coercion to pd.Timestamp.
    # This is also marginally faster than series.apply, for some reason
    return pd.Series([make_dt(ns, tz) for ns in series], dtype="O")


def _to_numpy_datetime64_consistent_unit(series: pd.Series,
                                         min_val: int,
                                         max_val: int) -> pd.Series:
    """Helper to convert integer series to np.datetime64 w/ consistent unit."""
    # try us, ms, s, m, h, D
    selected = None
    for test_unit in ("us", "ms", "s", "m", "h", "D"):
        scale_factor = _to_ns[test_unit]
        if (series % scale_factor).any():  # check for residuals
            break
        if (min_val // scale_factor >= -2**63 + 1 and
            max_val // scale_factor <= 2**63 - 1):  # within 64-bit range?
            selected = test_unit
    if selected:  # matched
        scale_factor = _to_ns[selected]
        make_dt = lambda x: np.datetime64(x // scale_factor, selected)
        return series.apply(lambda x: pd.NaT if pd.isna(x) else make_dt(x))

    # wierd overflow behavior observed with unit="W" that makes it impractical.
    # Values > 1317624576693540966 ((2**63 - 1) // 7 + 1565) wrap to negative,
    # and values < -1317624576693537835 (-max + 3131) wrap to positive.  As a
    # result, "W" has only 30 more years of range than "D", which is unlikely
    # to be useful (25252734927768554-07-27 vs. 25252734927768524-07-25).

    # try months, years
    if not (series % _to_ns["D"]).any():  # integer number of days
        # convert nanoseconds to days, then days to dates
        dates = days_to_date(series // _to_ns["D"])
        if (dates["day"] == 1).all():  # integer number of months
            if (dates["month"] == 1).all():  # integer number of years
                # try years
                years = pd.Series(dates["year"] - 1970)
                min_year = years.min()
                max_year = years.max()
                if min_year >= -2**63 + 1 and max_year <= (2**63 - 1) - 1970:
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

        # try ns, us, ms, s, m, h, D
        result = None
        for test_unit in ("ns", "us", "ms", "s", "m", "h", "D"):
            scale_factor = _to_ns[test_unit]
            if element % scale_factor:  # check for residual
                break
            rescaled = element // scale_factor
            if -2**63 + 1 <= rescaled <= 2**63 - 1:  # within 64-bit range?
                result = np.datetime64(rescaled, test_unit)
        if result:  # matched
            return result

        # wierd overflow behavior observed with unit="W" that makes it
        # impractical.  Values > 1317624576693540966 ((2**63 - 1) // 7 + 1565)
        # wrap to negative, and values < -1317624576693537835 (-max + 3131)
        # wrap to positive.  As a result, "W" has only 30 more years of range
        # than "D", which is unlikely to be useful (25252734927768554-07-27 vs.
        # 25252734927768524-07-25).

        # try months and years
        if not element % _to_ns["D"]:  # integer number of days
            # convert nanoseconds to days, then days to dates
            date = days_to_date(element // _to_ns["D"])
            if (date["day"] == 1).all():  # integer number of months
                if (date["month"] == 1).all():  # integer number of years
                    # try years
                    year = (date["year"] - 1970)[0]
                    if -2**63 + 1 <= year <= (2**63 - 1) - 1970:
                        return year
                # try months
                month = (12 * (date["year"] - 1970) + date["month"] - 1)[0]
                if -2**63 + 1 <= month <= 2**63 - 1:
                    return month

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


def _to_numpy_timedelta64_consistent_unit(
    series: pd.Series,
    min_val: int,
    max_val: int,
    start: pd.Timestamp | datetime.datetime | np.datetime64 | None
) -> pd.Series:
    """Helper to convert integer series to np.timedelta64 w/ consistent unit."""
    # try microseconds, milliseconds, seconds, minutes, hours, days, weeks
    selected = None
    for unit in ("us", "ms", "s", "m", "h", "D", "W"):
        scale_factor = _to_ns[unit]
        if (series % scale_factor).any():  # check for residuals
            break
        if (min_val // scale_factor >= -2**63 + 1 and
            max_val // scale_factor <= 2**63 - 1):  # within 64-bit range?
            selected = unit
    if selected:  # matched
        scale_factor = _to_ns[selected]
        make_td = lambda x: np.timedelta64(x // scale_factor, selected)
        return series.apply(lambda x: pd.NaT if pd.isna(x) else make_td(x))

    # get components of start, if provided
    if start is None:
        starting_year = 1970
        starting_month = 1
        starting_day = 1
    elif isinstance(start, np.datetime64):
        components = datetime64_components(start)
        starting_year = components["year"]
        starting_month = components["month"]
        starting_day = components["day"]
    else:
        starting_year = start.year
        starting_month = start.month
        starting_day = start.day

    # try months, years
    if not (series % _to_ns["D"]).any():  # integer days
        # convert nanoseconds to days, then days to dates
        dates = days_to_date(series // _to_ns["D"])
        if (dates["day"] == starting_day).all():  # integer months
            if (dates["month"] == starting_month).all():  # integer years
                # try years
                years = pd.Series(dates["year"] - starting_year)
                min_year = years.min()
                max_year = years.max()
                if min_year >= -2**63 + 1 and max_year <= 2**63 - 1:
                    make_dt = lambda x: np.timedelta64(x, "Y")
                    return years.apply(lambda x: pd.NaT if pd.isna(x)
                                                 else make_dt(x))
            # try months
            months = pd.Series(12 * (dates["year"] - starting_year) +
                               dates["month"] - 1)
            min_month = months.min()
            max_month = months.max()
            if min_month >= -2**63 + 1 and max_month <= 2**63 - 1:
                make_dt = lambda x: np.timedelta64(x, "M")
                return months.apply(lambda x: pd.NaT if pd.isna(x)
                                                else make_dt(x))

    # could not find a consistent unit to represent series -> raise ValueError
    raise ValueError("no consistent unit could be detected for series")


def _to_numpy_timedelta64_mixed_unit(
    series: pd.Series,
    start: pd.Timestamp | datetime.datetime | np.datetime64 | None
) -> pd.Series:
    """Helper to convert integer series to np.timedelta64 w/ mixed units."""
    # get components of start, if provided
    if start is None:
        starting_year = 1970
        starting_month = 1
        starting_day = 1
    elif isinstance(start, np.datetime64):
        components = datetime64_components(start)
        starting_year = components["year"]
        starting_month = components["month"]
        starting_day = components["day"]
    else:
        starting_year = start.year
        starting_month = start.month
        starting_day = start.day

    # try mixed units
    def to_timedelta64_any_precision(element):  # elementwise conversion
        # basically elementwise version of _to_numpy_timedelta64_consistent_unit
        if pd.isna(element):
            return pd.NaT
        result = None
        for unit in ("ns", "us", "ms", "s", "m", "h", "D", "W"):
            scale_factor = _to_ns[unit]
            if element % scale_factor:
                break
            rescaled = element // scale_factor
            if -2**63 + 1 <= rescaled <= 2**63 - 1:
                result = np.timedelta64(rescaled, unit)
        if result:
            return result
        if not element % _to_ns["D"]:  # try months and years
            date = days_to_date(element // _to_ns["D"])
            if (date["day"] == starting_day).all():
                if (date["month"] == starting_month).all():  # try years
                    year = (date["year"] - starting_year)[0]
                    if -2**63 + 1 <= year <= 2**63 - 1:
                        return year
                # try months
                month = (12 * (date["year"] - starting_year) +
                         date["month"] - 1)[0]
                if -2**63 + 1 <= month <= 2**63 - 1:
                    return month
        err_msg = (f"invalid nanosecond value for np.timedelta64: {element}")
        raise OverflowError(err_msg)  # stop at first bad value

    try:
        return series.apply(to_timedelta64_any_precision)
    except OverflowError as err:
        err_msg = (f"[{error_trace(stack_index=2)}] series cannot be "
                   f"represented as pd.Timedelta, datetime.timedelta, or "
                   f"np.timedelta64 with any choice of unit")
        raise OverflowError(err_msg) from err


###########################
####    Conversions    ####
###########################


def to_boolean(
    series: pd.Series,
    force: bool = False,
    sparse: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = bool
) -> pd.Series:
    """Convert a series containing integer data to its boolean equivalent.

    Args:
        series (pandas.Series):
            series to be converted.
        force (bool, optional):
            if True, coerces non-boolean values (i.e. not [0, 1, NA]) to their
            boolean equivalents, producing the same output as `bool(x)`. If
            False, throws a ValueError instead.  Defaults to False.
        sparse (bool, optional):
            if True, detect the most commonly occuring element (can be non-NA)
            and convert to a sparse series, with the chosen element masked
            from view.  Can significnatly increase performance on sparse data.
            Defaults to False.
        dtype (type | str | np.dtype | pd.api.extensions.ExtensionDtype,
               optional):
            final dtype of the resulting output.  Must be bool-like.  Defaults
            to bool.

    Raises:
        TypeError: if `series` does not contain integer data.
        TypeError: if `dtype` is not bool-like.
        ValueError: if `force=False` and `series` contains values which would
            lose precision during conversion (i.e. not [0, 1, NA]).

    Returns:
        pd.Series: series containing boolean equivalent of input series.

    TODO: Test Cases:
        - boolean input [0, 1, NA]
        - non-boolean input (negative numbers, > 1) with force=False
        - non-boolean input (negative numbers, > 1) with force=True
        - every choice of dtype
    """
    # check series contains integer data
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check dtype is boolean-like
    if not pd.api.types.is_bool_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be bool-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # coerce if applicable
    if force:
        # series.abs() doesn't work on object series with None as missing value
        if series.hasnans and pd.api.types.is_object_dtype(series):
            series = series.fillna(pd.NA)
        series = series.abs().clip(0, 1)  # series elements in [0, 1, pd.NA]

    # check for information loss
    elif series.min() < 0 or series.max() > 1:
        bad = series[(series < 0) | (series > 1)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] non-boolean value at index "
                       f"{list(bad)}")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] non-boolean values at indices "
                       f"{list(bad)}")
        else:  # plural, shortened
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] non-boolean values at indices "
                       f"[{shortened}, ...] ({len(bad)})")
        raise ValueError(err_msg)

    # convert to boolean
    if series.hasnans:
        result = series.astype(pd.BooleanDtype())
    else:
        result = series.astype(dtype)

    # convert to sparse, if applicable
    if sparse:
        return to_sparse(result)

    return result


def to_integer(
    series: pd.Series,
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = int
) -> pd.Series:
    """Convert a series containing integer data to a different integer dtype.

    Args:
        series (pd.Series):
            series to be converted.
        downcast (bool, optional):
            if True, attempts to reduce the byte size of `dtype` to match
            the underlying data and save memory.  Defaults to False.
        dtype (type | str | np.dtype | pd.api.extensions.ExtensionDtype,
               optional):
            final dtype of the resulting output.  Must be int-like.  Defaults
            to int.

    Raises:
        TypeError: if `series` does not contain integer data.
        TypeError: if `dtype` is not int-like.
        OverflowError: if `series` contains values that would not fit within
            the available range of `dtype`.

    Returns:
        pd.Series: series containing integer equivalent of input series, with
            new `dtype`.

    TODO: Test Cases:
        - downcast=True
        - dtype too small for range
        - non-integer dtype
        - maintains equality
    """
    # check series contains integer data
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check dtype is integer-like
    if not pd.api.types.is_integer_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be int-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # built-in integer special case - can be arbitrarily large
    if ((min_val < -2**63 or max_val > 2**63 - 1) and
        dtype in (int, "int", "i", "integer", "integers")):
        # these special cases are unaffected by downcast
        if min_val >= 0 and max_val <= 2**64 - 1:  # > int64 but < uint64
            if series.hasnans:
                return series.astype(pd.UInt64Dtype())
            return series.astype(np.uint64)
        # >int64 and >uint64, return as built-in python ints
        return series.astype(object).fillna(pd.NA)

    # convert to pandas dtype to expose itemsize attribute
    dtype = pd.api.types.pandas_dtype(dtype)

    # check whether result fits within specified dtype
    min_poss, max_poss = available_range(dtype)
    if min_val < min_poss or max_val > max_poss:
        bad = series[(series < min_poss) | (series > max_poss)].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values exceed {dtype} range "
                       f"at index: {list(bad)}")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values exceed {dtype} range "
                       f"at indices: {list(bad)}")
        else:  # plural, shortened
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values exceed {dtype} range "
                       f"at indices: [{shortened}, ...] ({len(bad)})")
        raise OverflowError(err_msg)

    # attempt to downcast if applicable
    if downcast:
        dtype = downcast_int_dtype(min_val, max_val, dtype)

    # convert and return
    if series.hasnans and not pd.api.types.is_extension_array_dtype(dtype):
        extension_types = {  # convert to extension dtype
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
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = float
) -> pd.Series:
    """Convert a series containing integer data to its float equivalent.

    Args:
        series (pd.Series):
            series to be converted
        downcast (bool, optional):
            if True, attempts to reduce the byte size of `dtype` to match
            the underlying data and save memory.  Defaults to False.
        dtype (type | str | np.dtype | pd.api.extensions.ExtensionDtype,
               optional):
            final dtype of the resulting output.  Must be float-like.  Defaults
            to float.

    Raises:
        TypeError: if `series` does not contain integer data.
        TypeError: if `dtype` is not float-like.
        OverflowError: if `series` contains values that would not fit within
            the available range of `dtype`.

    Returns:
        pd.Series: series containing float equivalent of input series.

    TODO: Test Cases:
        - correctly detects overflow
        - maintains maximum precision for dtype
        - downcast=True
        - non-float dtypes
    """
    # check series contains integer data
    if not pd.api.types.infer_dtype(series) == "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check dtype is float-like
    if not pd.api.types.is_float_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be float-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    # convert to float
    series = series.astype(dtype)

    # check for overflow
    if (series == np.inf).any():
        dtype = pd.api.types.pandas_dtype(dtype)
        bad = series[series == np.inf].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values exceed {dtype} range "
                       f"at index: {list(bad)}")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values exceed {dtype} range "
                       f"at indices: {list(bad)}")
        else:  # plural, shortened
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values exceed {dtype} range "
                       f"at indices: [{shortened, ...}] ({len(bad)})")
        raise OverflowError(err_msg)

    # attempt to downcast if applicable
    if downcast:
        return series.apply(downcast_float)

    # return
    return series


def to_complex(
    series: pd.Series,
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = complex
) -> pd.Series:
    """Convert a series containing integer data to its complex equivalent.

    Args:
        series (pd.Series):
            series to be converted.
        downcast (bool, optional):
            if True, attempts to reduce the byte size of `dtype` to match
            the underlying data and save memory.  Defaults to False.
        dtype (type | str | np.dtype | pd.api.extensions.ExtensionDtype,
               optional):
            final dtype of the resulting output.  Must be complex-like.  Defaults
            to complex.

    Raises:
        TypeError: if `series` does not contain integer data.
        TypeError: if `dtype` is not complex-like.
        OverflowError: if `series` contains values that would not fit within
            the available range of `dtype`.

    Returns:
        pd.Series: series containing complex equivalent of input series.

    TODO: Test Cases:
        - correctly detects overflow
        - maintains maximum precision for dtype
        - downcast=True
        - non-complex dtypes
    """
    # check series contains integer data
    if not pd.api.types.infer_dtype(series) == "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check dtype is complex-like
    if not pd.api.types.is_complex_dtype(dtype):
        err_msg = (f"[{error_trace()}] `dtype` must be complex-like "
                   f"(received: {dtype})")
        raise TypeError(err_msg)

    # convert to complex - astype(complex) can't handle missing values
    series = series.astype(object)
    series[series.isna()] = np.nan
    series = series.astype(dtype)

    # check for overflow
    if (series == np.inf).any():
        dtype = pd.api.types.pandas_dtype(dtype)
        bad = series[series == np.inf].index.values
        if len(bad) == 1:  # singular
            err_msg = (f"[{error_trace()}] series values exceed {dtype} range "
                       f"at index: {list(bad)}")
        elif len(bad) <= 5:  # plural
            err_msg = (f"[{error_trace()}] series values exceed {dtype} range "
                       f"at indices: {list(bad)}")
        else:  # plural, shortened
            shortened = ", ".join(str(i) for i in bad[:5])
            err_msg = (f"[{error_trace()}] series values exceed {dtype} range "
                       f"at indices: [{shortened}, ...] ({len(bad)})")
        raise OverflowError(err_msg)

    # attempt to downcast if applicable
    if downcast:
        return series.apply(downcast_complex)

    # return
    return series


def to_decimal(series: pd.Series) -> pd.Series:
    """Convert a series containing integer data to its arbitrary precision
    decimal equivalent.

    Args:
        series (pd.Series): series to be converted

    Raises:
        TypeError: if `series` does not contain integer data.

    Returns:
        pd.Series: series containing decimal equivalent of input series.

    TODO: Test Cases:
        - maintains equality.
    """
    # check series contains integer data
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # convert to decimal
    return series.apply(lambda x: decimal.Decimal(np.nan) if pd.isna(x)
                                  else decimal.Decimal(x))






def to_datetime(
    series: pd.Series,
    unit: str = "ns",
    offset: str | datetime_like = "1970-01-01 00:00:00+0000",
    tz: str | datetime.tzinfo | None = "local",
    errors: str = "warn"
) -> pd.Series:
    """Convert a series containing integer data to datetimes of the given unit,
    counting from the provided offset (UTC if offset is None).

    This function is essentially a beefed-up version of `pandas.to_datetime`
    that acts on integer data.  It works outside the nanosecond regime, supports
    arbitrary offsets/timezones, and can return non `pandas.Timestamp` objects,
    including `datetime.datetime` series and `np.datetime64` with arbitrary
    units.  As long as the underlying data can be represented as one of these
    return types, this function will return the corresponding result, preferring
    `pandas.Timestamp` to enable pandas' built-in `.dt` namespace.

    Note: raw np.datetime64 objects do not carry timezone information.

    Args:
        series (pd.Series):
            series to be converted.
        unit (str, optional):
            time unit to use during conversion.  Series will be interpreted as
            an integer number of the specified unit.  Defaults to "ns".
        offset (str | pd.Timestamp | datetime.datetime | np.datetime64 | None,
                optional):
            begin counting from this date and time.  Strings are interpreted as
            ISO 8601 dates.  If None, start from the UTC epoch time
            (1970-01-01 00:00:00+0000).  Defaults to None.
        tz (str | datetime.tzinfo | None, optional):
            timezone to localize results to.  Can be None, "local", a
            `zoneinfo.ZoneInfo` object, a pytz timezone, or an equivalent
            timezone string ("US/Pacific", "UTC", etc.).  If None, results
            will be returned as naive UTC times.  If "local", this function
            will localize results to the current system timezone.  Defaults to
            "local".

    Raises:
        TypeError: if `series` does not contain integer data.
        TypeError: if `offset` is provided and is not datetime-like.
        ValueError: if `unit` is not recognized.

    Returns:
        pd.Series: series containing datetimes corresponding to values of
            input series, with the chosen units and offset.

    TODO: Test Cases:
        - transitions between regimes (esp. pd.Timestamp -> datetime.datetime).
          Test a variety of timezones at each transition to ensure they are
          contiguous.
        - mixed units ([(2**63 - 1) * int(1e9), 1]), unit="ns".
        - consistent non-ns units ([(2**63 - 1) * int(1e9), int(1e9)]).
        - 'M' and 'Y' units.
        - tz=None, tz="local", tz="UTC", tz="Etc/GMT+12", tz="Etc/GMT-12".
        - offset provided.
        - does not modify in place.
        - ISO string offsets with or without timezones, in multiple regimes.
    """
    # vectorize input - object dtype prevents overflow
    series = pd.Series(series, dtype="O")

    # check series contains integer data
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check unit is valid
    valid_units = list(_to_ns) + ["M", "Y"]
    if unit not in valid_units:
        err_msg = (f"[{error_trace()}] `unit` {repr(unit)} not recognized - "
                   f"must be in {valid_units}")
        raise ValueError(err_msg)

    # check error handling is one of the available settings
    if errors not in ("raise", "warn", "ignore"):
        err_msg = (f"[{error_trace()}] `errors` must one of "
                   f"{['raise', 'warn', 'ignore']}, not {repr(errors)}")
        raise ValueError(err_msg)

    # convert tz to a tzinfo object, if provided
    if tz:
        if tz == "local":
            tz = tzlocal.get_localzone_name()
        tz = pytz.timezone(tz)  # TODO: zoneinfo?

    # convert offset to a properly localized datetime object
    if isinstance(offset, str):
        offset = convert_iso_string(offset, tz)
    if isinstance(offset, pd.Timestamp) and not offset.tzinfo:
        if (offset < pd.Timestamp.min + pd.Timedelta(hours=12) or
            offset > pd.Timestamp.max - pd.Timedelta(hours=12)):
            offset = convert_datetime_type(offset, datetime.datetime)
        else:
            offset = offset.tz_localize(tz)
    if isinstance(offset, datetime.datetime) and not offset.tzinfo:
        offset = offset.astimezone(tzinfo=tz)

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # pd.Timestamp - preferred to enable .dt namespace
    offset_ns = to_unit(offset, "ns")
    min_ns = to_unit((min_val, unit), "ns") + offset_ns
    max_ns = to_unit((max_val, unit), "ns") + offset_ns
    if (min_ns >= -2**63 + 1 + 43200 * int(1e9) and
        max_ns <= 2**63 - 1 - 43200 * int(1e9)):
        series = pd.Series(to_unit((series, unit), "ns", since=offset) +
                           offset_ns)
        series = pd.to_datetime(series, unit="ns")
        if tz:
            return series.dt.tz_localize("UTC").dt.tz_convert(tz)
        return series

    # datetime.datetime - allows >64 bit values while retaining timezone
    offset_us = to_unit(offset, "us")
    min_us = to_unit((min_val, unit), "us") + offset_us
    max_us = to_unit((max_val, unit), "us") + offset_us
    if (min_us >= to_unit(datetime.datetime.min, "us") and
        max_us <= to_unit(datetime.datetime.max, "us")):
        # to_unit converts to numpy array
        series = to_unit((series, unit), "us", since=offset) + offset_us
        nans = pd.isna(series)
        series[nans] = pd.NaT
        series[~nans] = series[~nans].astype("M8[us]").astype(object)
        if tz:  # localize to final tz
            # no choice but to do this elementwise -> slow
            make_utc = lambda dt: dt.astimezone(tzinfo=datetime.timezone.utc)
            localize = lambda dt: dt.astimezone(tz)
            series[~nans] = [localize(make_utc(dt)) for dt in series[~nans]]
        return pd.Series(series, dtype="O")  # convert back to series

    # np.datetime64 - widest range of all but no timezone; results are UTC
    # find appropriate unit
    final_unit = None
    for test_unit in valid_units[valid_units.index(unit):]:
        if test_unit == "W":
            # values outside this range wrap around infinity, skipping "NaT"
            min_poss = -((2**63 - 1) // 7) + 1566
            max_poss = (2**63 - 1) // 7 + 1565
        elif test_unit == "Y":
            min_poss = -2**63 + 1
            max_poss = 2**63 - 1 - 1970  # appears to be UTC offset
        else:
            min_poss = -2**63 + 1  # -2**63 reserved for np.datetime64("NaT")
            max_poss = 2**63 - 1
        test_offset = to_unit(offset, test_unit)
        if (to_unit((min_val, unit), test_unit) + test_offset >= min_poss and
            to_unit((max_val, unit), test_unit) + test_offset <= max_poss):
            final_unit = test_unit
            break
    if not final_unit:
        err_msg = (f"[{error_trace()}] could not convert series to datetime: "
                   f"values cannot be represented as `pandas.Timestamp`, "
                   f"`datetime.datetime`, or `numpy.datetime64` with any "
                   f"choice of unit")
        raise ValueError(err_msg)
    if final_unit != unit:
        warn_msg = (f"values out of range for {repr(unit)} precision")
        if errors == "raise":
            raise RuntimeError(f"[{error_trace()}] {warn_msg}")
        if errors == "warn":
            warnings.warn(f"{warn_msg}: converting to {repr(final_unit)} "
                          f"instead", RuntimeWarning)
    if tz and tz not in (pytz.timezone("UTC"), zoneinfo.ZoneInfo("UTC")):
        warn_msg = ("`numpy.datetime64` objects do not carry timezone "
                    "information")
        if errors == "raise":
            raise RuntimeError(f"[{error_trace()}] {warn_msg}")
        if errors == "warn":
            warnings.warn(f"{warn_msg}: returned times are UTC", RuntimeWarning)
    series = series.copy()
    nans = pd.isna(series)
    series[nans] = pd.NaT
    result = (to_unit((series[~nans], unit), final_unit, since=offset) +
              test_offset).astype(f"M8[{final_unit}]")
    series[~nans] = list(result)
    return series


def to_timedelta(
    series: pd.Series,
    unit: str = "ns",
    offset: pd.Timedelta | datetime.timedelta | np.timedelta64 | None = None,
    start: str | pd.Timestamp | datetime.datetime | datetime.date |
           np.datetime64 | None = None
) -> pd.Series:
    """Convert a series containing integer data to timedeltas of the given unit.

    Args:
        series (pd.Series):
            series to be converted.  Must contain integer data.
        unit (str, optional):
            time unit to use during conversion.  Series will be interpreted as
            an integer number of the specified unit.Defaults to "ns".
        offset (pd.Timedelta | datetime.timedelta | np.timedelta64 | None,
                optional):
            offset each timedelta by a given amount.  If None, do not offset.
            Defaults to None.
        start (str | pd.Timestamp | datetime.datetime | datetime.date |
               np.datetime64 | None, optional):
            Only used for units 'M' and 'Y'.  Specifies a date from which to
            count months and years, accounting for unequal month lengths and
            leap years.  Strings are interpreted as ISO 8601 dates.  If left
            None, assumes the beginning of a 400-year Gregorian cycle.
            Defaults to None.

    Raises:
        TypeError: if `series` does not contain integer data.
        TypeError: if `offset` is provided and is not timedelta-like.
        TypeError: if `start` is provided and is not datetime-like.
        ValueError: if `unit` is not recognized.

    Returns:
        pd.Series: series containing datetimes corresponding to values of
            input series, with the chosen units and offset.

    TODO: Test Cases:
        - transitions between regimes.
        - mixed units ([(2**63 - 1) * int(1e9), 1]), unit="ns".
        - consistent non-ns units ([(2**63 - 1) * int(1e9), int(1e9)]).
        - 'M' and 'Y' units.
        - offset provided.
        - different start offsets.
    """
    # check series contains integer data
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check offset is timedelta-like, if provided
    if offset and not isinstance(offset, (pd.Timedelta, datetime.timedelta,
                                          np.timedelta64)):
        err_msg = (f"[{error_trace()}] `offset` must be an instance of "
                   f"pandas.Timedelta, datetime.timedelta, or "
                   f"numpy.timedelta64, not {type(offset)}")
        raise TypeError(err_msg)

    # check start is datetime-like, if provided
    if start:
        if isinstance(start, str):
            start = np.datetime64(start)
        if not isinstance(start, (pd.Timestamp, datetime.datetime,
                                     datetime.date, np.datetime64)):
            err_msg = (f"[{error_trace()}] `start` must be an ISO 8601 "
                       f"string, an instance of pandas.Timestamp, "
                       f"datetime.datetime, or numpy.datetime64, not "
                       f"{type(start)}")
            raise TypeError(err_msg)

    # convert series to nanoseconds
    series = series.astype(object)  # prevents overflow
    if unit in _to_ns:
        series = series * _to_ns[unit]
    elif unit in ("M", "month", "months", "Y", "year", "years"):
        # account for leap years, unequal month lengths
        if start is None:
            year = 2001
            month = 1
            day = 1
        elif isinstance(start, np.datetime64):
            components = datetime64_components(start)
            year = components["year"]
            month = components["month"]
            day = components["day"]
        else:
            year = start.year
            month = start.month
            day = start.day
        if unit in ("Y", "year", "years"):
            series = pd.Series(date_to_days(year + series, month, day) -
                               date_to_days(year, month, day)) * _to_ns["D"]
        else:  # unit in ("M", "month", "months")
            series = pd.Series(date_to_days(year, month + series, day) -
                               date_to_days(year, month, day)) * _to_ns["D"]
    else:  # unrecognized unit
        valid_units = (list(_to_ns) +
                       ["M", "month", "months", "Y", "year", "years"])
        err_msg = (f"[{error_trace()}] `unit` {repr(unit)} not recognized.  "
                   f"Must be in {valid_units}")
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
    if min_val >= -2**63 + 1 and max_val <= 2**63 - 1:
        # pd.to_timedelta can't parse pd.NA by default for some reason
        return pd.to_timedelta(series, unit="ns", errors="coerce")

    # datetime.timedelta - slightly wider range than pd.Timedelta
    if (min_val >= total_nanoseconds(datetime.timedelta.min) and
        max_val <= total_nanoseconds(datetime.timedelta.max) and
        not (series % 1000).any()):
        make_td = lambda x: datetime.timedelta(microseconds=int(x) // 1000)
        return series.apply(lambda x: pd.NaT if pd.isna(x) else make_td(x))

    # np.timedelta64 - consistent units
    try:
        return _to_numpy_timedelta64_consistent_unit(series, min_val, max_val,
                                                     start)
    except ValueError:
        pass

    # np.timedelta64 - mixed units
    return _to_numpy_timedelta64_mixed_unit(series, start)


def to_string(
    series: pd.Series,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = str
) -> pd.Series:
    """Convert a series containing integer data to its string equivalent.

    Args:
        series (pd.Series):
            series to be converted.
        dtype (type | str | np.dtype | pd.api.extensions.ExtensionDtype,
               optional):
            final dtype of the resulting output.  Must be string-like.
            Defaults to str.

    Raises:
        TypeError: if `series` does not contain integer data.
        TypeError: if `dtype` is not string-like.

    Returns:
        pd.Series: series containing string equivalent of input series.

    TODO: Test Cases:
        - maintains equality
        - correctly converts missing values
        - non-string dtype
    """
    # check series contains integer data
    if not pd.api.types.infer_dtype(series) == "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check dtype is string-like - exclude object dtypes
    # pandas isn't picky about what constitutes a string dtype
    # TODO: allow sparse dtypes
    if (pd.api.types.is_object_dtype(dtype) or
        not pd.api.types.is_string_dtype(dtype)):
        err_msg = (f"[{error_trace()}] `dtype` must be string-like (received: "
                   f"{dtype})")
        raise TypeError(err_msg)

    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)


def to_categorical(
    series: pd.Series,
    categories: list | np.ndarray | pd.Series | None = None,
    ordered: bool = False
) -> pd.Series:
    """Convert a series containing integer data to its categorical equivalent,
    in R / S-plus fashion.

    See `pandas.Categorical` for more information.

    Args:
        series (pd.Series):
            series to be converted.
        categories (list | np.ndarray | pd.Series | None, optional):
            the unique categories for this categorical.  If None, categories
            are assumed to be the unique values of `series` (sorted, if
            possible, otherwise in the order in which they appear).  Defaults
            to None.
        ordered (bool, optional):
            whether or not this categorical is treated as a ordered categorical.
            If True, the resulting categorical will be ordered.  An ordered
            categorical respects, when sorted, the order of its categories
            attribute (which in turn is the categories argument, if provided).
            Defaults to False.

    Raises:
        ValueError: if `categories` do not validate.
        TypeError: if `ordered=True`, but no categories are given and `series`
            values are not sortable.

    Returns:
        pd.Series: series containing categorical equivalent of input series.

    TODO: Test Cases:
        - explicit categories.
        - ordered.
        - maintains equality.
    """
    values = pd.Categorical(series, categories=categories, ordered=ordered)
    return pd.Series(values)
