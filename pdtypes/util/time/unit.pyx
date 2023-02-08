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
import datetime
import decimal

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd
import pytz

from pdtypes.type_hints import array_like, datetime_like, numeric

from ..round import round_div
from ..round import round_float as round_generic  # TODO: unpatch this

from .calendar import (
    date_to_days, days_in_month, days_to_date, is_leap_year
)

cimport pdtypes.util.time.epoch as epoch
import pdtypes.util.time.epoch as epoch


# TODO: ensure unit length definitions are correct for all values/offsets


#########################
####    CONSTANTS    ####
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
valid_units_public = valid_units  # python-facing alias for valid_units


######################
####    PUBLIC    ####
######################


def convert_unit(
    val: numeric | array_like,
    from_unit: str,
    to_unit: str,
    rounding: str | None = "down",
    since: epoch.Epoch = None
) -> numeric | array_like:
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
        raise ValueError(
            f"`from_unit` must be one of {valid_units}, not {repr(from_unit)}"
        )
    if to_unit not in valid_units:
        raise ValueError(
            f"`to_unit` must be one of {valid_units}, not {repr(to_unit)}"
        )
    if since is None:
        since = epoch.Epoch("utc")

    # trivial case - no conversion necessary
    if from_unit == to_unit:
        return val

    # regular -> regular ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W')
    if from_unit in as_ns and to_unit in as_ns:
        if rounding is None:
            return val * as_ns[from_unit] / as_ns[to_unit]
        return round_div(
            val * as_ns[from_unit],
            as_ns[to_unit],
            rule=rounding
        )

    # irregular -> irregular ('M', 'Y')
    if from_unit not in as_ns and to_unit not in as_ns:
        # month -> year
        if from_unit == "M" and to_unit == "Y":
            if rounding is None:
                return val / 12
            return round_div(val, 12, rule=rounding)

        # year -> month
        return 12 * val

    # regular -> irregular
    if from_unit in as_ns:
        return _convert_unit_integer_regular_to_irregular(
            val,
            from_unit=from_unit,
            to_unit=to_unit,
            rounding=rounding,
            since=since
        )

    # irregular -> regular
    return _convert_unit_integer_irregular_to_regular(
        val,
        from_unit=from_unit,
        to_unit=to_unit,
        rounding=rounding,
        since=since
    )


#######################
####    PRIVATE    ####
#######################


cdef object round_years_to_ns(object years, epoch.Epoch since):
    """An optimized fastpath for `convert_unit_float()` that assumes fractional
    year input and integer nanosecond output.  Only accepts scalars.

    This task is required in the main loop of all string to timedelta
    conversions wherever they contain references to years.  By importing and
    using this function over the more full-featured `convert_unit_float()`,
    each conversion can avoid a significant number of potential branch checks
    and vectorization loops.

        .. note:: always rounds down (toward zero).
    """
    cdef object diff
    cdef object residual
    cdef dict end
    cdef short unit_length

    # handle integer and fractional components separately
    diff = int(years)
    residual = years - diff

    # convert integer component to utc day offset and establish ending date
    diff = date_to_days(since.year + diff, since.month, since.day)
    end = days_to_date(diff)

    # establish length of final unit in days.  NOTE: if start date occurs on or
    # after a potential leap day, use next year's calendar.  If residual years
    # are negative, use last year's instead.
    unit_length = 365 + is_leap_year(
        end["year"] + (since.ordinal > 58) - (residual < 0)
    )

    # subtract off epoch to get total elapsed days
    if epoch:
        diff -= epoch.offset // as_ns["D"]

    # scale fractional component by unit length and reintroduce integer
    return diff * as_ns["D"] + int(residual * unit_length * as_ns["D"])


cdef object round_months_to_ns(object months, epoch.Epoch since):
    """An optimized fastpath for `convert_unit_float()` that assumes fractional
    month input and integer nanosecond output.  Only accepts scalars.

    This task is required in the main loop of all string to timedelta
    conversions wherever they contain references to months.  By importing and
    using this function over the more full-featured `convert_unit_float()`,
    each conversion can avoid a significant number of potential branch checks
    and vectorization loops.

        .. note:: always rounds down (toward zero).
    """
    cdef object diff
    cdef object residual
    cdef dict end
    cdef char unit_length

    # handle integer and fractional components separately
    diff = int(months)
    residual = months - diff

    # convert integer component to utc day offset and establish ending date
    diff = date_to_days(since.year, since.month + diff, since.day)
    end = days_to_date(diff)

    # establish length of final unit in days.  NOTE: if residual months are
    # negative, use the previous month.
    unit_length = days_in_month(end["month"] - (residual < 0), end["year"])

    # subtract off start date to get total elapsed days
    if since:
        diff -= since.offset // as_ns["D"]

    # scale fractional component by unit length and reintroduce integer
    return diff * as_ns["D"] + int(residual * unit_length * as_ns["D"])


