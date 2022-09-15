from __future__ import annotations
import datetime
import re
from typing import Union
import warnings

import dateutil
import numpy as np
import pandas as pd
import pytz
from sympy import comp
import tzlocal
import zoneinfo

from pdtypes.check import get_dtype
from pdtypes.error import error_trace
from pdtypes.round import round_div
from pdtypes.util.array import broadcast_args, replace_with_dict
from pdtypes.util.type_hints import datetime_like


"""
TODO: https://i.stack.imgur.com/uiXQd.png
TODO: use numpy arrays (rather than pd.Series) for all functions in this module
"""



# TODO: these should become part of core.py, along with convert_unit, to_unit,
# etc.


def months_to_ns(
    val: np.ndarray,
    since: datetime.date | datetime_like | list | np.ndarray | pd.Series
) -> np.ndarray:
    """Convert a number of months to a day offset from a given date."""
    # TODO: check if since is missing? -> leads to nan injection
    start_year, start_month, start_day = date_components(since)
    offset = date_to_days(start_year, start_month, start_day)
    result = date_to_days(start_year, start_month + val, start_day)
    return (result - offset) * _to_ns["D"]


def years_to_ns(
    val: np.ndarray,
    since: datetime.date | datetime_like | list | np.ndarray | pd.Series
) -> np.ndarray:
    """Convert a number of years to a day offset from a given date."""
    # TODO: check if since is missing? -> leads to nan injection
    start_year, start_month, start_day = date_components(since)
    offset = date_to_days(start_year, start_month, start_day)
    result = date_to_days(start_year + val, start_month, start_day)
    return (result - offset) * _to_ns["D"]


def ns_to_months(
    val: np.ndarray,
    since: datetime.date | datetime_like | list | np.ndarray | pd.Series,
    rounding: str = "truncate"
) -> np.ndarray:
    """Convert a number of days to a month offset from a given date."""
    # TODO: check if since is missing? -> leads to nan injection
    # get (Y, M, D) components of `since` and establish UTC offset in days
    start_year, start_month, start_day = date_components(since)
    offset = date_to_days(start_year, start_month, start_day)

    # convert val to days and add offset to compute final calendar date
    val = round_div(val, _to_ns["D"], rounding=rounding) + offset
    end_date = days_to_date(val)

    # result is the difference between end month and start month, plus years
    result = (12 * (end_date["year"] - start_year) +
              end_date["month"] - start_month)

    # correct for premature rollover and apply floor/ceiling rules
    right = (end_date["day"] < start_day)
    if rounding == "floor":  # emulate floor rounding to -infinity
        return result - right
    left = (end_date["day"] > start_day)
    if rounding == "ceiling":  # emulate ceiling rounding to +infinity
        return result + left

    # truncate, using floor for positive values and ceiling for negative
    positive = (result > 0)
    negative = (result < 0)
    result[positive] -= right[positive]
    result[negative] += left[negative]
    if rounding == "truncate":  # skip processing residuals
        return result

    # compute residuals and round
    result_days = months_to_ns(result, since=since)
    residuals = val - result_days  # residual in nanoseconds
    days_in_last_month = months_to_ns(result + 1, since=since) // _to_ns["D"]
    days_in_last_month -= result_days
    return result + round_div(residuals, days_in_last_month, rounding="round")


def ns_to_years(
    val: np.ndarray,
    since: datetime.date | datetime_like | list | np.ndarray | pd.Series,
    rounding: str = "truncate"
) -> np.ndarray:
    """Convert a number of days to a month offset from a given date."""
    # TODO: check if since is missing? -> leads to nan injection
    # get (Y, M, D) components of `since` and establish UTC offset in days
    start_year, start_month, start_day = date_components(since)
    offset = date_to_days(start_year, start_month, start_day)

    # convert val to days and add offset to compute final calendar date
    val = round_div(val, _to_ns["D"], rounding=rounding) + offset
    end_date = days_to_date(val)

    # result is the difference between end year and start year
    result = end_date["year"] - start_year

    # correct for premature rollover and apply floor/ceiling rules
    right = (end_date["month"] < start_month) | (end_date["day"] < start_day)
    if rounding == "floor":  # emulate floor rounding to -infinity
        return result - right
    left = (end_date["month"] > start_month) | (end_date["day"] > start_day)
    if rounding == "ceiling":  # emulate ceiling rounding to +infinity
        return result + left

    # truncate, using floor for positive values and ceiling for negative
    positive = (result > 0)
    negative = (result < 0)
    result[positive] -= right[positive]
    result[negative] += left[negative]
    if rounding == "truncate":  # skip processing residuals
        return result

    # compute residuals and round
    residuals = val - years_to_ns(result, since=since) // _to_ns["D"]
    days_in_last_year = 365 + is_leap(start_year + result)
    return result + round_div(residuals, days_in_last_year, rounding="round")


