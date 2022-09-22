"""Datetime and timedelta unit conversions.

The units covered in this module are the same as implemented in the
`numpy.datetime64`/`numpy.timedelta64` interface.

Functions
---------
    convert_unit_float(
        arg: float | complex | decimal.Decimal | np.ndarray | pd.Series,
        from_unit: str,
        to_unit: str,
        rounding: str | None = "down",
        since: datetime_like = pd.Timestamp("2001-01-01 00:00:00+0000")
    ) -> int | float | complex | decimal.Decimal | np.ndarray | pd.Series:
        Convert fractional quantities of a given time unit into a different
        unit.

    convert_unit_integer(
        arg: int | np.ndarray | pd.Series,
        from_unit: str,
        to_unit: str,
        rounding: str | None = "down",
        since: datetime_like = pd.Timestamp("2001-01-01 00:00:00+0000")
    ) -> int | float | np.ndarray | pd.Series:
        Convert integer quantities of a given time unit into a different unit.

Examples
--------
For converting fractional values (`float`, `complex`, `decimal.Decimal`):

>>> convert_unit_float(1.0, "s", "ns")
1000000000
>>> convert_unit_float(0.123, "s", "ns")
123000000
>>> convert_unit_float(10.3864, "D", "s")
897384
>>> convert_unit_float(1.0, "Y", "M")
12
>>> convert_unit_float(42.3, "M", "Y")
3
>>> convert_unit_float(1.0, "Y", "D")
365
>>> convert_unit_float(1.0, "Y", "D", since=pd.Timestamp("2000-01-01"))
366
>>> convert_unit_float(32.4928, "M", "s")
85344537
>>> convert_unit_float(365.0, "D", "Y")
1
>>> convert_unit_float(365.0, "D", "Y", since=pd.Timestamp("2000-01-01"))
0
>>> convert_unit_float(0.12345678910, "s", "ns")
123456789
>>> convert_unit_float(1.11111111, "Y", "ns")
35039999964959996
>>> convert_unit_float(0.12345678910, "s", "ns", rounding=None)
123456789.10000001
>>> convert_unit_float(365.0, "D", "Y", rounding=None, since=pd.Timestamp("2000-01-01"))
0.9972677595628415

And for integers:

>>> convert_unit_integer(1, "s", "ns")
1000000000
>>> convert_unit_integer(123, "s", "ns")
123000000000
>>> convert_unit_integer(10, "D", "s")
864000
>>> convert_unit_integer(1, "Y", "M")
12
>>> convert_unit_integer(42, "M", "Y")
3
>>> convert_unit_integer(1, "Y", "D")
365
>>> convert_unit_integer(1, "Y", "D", since=pd.Timestamp("2000-01-01"))
366
>>> convert_unit_integer(32, "M", "s")
84067200
>>> convert_unit_integer(365, "D", "Y")
1
>>> convert_unit_integer(365, "D", "Y", since=pd.Timestamp("2000-01-01"))
0
>>> convert_unit_integer(1, "s", "m", rounding=None)
0.016666666666666666
>>> convert_unit_integer(365, "D", "Y", rounding=None, since=pd.Timestamp("2000-01-01"))
0.9972677595628415
"""
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


# TODO: ensure unit length definitions are correct for all values/offsets


#########################
####    Constants    ####
#########################


# conversion coefficients for each of the regular units (not 'M', 'Y')
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


