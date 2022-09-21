"""Gregorian calendar utility functions.

This module allows users to do efficient math around the Gregorian calendar.
It contains functionality to convert calendar dates both to and from day
offsets from the UTC epoch ('1971-01-01 00:00:00+0000'), as well as track the
passage of leap years and obtain the length of each month within a year.

Functions
---------
    date_to_days(
        year: int | np.ndarray | pd.Series,
        month: int | np.ndarray | pd.Series,
        day: int | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Convert proleptic Gregorian calendar dates into day offsets from the
        utc epoch.

    days_in_month(
        month: int | np.ndarray | pd.Series,
        year: int | np.ndarray | pd.Series = 2001
    ) -> int | np.ndarray | pd.Series:
        Get the length in days of a given month, taking leap years into
        account.

    days_to_date(
        days: int | np.ndarray | pd.Series
    ) -> dict[str, int | np.ndarray | pd.Series]:
        Convert day offsets from the utc epoch ('1970-01-01 00:00:00+0000')
        into proleptic Gregorian calendar dates.

    decompose_date(
        date: date_like | np.ndarray | pd.Series
    ) -> dict[str, int | np.ndarray | pd.Series]:
        Split datelike objects into their constituent parts (`years`, `months`,
        `days`).

    is_leap_year(
        year: int | np.ndarray | pd.Series
    ) -> bool | np.ndarray | pd.Series:
        Check if the given year is a leap year according to the proleptic
        Gregorian calendar.

    leaps_between(
        lower: int | np.ndarray | pd.Series,
        upper: int | np.ndarray | pd.Series
    ) -> int | np.ndarray | pd.Series:
        Return the number of leap days between the years `lower` and `upper`.

Examples
--------
>>> date_to_days(1970, 1, 1)
>>> date_to_days(1970, 1, 2)
>>> date_to_days(1970, 1, 0)
>>> date_to_days(1970, 1, -1)
>>> date_to_days(1970, 2, 1)
>>> date_to_days(1970, 3, 1)
>>> date_to_days(1970, 0, 1)
>>> date_to_days(1970, -1, 1)
>>> date_to_days(1971, 1, 1)
>>> date_to_days(1972, 1, 1)
>>> date_to_days(1969, 1, 1)
>>> date_to_days(1968, 1, 1)
>>> date_to_days(1970, 1, np.arange(-1, 3))
>>> date_to_days(1970, np.arange(-1, 3), 1)
>>> date_to_days(np.arange(1968, 1973), 1, 1)

>>> days_in_month(1, 2001)
>>> days_in_month(2, 2001)
>>> days_in_month(2, 2000)
>>> days_in_month(np.arange(-2, 3), 2001)

>>> days_to_date(0)
>>> days_to_date(1)
>>> days_to_date(-1)
>>> days_to_date(365)
>>> days_to_date(365 + 365)
>>> days_to_date(365 + 365 + 365)  # leap year
>>> days_to_date(np.arange(-1, 2))

>>> decompose_date(pd.Timestamp.now())
>>> decompose_date(datetime.datetime.now())
>>> decompose_date(datetime.date.today())
>>> decompose_date(np.datetime64("2022-10-15"))
>>> decompose_date(pd.Timestamp.now() + np.array([pd.Timedelta(days=i) for i in range(1, 4)]))

>>> is_leap_year(1971)
>>> is_leap_year(1972)
>>> is_leap_year(np.arange(1968, 1973))

>>> leaps_between(1968, 1970)  # 1968 was a leap year
>>> leaps_between(1968, 1974)  # 1972 was also a leap year
>>> leaps_between(0, 2022)
>>> leaps_between(1970 + np.arange(-6, 6), 2000)
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
    """Convert an array of date-like objects with `year`, `month`, and `day`
    attributes into a dictionary containing each attribute.  This dictionary
    can be used as a **kwargs dict for `date_to_days()`.
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
    """Convert proleptic Gregorian calendar dates into day offsets from the utc
    epoch ('1970-01-01 00:00:00+0000').

    Each of this function's arguments can be vectorized, and any excess beyond
    the normal range ([1-12] for `month`, [1-31] for `day`) is accurately
    reflected in the returned day offset.  For example, to measure the length
    (in days) of a 1 month period starting on December 31st, 2000, one can
    simply call:

        >>> `date_to_days(2000, 12 + 1, 31) - date_to_days(2000, 12, 31)`

    This works for arbitrarily large inputs, and has a pure integer approach;
    no float conversion is performed at any point.

        Note: for the sake of efficiency, this function will not attempt to
        coerce numpy integers or integer arrays into their built-in python
        equivalents.  As such, they may silently overflow (and wrap around
        infinity) if 64-bit limits are exceeded during conversion.  This
        shouldn't be a problem in practice; even with day-level precision, the
        valid 64-bit range vastly exceeds the observed age of the universe.
        Nevertheless, this can be avoided by converting the inputs into python
        integers (which do not overflow) beforehand.

    Algorithm adapted from:
        http://www.algonomicon.org/calendar/gregorian/to_jdn.html
        https://stackoverflow.com/questions/11609769/efficient-algorithm-for-converting-number-of-days-to-years-including-leap-years

    Parameters
    ----------
    year : int | array-like
        Proleptic Gregorian calendar year.

            Note: This function assumes the existence of a year 0, which does
            not correspond to real-world historical dates.  In order to convert
            a historical BC year (`-1 BC`, `-2 BC`, ...) to a negative year
            (`0`, `-1`, ...), simply add one to the BC year.  AD years are
            unaffected.

    month : int | array-like
        Proleptic Gregorian calendar month, indexed from 1 (January).  If a
        month value exceeds the range [1, 12], then any excess is automatically
        carried over into `year`.
    day : int | array-like
        Proleptic Gregorian calendar day, indexed from 1.  If a day value
        exceeds the maximum for the selected month, any excess is automatically
        carried over into `month` and `year`.

    Returns
    -------
    int | array-like:
        An integer day offset from the utc epoch.

    Examples
    --------
    >>> date_to_days(1970, 1, 1)
    >>> date_to_days(1970, 1, 2)
    >>> date_to_days(1970, 1, 0)
    >>> date_to_days(1970, 1, -1)

    >>> date_to_days(1970, 2, 1)
    >>> date_to_days(1970, 3, 1)
    >>> date_to_days(1970, 0, 1)
    >>> date_to_days(1970, -1, 1)

    >>> date_to_days(1971, 1, 1)
    >>> date_to_days(1972, 1, 1)
    >>> date_to_days(1969, 1, 1)
    >>> date_to_days(1968, 1, 1)

    >>> date_to_days(1970, 1, np.arange(-1, 3))
    >>> date_to_days(1970, np.arange(-1, 3), 1)
    >>> date_to_days(np.arange(1968, 1973), 1, 1)
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
    """Get the length in days of a given month, taking leap years into
    account.

    Parameters
    ----------
    month : int | array-like
        The month in question, indexed from 1 (January).  Can be vectorized.
    year : int | array-like
        The year to consider for each month.  Relevant for determining the
        length of February, which varies between 28 and 29 based on this
        setting.

    Returns
    -------
    int | array-like
        The number of days in the given month or a vector of month lengths.

    Examples
    --------
    >>> days_in_month(1, 2001)
    >>> days_in_month(2, 2001)
    >>> days_in_month(2, 2000)

    >>> days_in_month(np.arange(-2, 3), 2001)
    """
    if hasattr(month, "astype"):
        month = month.astype(np.int8, copy=False)
    return days_per_month[month - 1] + ((month == 2) & is_leap_year(year))


def days_to_date(
    days: int | np.ndarray | pd.Series
) -> dict[str, int | np.ndarray | pd.Series]:
    """Convert day offsets from the utc epoch ('1970-01-01 00:00:00+0000') into
    proleptic Gregorian calendar dates.

    This is the inverse operation of `date_to_days()`.  The output of this
    function can be used as a **kwargs dict for that function.

        Note: for the sake of efficiency, this function will not attempt to
        coerce numpy integers or integer arrays into their built-in python
        equivalents.  As such, they may silently overflow (and wrap around
        infinity) if 64-bit limits are exceeded during conversion.  This
        shouldn't be a problem in practice; even with day-level precision, the
        valid 64-bit range vastly exceeds the observed age of the universe.
        Nevertheless, this can be avoided by converting the inputs into python
        integers (which do not overflow) beforehand.

    Algorithm adapted from:
        http://www.algonomicon.org/calendar/gregorian/from_jdn.html
        https://stackoverflow.com/questions/11609769/efficient-algorithm-for-converting-number-of-days-to-years-including-leap-years

    Parameters
    ----------
    days : int | array-like
        An integer day offset from the utc epoch or a vector of such offsets.  

    Returns
    -------
    dict[str, int | array-like]
        A dictionary with the following keys/values:
            * `'year'`: Proleptic Gregorian calendar year.
            * `'month'`: Proleptic Gregorian calendar month, indexed from 1.
            * `'day'`: Proleptic Gregorian calendar day, indexed from 1.

        Note: This function assumes the existence of a year 0, which does not
        correspond to real-world historical dates.  In order to convert a
        historical BC year (`-1 BC`, `-2 BC`, ...) to a negative year (`0`,
        `-1`, ...), simply add one to the BC year.  AD years are unaffected.

    Examples
    --------
    >>> days_to_date(0)
    >>> days_to_date(1)
    >>> days_to_date(-1)

    >>> days_to_date(365)
    >>> days_to_date(365 + 365)
    >>> days_to_date(365 + 365 + 365)  # leap year

    >>> days_to_date(np.arange(-1, 2))
    """
    # move origin from utc to March 1st, 2000 (start of 400-year cycle)
    days = days - 11017

    # count years since 2000, correcting for leaps in each Gregorian cycle
    years = days - (days + 1) // days_per_400_years  # 400-year cycles
    years += years // days_per_100_years  # 100-year cycles
    years -= (years + 1) // days_per_4_years  # 4-year cycles
    years //= days_per_year  # whole years

    # compute residual days in final year
    days -= days_per_year * years + years // 4 - years // 100 + years // 400

    # exploit symmetry in month lengths, starting from March 1st
    months = (5 * days + 2) // 153  # no need for a lookup table
    days -= (153 * months + 2) // 5  # residual days in month

    # convert to proper calendar year
    years += 2000 + (months >= 10)  # treat Jan, Feb as belonging to next year

    # undo bias toward march 1st
    months += 2
    months %= 12

    # index from 1
    months += 1
    days += 1

    # return as dict
    return {"year": years, "month": months, "day": days}


def decompose_date(
    date: date_like | np.ndarray | pd.Series
) -> dict[str, int | np.ndarray | pd.Series]:
    """Split datelike objects into their constituent parts (`years`, `months`,
    `days`).

    The output of this function can be used as a **kwargs dict for
    ``date_to_days()``.

    Parameters
    ----------
    date : date-like | array-like
        A date-like object or a vector of such objects.  Accepts duck-types as
        long as they implement the `.year`, `.month`, and `.day` attributes.

    Returns
    -------
    dict[str, int | array-like]
        A dictionary with the following keys/values:
            * `'year'`: Proleptic Gregorian calendar year.
            * `'month'`: Proleptic Gregorian calendar month.
            * `'day'`: Proleptic Gregorian calendar day.

    Raises
    ------
    TypeError:
        If `date` consists of mixed element types.
    AttributeError:
        If `date` is a duck-type and does not implement the `.year`, `.month`,
        and `.day` attributes.

    Examples
    --------
    >>> decompose_date(pd.Timestamp.now())
    >>> decompose_date(datetime.datetime.now())
    >>> decompose_date(datetime.date.today())
    >>> decompose_date(np.datetime64("2022-10-15"))

    >>> decompose_date(pd.Timestamp.now() + np.array([pd.Timedelta(days=i) for i in range(1, 4)]))
    """
    # resolve datetime type and ensure homogenous
    dtype = get_dtype(date)
    if isinstance(dtype, set):
        raise TypeError(f"`date` must have homogenous, date-like element "
                        f"types, not {dtype}")

    # pd.Timestamp
    if dtype == pd.Timestamp:
        # np.ndarray
        if isinstance(date, np.ndarray):
            return decompose_datelike_objects(date)

        # pd.Series
        if isinstance(date, pd.Series):
            # M8[ns] dtype
            if pd.api.types.is_datetime64_ns_dtype(date):  # use `.dt` namespace
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
    """Check if the given year is a leap year according to the proleptic
    Gregorian calendar.

    Can also be used to obtain the length of a particular year.

    Parameters
    ----------
    year : int | array-like
        The year in question.

    Returns
    -------
    bool

    Examples
    --------
    >>> is_leap_year(1971)
    >>> is_leap_year(1972)

    >>> is_leap_year(np.arange(1968, 1973))
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

    Parameters
    ----------
    lower : int | array-like
        The starting year to consider or a vector of starting years.
    upper : int | array-like
        The ending year to consider or a vector of ending years.

    Returns
    -------
    int
        The number of leap days between each index of `lower` and `upper`.

    Examples
    --------
    >>> leaps_between(1968, 1970)  # 1968 was a leap year
    >>> leaps_between(1968, 1974)  # 1972 was also a leap year
    >>> leaps_between(0, 2022)

    >>> leaps_between(1970 + np.arange(-6, 6), 2000)
    """
    count = lambda x: x // 4 - x // 100 + x // 400
    return count(upper - 1) - count(lower - 1)
