from __future__ import annotations
import datetime
import decimal
import warnings

import dateutil
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

# TODO: allow vectorized units/since


#######################
####    Helpers    ####
#######################

def _validate_integer_series(series: np.ndarray | pd.Series) -> None:
    """Check whether a series contains integer data."""
    # option 1: series is properly formatted
    if pd.api.types.is_integer_dtype(series):
        return None

    # option 2: series is object dtype and contains integer-like elements
    inferred = pd.api.types.infer_dtype(series)
    if pd.api.types.is_object_dtype(series) and inferred == "integer":
        return None

    err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
               f"integer data, not {inferred}")
    raise TypeError(err_msg)


def _validate_pandas_timestamp_series(series: np.ndarray | pd.Series) -> None:
    """Check whether a series contains `pandas.Timestamp` objects"""
    # option 1: series is properly formatted
    if pd.api.types.is_datetime64_ns_dtype(series):
        return None

    # option 2: series is object dtype and contains datetime-like elements
    inferred = pd.api.types.infer_dtype(series)
    if pd.api.types.is_object_dtype(series) and inferred == "datetime":
        # verify objects are actually pd.Timestamps
        type_ufunc = np.frompyfunc(type, 1, 1)
        result = pd.unique(type_ufunc(series))
        if len(result) == 1 and issubclass(result[0], pd.Timestamp):
            return None

        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"`pandas.Timestamp` objects, not {list(result)}")
        raise TypeError(err_msg)

    err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
               f"`pandas.Timestamp` objects, not {inferred}")
    raise TypeError(err_msg)


def _validate_pydatetime_series(series: np.ndarray | pd.Series) -> None:
    """Check whether a series contains `datetime.datetime` objects."""
    # check series is properly formatted
    inferred = pd.api.types.infer_dtype(series)
    if pd.api.types.is_object_dtype(series) and inferred == "datetime":
        # verify objects are actually datetime.datetimes
        type_ufunc = np.frompyfunc(type, 1, 1)
        result = type_ufunc(series).unique()
        if len(result) == 1 and issubclass(result[0], datetime.datetime):
            return None

        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"`datetime.datetime` objects, not {list(result)}")
        raise TypeError(err_msg)

    err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
               f"`datetime.datetime` objects, not {inferred}")
    raise TypeError(err_msg)


def _validate_numpy_datetime64_series(series: np.ndarray | pd.Series) -> bool:
    """Check whether a series contains `numpy.datetime64` objects."""
    # check series is properly formatted
    inferred = pd.api.types.infer_dtype(series)
    if pd.api.types.is_object_dtype(series) and inferred == "datetime64":
        # verify objects are actually numpy.datetime64s
        type_ufunc = np.frompyfunc(type, 1, 1)
        result = pd.unique(type_ufunc(series))
        if len(result) == 1 and issubclass(result[0], np.datetime64):
            return None

        err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
                   f"`numpy.datetime64` objects, not {list(result)}")
        raise TypeError(err_msg)

    err_msg = (f"[{error_trace(stack_index=2)}] `series` must contain "
               f"`numpy.datetime64` objects, not {inferred}")
    raise TypeError(err_msg)


def _validate_unit(unit: np.ndarray | pd.Series) -> None:
    valid = list(_to_ns) + ["M", "Y"]
    if not np.isin(unit, valid).all():
        bad = list(np.unique(unit[~np.isin(unit, valid)]))
        err_msg = (f"[{error_trace(stack_index=2)}] `unit` {bad} not "
                   f"recognized: must be in {valid}")
        raise ValueError(err_msg)


def _validate_rounding(rounding: str) -> None:
    valid = ["floor", "truncate", "round", "ceiling"]
    if rounding not in valid:
        err_msg = (f"[{error_trace(stack_index=2)}] `rounding` must be one of "
                   f"{repr(valid)}, not {repr(rounding)}")
        raise ValueError(err_msg)


# TODO: revise `errors` handling to match pandas arg and make sure it's used
# where appropriate


###################################
####    Integer Conversions    ####
###################################


