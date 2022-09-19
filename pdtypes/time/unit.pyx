import decimal

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.round import round_div, round_generic
from pdtypes.util.type_hints import datetime_like

from .calendar import (
    date_to_days, days_in_month, days_to_date, decompose_date, is_leap_year
)


# TODO: ensure unit length definitions are correct


#########################
####    Constants    ####
#########################


cdef dict as_ns = {
    # "as": 1e-9,
    # "fs": 1e-6,
    # "ps": 1e-3,
    "ns": 1,
    "us": 10**3,
    "ms": 10**6,
    "s": 10**9,
    "m": 60 * 10**9,
    "h": 60 * 60 * 10**9,
    "D": 24 * 60 * 60 * 10**9,
    "W": 7 * 24 * 60 * 60 * 10**9,
}


cdef tuple valid_units = tuple(as_ns) + ("M", "Y")


#######################
####    Private    ####
#######################


cdef object round_years_to_ns(
    object years,
    object start_year,
    object start_month,
    object start_day
):
    """An optimized fastpath for convert_unit_float() that assumes imprecise
    (decimal) year input and integer nanosecond output.  Only accepts scalars.
    
    This task is required in the main loop of all string to timedelta
    conversions wherever they contain references to years.  By importing and
    using this function over the more full-featured `convert_unit()` or
    `convert_unit_float()`, each conversion can avoid a significant number of
    potential branch checks and vectorization loops.

    Note: always rounds down (toward zero).
    """
    cdef object rounded
    cdef dict end
    cdef bint skip
    cdef short unit_length

    # handle integer and fractional components separately
    rounded = int(years)
    years = years - rounded  # residual

    # convert integer component to utc day offset and establish ending date
    rounded = date_to_days(
        start_year + rounded,
        start_month,
        start_day
    )
    end = days_to_date(rounded)

    # establish length of final unit in days
    # note: if start date occurs on or after a potential leap day, use next
    # year's calendar.  If residual `years` are negative, use last year's.
    skip = (
        (
            date_to_days(start_year, start_month, start_day) -
            date_to_days(start_year, 1, 1)
        ) > 58
    )
    unit_length = 365 + is_leap_year(end["year"] + skip - (years < 0))

    # subtract off start date to get total elapsed days
    if (start_year, start_month, start_day) != (1970, 1, 1):
        rounded -= date_to_days(start_year, start_month, start_day)

    # scale fractional component by unit length and reintroduce integer
    return rounded * as_ns["D"] + int(years * unit_length * as_ns["D"])


cdef object round_months_to_ns(
    object months,
    object start_year,
    object start_month,
    object start_day
):
    """An optimized fastpath for convert_unit_float() that assumes imprecise
    (decimal) month input and integer nanosecond output.  Only accepts scalars
    
    This task is required in the main loop of all string to timedelta
    conversions wherever they contain references to months.  By importing and
    using this function over the more full-featured `convert_unit()` or
    `convert_unit_float()`, each conversion can avoid a significant number of
    potential branch checks and vectorization loops.
    """
    cdef object rounded
    cdef dict end
    cdef char unit_length

    # handle integer and fractional components separately
    rounded = int(months)
    months = months - rounded

    # convert integer component to utc day offset and establish ending date
    rounded = date_to_days(
        start_year,
        start_month + rounded,
        start_day
    )
    end = days_to_date(rounded)

    # establish length of final unit in days
    # note: if residual `months` are negative, use the previous month
    unit_length = days_in_month(end["month"] - (months < 0), end["year"])

    # subtract off start date to get total elapsed days
    if (start_year, start_month, start_day) != (1970, 1, 1):
        rounded -= date_to_days(start_year, start_month, start_day)

    # scale fractional component by unit length and reintroduce integer
    return rounded * as_ns["D"] + int(months * unit_length * as_ns["D"])


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] cast_to_int_vector(
    np.ndarray arr,
    str rounding
):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        element = arr[i]
        if isinstance(element, int):
            continue
        result[i] = int(round_generic(element, rule=rounding))

    return result


