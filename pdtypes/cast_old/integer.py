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
    datetime_like, timedelta_like
    # convert_iso_string
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


def _validate_integer_series(series: pd.Series) -> None:
    inferred_type = pd.api.types.infer_dtype(series)
    if inferred_type != "integer":
        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"integer data (received: {inferred_type})")
        raise TypeError(err_msg)


def _validate_time_unit(unit: str) -> None:
    valid_units = list(_to_ns) + ["M", "Y"]
    if unit not in valid_units:
        err_msg = (f"[{error_trace(stack_index=2)}] `unit` {repr(unit)} not "
                   f"recognized - must be in {valid_units}")
        raise ValueError(err_msg)


def _validate_errors(errors: str) -> None:
    valid_errors = ["raise", "warn", "ignore"]
    if errors not in valid_errors:
        err_msg = (f"[{error_trace(stack_index=2)}] `errors` must one of "
                   f"{valid_errors}, not {repr(errors)}")
        raise ValueError(err_msg)


def _validate_timezone(
    timezone: str | datetime.tzinfo | None
) -> datetime.tzinfo | None:
    if timezone is None:
        return None
    if isinstance(timezone, str):
        if timezone == "local":
            return pytz.timezone(tzlocal.get_localzone_name())
        return pytz.timezone(timezone)  # TODO: zoneinfo?
    if isinstance(timezone, datetime.tzinfo):
        return timezone
    err_msg = (f"[{error_trace(stack_index=2)}] unrecognized timezone: "
               f"{repr(timezone)}")
    raise TypeError(err_msg)


def _validate_offset_timedelta(
    td_like: timedelta_like | None
) -> timedelta_like:
    if td_like is None:
        return pd.Timedelta(0)
    valid_types = (pd.Timedelta, datetime.timedelta, np.timedelta64)
    if not isinstance(td_like, valid_types):
        err_msg = (f"[{error_trace(stack_index=2)}] `offset` must be "
                   f"an instance of `pandas.Timedelta`, `datetime.timedelta`, "
                   f"or `numpy.timedelta64`, not {type(td_like)}")
        raise TypeError(err_msg)
    return td_like


def _validate_since_datetime(
    dt_like: str | datetime_like,
    timezone: datetime.tzinfo | None
) -> datetime_like:
    valid_types = (str, pd.Timestamp, datetime.datetime, np.datetime64)
    if not isinstance(dt_like, valid_types):
        err_msg = (f"[{error_trace(stack_index=2)}] `since` must be a valid "
                   f"ISO 8601 string or an instance of `pandas.Timestamp`, "
                   f"`datetime.datetime`, or `numpy.datetime64`, not "
                   f"{type(dt_like)}")
        raise TypeError(err_msg)

    # handle ISO 8601 strings
    if isinstance(dt_like, str):
        return convert_iso_string(dt_like, timezone)

    # localize naive datetimes
    if isinstance(dt_like, pd.Timestamp) and not dt_like.tzinfo:
        if (dt_like < pd.Timestamp.min + pd.Timedelta(hours=12) or
            dt_like > pd.Timestamp.max - pd.Timedelta(hours=12)):
            return convert_datetime_type(dt_like, datetime.datetime)
        return dt_like.tz_localize(timezone)
    if isinstance(dt_like, datetime.datetime) and not dt_like.tzinfo:
        return dt_like.astimezone(tzinfo=timezone)

    return dt_like


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
    _validate_integer_series(series)

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
    _validate_integer_series(series)

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
    _validate_integer_series(series)

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
    _validate_integer_series(series)

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
    # vectorize using object series
    series = pd.Series(series, dtype="O")

    # check series contains integer data
    _validate_integer_series(series)

    # convert to decimal using custom numpy ufunc - about 5x faster than
    # list comprehension
    nans = pd.isna(series)
    decimal_ufunc = np.frompyfunc(decimal.Decimal, 1, 1)
    series[nans] = np.nan
    series[~nans] = decimal_ufunc(series[~nans])
    return pd.Series(series)