def integer_to_pandas_timedelta(
    series: int | list | np.ndarray | pd.Series,
    unit: str | list | np.ndarray | pd.Series = "ns",
    since: str | datetime_like | list | np.ndarray | pd.Series = "2001-01-01 00:00:00+0000"
) -> pd.Series:
    """Convert an integer series to `pandas.Timedelta` objects."""
    # vectorize input
    series = np.atleast_1d(np.array(series, dtype="O"))  # prevents overflow
    unit = np.atleast_1d(np.array(unit))
    since = np.atleast_1d(np.array(since))

    # broadcast to same size
    series, unit, since = np.broadcast_arrays(series, unit, since)
    series = pd.Series(series, dtype="O")

    # detect missing values and subset
    to_convert = pd.notna(series)
    subset = series[to_convert]
    sub_unit = unit[to_convert]
    sub_since = since[to_convert]
    series[~to_convert] = pd.NaT  # missing values coerced to pd.NaT

    # validate input and establish since_offset in given units
    _validate_integer_series(subset)
    _validate_unit(sub_unit)
    if pd.api.types.infer_dtype(sub_since) == "string":
        sub_since = string_to_datetime(sub_since, tz=None)

    # handle month/year conversions separately
    irregular = np.isin(sub_unit, ("M", "Y"))
    subset = subset[irregular]
    sub_unit = sub_unit[irregular]
    sub_since = sub_since[irregular]
    to_convert[to_convert] = ~irregular  # remove M, Y from conversions
    return subset, sub_unit, sub_since
    # TODO: convert_unit doesn't accept pd.Timestamp series as `since` arg
    return sub_since[irregular]
    subset[irregular] = convert_unit(subset[irregular], sub_unit[irregular],
                                     "D", since=sub_since[irregular])
    subset[irregular] = pd.to_timedelta(subset[irregular], unit="D")

    return sub_unit

    # refine subset to not include months/years
    to_convert[to_convert] = ~irregular
    subset = subset[~irregular]
    sub_unit = sub_unit[~irregular]
    sub_since = sub_since[~irregular]

    # handle all other conversions by group/transform
    # TODO: this works surprisingly well
    by_group = lambda x: pd.to_timedelta(x, unit=x.name)
    subset = subset.groupby(sub_unit, as_index=True).transform(by_group)

    # reassign subset back to group and return
    series[to_convert] = subset
    return series



    if unit in ("M", "Y"):  # account for unequal month lengths/leap years
        series = convert_unit(series, sub_unit, "D", since=sub_since)
        unit = "D"  # demote to days
    # use regular scale factor
    # TODO: pd.to_timedelta doesn't accept vectorized units
    return pd.to_timedelta(series, unit=unit)


def integer_to_pandas_timestamp(
    series: int | list | np.ndarray | pd.Series,
    unit: str = "ns",
    tz: str | pytz.BaseTzInfo | None = "local",
    since: str | pd.Timestamp = "1970-01-01 00:00:00+0000",
    errors: str = "warn"
) -> pd.Series:
    """Convert an integer series to `pandas.Timestamp` objects."""
    # vectorize input - object dtype prevents overflow
    series = pd.Series(series, dtype="O")

    # validate series contains integer data
    if pd.api.types.infer_dtype(series) != "integer":
        err_msg = (f"[{error_trace()}] `series` must contain integer data, "
                   f"not {pd.api.types.infer_dtype(series)}")
        raise TypeError(err_msg)

    # validate unit is expected
    if unit not in list(_to_ns) + ["M", "Y"]:
        err_msg = (f"[{error_trace()}] `unit` {repr(unit)} not recognized - "
                   f"must be in {list(_to_ns) + ['M', 'Y']}")
        raise ValueError(err_msg)

    # validate errors is expected
    if errors not in ["raise", "warn", "ignore"]:
        err_msg = (f"[{error_trace()}] `errors` must one of "
                   f"{['raise', 'warn', 'ignore']}, not {repr(errors)}")
        raise ValueError(err_msg)

    # validate timezone and convert to datetime.tzinfo object
    if isinstance(tz, str):
        if tz == "local":
            tz = pytz.timezone(tzlocal.get_localzone_name())
        else:
            tz = pytz.timezone(tz)
    if tz and not isinstance(tz, pytz.BaseTzInfo):
        err_msg = (f"[{error_trace()}] `tz` must be a pytz.timezone object or "
                   f"an IANA-recognized timezone string, not {type(tz)}")
        raise TypeError(err_msg)

    # convert since to pd.Timestamp and localize
    # TODO: allow arbitrary offsets, not just pd.Timestamp
    if isinstance(since, str):
        since = string_to_pandas_timestamp(since, tz)[0]
    elif isinstance(since, pd.Timestamp):
        if since.tzinfo is None:  # assume UTC
            since = since.tz_localize("UTC")
        since = since.tz_convert(tz)
    else:
        err_msg = (f"[{error_trace()}] `since` must be an instance of "
                   f"`pandas.Timestamp` or a valid datetime string, not "
                   f"{type(since)}")
        raise TypeError(err_msg)

    # convert series to ns, add offset, and then return using pd.to_datetime
    series = convert_unit(series, unit, "ns", since=since)
    series += pandas_timestamp_to_integer(since, "ns")
    if series.min() < -2**63 + 1:  # prevent conversion to NaT
        err_msg = (f"[{error_trace()}] `-2**63` is reserved for `NaT`")
        raise OverflowError(err_msg)
    series = pd.to_datetime(series, unit="ns")
    if tz:
        return series.dt.tz_localize("UTC").dt.tz_convert(str(tz))
    return series


