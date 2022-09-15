import decimal

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.round import round_div, round_generic
from pdtypes.util.type_hints import datetime_like

from .date import (
    date_to_days, days_in_month, days_to_date, decompose_date, is_leap_year
)


# TODO: shorten and consolidate code in convert_unit


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


@cython.boundscheck(False)
@cython.wraparound(False)
cdef np.ndarray[object] cast_to_int_vector(np.ndarray arr):
    """TODO"""
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef np.ndarray[object] result = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        result[i] = int(arr[i])

    return result


cdef object cast_to_int(object val):
    """TODO"""
    # np.ndarray
    if isinstance(val, np.ndarray):
        return cast_to_int_vector(val)

    # pd.Series
    if isinstance(val, pd.Series):
        index = val.index
        val = cast_to_int_vector(val.to_numpy())
        return pd.Series(val, index=index, copy=False)

    # scalar
    return int(val)


######################
####    Public    ####
######################


def convert_unit(
    arg: int | np.ndarray | pd.Series,
    from_unit: str,
    to_unit: str,
    since: datetime_like = pd.Timestamp("2001-01-01 00:00:00+0000"),
    rounding: str = "down"
) -> int | np.ndarray | pd.Series:
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
        return arg

    # regular -> regular ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W') 
    if from_unit in as_ns and to_unit in as_ns:
        return round_div(arg * as_ns[from_unit], as_ns[to_unit], rule=rounding)

    # irregular -> irregular ('M', 'Y')
    if from_unit not in as_ns and to_unit not in as_ns:
        # month -> year
        if from_unit == "M" and to_unit == "Y":
            return round_div(arg, 12, rule=rounding)

        # year -> month
        return 12 * arg

    # decompose starting date into year, month, day
    since = decompose_date(since)

    # conversion crosses regular/irregular boundary
    if from_unit in as_ns:  # regular -> irregular
         # convert to days, then use days_to_date to get final date
        delta = round_div(arg * as_ns[from_unit], as_ns["D"], rule=rounding)
        if since != {"year": 1970, "month": 1, "day": 1}:
            delta += date_to_days(**since)
        end = days_to_date(delta)

        # TODO: this can probably be shortened considerably by merging rounding
        # operations between branches

        # day -> year
        if to_unit == "Y":
            # round toward/away from zero
            result = end["year"] - since["year"]
            delta -= date_to_days(end["year"], since["month"], since["day"])
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

            # round to nearest
            year_length = 365 + is_leap_year(end["year"])
            if rounding == "half_even":
                result += np.where(
                    result % 2,
                    round_div(delta, year_length, rule="half_ceiling"),
                    round_div(delta, year_length, rule="half_floor")
                )
                return result[()] if not result.shape else result
            return result + round_div(delta, year_length, rule=rounding)

        # day -> month
        result = 12 * (end["year"] - since["year"])
        result += (end["month"] - since["month"])
        delta -= date_to_days(end["year"], end["month"], since["day"])
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

        # round to nearest
        month_length = days_in_month(end["month"], end["year"])
        if rounding == "half_even":
            result += np.where(
                result % 2,
                round_div(delta, month_length, rule="half_ceiling"),
                round_div(delta, month_length, rule="half_floor")
            )
            return result[()] if not result.shape else result
        if rounding == "half_down":
            delta += (arg < 0)
        if rounding == "half_up":
            delta -= (arg < 0)
        return result + round_div(delta, month_length, rule=rounding)

    # irregular -> regular
    if from_unit == "M":  # month -> day
        arg = date_to_days(since["year"], since["month"] + arg, since["day"])
    else:  # year -> day
        arg = date_to_days(since["year"] + arg, since["month"], since["day"])
    if since != {"year": 1970, "month": 1, "day": 1}:
        arg -= date_to_days(**since)

    # day -> day
    if to_unit == "D":
        return arg

    # day -> week
    if to_unit == "W":
        return round_div(arg, 7, rule=rounding)

    # day -> ns, us, ms, s, m, h
    return round_div(arg * as_ns["D"], as_ns[to_unit], rule=rounding)


def float_to_ns(
    arg: float | complex | decimal.Decimal | np.ndarray | pd.Series,
    from_unit: str,
    since: datetime_like = pd.Timestamp("2001-01-01 00:00:00+0000")
) -> int | np.ndarray | pd.Series:
    """Convert a decimal number of specified units into an integer number of
    nanoseconds.
    """
    # ensure units are valid
    if from_unit not in valid_units:
        raise ValueError(f"`from_unit` must be one of {valid_units}, not "
                         f"{from_unit}")

    # regular -> ns
    if from_unit in as_ns:
        return cast_to_int(arg * as_ns[from_unit])

    # split arg into integer, fractional components
    base = cast_to_int(arg)
    residual = arg % 1

    # convert integer component to ns
    result = convert_unit(
        base,
        from_unit,
        "ns",
        since=since,
        rounding="floor"
    )

    # decompose since offset
    since = decompose_date(since)

    # months -> ns
    if from_unit == "M":
        month_length = days_in_month(
            base % 12 + since["month"],
            base // 12 + since["year"]
        )
        return result + cast_to_int(residual * month_length * as_ns["D"])

    # years -> ns
    year_length = 365 + is_leap_year(base + since["year"])
    return result + cast_to_int(residual * year_length * as_ns["D"])