# a list of all units recognized by this module
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
    """An optimized fastpath for `convert_unit_float()` that assumes fractional
    year input and integer nanosecond output.  Only accepts scalars.

    This task is required in the main loop of all string to timedelta
    conversions wherever they contain references to years.  By importing and
    using this function over the more full-featured `convert_unit_float()`,
    each conversion can avoid a significant number of potential branch checks
    and vectorization loops.

        .. note:: always rounds down (toward zero).
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

    # establish length of final unit in days.  Note: if start date occurs on or
    # after a potential leap day, use next year's calendar.  If residual years
    # are negative, use last year's instead.
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
    """An optimized fastpath for `convert_unit_float()` that assumes fractional
    month input and integer nanosecond output.  Only accepts scalars.

    This task is required in the main loop of all string to timedelta
    conversions wherever they contain references to months.  By importing and
    using this function over the more full-featured `convert_unit_float()`,
    each conversion can avoid a significant number of potential branch checks
    and vectorization loops.

        .. note:: always rounds down (toward zero).
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

    # establish length of final unit in days.  Note: if residual months are
    # negative, use the previous month.
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
    """Cast each element in a numeric array to python integers, using the
    specified rounding rule.
    """
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
    """Cast arbitrary numerics and numerical vectors into python integers,
    according to the specified rounding rule.
    """
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
    """Helper to convert fractional numbers of irregular units ('M', 'Y') into
    regular units ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W').
    """
    # handle integer and fractional components separately
    rounded = cast_to_int(arg, rounding="down")
    arg = arg - rounded

    # month -> day
    if from_unit == "M":  # month -> day
        # convert integer component into day offset from utc
        rounded = date_to_days(
            since["year"],
            since["month"] + rounded,
            since["day"]
        )

        # get ending date
        end = days_to_date(rounded)

        # establish length of final month.  Note: if residual months are
        # negative, use the previous month.
        unit_length = days_in_month(end["month"] - (arg < 0), end["year"])

    # year -> day
    else:
        # convert integer component into day offset from utc
        rounded = date_to_days(
            since["year"] + rounded,
            since["month"],
            since["day"]
        )

        # get ending date
        end = days_to_date(rounded)

        # establish length of final year.  Note: if `since` occurs on or after
        # a potential leap day, use next year's calendar.  If residual years
        # are negative, use last year's instead.
        skip = (date_to_days(**since) - date_to_days(since["year"], 1, 1)) > 58
        unit_length = 365 + is_leap_year(end["year"] + skip - (arg < 0))

    # subtract off `since` day offset
    if since != {"year": 1970, "month": 1, "day": 1}:
        rounded -= date_to_days(**since)

    # scale fraction by unit length (in days) and reintroduce integer component
    arg *= unit_length
    arg += rounded

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
    """Helper to convert fractional numbers of regular units ('ns', 'us', 'ms',
    's', 'm', 'h', 'D', 'W') into irregular units ('M', 'Y').
    """
    # convert arg to days
    arg = arg * as_ns[from_unit] / as_ns["D"]
    
    # handle integer and fractional components separately
    rounded = cast_to_int(arg, rounding="down")
    arg -= rounded

    # add `since` offset, if applicable
    if since != {"year": 1970, "month": 1, "day": 1}:
        rounded += date_to_days(**since)

    # get ending date using `days_to_date`
    end = days_to_date(rounded)

    # day -> year
    if to_unit == "Y":
        # change origin to nearest multiple of `since`
        rounded -= date_to_days(end["year"], since["month"], since["day"])

        # reintroduce fractional days
        arg += rounded

        # divide by length of final year to get fractional years
        arg /= 365 + is_leap_year(end["year"])

        # reintroduce whole years to get final answer
        arg += end["year"] - since["year"]

    # day -> month
    else:
        # change origin to nearest multiple of `since`
        rounded -= date_to_days(end["year"], end["month"], since["day"])

        # reintroduce fractional days
        arg += rounded

        # divide by length of final month to get fractional months
        arg /= days_in_month(end["month"], end["year"])

        # reintroduce whole months to get final answer
        arg += 12 * (end["year"] - since["year"])
        arg += end["month"] - since["month"]

    # round to integer, if applicable
    return arg if rounding is None else cast_to_int(arg, rounding=rounding)