cdef object cast_to_int(object val, str rounding):
    """TODO"""
    # np.ndarray
    if isinstance(val, np.ndarray):
        return cast_to_int_vector(val, rounding=rounding)

    # pd.Series
    if isinstance(val, pd.Series):
        index = val.index
        val = cast_to_int_vector(val.to_numpy(), rounding=rounding)
        return pd.Series(val, index=index, copy=False)

    # scalar
    if isinstance(val, int):
        return val
    return int(round_generic(val, rule=rounding))


#######################
####    Helpers    ####
#######################


def _convert_unit_float_irregular_to_regular(
    arg : float | complex | decimal.Decimal | np.ndarray | pd.Series,
    from_unit: str,
    to_unit: str,
    rounding: str,
    since: dict
) -> float | complex | decimal.Decimal | np.ndarray | pd.Series:
    """TODO"""
    # handle integer and fractional components separately
    delta = cast_to_int(arg, rounding="down")
    arg = arg - delta

    # TODO: check unit lengths are correct for +/- inputs, different offsets
    # months are good, years idk

    # convert to utc day offset and establish length of final unit
    if from_unit == "M":  # month -> day
        delta = date_to_days(
            since["year"],
            since["month"] + delta,
            since["day"]
        )
        end = days_to_date(delta)
        unit_length = days_in_month(end["month"] - (arg < 0), end["year"])
    else:  # year -> day
        delta = date_to_days(
            since["year"] + delta,
            since["month"],
            since["day"]
        )
        end = days_to_date(delta)
        skip = (date_to_days(**since) - date_to_days(since["year"], 1, 1)) > 58
        unit_length = 365 + is_leap_year(end["year"] + skip - (arg < 0))

    # subtract off `since` day offset
    if since != {"year": 1970, "month": 1, "day": 1}:
        delta -= date_to_days(**since)

    # scale fractional component by unit length and reintroduce integer
    arg *= unit_length
    arg += delta

    # day -> day
    if to_unit == "D":
        return arg if rounding is None else cast_to_int(arg, rounding=rounding)

    # day -> week
    if to_unit == "W":
        arg /= 7
        return arg if rounding is None else cast_to_int(arg, rounding=rounding)

    # day -> ns, us, ms, s, m, h
    arg *= as_ns["D"]
    arg /= as_ns[to_unit]
    return arg if rounding is None else cast_to_int(arg, rounding=rounding)


def _convert_unit_float_regular_to_irregular(
    arg: float | complex | decimal.Decimal | np.ndarray | pd.Series,
    from_unit: str,
    to_unit: str,
    rounding: str,
    since: dict
) -> float | complex | decimal.Decimal | np.ndarray | pd.Series:
    """TODO"""
    # convert to days, then use days_to_date to get final date
    arg = arg * as_ns[from_unit] / as_ns["D"]
    delta = cast_to_int(arg, rounding="down")
    arg -= delta
    if since != {"year": 1970, "month": 1, "day": 1}:
        delta += date_to_days(**since)
    end = days_to_date(delta)

    # day -> year
    if to_unit == "Y":
        delta -= date_to_days(end["year"], since["month"], since["day"])
        arg += delta
        arg /= 365 + is_leap_year(end["year"])
        arg += end["year"] - since["year"]
    else:  # day -> month
        delta -= date_to_days(end["year"], end["month"], since["day"])
        arg += delta
        arg /= days_in_month(end["month"], end["year"])
        arg += 12 * (end["year"] - since["year"])
        arg += end["month"] - since["month"]
    return arg if rounding is None else cast_to_int(arg, rounding=rounding)


