"""Implements 2 functions, `is_leap_year()` and `leaps_between()`, which help
facilitate vectorized leap year calculations according to the proleptic
Gregorian calendar.

`is_leap_year()` accepts an integer scalar, array, or Series representing
Gregorian calendar years, returning `True` where the input corresponds to a
leap year and `False` where it does not.

`leaps_between()` accepts a bounded range of integer-based Gregorian calendar
years and returns the number of leap days that occur between them.  Both bounds
can be independently vectorized.
"""

"""Implements 3 functions, `decompose_date()`, `date_to_days()`, and
`days_to_date()`, which allow accurate, vectorized math using the Gregorian
calendar.

`decompose_date()` accepts a date-like scalar, array, or Series and returns
the corresponding Gregorian calendar date(s) as integer year, month, and day
components.  The output can be passed directly to `date_to_days()` as a
**kwargs dict.

`date_to_days()` accepts a calendar date as integer year, month, and day
components (which can be independently vectorized) and converts them into an
integer count of days from a customizeable epoch.  This allows for easy
date-based math, including the conversion of months and years to unambiguous
day equivalents, which accurately account for leap years and unequal month
lengths.

`days_to_date()` accepts a day offset from a given epoch into the corresponding
Gregorian calendar date, as integer year, month, and day components.  Performs
the inverse of `date_to_days()`.
"""
cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import get_dtype
from pdtypes.util.type_hints import date_like


#########################
####    Constants    ####
#########################


# Gregorian cycle lengths
cdef unsigned int days_per_400_years = 146097
cdef unsigned short days_per_100_years = 36524
cdef unsigned short days_per_4_years = 1461
cdef unsigned short days_per_year = 365


# raw days per month, starting from January 1st
cdef np.ndarray days_per_month = np.array(
    [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31],
    dtype=np.uint8
)


#######################
####    Private    ####
#######################


@cython.boundscheck(False)
@cython.wraparound(False)
cdef dict decompose_datelike_objects(
    np.ndarray[object] arr
):
    """Internal C interface for public-facing decompose_date() function when
    used on date-like object arrays.
    """
    cdef int arr_length = arr.shape[0]
    cdef int i
    cdef object element
    cdef np.ndarray[object] year = np.empty(arr_length, dtype="O")
    cdef np.ndarray[object] month = np.empty(arr_length, dtype="O")
    cdef np.ndarray[object] day = np.empty(arr_length, dtype="O")

    for i in range(arr_length):
        element = arr[i]
        year[i] = element.year
        month[i] = element.month
        day[i] = element.day

    return {"year": year, "month": month, "day": day}


######################
####    Public    ####
######################