#####################################
####    Timedelta Conversions    ####
#####################################


# TODO: if using vectorized type function to dispatch, series type validation
# becomes more or less redundant


def pandas_timedelta_to_integer(
    series: pd.Timedelta | list | np.ndarray | pd.Series,
    unit: str = "ns",
    since: str | datetime_like = "2001-01-01 00:00:00+0000",
    rounding: str = "floor",
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = int,
    errors: str = "raise"
) -> pd.Series:
    """Convert a series of `pandas.Timedelta` objects to an integer number of
    units.

    Test Cases:
        - input dtypes:
            scalar: timedelta, non-timedelta, missing value
            list: timedeltas, non-timedeltas, mixed, w/ and w/o missing values
            array: timedelta, non-timedelta, mixed, w/ and w/o missing values
            series: timedelta64[ns] series, timedelta series w/ dtype="O"
                    mixed, w/ and w/o missing values
        - units: good/bad
        - since:
            scalar: ISO 8601 string, pd.Timestamp, datetime.datetime,
                    np.datetime64
            vectorized: ISO 8601 string, pd.Timestamp, datetime.datetime,
                        np.datetime64
        - rounding: good/bad
        - downcast: good/bad
        - dtype: int, non-int, within and outside range
        - errors: good/bad
        - accuracy:
            check function of all args
            check missing values handled
            check output matches expected
            check for overflow, boundary conds
    """
    # vectorize input
    series = pd.Series(series).infer_objects()

    # validate series contains timedeltas
    if not pd.api.types.is_timedelta64_ns_dtype(series):
        err_msg = (f"[{error_trace()}] `series` must contain "
                   f"`datetime.timedelta` objects, not "
                   f"{pd.api.types.infer_dtype(series)}")
        raise TypeError(err_msg)

    # validate unit is expected
    if unit not in list(_to_ns) + ["M", "Y"]:
        err_msg = (f"[{error_trace()}] `unit` {repr(unit)} not recognized - "
                   f"must be in {list(_to_ns) + ['M', 'Y']}")
        raise ValueError(err_msg)

    # convert since to datetime.datetime and localize to UTC
    # TODO: allow arbitrary offsets, not just datetime.datetime
    if isinstance(since, str):
        since = string_to_pydatetime(since, "UTC")[0]
    elif isinstance(since, datetime.datetime):
        if since.tzinfo is None:  # assume UTC
            since = since.replace(tzinfo=datetime.timezone.utc)
        else:
            since = since.astimezone(datetime.timezone.utc)
    else:
        err_msg = (f"[{error_trace()}] `since` must be an instance of "
                   f"`datetime.datetime` or a valid datetime string, not "
                   f"{type(since)}")
        raise TypeError(err_msg)

    # validate rounding arg is one of available settings
    if rounding not in ["floor", "round", "ceiling"]:
        err_msg = (f"[{error_trace()}] `rounding` must be one of "
                   f"{repr(['floor', 'round', 'ceiling'])}, not "
                   f"{repr(rounding)}")
        raise ValueError(err_msg)

    # perform conversion, using pd.to_numeric to convert to nanoseconds,
    # followed by convert_unit to convert to final unit.
    nans = pd.isna(series)  # do not operate on missing values
    # allocate result as all NA
    result = pd.Series(np.full(len(series), pd.NA, dtype="O"))
    # replace non-NAs with appropriate result
    result[~nans] = convert_unit(pd.to_numeric(series[~nans]), "ns", unit,
                                 since=since, rounding=rounding)
    # TODO: pass through integer_to_integer to handle dtype conversion
    return result