def convert_unit(
    val: int | list | np.ndarray | pd.Series,
    before: str | list | np.ndarray | pd.Series,
    after: str | list | np.ndarray | pd.Series,
    since: datetime.date | datetime_like | list | np.ndarray | pd.Series,
    rounding: str = "truncate"
) -> np.ndarray:
    """Convert an integer number of the given units to another unit."""
    # TODO: for float input, round to nearest nanosecond and change `before`
    # to match prior to inputting to this function
    # vectorize input
    val = np.atleast_1d(np.array(val, dtype="O"))  # dtype="O" prevents overflow
    before = np.atleast_1d(np.array(before))
    after = np.atleast_1d(np.array(after))
    since = np.atleast_1d(np.array(since))

    # broadcast inputs and initialize result
    val, before, after, since = np.broadcast_arrays(val, before, after, since)
    val = val.copy()  # converts memory view into assignable result

    # check units are valid
    valid_units = list(_to_ns) + ["M", "Y"]
    if not np.isin(before, valid_units).all():
        bad = list(np.unique(before[~np.isin(before, valid_units)]))
        err_msg = (f"[{error_trace()}] `before` unit {bad} not recognized: "
                   f"must be in {valid_units}")
        raise ValueError(err_msg)
    if not np.isin(after, valid_units).all():
        bad = list(np.unique(after[~np.isin(after, valid_units)]))
        err_msg = (f"[{error_trace()}] `after` unit {bad} not recognized: "
                   f"must be in {valid_units}")
        raise ValueError(err_msg)

    # trivial case (no conversion)
    if np.array_equal(before, after):
        return val

    # get indices where conversion is necessary
    to_convert = (before != after)

    # trivial year/month conversions
    trivial_years = (before == "Y") & (after == "M")
    val[trivial_years] *= 12  # multiply years by 12 to get months
    to_convert ^= trivial_years  # ignore trivial indices

    # trivial month/year conversions
    trivial_months = (before == "M") & (after == "Y")
    val[trivial_months] = round_div(val[trivial_months], 12, rounding=rounding)
    to_convert ^= trivial_months  # ignore trivial indices

    # check for completeness
    if not to_convert.any():
        return val

    # continue converting non-trivial indices
    subset = val[to_convert]
    before = before[to_convert]
    after = after[to_convert]
    since = since[to_convert]

    # convert subset to nanoseconds
    months = (before == "M")
    years = (before == "Y")
    other = ~(months | years)
    subset[months] = months_to_ns(subset[months], since[months])
    subset[years] = years_to_ns(subset[years], since[years])
    subset[other] *= replace_with_dict(before[other], _to_ns)

    # convert subset nanoseconds to final unit
    months = (after == "M")
    years = (after == "Y")
    other = ~(months | years)
    subset[months] = ns_to_months(subset[months], since[months],
                                  rounding=rounding)
    subset[years] = ns_to_years(subset[years], since[years], rounding=rounding)
    coefficients = replace_with_dict(after[other], _to_ns)
    subset[other] = round_div(subset[other], coefficients, rounding=rounding)

    # reassign subset to val and return
    val[to_convert] = subset
    return val













# def convert_unit(
#     val: int | np.ndarray | pd.Series,
#     before: str,
#     after: str,
#     since: str | datetime_like = "1970-01-01 00:00:00+0000",
#     rounding: str = "floor"
# ) -> pd.Series | tuple[pd.Series, pd.Series]:
#     """Convert an integer number of the given units to another unit."""
#     # vectorize input
#     val = pd.Series(val, dtype="O")  # object dtype prevents overflow

#     # check units are valid
#     valid_units = list(_to_ns) + ["M", "Y"]
#     if not (before in valid_units and after in valid_units):
#         bad = before if before not in valid_units else after
#         err_msg = (f"[{error_trace()}] unit {repr(bad)} not recognized - "
#                    f"must be in {valid_units}")
#         raise ValueError(err_msg)

#     # trivial cases
#     if before == after:
#         return val
#     if before == "Y" and after == "M":
#         return val * 12
#     if before == "M" and after == "Y":
#         if rounding == "floor":
#             return val // 12
#         result = val // 12
#         residuals = ((val % 12) / 12).astype(float)
#         if rounding == "round":
#             result[residuals >= 0.5] += 1
#         else:  # rounding == "ceiling"
#             result[residuals > 0] += 1
#         return result