def to_datetime(
    series: pd.Series,
    unit: str = "ns",
    since: str | datetime_like = "1970-01-01 00:00:00+0000",
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
    `pandas.Timestamp` to enable the built-in pandas `.dt` namespace.

    Note: raw np.datetime64 objects do not carry timezone information. All

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

    # validate args
    _validate_integer_series(series)
    _validate_time_unit(unit)
    _validate_errors(errors)
    tz = _validate_timezone(tz)
    since = _validate_since_datetime(since, tz)
    # TODO: since timezone localization is wrong for datetime.datetime

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # pd.Timestamp - preferred to enable .dt namespace
    since_ns = to_unit(since, "ns")
    min_ns = to_unit((min_val, unit), "ns") + since_ns
    max_ns = to_unit((max_val, unit), "ns") + since_ns
    if (min_ns >= -2**63 + 1 + 43200 * int(1e9) and
        max_ns <= 2**63 - 1 - 43200 * int(1e9)):
        series = pd.Series(to_unit((series, unit), "ns", since=since) +
                           since_ns)
        series = pd.to_datetime(series, unit="ns")
        if tz:
            return series.dt.tz_localize("UTC").dt.tz_convert(tz)
        return series

    # datetime.datetime - allows >64 bit values while retaining timezone
    since_us = to_unit(since, "us")
    min_us = to_unit((min_val, unit), "us") + since_us
    max_us = to_unit((max_val, unit), "us") + since_us
    if (min_us >= to_unit(datetime.datetime.min, "us") and
        max_us <= to_unit(datetime.datetime.max, "us")):
        # TODO: if given unit is ns, emit a warning
        # TODO: datetime offsets don't properly account for timezone
        # to_unit converts to numpy array
        series = to_unit((series, unit), "us", since=since) + since_us
        nans = pd.isna(series)
        series[nans] = pd.NaT
        series[~nans] = series[~nans].astype("M8[us]").astype(object)
        if tz:  # localize to final tz - no choice but to do this elementwise
            make_utc = lambda dt: dt.replace(tzinfo=datetime.timezone.utc)
            localize = lambda dt: dt.astimezone(tz)
            series[~nans] = [localize(make_utc(dt)) for dt in series[~nans]]
        return pd.Series(series, dtype="O")  # convert back to series

    # np.datetime64 - widest range of all but no timezone; results are UTC
    if tz and tz not in (pytz.timezone("UTC"), zoneinfo.ZoneInfo("UTC")):
        warn_msg = ("`numpy.datetime64` objects do not carry timezone "
                    "information")
        if errors == "raise":
            raise RuntimeError(f"[{error_trace()}] {warn_msg}")
        if errors == "warn":
            warnings.warn(f"{warn_msg}: returned times are UTC", RuntimeWarning)
    # find appropriate unit to fit data
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
        test_since = to_unit(since, test_unit)  # `since` in appropriate unit
        if (to_unit((min_val, unit), test_unit) + test_since >= min_poss and
            to_unit((max_val, unit), test_unit) + test_since <= max_poss):
            final_unit = test_unit
            break  # stop at highest precision unit that fits data
    if not final_unit:  # no unit could be found
        err_msg = (f"[{error_trace()}] could not convert series to datetime: "
                   f"values cannot be represented as `pandas.Timestamp`, "
                   f"`datetime.datetime`, or `numpy.datetime64` with any "
                   f"choice of unit")
        raise ValueError(err_msg)
    if final_unit != unit:  # first valid unit is different than specified
        warn_msg = (f"values out of range for {repr(unit)} precision")
        if errors == "raise":
            raise RuntimeError(f"[{error_trace()}] {warn_msg}")
        if errors == "warn":
            warnings.warn(f"{warn_msg}: converting to {repr(final_unit)} "
                          f"instead", RuntimeWarning)
    # cast to datetime64 using chosen unit
    # series = series.copy()
    nans = pd.isna(series)
    series[nans] = pd.NaT
    result = (to_unit((series[~nans], unit), final_unit, since=since) +
              test_since).astype(f"M8[{final_unit}]")
    series[~nans] = list(result)  # preserves object dtype
    return series


def to_timedelta(
    series: pd.Series,
    unit: str = "ns",
    offset: timedelta_like | None = None,
    since: str | datetime_like | datetime.date = "2001-01-01 00:00:00+0000",
    errors: str = "warn"
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
    # vectorize input - object dtype prevents overflow
    series = pd.Series(series, dtype="O")  # prevents modification in-place

    # validate args
    _validate_integer_series(series)
    _validate_time_unit(unit)
    _validate_errors(errors)
    offset = _validate_offset_timedelta(offset)
    since = _validate_since_datetime(since, datetime.timezone.utc)

    # get min/max to evaluate range
    min_val = series.min()
    max_val = series.max()

    # pd.Timedelta - preferred to enable .dt namespace
    offset_ns = to_unit(offset, "ns", since=since)
    min_ns = to_unit((min_val, unit), "ns", since=since) + offset_ns
    max_ns = to_unit((max_val, unit), "ns", since=since) + offset_ns
    if min_ns >= -2**63 + 1 and max_ns <= 2**63 - 1:
        series = pd.Series(to_unit((series, unit), "ns", since=since) +
                           offset_ns)
        return pd.to_timedelta(series, unit="ns")

    # datetime.timedelta - allows >64 bit values while retaining timezone
    offset_us = to_unit(offset, "us", since=since)
    min_us = to_unit((min_val, unit), "us", since=since) + offset_us
    max_us = to_unit((max_val, unit), "us", since=since) + offset_us
    if (min_us >= to_unit(datetime.timedelta.min, "us", since=since) and
        max_us <= to_unit(datetime.timedelta.max, "us", since=since)):
        # TODO: if given unit is ns, emit a warning
        # to_unit converts to numpy array
        series = to_unit((series, unit), "us", since=since) + offset_us
        nans = pd.isna(series)
        series[nans] = pd.NaT
        series[~nans] = series[~nans].astype("m8[us]").astype(object)
        return pd.Series(series, dtype="O")  # convert back to series

    # np.timedelta64 - widest range of all but no timezone; results are UTC
    # find appropriate unit to fit data
    final_unit = None
    for test_unit in valid_units[valid_units.index(unit):]:
        test_offset = to_unit(offset, test_unit, since=since)
        if (to_unit((min_val, unit), test_unit) + test_offset >= -2**63 + 1 and
            to_unit((max_val, unit), test_unit) + test_offset <= 2**63 - 1):
            final_unit = test_unit
            break  # stop at highest precision unit that fits data
    if not final_unit:  # no unit could be found
        err_msg = (f"[{error_trace()}] could not convert series to datetime: "
                   f"values cannot be represented as `pandas.Timedelta`, "
                   f"`datetime.timedelta`, or `numpy.timedelta64` with any "
                   f"choice of unit")
        raise ValueError(err_msg)
    if final_unit != unit:  # first valid unit is different than specified
        warn_msg = (f"values out of range for {repr(unit)} precision")
        if errors == "raise":
            raise RuntimeError(f"[{error_trace()}] {warn_msg}")
        if errors == "warn":
            warnings.warn(f"{warn_msg}: converting to {repr(final_unit)} "
                          f"instead", RuntimeWarning)
    # cast to timedelta64 using chosen unit
    # series = series.copy()
    nans = pd.isna(series)
    series[nans] = pd.NaT
    result = (to_unit((series[~nans], unit), final_unit, since=since) +
              test_offset).astype(f"m8[{final_unit}]")
    series[~nans] = list(result)  # preserves object dtype
    return series


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
    _validate_integer_series(series)

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