def datetime_timedelta_to_integer(
    series: datetime.timedelta | list | np.ndarray | pd.Series,
    unit: str = "ns",
    since: str | datetime_like = "2001-01-01 00:00:00+0000",
    rounding: str = "floor",
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = int,
    errors: str = "raise"
) -> pd.Series:
    """Convert a series of `datetime.timedelta` objects to an integer number of
    units."""
    # vectorize input - use arrays to avoid automatic pd.Timedelta conversion
    series = np.atleast_1d(np.array(series, dtype="O"))

    # validate series contains timedeltas
    if pd.api.types.infer_dtype(series) != "timedelta":
        err_msg = (f"[{error_trace()}] `series` must contain "
                   f"`datetime.timedelta` objects, not "
                   f"{pd.api.types.infer_dtype(series)}")
        raise TypeError(err_msg)

    # validate unit is expected
    if unit not in list(_to_ns) + ["M", "Y"]:
        err_msg = (f"[{error_trace()}] `unit` {repr(unit)} not recognized - "
                   f"must be in {list(_to_ns) + ['M', 'Y']}")
        raise ValueError(err_msg)

    # convert since to datetime.datetime and localize to UTC
    # TODO: allow arbitrary offsets, not just datetime.datetime
    if isinstance(since, str):
        since = string_to_pydatetime(since, "UTC")[0]
    elif isinstance(since, datetime.datetime):
        if since.tzinfo is None:  # assume UTC
            since = since.replace(tzinfo=datetime.timezone.utc)
        else:
            since = since.astimezone(datetime.timezone.utc)
    else:
        err_msg = (f"[{error_trace()}] `since` must be an instance of "
                   f"`datetime.datetime` or a valid datetime string, not "
                   f"{type(since)}")
        raise TypeError(err_msg)

    # validate rounding arg is one of available settings
    if rounding not in ["floor", "round", "ceiling"]:
        err_msg = (f"[{error_trace()}] `rounding` must be one of "
                   f"{repr(['floor', 'round', 'ceiling'])}, not "
                   f"{repr(rounding)}")
        raise ValueError(err_msg)

    # allocate result & allow for missing values
    nans = pd.isna(series)
    result = pd.Series(np.full(len(series), pd.NA, dtype="O"))

    # split timedeltas into days, seconds, and microseconds columns
    split_timedelta = lambda td: (td.days, td.seconds, td.microseconds)
    split_timedelta = np.frompyfunc(split_timedelta, 1, 3)  # vectorize
    splits = np.array(split_timedelta(series[~nans])).T

    # scale (n, 3) splits array by appropriate coefs + sum row-wise to get ns
    coefficients = np.array([24*60*60*int(1e9), int(1e9), int(1e3)], dtype="O")
    nanoseconds = np.sum(splits * coefficients, axis=1)

    # use convert_unit to convert nanoseconds to final unit
    result[~nans] = convert_unit(nanoseconds, "ns", unit, since=since,
                                 rounding=rounding)
    # TODO: pass through integer_to_integer to handle dtype conversion
    return result


def numpy_timedelta64_to_integer(
    series: np.timedelta64 | list | np.ndarray | pd.Series,
    unit: str = "ns",
    since: str | datetime_like = "2001-01-01 00:00:00+0000",
    rounding: str = "floor",
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = int,
    errors: str = "raise"
) -> pd.Series:
    """Convert a series of `numpy.timedelta64` objects to an integer number of
    units."""
    # vectorize input - use arrays to enable native support methods
    series = np.atleast_1d(np.array(series))

    # TODO: consider timedelta64s with mixed units, such as
    # [np.timedelta64(2**63 - 1, "D"), np.timedelta64(1, "ns"), None]

    # validate series contains timedeltas
    if pd.api.types.infer_dtype(series) not in ("timedelta", "timedelta64"):
        err_msg = (f"[{error_trace()}] `series` must contain "
                   f"`numpy.timedelta64` objects, not "
                   f"{pd.api.types.infer_dtype(series)}")
        raise TypeError(err_msg)

    # validate unit is expected
    if unit not in list(_to_ns) + ["M", "Y"]:
        err_msg = (f"[{error_trace()}] `unit` {repr(unit)} not recognized - "
                   f"must be in {list(_to_ns) + ['M', 'Y']}")
        raise ValueError(err_msg)

    # convert since to datetime.datetime and localize to UTC
    # TODO: allow arbitrary offsets, not just datetime.datetime
    if isinstance(since, str):
        since = string_to_pydatetime(since, "UTC")[0]
    elif isinstance(since, datetime.datetime):
        if since.tzinfo is None:  # assume UTC
            since = since.replace(tzinfo=datetime.timezone.utc)
        else:
            since = since.astimezone(datetime.timezone.utc)
    else:
        err_msg = (f"[{error_trace()}] `since` must be an instance of "
                   f"`datetime.datetime` or a valid datetime string, not "
                   f"{type(since)}")
        raise TypeError(err_msg)

    # validate rounding arg is one of available settings
    if rounding not in ["floor", "round", "ceiling"]:
        err_msg = (f"[{error_trace()}] `rounding` must be one of "
                   f"{repr(['floor', 'round', 'ceiling'])}, not "
                   f"{repr(rounding)}")
        raise ValueError(err_msg)

    # only act on non-na values
    nans = pd.isna(series)
    result = pd.Series(np.full(len(series), pd.NA, dtype="O"))
    subset = np.array(series[~nans], dtype="m8")  # destroys mixed timedeltas
    # consider applying np.frompyfunc(lambda x: np.datetime_data(x), 1, 2)
    # to get units and step size of each element.  Multiply values by step size
    # after converting to integer, then supply units as a vector to
    # convert_unit.