#     # get start date and establish year/month/day conversion functions
#     if isinstance(since, (str, np.datetime64)):
#         components = datetime64_components(np.datetime64(since))
#         start_year = int(components["year"])
#         start_month = int(components["month"])
#         start_day = int(components["day"])
#     else:
#         start_year = since.year
#         start_month = since.month
#         start_day = since.day
#     y2d = lambda y: date_to_days(start_year + y, start_month, start_day)
#     m2d = lambda m: date_to_days(start_year, start_month + m, start_day)
#     d2y = lambda d: days_to_date(d)["year"]
#     d2m = lambda d: 12 * (cal := days_to_date(d))["year"] + cal["month"]

#     # convert to nanoseconds
#     if before == "M":
#         # TODO: if vectorizing units, just convert both entries to the
#         # appropriate values
#         nanoseconds = pd.Series(m2d(val) - m2d(0)) * _to_ns["D"]
#     elif before == "Y":
#         # TODO: if vectorizing units, just convert both entries to the
#         # appropriate values
#         nanoseconds = pd.Series(y2d(val) - y2d(0)) * _to_ns["D"]
#     else:
#         # TODO: use replace_with_dict to get a vector of ns coefficients from
#         # _to_ns.  Multiply these to get nanoseconds
#         nanoseconds = val * _to_ns[before]

#     # convert nanoseconds to final unit
#     if after == "M":  # convert nanoseconds to days, then days to months
#         # TODO: apply this in a vectorized fashion wherever after == "M"
#         # modify both entries, as with nanoseconds above
#         days = nanoseconds // _to_ns["D"]
#         day_offset = m2d(0)

#         # get integer result
#         start_offset = 12 * (start_year) + start_month
#         result = pd.Series(d2m(days + day_offset) - start_offset)
#         if rounding == "floor":  # fastpath: don't bother calculating residuals
#             return result

#         # compute residuals
#         result_days = m2d(result)
#         residual_days = days - result_days + day_offset
#         days_in_last_month = m2d(1 + result) - result_days
#         residuals = (residual_days / days_in_last_month).astype(float)
#     elif after == "Y":  # convert nanoseconds to days, then days to years
#         # TODO: apply this in a vectorized fashion wherever after == "Y"
#         # modify both entries, as with nanoseconds above
#         days = nanoseconds // _to_ns["D"]
#         day_offset = y2d(0)

#         # get integer result
#         result = pd.Series(d2y(days + day_offset) - start_year)
#         if rounding == "floor":  # fastpath: don't bother calculating residuals
#             return result

#         # compute residuals
#         result_days = y2d(result)
#         residual_days = days - result_days + day_offset
#         days_in_last_year = 365 + is_leap(start_year + result)
#         residuals = (residual_days / days_in_last_year).astype(float)
#     else:  # use regular scale factor
#         # TODO: vectorized access via array indexing -> stack unit coefficients
#         # from lowest to highest and refer to them by index.
#         # TODO: could also just manually compare
#         result = nanoseconds // _to_ns[after]
#         if rounding == "floor":  # fastpath: don't bother calculating residuals
#             return result

#         # compute residuals
#         scale_factor = _to_ns[after]  # TODO: vectorized access via labeled array
#         residuals = ((nanoseconds % scale_factor) / scale_factor).astype(float)

#     # handle rounding if not floor
#     if rounding == "round":
#         result[residuals >= 0.5] += 1
#     else:  # rounding == "ceiling"
#         result[residuals > 0] += 1
#     return result