def _convert_unit_integer_irregular_to_regular(
    arg: int | np.ndarray | pd.Series,
    from_unit: str,
    to_unit: str,
    rounding: str | None,
    since: dict
) -> int | float | np.ndarray | pd.Series:
    """Helper to convert integer numbers of irregular units ('M', 'Y') to
    regular units ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W').
    """
    # month -> day
    if from_unit == "M":
        # convert to days since utc
        arg = date_to_days(
            since["year"],
            since["month"] + arg,
            since["day"]
        )
    else:  # year -> day
        # convert to days since utc
        arg = date_to_days(
            since["year"] + arg,
            since["month"],
            since["day"]
        )

    # subtract off `since` offset, if applicable
    if since != {"year": 1970, "month": 1, "day": 1}:
        arg -= date_to_days(**since)

    # day -> day
    if to_unit == "D":
        if rounding is None:
            arg *= 1.0  # convert to float
        return arg

    # day -> week
    if to_unit == "W":
        if rounding is None:
            arg /= 7  # float division
        else:
            arg = round_div(arg, 7, rule=rounding, copy=False)
        return arg

    # day -> ns, us, ms, s, m, h
    arg *= as_ns["D"]
    if rounding is None:
        arg /= as_ns[to_unit]  # float division
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
    """Helper to convert integer numbers of regular units ('ns', 'us', 'ms',
    's', 'm', 'h', 'D', 'W') to irregular units ('M', 'Y').
    """
    # convert to nanoseconds
    arg = arg * as_ns[from_unit]

    # get integer number of days along with residual nanoseconds
    rounded = round_div(arg, as_ns["D"], rule="down")
    residual = arg - rounded * as_ns["D"]

    # add `since` offset, if applicable
    if since != {"year": 1970, "month": 1, "day": 1}:
        rounded += date_to_days(**since)

    # get ending date
    end = days_to_date(rounded)

    # day -> year
    if to_unit == "Y":
        # get whole number of years
        result = end["year"] - since["year"]

        # move origin to nearest multiple of `since`
        rounded -= date_to_days(end["year"], since["month"], since["day"])

        # establish length of final year.  Note: if `since` occurs on or after
        # a potential leap day, use next year's calendar.
        skip = (date_to_days(**since) - date_to_days(since["year"], 1, 1)) > 58
        unit_length = 365 + is_leap_year(end["year"] + skip)

    # day -> month
    else:
        # get whole number of months
        result = 12 * (end["year"] - since["year"])
        result += end["month"] - since["month"]

        # move origin to nearest multiple of `since`
        rounded -= date_to_days(end["year"], end["month"], since["day"])

        # establish length of final month
        unit_length = days_in_month(end["month"], end["year"])

    # convert integer component, unit length back to nanoseconds
    unit_length *= as_ns["D"]
    rounded *= as_ns["D"]

    # reintroduce residual nanoseconds
    rounded += residual

    # `rounded` now holds the time separation (in ns) of `result` from the
    # nearest occurence of `since`, bounded to within 1 unit.  Negative
    # values indicate that `end` occurs before `since`, meaning `result`
    # is an overestimate.  Positive values indicate the opposite.  This
    # allows us to apply customizable rounding while retaining full integer
    # accuracy.

    # return exact (do not round)
    if rounding is None:
        return result + rounded / unit_length  # float division

    # round toward/away from zero
    if rounding == "floor":
        return result - (rounded < 0)
    if rounding == "ceiling":
        return result + (rounded > 0)
    if rounding == "down":
        result -= ((arg > 0) & (rounded < 0))  # floor where arg > 0
        result += ((arg < 0) & (rounded > 0))  # ceiling where arg < 0
        return result
    if rounding == "up":
        result -= ((arg < 0) & (rounded < 0))  # floor where arg < 0
        result += ((arg > 0) & (rounded > 0))  # ceiling where arg > 0
        return result

    # corrections for round to nearest
    if to_unit == "M":
        if rounding == "half_down":
            rounded += (arg < 0)
        elif rounding == "half_up":
            rounded -= (arg < 0)

    # round half even has to be handled separately
    if rounding == "half_even":
        result += np.where(
            result % 2,
            round_div(rounded, unit_length, rule="half_ceiling"),
            round_div(rounded, unit_length, rule="half_floor")
        )
        return result[()] if not result.shape else result

    # round to nearest
    result += round_div(rounded, unit_length, rule=rounding, copy=False)
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
    """Convert fractional quantities of a given time unit into a different
    unit.

    This function performs the same operation as
    :func:`convert_unit_integer()`, except that it is designed for fractional
    unit representations, like `float`, `complex`, and `decimal.Decimal`.  This
    allows easy conversion of such objects into nanosecond offsets for the
    various :func:`ns_to_datetime()` and :func:`ns_to_timedelta()` conversion
    functions, for example, while keeping the number of significant digits as
    high as possible.

    Parameters
    ----------
    arg : float | complex | decimal.Decimal | array-like
        The quantity to convert.  Can be vectorized.
    from_unit : {'ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y'}
        The unit to convert from.
    to_unit : {'ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y'}
        The unit to convert to.
    rounding : {'floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
    'half_down', 'half_up', 'half_even', None}
        The rounding strategy to use if performing integer conversion.  If this
        is set to a value other than `None`, then the return type of this
        function will be converted to int.  If this is set to `None`, it will
        return the exact result, in the same type as `arg`.
    since : datetime_like, default pd.Timestamp("2001-01-01 00:00:00+0000")
        The date from which to begin counting.  This is only used when
        converting to or from units 'M' and 'Y', in order to accurately account
        for leap days and unequal month lengths.  Only the `year`, `month`, and
        `day` components are used.  Defaults to '2001-01-01 00:00:00+0000',
        which represents the start of a 400-year Gregorian calendar cycle.

    Returns
    -------
    int | float | complex | decimal.Decimal | array-like
        The result of the unit conversion.  If `rounding=None`, this will be
        the same type as the input (`float`, `complex`, `decimal.Decimal`).
        Otherwise, it will be cast to integer, with the specified rounding
        strategy.

    Raises
    ------
    ValueError
        If either `from_unit` or `to_unit` is not one of the recognized units
        ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y').

    See Also
    --------
    convert_unit_integer : convert integer numbers of units.
    ns_to_datetime : convert nanosecond UTC offsets into datetimes.
    ns_to_timedelta : convert nanosecond UTC offsets into timedeltas.

    Examples
    --------
    Units can be regular ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W'):

    >>> convert_unit_float(1.0, "s", "ns")
    1000000000
    >>> convert_unit_float(0.123, "s", "ns")
    123000000
    >>> convert_unit_float(10.3864, "D", "s")
    897384

    Or irregular ('M', 'Y'):

    >>> convert_unit_float(1.0, "Y", "M")
    12
    >>> convert_unit_float(42.3, "M", "Y")
    3

    With conversion between the two:

    >>> convert_unit_float(1.0, "Y", "D")
    365
    >>> convert_unit_float(1.0, "Y", "D", since=pd.Timestamp("2000-01-01"))
    366
    >>> convert_unit_float(32.4928, "M", "s")
    85344537
    >>> convert_unit_float(365.0, "D", "Y")
    1
    >>> convert_unit_float(365.0, "D", "Y", since=pd.Timestamp("2000-01-01"))
    0

    Units can have precision down to nanoseconds:

    >>> convert_unit_float(0.12345678910, "s", "ns")
    123456789
    >>> convert_unit_float(1.11111111, "Y", "ns")
    35039999964959996

    And can be returned as exact values, without rounding:

    >>> convert_unit_float(0.12345678910, "s", "ns", rounding=None)
    123456789.10000001
    >>> convert_unit_float(365.0, "D", "Y", rounding=None, since=pd.Timestamp("2000-01-01"))
    0.9972677595628415
    """
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
    """Convert integer quantities of a given time unit into a different unit.

    This function performs the same operation as :func:`convert_unit_float()`,
    except that it is designed for integer unit representations and avoids
    conversion to imprecise floating-point formats.

    Parameters
    ----------
    arg : int | array-like
        The quantity to convert.  Can be vectorized.
    from_unit : {'ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y'}
        The unit to convert from.
    to_unit : {'ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y'}
        The unit to convert to.
    rounding : {'floor', 'ceiling', 'down', 'up', 'half_floor', 'half_ceiling',
    'half_down', 'half_up', 'half_even', None}
        The rounding strategy to use in the case of residual units.  If this
        is set to `None`, then the result will be coerced to float and the
        residual appended as a decimal component.  Otherwise, it will stay in
        a pure-integer representation, and the specified rounding rule will be
        applied on that basis.
    since : datetime_like, default pd.Timestamp("2001-01-01 00:00:00+0000")
        The date from which to begin counting.  This is only used when
        converting to or from units 'M' and 'Y', in order to accurately account
        for leap days and unequal month lengths.  Only the `year`, `month`, and
        `day` components are used.  Defaults to '2001-01-01 00:00:00+0000',
        which represents the start of a 400-year Gregorian calendar cycle.

    Returns
    -------
    int | float | array-like
        The result of the unit conversion.  If `rounding=None`, this will
        be coerced to float, with residuals as decimal components.  Otherwise,
        it will stay in integer format and apply the given rounding rule.

    Raises
    ------
    ValueError
        If either `from_unit` or `to_unit` is not one of the recognized
        units ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W', 'M', 'Y').

    See Also
    --------
    convert_unit_float : convert fractional numbers of units.
    ns_to_datetime : convert nanosecond UTC offsets into datetimes.
    ns_to_timedelta : convert nanosecond UTC offsets into timedeltas.

    Examples
    --------
    Units can be regular ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W'):

    >>> convert_unit_integer(1, "s", "ns")
    1000000000
    >>> convert_unit_integer(123, "s", "ns")
    123000000000
    >>> convert_unit_integer(10, "D", "s")
    864000

    Or irregular ('M', 'Y'):

    >>> convert_unit_integer(1, "Y", "M")
    12
    >>> convert_unit_integer(42, "M", "Y")
    3

    With conversion between the two:

    >>> convert_unit_integer(1, "Y", "D")
    365
    >>> convert_unit_integer(1, "Y", "D", since=pd.Timestamp("2000-01-01"))
    366
    >>> convert_unit_integer(32, "M", "s")
    84067200
    >>> convert_unit_integer(365, "D", "Y")
    1
    >>> convert_unit_integer(365, "D", "Y", since=pd.Timestamp("2000-01-01"))
    0

    Optionally, results can be returned as floats, exposing the residuals:

    >>> convert_unit_integer(1, "s", "m", rounding=None)
    0.016666666666666666
    >>> convert_unit_integer(365, "D", "Y", rounding=None, since=pd.Timestamp("2000-01-01"))
    0.9972677595628415
    """
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