def date_to_days(
    year: int | np.ndarray | pd.Series,
    month: int | np.ndarray | pd.Series,
    day: int | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """Convert a (proleptic) Gregorian calendar date `(year, month, day)` into
    a day offset from the utc epoch (1970-01-01).

    Note: for the sake of efficiency, this function will not attempt to coerce
    numpy integers or integer arrays into their built-in python equivalents.
    As such, they may silently overflow (and wrap around the number line) if
    64-bit limits are exceeded during conversion.  This shouldn't be a problem
    in practice; even with day-level precision, the valid 64-bit range vastly
    exceeds the observed age of the universe.  Nevertheless, this can be
    explicitly avoided by converting the inputs into python integers (which do
    not overflow) beforehand.

    See also:
        http://www.algonomicon.org/calendar/gregorian/to_jdn.html
        https://stackoverflow.com/questions/11609769/efficient-algorithm-for-converting-number-of-days-to-years-including-leap-years

    Parameters
    ----------
    year (int | np.ndarray | pd.Series):
        Proleptic Gregorian calendar year.  This function assumes the existence
        of a year 0, which does not correspond to real-world historical dates.
        In order to convert a historical BC year (`-1 BC`, `-2 BC`, ...) to a
        negative year (`0`, `-1`, ...), simply add one to the BC year.  AD
        years are unaffected.
    month (int | np.ndarray | pd.Series):
        Proleptic Gregorian calendar month, indexed from 1 (January).  If a
        month value exceeds the range [1, 12], then any excess is automatically
        carried over into `year`.
    day (int | np.ndarray | pd.Series):
        Proleptic Gregorian calendar day, indexed from 1.  If a day value
        exceeds the maximum for the selected month, any excess is automatically
        carried over into `month` and `year`.

    Returns
    -------
    int | np.ndarray | pd.Series:
        The integer day offset from utc.
    """
    # normalize months to start with March 1st, indexed from 1 (January)
    month = month - 3
    year = year + month // 12
    day = day - 1
    month %= 12

    # build result
    result = 365 * year + year // 4 - year // 100 + year // 400
    result += (153 * month + 2) // 5
    result += day
    result -= 719468  # move origin from March 1st, year 0 to utc
    return result


def days_in_month(
    month: int | np.ndarray | pd.Series,
    year: int | np.ndarray | pd.Series = 2001
) -> int | np.ndarray | pd.Series:
    """Get the length (in days) of a given month, taking leap years into
    account.
    """
    if hasattr(month, "astype"):
        month = month.astype(np.int8, copy=False)
    return days_per_month[month - 1] + ((month == 2) & is_leap_year(year))


def days_to_date(
    days: int | np.ndarray | pd.Series
) -> dict[str, int] | dict[str, np.ndarray] | dict[str, pd.Series]:
    """Convert a day offset from the utc epoch (1970-01-01) into the
    corresponding (proleptic) Gregorian calendar date `(year, month, day)`.

    Note: for the sake of efficiency, this function will not attempt to coerce
    numpy integers or integer arrays into their built-in python equivalents.
    As such, they may silently overflow (and wrap around the number line) if
    64-bit limits are exceeded during conversion.  This shouldn't be a problem
    in practice; even with day-level precision, the valid 64-bit range vastly
    exceeds the observed age of the universe.  Nevertheless, this can be
    explicitly avoided by converting the inputs into python integers (which do
    not overflow) beforehand.

    See also:
        http://www.algonomicon.org/calendar/gregorian/from_jdn.html
        https://stackoverflow.com/questions/11609769/efficient-algorithm-for-converting-number-of-days-to-years-including-leap-years

    Parameters
    ----------
    days (int | np.ndarray | pd.Series):
        Integer day offset from utc.  

    Returns
    -------
    dict[str, int] | dict[str, np.ndarray] | dict[str, pd.Series]:
        A dictionary with the following keys/values:
            - `'year'`:
                Proleptic Gregorian calendar year.  This function assumes the
                existence of a year 0, which does not correspond to real-world
                historical dates.  In order to convert a historical BC year
                (`-1 BC`, `-2 BC`, ...) to a negative year (`0`, `-1`, ...),
                simply add one to the BC year.  AD years are unaffected.
            - `'month'`:
                Proleptic Gregorian calendar month, indexed from 1 (January).
            - `'day'`:
                Proleptic Gregorian calendar day, indexed from 1.
    """
    # move origin from utc to March 1st, 2000 (start of 400-year cycle)
    days = days - 11017

    # count years since 2000, correcting for leaps in each Gregorian cycle
    years = days - (days + 1) // days_per_400_years  # 400-year cycles
    years += years // days_per_100_years  # 100-year cycles
    years -= (years + 1) // days_per_4_years  # 4-year cycles
    years //= days_per_year  # whole years

    # compute residuals
    days -= days_per_year * years + years // 4 - years // 100 + years // 400
    months = (5 * days + 2) // 153
    days -= (153 * months + 2) // 5  # residual days in month

    # convert to proper calendar year
    years += 2000 + (months >= 10)  # treat Jan, Feb as belonging to next year

    # undo bias toward march 1st
    months += 2
    months %= 12
    months += 1

    # index from 1
    days += 1

    # return as dict
    return {"year": years, "month": months, "day": days}


def decompose_date(
    date: date_like | np.ndarray | pd.Series
) -> dict[str, int] | dict[str, np.ndarray] | dict[str, pd.Series]:
    """Split datelike objects into their constituent parts (years, months,
    days).

    Parameters
    ----------
    date (date-like | np.ndarray | pd.Series):
        A date-like object or an array/Series of date-like objects.  Accepts
        duck-types as long as they implement the `.year`, `.month`, and `.day`
        attributes.

    Returns
    -------
    dict[str, int] | dict[str, np.ndarray] | dict[str, pd.Series]:
        A dictionary with the following keys/values:
            - `'year'`: Proleptic Gregorian calendar year.
            - `'month'`: Proleptic Gregorian calendar month.
            - `'day'`: Proleptic Gregorian calendar day.

    Raises
    ------
    TypeError:
        If `date` consists of mixed element types.
    AttributeError:
        If `date` is duck-typed and does not implement the `.year`, `.month`,
        and `.day` attributes.
    """
    dtype = get_dtype(date)
    if isinstance(dtype, set):
        raise TypeError(f"`date` must have homogenous, date-like element "
                        f"types, not {dtype}")

    # pd.Timestamp
    if dtype == pd.Timestamp:
        # np.array
        if isinstance(date, np.ndarray):
            return decompose_datelike_objects(date)

        # pd.Series
        if isinstance(date, pd.Series):
            # M8 dtype
            if pd.api.types.is_datetime64_ns_dtype(date):
                return {
                    "year": date.dt.year,
                    "month": date.dt.month,
                    "day": date.dt.day
                }

            # object dtype
            index = date.index
            result = decompose_datelike_objects(date.to_numpy())
            return {k: pd.Series(v, index=index, copy=False)
                    for k, v in result.items()}

        # scalar
        return {"year": date.year, "month": date.month, "day": date.day}

    # np.datetime64
    if dtype == np.datetime64:
        # use numpy implementation for series input
        is_series = isinstance(date, pd.Series)
        if is_series:
            date = date.to_numpy()

        # extract year, month, day
        Y, M, D = [date.astype(f"M8[{x}]") for x in "YMD"]
        year = Y.astype(np.int64).astype("O") + 1970
        month = (M - Y).astype(np.int64).astype("O") + 1
        day = (D - M).astype(np.int64).astype("O") + 1

        # return
        if is_series:  # copy index and convert back to series
            index = date.index
            return {
                "year": pd.Series(year, index=index, copy=False),
                "month": pd.Series(month, index=index, copy=False),
                "day": pd.Series(day, index=index, copy=False)
            }
        return {"year": year, "month": month, "day": day}

    # datetime.datetime-like (duck-type)
    if isinstance(date, np.ndarray):
        return decompose_datelike_objects(date)
    if isinstance(date, pd.Series):
        return decompose_datelike_objects(date.to_numpy())
    return {"year": date.year, "month": date.month, "day": date.day}


def is_leap_year(
    year: int | np.ndarray | pd.Series
) -> bool | np.ndarray | pd.Series:
    """Returns True if the given year is a leap year according to the proleptic
    Gregorian calendar.
    """
    return (year % 4 == 0) & ((year % 100 != 0) | (year % 400 == 0))


def leaps_between(
    lower: int | np.ndarray | pd.Series,
    upper: int | np.ndarray | pd.Series
) -> int | np.ndarray | pd.Series:
    """Return the number of leap days between the years `lower` and `upper`.

    This function counts from the beginning of each year.  This means that
    `leaps_between(x, x + 1)` will return 1 if and only if `x` was a leap year.

    Identical to `calendar.leapdays()` from the built-in `calendar` package,
    but avoids an import and is compiled (and thus slightly faster).
    """
    count = lambda x: x // 4 - x // 100 + x // 400
    return count(upper - 1) - count(lower - 1)