####################################
####    Datetime Conversions    ####
####################################


def pandas_timestamp_to_integer(
    series: pd.Timestamp | list | np.ndarray | pd.Series,
    unit: str = "ns",
    since: str | datetime_like | list | np.ndarray | pd.Series = "1970-01-01 00:00:00+0000",
    rounding: str = "floor",
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = int,
    errors: str = "raise"
) -> pd.Series:
    """Convert a `pandas.Timestamp` series to integers."""
    # vectorize input, detect missing values, and allocate result
    series = pd.Series(series).infer_objects()
    nans = pd.isna(series)
    subset = series[~nans]
    result = pd.Series(np.full(len(series), pd.NA), dtype="O")

    # validate inputs
    _validate_pandas_timestamp_series(subset)
    _validate_unit(unit)
    # since_offset = datetime_to_integer(since, unit="ns")
    _validate_rounding(rounding)


    # convert since to pd.Timestamp and localize to UTC
    # TODO: allow arbitrary offsets, not just pd.Timestamp
    if isinstance(since, str):
        since = string_to_pandas_timestamp(since, "UTC")[0]
    elif isinstance(since, pd.Timestamp):
        if since.tzinfo is None:  # assume UTC
            since = since.tz_localize("UTC")
        else:
            since = since.tz_convert("UTC")
    else:
        err_msg = (f"[{error_trace()}] `since` must be an instance of "
                   f"`pandas.Timestamp` or a valid datetime string, not "
                   f"{type(since)}")
        raise TypeError(err_msg)


    # pd.to_numeric fails on object-dtypes series (mixed tz, aware/naive)
    if pd.api.types.is_object_dtype(series):
        to_utc = (lambda dt: dt.tz_convert("UTC") if dt.tzinfo
                             else dt.tz_localize("UTC"))
        to_utc = np.frompyfunc(to_utc, 1, 1)
        subset = to_utc(subset)  # convert all timestamps to UTC

    # convert to nanoseconds since UTC and correct for offset
    offset = int(since.asm8)  # TODO: use general datetime_to_integer instead
    subset = pd.to_numeric(subset, errors=errors).astype("O") - offset

    # convert to final unit and assign to result
    result[~nans] = convert_unit(subset, "ns", unit, since=since,
                                 rounding=rounding)
    # TODO: run through integer_to_integer to handle dtype, downcast
    return result


def pydatetime_to_integer(
    series: datetime.datetime | list | np.ndarray | pd.Series,
    unit: str = "ns",
    since: str | datetime_like = "1970-01-01 00:00:00+0000",
    rounding: str = "floor",
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = int,
    errors: str = "raise"
) -> pd.Series:
    """Convert a `datetime.datetime` series to integers."""
    # vectorize input, detect missing values, and allocate result
    series = pd.Series(series, dtype="O")
    nans = pd.isna(series)
    subset = series[~nans]
    result = pd.Series(np.full(len(series), pd.NaT), dtype="O")

    # validate input
    _validate_pydatetime_series(subset)
    _validate_unit(unit)
    # since_offset = datetime_to_integer(since, unit="ns")
    _validate_rounding(rounding)

    # convert since to datetime.datetime and localize to UTC
    # TODO: allow arbitrary offsets, not just datetime.datetime
    utc = datetime.timezone.utc
    if isinstance(since, str):
        since = string_to_pydatetime(since, "UTC")[0]
    elif isinstance(since, datetime.datetime):
        if since.tzinfo is None:  # assume UTC
            since = since.replace(tzinfo=utc)
        else:
            since = since.astimezone(utc)
    else:
        err_msg = (f"[{error_trace()}] `since` must be an instance of "
                   f"`datetime.datetime` or a valid datetime string, not "
                   f"{type(since)}")
        raise TypeError(err_msg)

    # convert datetimes to timedeltas since UTC.  Note: this supports (almost)
    # arbitrary offsets because the available range for datetime.timedelta >>
    # [datetime.datetime.min, datetime.datetime.max]
    try:
        epoch = datetime.datetime.fromtimestamp(0).astimezone(utc)
        result[~nans] = series[~nans] - epoch  # series is tz-aware
    except TypeError:  # series might be naive
        naive_epoch = epoch.replace(tzinfo=None)
        try:
            result[~nans] = series[~nans] - naive_epoch  # series is tz-naive
        except TypeError:  # series might be mixed aware/naive
            # convert elementwise using custom ufunc (slow)
            convert_to_timedelta = (lambda dt: dt - epoch if dt.tzinfo
                                               else dt - naive_epoch)
            convert_to_timedelta = np.frompyfunc(convert_to_timedelta, 1, 1)
            result[~nans] = convert_to_timedelta(series[~nans])

    # convert timedeltas to integer using datetime_timedelta_to_integer
    return datetime_timedelta_to_integer(result, unit=unit, since=since,
                                         rounding=rounding, downcast=downcast,
                                         dtype=dtype, errors=errors)