def _convert_unit_integer_irregular_to_regular(
    val: numeric | array_like,
    from_unit: str,
    to_unit: str,
    rounding: str,
    since: epoch.Epoch
) -> numeric | array_like:
    """Helper to convert integer numbers of irregular units ('M', 'Y') to
    regular units ('ns', 'us', 'ms', 's', 'm', 'h', 'D', 'W').
    """
    # month -> day
    if from_unit == "M":
        val = date_to_days(
            since.year,
            since.month + val,
            since.day
        )

    # year -> day
    else:
        val = date_to_days(
            since.year + val,
            since.month,
            since.day
        )

    # move origin to epoch
    if since:
        val -= since.offset // as_ns["D"]

    # day -> day
    if to_unit == "D":
        return val

    # day -> week
    if to_unit == "W":
        if rounding is None:
            return val / 7
        return round_div(val, 7, rule=rounding)

    # day -> ns, us, ms, s, m, h
    val *= as_ns["D"]
    if rounding is None:
        return val / as_ns[to_unit]
    return round_div(val, as_ns[to_unit], rule=rounding)


def _convert_unit_integer_regular_to_irregular(
    val: numeric | array_like,
    from_unit: str,
    to_unit: str,
    rounding: str,
    since: epoch.Epoch
) -> numeric | array_like:
    """Helper to convert integer numbers of regular units ('ns', 'us', 'ms',
    's', 'm', 'h', 'D', 'W') to irregular units ('M', 'Y').
    """
    # TODO: since -> start

    # convert val to whole days + residual ns
    val = val * as_ns[from_unit]
    diff = round_div(val, as_ns["D"], rule="down")
    residual = val - diff * as_ns["D"]

    # convert whole days into final end date
    if since:
        diff += since.offset // as_ns["D"]  # move origin to epoch
    end = days_to_date(diff)

    # build result, starting with whole number of years
    result = end["year"] - since.year

    # day -> year
    if to_unit == "Y":
        # move origin to nearest multiple of `since`
        diff -= date_to_days(end["year"], since.month, since.day)

        # get length of final unit.  NOTE: if `since` occurs on or after a
        # leap day, use next year's calendar.
        unit_length = 365 + is_leap_year(end["year"] + (since.ordinal > 58))

    # day -> month
    else:
        # get whole number of months
        result *= 12
        result += end["month"] - since.month

        # move origin to nearest multiple of `since`
        diff -= date_to_days(end["year"], end["month"], since.day)

        # establish length of final month
        unit_length = days_in_month(end["month"], end["year"])

    # convert back to nanoseconds and reintroduce residuals
    unit_length *= as_ns["D"]
    diff *= as_ns["D"]
    diff += residual

    # `diff` now holds the time separation (in ns) of `result` from the
    # nearest occurence of `since`, bounded to within 1 unit.  Negative
    # values indicate that `end` occurs before `since`, meaning `result`
    # is an overestimate.  Positive values indicate the opposite.  This
    # allows us to apply customizable rounding while retaining full integer
    # accuracy.

    # return exact (do not round)
    if rounding is None:
        return result + diff / unit_length  # float division

    # round toward/away from zero
    if rounding == "floor":
        return result - (diff < 0)
    if rounding == "ceiling":
        return result + (diff > 0)
    if rounding == "down":
        result -= ((val > 0) & (diff < 0))  # floor where val > 0
        result += ((val < 0) & (diff > 0))  # ceiling where val < 0
        return result
    if rounding == "up":
        result -= ((val < 0) & (diff < 0))  # floor where val < 0
        result += ((val > 0) & (diff > 0))  # ceiling where val > 0
        return result

    # corrections for round to nearest
    if to_unit == "M":
        if rounding == "half_down":
            diff += (val < 0)
        elif rounding == "half_up":
            diff -= (val < 0)

    # round half even has to be handled separately
    if rounding == "half_even":
        result += np.where(
            result % 2,
            round_div(diff, unit_length, rule="half_ceiling"),
            round_div(diff, unit_length, rule="half_floor")
        )
        return result[()] if not result.shape else result

    # round to nearest
    return result + round_div(diff, unit_length, rule=rounding)