def _convert_unit_integer_irregular_to_regular(
    arg: int | np.ndarray | pd.Series,
    from_unit: str,
    to_unit: str,
    rounding: str | None,
    since: dict
) -> int | float | np.ndarray | pd.Series:
    """Helper to convert an integer number of irregular time unit ('M', 'Y')
    to regular units ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W').
    """
    if from_unit == "M":  # month -> day
        arg = date_to_days(
            since["year"],
            since["month"] + arg,
            since["day"]
        )
    else:  # year -> day
        arg = date_to_days(
            since["year"] + arg,
            since["month"],
            since["day"]
        )

    # subtract off `since` offset
    if since != {"year": 1970, "month": 1, "day": 1}:
        arg -= date_to_days(**since)

    # day -> day
    if to_unit == "D":
        if rounding is None:
            arg *= 1.0
        return arg

    # day -> week
    if to_unit == "W":
        if rounding is None:
            arg /= 7
        else:
            arg = round_div(arg, 7, rule=rounding, copy=False)
        return arg

    # day -> ns, us, ms, s, m, h
    arg *= as_ns["D"]
    if rounding is None:
        arg /= as_ns[to_unit]
    else:
        arg = round_div(arg, as_ns[to_unit], rule=rounding, copy=False)
    return arg


def _convert_unit_integer_regular_to_irregular(
    arg: int | np.ndarray | pd.Series,
    from_unit: str,
    to_unit: str,
    rounding: str,
    since: dict
) -> int | float | np.ndarray | pd.Series:
    """Helper to convert an integer number of regular time units ('ns', 'us',
    'ms', 's', 'm', 'h', 'D', 'W') to irregular units ('M', 'Y').
    """
    # convert to days, then use days_to_date to get final date
    arg = arg * as_ns[from_unit]
    delta = round_div(arg, as_ns["D"], rule="down")
    residual = arg - delta * as_ns["D"]
    if since != {"year": 1970, "month": 1, "day": 1}:
        delta += date_to_days(**since)
    end = days_to_date(delta)

    # day -> year
    if to_unit == "Y":
        result = end["year"] - since["year"]
        delta -= date_to_days(end["year"], since["month"], since["day"])
        skip = (date_to_days(**since) - date_to_days(since["year"], 1, 1)) > 58
        unit_length = 365 + is_leap_year(end["year"] + skip)
    else:  # day -> month
        result = 12 * (end["year"] - since["year"])
        result += end["month"] - since["month"]
        delta -= date_to_days(end["year"], end["month"], since["day"])
        unit_length = days_in_month(end["month"], end["year"])

    # convert delta, unit length back to ns and reintroduce residual
    unit_length *= as_ns["D"]
    delta *= as_ns["D"]
    delta += residual

    # `delta` now holds the time separation (in ns) of `result` from the
    # nearest occurence of `since`, bounded to within 1 unit.  Negative
    # values indicate that `end` occurs before `since`, meaning `result`
    # is an overestimate.  Positive values indicate the opposite.  This
    # allows us to apply customizable rounding while retaining full integer
    # accuracy.

    # return exact (do not round)
    if rounding is None:
        return result + delta / unit_length

    # round toward/away from zero
    if rounding == "floor":
        return result - (delta < 0)
    if rounding == "ceiling":
        return result + (delta > 0)
    if rounding == "down":
        result -= ((arg > 0) & (delta < 0))  # floor where arg > 0
        result += ((arg < 0) & (delta > 0))  # ceiling where arg < 0
        return result
    if rounding == "up":
        result -= ((arg < 0) & (delta < 0))  # floor where arg < 0
        result += ((arg > 0) & (delta > 0))  # ceiling where arg > 0
        return result

    # corrections for round to nearest
    if to_unit == "M":
        if rounding == "half_down":
            delta += (arg < 0)
        elif rounding == "half_up":
            delta -= (arg < 0)

    # round half even has to be handled separately
    if rounding == "half_even":
        result += np.where(
            result % 2,
            round_div(delta, unit_length, rule="half_ceiling"),
            round_div(delta, unit_length, rule="half_floor")
        )
        return result[()] if not result.shape else result

    # round to nearest
    result += round_div(delta, unit_length, rule=rounding, copy=False)
    return result