def numpy_datetime64_to_integer(
    series: np.datetime64 | list | np.ndarray | pd.Series,
    unit: str | list | np.ndarray | pd.Series = "ns",
    since: str | datetime_like | list | np.ndarray | pd.Series = "1970-01-01 00:00:00+0000",
    rounding: str = "truncate",
    downcast: bool = False,
    dtype: type | str | np.dtype | pd.api.extensions.ExtensionDtype = int
) -> pd.Series:
    """Convert a `numpy.datetime64` series to integers."""
    # vectorize input
    if isinstance(series, np.ndarray) and np.issubdtype(series.dtype, "M8"):
        # This prevents automatic M8 conversion, which can destroy information:
        # -> series = [np.datetime64(2**63 - 1971, "Y"), np.datetime64(1, "M")]
        # -> np.array(series) == ['-001-01', '1970-02']
        # -> np.array(series, dtype="O") = ['9223372036854775807', '1970-02']
        series = list(series)
    series = np.atleast_1d(np.array(series, dtype="O"))
    unit = np.atleast_1d(np.array(unit))
    since = np.atleast_1d(np.array(since))

    # broadcast to same size
    series, unit, since = np.broadcast_arrays(series, unit, since)
    series = series.copy()

    # detect missing values and subset
    to_convert = pd.notna(series)
    subset = series[to_convert]
    sub_unit = unit[to_convert]
    sub_since = since[to_convert]
    series[~to_convert] = pd.NA  # missing values coerced to pd.NA

    # validate input and establish since_offset in given units
    _validate_numpy_datetime64_series(subset)
    _validate_unit(sub_unit)
    if (sub_since == "1970-01-01 00:00:00+0000").all():  # default
        since_offset = 0  # breaks circular reference
    else:  # attempt to interpret non-default `since` offset
        if pd.api.types.infer_dtype(sub_since) == "string":
            sub_since = string_to_datetime(sub_since, tz=None)
        since_offset = datetime_to_integer(sub_since, unit=sub_unit,
                                           rounding=rounding)
    _validate_rounding(rounding)

    # convert to integer representation in native units, adjusted for step size
    def convert_to_integer(dt64: np.datetime64) -> tuple[int, str]:
        dt64_unit, step_size = np.datetime_data(dt64.dtype)
        return (int(dt64.view(np.int64)) * step_size, dt64_unit)
    convert_to_integer = np.frompyfunc(convert_to_integer, 1, 2)
    subset, dt64_unit = convert_to_integer(subset)

    # convert to given units and apply offset
    utc_epoch = datetime.date(1970, 1, 1)
    subset = convert_unit(subset, dt64_unit, sub_unit, utc_epoch, rounding)
    subset -= since_offset

    # reassign subset to series
    series[to_convert] = subset

    # TODO: run through integer_to_integer to sort out `dtype` and `downcast`
    return pd.Series(series)




    # TODO: when converting arbitrary datetimes to integer, use
    # np.frompyfunc(type, 1, 1) and then compute each subset separately
    # -> allows for even mixed series of Timestamp, datetime, np.datetime64
    # if pd.api.types.is_datetime64_dtype(series):
    #     # use pandas_timestamp_to_integer directly
    # elif pd.api.types.infer_dtype(series) in ("datetime", "datetime64"):
    #     # apply vectorized type function and process each subset separately
    #     # drop to unique values, ignoring indices that are na in original
    #     # series.  Compare against [pd.Timestamp, datetime.datetime,
    #     # np.datetime64] to detect bad series