def to_unit(
    arg: tuple[int, str] | str | datetime_like | timedelta_like,
    unit: str,
    since: str | datetime_like = "1970-01-01 00:00:00+0000",
    tz: str | datetime.tzinfo = "UTC",
    format: str | None = None,
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    errors: str = "warn"
) -> int:
    """Convert a timedelta or an integer and associated unit into an integer
    number of the specified unit.

    Args:
        arg (tuple[int, str] | pd.Timedelta | datetime.timedelta | np.timedelta64): _description_
        unit (str): _description_

    Raises:
        ValueError: _description_

    Returns:
        int: _description_
    """
    # # vectorize inputs -> retain original arg if necessary
    # if isinstance(arg, tuple):
    #     series = pd.Series(arg[0], dtype="O")
    # else:
    #     series = pd.Series(arg, dtype="O")

    # # convert IANA timezone key to pytz.timezone
    # if isinstance(tz, str):
    #     tz = pytz.timezone(tz)

    # # convert ISO 8601 strings to datetimes
    # if pd.api.types.infer_dtype(series) == "string":
    #     series = string_to_datetime(series, tz=tz, format=format,
    #                                 day_first=day_first, year_first=year_first,
    #                                 fuzzy=fuzzy, errors=errors)
    # if isinstance(since, str):
    #     since = string_to_datetime(since, tz=tz, errors=errors)[0]

    # # convert datetimes to timedeltas
    # if pd.api.types.is_datetime64_any_dtype(series.infer_objects()):
    #     # series contains pd.Timestamp objects
    #     def localize_timestamp(timestamp: pd.Timestamp) -> pd.Timestamp:
    #         if timestamp.tzinfo is None:  # assume utc
    #             timestamp = timestamp.tz_localize(datetime.timezone.utc)
    #         return timestamp.tz_convert(tz)

    #     localize_timestamp = np.frompyfunc(localize_timestamp, 1, 1)
    #     try:
    #         offset = convert_datetime_type(since, pd.Timestamp)
    #         series = localize_timestamp(series) - offset
    #     except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime:
    #         series = convert_datetime_type(series, datetime.datetime)
    # if pd.api.types.infer_dtype(series) == "datetime":
    #     # series contains datetime.datetime objects
    #     def localize_datetime(dt: datetime.datetime) -> datetime.datetime:
    #         if dt.tzinfo is None:
    #             dt = dt.replace(tzinfo=datetime.timezone.utc)
    #         return dt.astimezone(tz)

    #     localize_datetime = np.frompyfunc(localize_datetime, 1, 1)
    #     try:
    #         offset = convert_datetime_type(since, pd.Timestamp)
    #         series = localize_datetime(series) - offset
    #     except OverflowError:
    #         series = convert_datetime_type(series, np.datetime64)
    # if pd.api.types.infer_dtype(series) == "datetime64":
    #     # series contains np.datetime64 objects
    #     pass


    # return series

    # # if pd.api.types.infer_dtype(arg) == "datetime"


    # # return arg



    original_arg = arg  # TODO: add an explicit type check at the top
    # arg = np.array(arg, dtype="O")  # object dtype prevents overflow
    # TODO: allow both aware and naive args
    # TODO: explicitly vectorize - use pd.api.types.infer_dtype for isinstance

    if isinstance(arg, str):
        arg = convert_iso_string(arg, tz)
    if isinstance(since, str):
        since = convert_iso_string(since, tz)
    if isinstance(tz, str):
        tz = pytz.timezone(tz)

    # convert datetimes to timedeltas
    # TODO: test each of these
    if isinstance(arg, pd.Timestamp):
        if not arg.tzinfo:
            arg = arg.tz_localize(tz)
        else:
            arg = arg.tz_convert(tz)
        try:
            arg -= convert_datetime_type(since, pd.Timestamp)
        except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime:
            arg = convert_datetime_type(arg, datetime.datetime)
    if isinstance(arg, datetime.datetime):
        if not arg.tzinfo:
            arg = arg.replace(tzinfo=tz)
        elif tz:
            arg = arg.astimezone(tz)
        else:
            arg = arg.astimezone(datetime.timezone.utc).replace(tzinfo=None)
        try:
            arg -= convert_datetime_type(since, datetime.datetime)
        except OverflowError:
            arg = convert_datetime_type(arg, np.datetime64)
    if isinstance(arg, np.datetime64):
        # TODO: strip timezone?
        arg_unit, _ = np.datetime_data(arg)
        offset = convert_datetime_type(since, np.datetime64)
        arg -= np.datetime64(offset, arg_unit)

    # convert timedeltas to final units
    if isinstance(arg, tuple):
        return convert_unit(arg[0], arg[1], unit, since)
    if isinstance(arg, pd.Timedelta):
        nanoseconds = int(arg.asm8.astype(np.int64))
        return convert_unit(nanoseconds, "ns", unit, since)
    if isinstance(arg, datetime.timedelta):
        coefs = np.array([24 * 60 * 60 * int(1e6), int(1e6), 1], dtype="O")
        comps = np.array([arg.days, arg.seconds, arg.microseconds], dtype="O")
        microseconds = int(np.sum(coefs * comps))
        return convert_unit(microseconds, "us", unit, since)
    if isinstance(arg, np.timedelta64):
        arg_unit, _ = np.datetime_data(arg)
        int_repr = int(arg.astype(np.int64))
        return convert_unit(int_repr, arg_unit, unit, since)

    # TODO: fill out error message
    err_msg = (f"[{error_trace()}] could not convert value to unit "
               f"{repr(unit)}: {repr(original_arg)}")
    raise RuntimeError(err_msg)