######################
####    Public    ####
######################


def convert_unit_float(
    arg: float | complex | decimal.Decimal | np.ndarray | pd.Series,
    from_unit: str,
    to_unit: str,
    rounding: str | None = "down",
    since: datetime_like = pd.Timestamp("2001-01-01 00:00:00+0000")
) -> int | float | complex | decimal.Decimal | np.ndarray | pd.Series:
    """TODO"""
    # ensure units are valid
    if from_unit not in valid_units:
        raise ValueError(f"`from_unit` must be one of {valid_units}, not "
                         f"{repr(from_unit)}")
    if to_unit not in valid_units:
        raise ValueError(f"`to_unit` must be one of {valid_units}, not "
                         f"{repr(to_unit)}")

    # trivial case - no conversion necessary
    if from_unit == to_unit:
        return arg if rounding is None else cast_to_int(arg, rounding=rounding)

    # regular -> regular ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W')
    if from_unit in as_ns and to_unit in as_ns:
        arg = arg * as_ns[from_unit] / as_ns[to_unit]
        return arg if rounding is None else cast_to_int(arg, rounding=rounding)

    # irregular -> irregular ('M', 'Y')
    if from_unit not in as_ns and to_unit not in as_ns:
        # month -> year
        if from_unit == "M" and to_unit == "Y":
            arg = arg / 12
        else:
            arg = 12 * arg
        return arg if rounding is None else cast_to_int(arg, rounding=rounding)

    # decompose starting date into year, month, day
    since = decompose_date(since)

    # regular -> irregular
    if from_unit in as_ns:
        return _convert_unit_float_regular_to_irregular(
            arg=arg,
            from_unit=from_unit,
            to_unit=to_unit,
            rounding=rounding,
            since=since
        )

    return _convert_unit_float_irregular_to_regular(
        arg=arg,
        from_unit=from_unit,
        to_unit=to_unit,
        rounding=rounding,
        since=since
    )


def convert_unit_integer(
    arg: int | np.ndarray | pd.Series,
    from_unit: str,
    to_unit: str,
    rounding: str | None = "down",
    since: datetime_like = pd.Timestamp("2001-01-01 00:00:00+0000")
) -> int | float | np.ndarray | pd.Series:
    """TODO"""
    # ensure units are valid
    if from_unit not in valid_units:
        raise ValueError(f"`from_unit` must be one of {valid_units}, not "
                         f"{repr(from_unit)}")
    if to_unit not in valid_units:
        raise ValueError(f"`to_unit` must be one of {valid_units}, not "
                         f"{repr(to_unit)}")

    # trivial case - no conversion necessary
    if from_unit == to_unit:
        return 1.0 * arg if rounding is None else arg

    # regular -> regular ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W')
    if from_unit in as_ns and to_unit in as_ns:
        if rounding is None:
            return arg * as_ns[from_unit] / as_ns[to_unit]
        return round_div(arg * as_ns[from_unit], as_ns[to_unit], rule=rounding)

    # irregular -> irregular ('M', 'Y')
    if from_unit not in as_ns and to_unit not in as_ns:
        # month -> year
        if from_unit == "M" and to_unit == "Y":
            if rounding is None:
                return arg / 12
            return round_div(arg, 12, rule=rounding)

        # year -> month
        return 12.0 * arg if rounding is None else 12 * arg

    # decompose starting date into year, month, day
    since = decompose_date(since)

    # regular -> irregular
    if from_unit in as_ns:
        return _convert_unit_integer_regular_to_irregular(
            arg=arg,
            from_unit=from_unit,
            to_unit=to_unit,
            rounding=rounding,
            since=since
        )

    # irregular -> regular
    return _convert_unit_integer_irregular_to_regular(
        arg=arg,
        from_unit=from_unit,
        to_unit=to_unit,
        rounding=rounding,
        since=since
    )