##################################
####    String Conversions    ####
##################################


def string_to_pandas_timedelta(
    series: str | list | np.ndarray | pd.Series,
    errors: str = "raise"
) -> pd.Series:
    """Convert a timedelta string series to `pandas.Timedelta` objects."""
    # vectorize input
    series = pd.Series(series)

    # check series contains string data
    if pd.api.types.infer_dtype(series) != "string":
        err_msg = (f"[{error_trace()}] `series` must contain string data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check errors is valid
    if errors not in ['raise', 'warn', 'ignore']:
        err_msg = (f"[{error_trace()}] `errors` must one of "
                   f"{['raise', 'warn', 'ignore']}, not {repr(errors)}")
        raise ValueError(err_msg)

    # do conversion -> use pd.to_timedelta directly
    return pd.to_timedelta(series, errors=errors)


def string_to_datetime(
    series: str | list | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = "local",
    format: str | None = None,
    fuzzy: bool = False,
    day_first: bool = False,
    year_first: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert a datetime string into any form of datetime object."""
    series = pd.Series(series)

    # try pd.Timestamp
    try:
        return string_to_pandas_timestamp(series, tz=tz, format=format, 
                                          fuzzy=fuzzy, day_first=day_first,
                                          year_first=year_first, errors=errors)
    except (OverflowError, pd._libs.tslibs.np_datetime.OutOfBoundsDatetime,
            dateutil.parser.ParserError):
        pass

    # try datetime.datetime
    try:
        return string_to_pydatetime(series, tz=tz, format=format,
                                           fuzzy=fuzzy, day_first=day_first,
                                           year_first=year_first, errors=errors)
    except (OverflowError, dateutil.parser.ParserError):
        pass

    # try np.datetime64
    if any((format, fuzzy, day_first, year_first)):
        err_msg = (f"[{error_trace()}] `numpy.datetime64` objects do not "
                   f"support arbitrary string parsing (string must be ISO "
                   f"8601-compliant)")
        raise TypeError(err_msg)
    if tz and tz not in ("UTC", datetime.timezone.utc, pytz.utc,
                         zoneinfo.ZoneInfo("UTC")):
        warn_msg = ("`numpy.datetime64` objects do not carry timezone "
                    "information - returned time is UTC")
        warnings.warn(warn_msg, RuntimeWarning)
    try:
        return string_to_numpy_datetime64(series, errors)
    except Exception as err:
        err_msg = (f"[{error_trace()}] could not convert string to any form "
                   f"of datetime object")
        raise ValueError(err_msg) from err


def string_to_pandas_timestamp(
    series: str | list | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = "local",
    format: str | None = None,
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    errors: str = "warn"
) -> pd.Series:
    """Convert a datetime string series into `pandas.Timestamp` objects."""
    # TODO: replicate behavior of pandas errors arg {'ignore', 'raise', 'coerce'}

    # vectorize input
    series = pd.Series(series)

    # check series contains string data
    if pd.api.types.infer_dtype(series) != "string":
        err_msg = (f"[{error_trace()}] `series` must contain string data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # validate timezone and convert to datetime.tzinfo object
    if isinstance(tz, str):
        if tz == "local":
            tz = pytz.timezone(tzlocal.get_localzone_name())
        else:
            tz = pytz.timezone(tz)
    if tz and not isinstance(tz, pytz.BaseTzInfo):
        err_msg = (f"[{error_trace()}] `tz` must be a pytz.timezone object or "
                   f"an IANA-recognized timezone string, not {type(tz)}")
        raise TypeError(err_msg)

    # check format is a string or None
    if format is not None:
        if day_first or year_first:
            err_msg = (f"[{error_trace()}] `day_first` and `year_first` only "
                       f"apply when no format is specified")
            raise RuntimeError(err_msg)
        if not isinstance(format, str):
            err_msg = (f"[{error_trace()}] if given, `format` must be a "
                       f"datetime format string, not {type(format)}")
            raise TypeError(err_msg)

    # check errors is valid
    if errors not in ['raise', 'warn', 'ignore']:
        err_msg = (f"[{error_trace()}] `errors` must one of "
                   f"{['raise', 'warn', 'ignore']}, not {repr(errors)}")
        raise ValueError(err_msg)

    # do conversion -> use pd.to_datetime with appropriate args
    if format:  # use specified format
        result = pd.to_datetime(series, utc=True, format=format,
                                exact=not fuzzy)
    else:  # infer format
        result = pd.to_datetime(series, utc=True, dayfirst=day_first,
                                yearfirst=year_first,
                                infer_datetime_format=True)
    # TODO: this last localize step uses LMT (local mean time) for dates prior
    # to 1902 for some reason.  This appears to be a known pytz limitation.
    # https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
    # https://github.com/pandas-dev/pandas/issues/41834
    # solution: use zoneinfo.ZoneInfo instead once pandas supports it
    # https://github.com/pandas-dev/pandas/pull/46425
    return result.dt.tz_convert(tz)


def string_to_pydatetime(
    series: str | list | np.ndarray | pd.Series,
    tz: str | datetime.tzinfo = "local",
    format: str | None = None,
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    errors: str = "warn"
) -> pd.Series:
    """Convert a datetime string series into `datetime.datetime` objects."""
    # TODO: replicate behavior of pandas errors arg {'ignore', 'raise', 'coerce'}

    # vectorize input
    series = pd.Series(series)

    # check series contains string data
    if pd.api.types.infer_dtype(series) != "string":
        err_msg = (f"[{error_trace()}] `series` must contain string data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # validate timezone and convert to datetime.tzinfo object
    if isinstance(tz, str):
        if tz == "local":
            tz = pytz.timezone(tzlocal.get_localzone_name())
        else:
            tz = pytz.timezone(tz)
    if tz and not isinstance(tz, pytz.BaseTzInfo):
        err_msg = (f"[{error_trace()}] `tz` must be a pytz.timezone object or "
                   f"an IANA-recognized timezone string, not {type(tz)}")
        raise TypeError(err_msg)

    # check format is a string or None
    if format is not None:
        if day_first or year_first:
            err_msg = (f"[{error_trace()}] `day_first` and `year_first` only "
                       f"apply when no format is specified")
            raise RuntimeError(err_msg)
        if not isinstance(format, str):
            err_msg = (f"[{error_trace()}] if given, `format` must be a "
                       f"datetime format string, not {type(format)}")
            raise TypeError(err_msg)

    # check errors is valid
    if errors not in ['raise', 'warn', 'ignore']:
        err_msg = (f"[{error_trace()}] `errors` must one of "
                   f"{['raise', 'warn', 'ignore']}, not {repr(errors)}")
        raise ValueError(err_msg)

    # do conversion -> use an elementwise conversion func + dateutil
    if format:
        def convert_to_datetime(datetime_string: str) -> datetime.datetime:
            try:
                result = datetime.datetime.strptime(datetime_string.strip(),
                                                    format)
            except ValueError as err:
                err_msg = (f"[{error_trace(stack_index=4)}] unable to "
                           f"interpret {repr(datetime_string)} according to "
                           f"format {repr(format)}")
                raise ValueError(err_msg) from err
            if not result.tzinfo:
                result = result.replace(tzinfo=datetime.timezone.utc)
            return result.astimezone(tz)
    else:
        def convert_to_datetime(datetime_string: str) -> datetime.datetime:
            result = dateutil.parser.parse(datetime_string, dayfirst=day_first,
                                           yearfirst=year_first, fuzzy=fuzzy)
            if not result.tzinfo:
                result = result.replace(tzinfo=datetime.timezone.utc)
            return result.astimezone(tz)

    convert_to_datetime = np.frompyfunc(convert_to_datetime, 1, 1)
    return pd.Series(convert_to_datetime(np.array(series)), dtype="O")


def string_to_numpy_datetime64(
    series: str | list | np.ndarray | pd.Series,
    errors: str = "warn"
) -> pd.Series:
    """Convert a datetime string series into `numpy.datetime64` objects."""
    # TODO: replicate behavior of pandas errors arg {'ignore', 'raise', 'coerce'}

    # vectorize input
    series = pd.Series(series)

    # check series contains string data
    if pd.api.types.infer_dtype(series) != "string":
        err_msg = (f"[{error_trace()}] `series` must contain string data "
                   f"(received: {pd.api.types.infer_dtype(series)})")
        raise TypeError(err_msg)

    # check errors is valid
    if errors not in ['raise', 'warn', 'ignore']:
        err_msg = (f"[{error_trace()}] `errors` must one of "
                   f"{['raise', 'warn', 'ignore']}, not {repr(errors)}")
        raise ValueError(err_msg)

    # do conversion -> requires ISO format and does not carry tzinfo
    return pd.Series(list(series.array.astype("M8")), dtype="O")
