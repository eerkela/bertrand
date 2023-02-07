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
Converting Gregorian dates into UTC day offsets:

>>> date_to_days(1970, 1, -1)
-2
>>> date_to_days(1970, 1, 0)
-1
>>> date_to_days(1970, 1, 1)
0
>>> date_to_days(1970, 1, 2)
1
>>> date_to_days(1970, -1, 1)
-61
>>> date_to_days(1970, 0, 1)
-31
>>> date_to_days(1970, 2, 1)
31
>>> date_to_days(1970, 3, 1)
59
>>> date_to_days(1968, 1, 1)
-731
>>> date_to_days(1969, 1, 1)
-365
>>> date_to_days(1971, 1, 1)
365
>>> date_to_days(1972, 1, 1)
730
>>> date_to_days(1970, 1, np.arange(-1, 3))
array([-2, -1,  0,  1])
>>> date_to_days(1970, np.arange(-1, 3), 1)
array([-61, -31,   0,  31])
>>> date_to_days(np.arange(1968, 1973), 1, 1)
array([-731, -365,    0,  365,  730])

Getting month lengths:

>>> days_in_month(1, 2001)
31
>>> days_in_month(2, 2001)
28
>>> days_in_month(2, 2000)
29
>>> days_in_month(np.arange(-2, 3), 2001)
array([31, 30, 31, 31, 28], dtype=uint8)

Converting UTC day offsets into Gregorian dates:

>>> days_to_date(0)
{'year': 1970, 'month': 1, 'day': 1}
>>> days_to_date(1)
{'year': 1970, 'month': 1, 'day': 2}
>>> days_to_date(-1)
{'year': 1969, 'month': 12, 'day': 31}
>>> days_to_date(31)
{'year': 1970, 'month': 2, 'day': 1}
>>> days_to_date(31 + 28)
{'year': 1970, 'month': 3, 'day': 1}
>>> days_to_date(365 + 365 + 31 + 28)  # leap day
{'year': 1972, 'month': 2, 'day': 29}
>>> days_to_date(365)
{'year': 1971, 'month': 1, 'day': 1}
>>> days_to_date(365 + 365)
{'year': 1972, 'month': 1, 'day': 1}
>>> days_to_date(365 + 365 + 365)  # leap year
{'year': 1972, 'month': 12, 'day': 31}
>>> days_to_date(np.arange(-1, 2))
{'year': array([1969, 1970, 1970]), 'month': array([12,  1,  1]), 'day': array([31,  1,  2])}

Getting date dictionaries from datetime objects:

>>> decompose_date(pd.Timestamp("2022-10-15"))
{'year': 2022, 'month': 10, 'day': 15}
>>> decompose_date(datetime.datetime.fromisoformat("2022-10-15 12:37:00"))
{'year': 2022, 'month': 10, 'day': 15}
>>> decompose_date(np.datetime64("2022-10-15 20:12:34.235678"))
{'year': 2022, 'month': 10, 'day': 15}
>>> decompose_date(datetime.date(2022, 10, 15))
{'year': 2022, 'month': 10, 'day': 15}
>>> decompose_date(np.arange(0, 3, dtype="M8[D]"))
{'year': array([1970, 1970, 1970], dtype=object), 'month': array([1, 1, 1], dtype=object), 'day': array([1, 2, 3], dtype=object)}

Identifying leap years:

>>> is_leap_year(1971)
False
>>> is_leap_year(1972)
True
>>> is_leap_year(np.arange(1968, 1973))
array([ True, False, False, False,  True])

Counting leap years:

>>> leaps_between(1968, 1970)  # 1968 was a leap year
1
>>> leaps_between(1968, 1974)  # 1972 was also a leap year
2
>>> leaps_between(0, 2022)
491
>>> leaps_between(1970 + np.arange(-6, 6), 2000)
array([9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6])
"""
import numpy as np
cimport numpy as np

from pdtypes.type_hints import array_like


#########################
####    CONSTANTS    ####
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


######################
####    PUBLIC    ####
######################


def date_to_days(
    year: int | array_like,
    month: int | array_like,
    day: int | array_like
) -> int | array_like:
    """Convert proleptic Gregorian calendar dates into day offsets from the utc
    epoch ('1970-01-01 00:00:00+0000').

    Each of this function's arguments can be vectorized, and any excess beyond
    the normal range ([1-12] for `month`, [1-31] for `day`) is accurately
    reflected in the returned day offset.  For example, to measure the length
    (in days) of a 1 month period starting on December 31st, 2000, one can
    simply call:

        >>> date_to_days(2000, 12 + 1, 31) - date_to_days(2000, 12, 31)
        31

    .. note:: for the sake of efficiency, this function will not attempt to
        coerce numpy integers or integer arrays into their built-in python
        equivalents.  As such, they may silently overflow (and wrap around
        infinity) if 64-bit limits are exceeded during conversion.  This
        shouldn't be a problem in practice; even with day-level precision, the
        valid 64-bit range vastly exceeds the observed age of the universe.
        Nevertheless, this can be avoided by converting the inputs into python
        integers (which do not overflow) beforehand.

    Parameters
    ----------
    year : int | array-like
        Proleptic Gregorian calendar year.

        .. note:: This function assumes the existence of a year 0, which does
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

    See Also
    --------
    days_to_date : UTC day offset to Gregorian calendar date.

    References
    ----------
    http://www.algonomicon.org/calendar/gregorian/to_jdn.html
    https://stackoverflow.com/questions/11609769/efficient-algorithm-for-converting-number-of-days-to-years-including-leap-years

    Examples
    --------
    Inputs can be realistic:

    >>> date_to_days(1970, 1, 1)
    0
    >>> date_to_days(1970, 1, 2)
    1
    >>> date_to_days(1970, 2, 1)
    31
    >>> date_to_days(1970, 3, 1)
    59
    >>> date_to_days(1968, 1, 1)
    -731
    >>> date_to_days(1969, 1, 1)
    -365
    >>> date_to_days(1971, 1, 1)
    365
    >>> date_to_days(1972, 1, 1)
    730

    Or unrealistic:

    >>> date_to_days(1970, 1, 0)
    -1
    >>> date_to_days(1970, 1, -1)
    -2
    >>> date_to_days(1970, 0, 1)
    -31
    >>> date_to_days(1970, -1, 1)
    -61

    And potentially vectorized:

    >>> date_to_days(1970, 1, np.arange(-1, 3))
    array([-2, -1,  0,  1])
    >>> date_to_days(1970, np.arange(-1, 3), 1)
    array([-61, -31,   0,  31])
    >>> date_to_days(np.arange(1968, 1973), 1, 1)
    array([-731, -365,    0,  365,  730])
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
    month: int | array_like,
    year: int | array_like = 2001
) -> int | array_like:
    """Get the length in days of a given month, taking leap years into
    account.

    .. warning:: This should not be used with indices outside the range 1
        (January) to 12 (December).

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

    Raises
    ------
    ValueError
        If `month` isn't between -11 and 12.  Negative values have to be
        accepted due to python's native support for negative indexing, which
        cannot easily be disabled.  Nevertheless, only values between 1
        (January) and 12 (December) should be used in practice.

    Examples
    --------
    Inputs can be scalar:

    >>> days_in_month(1, 2001)
    31
    >>> days_in_month(2, 2001)
    28
    >>> days_in_month(2, 2000)
    29

    Or vectorized:

    >>> days_in_month(np.arange(-2, 3), 2001)
    array([31, 30, 31, 31, 28], dtype=uint8)
    """
    if hasattr(month, "astype"):
        month = month.astype(np.int8, copy=False)
    try:
        return days_per_month[month - 1] + ((month == 2) & is_leap_year(year))
    except IndexError as err:
        err_msg = (
            f"`month` must be between 1 (January) and 12 (December), not "
            f"{month}"
        )
        raise ValueError(err_msg) from err


def days_to_date(days: int | array_like) -> dict[str, int | array_like]:
    """Convert day offsets from the utc epoch ('1970-01-01 00:00:00+0000') into
    proleptic Gregorian calendar dates.

    This is the inverse operation of :func:`date_to_days()`.  The output of
    this function can be used as a **kwargs dict for that function.

    .. note:: for the sake of efficiency, this function will not attempt to
        coerce numpy integers or integer arrays into their built-in python
        equivalents.  As such, they may silently overflow (and wrap around
        infinity) if 64-bit limits are exceeded during conversion.  This
        shouldn't be a problem in practice; even with day-level precision, the
        valid 64-bit range vastly exceeds the observed age of the universe.
        Nevertheless, this can be avoided by converting the inputs into python
        integers (which do not overflow) beforehand.

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

        .. note:: This function assumes the existence of a year 0, which does
            not correspond to real-world historical dates.  In order to convert
            a historical BC year (`-1 BC`, `-2 BC`, ...) to a negative year
            (`0`, `-1`, ...), simply add one to the BC year.  AD years are
            unaffected.

    See Also
    --------
    date_to_days : Gregorian calendar date to UTC day offset.

    References
    ----------
    http://www.algonomicon.org/calendar/gregorian/from_jdn.html
    https://stackoverflow.com/questions/11609769/efficient-algorithm-for-converting-number-of-days-to-years-including-leap-years

    Examples
    --------
    Day offsets can be positive or negative:

    >>> days_to_date(0)
    {'year': 1970, 'month': 1, 'day': 1}
    >>> days_to_date(1)
    {'year': 1970, 'month': 1, 'day': 2}
    >>> days_to_date(-1)
    {'year': 1969, 'month': 12, 'day': 31}

    With correct month lengths:

    >>> days_to_date(31)
    {'year': 1970, 'month': 2, 'day': 1}
    >>> days_to_date(31 + 28)
    {'year': 1970, 'month': 3, 'day': 1}
    >>> days_to_date(365 + 365 + 31 + 28)  # leap day
    {'year': 1972, 'month': 2, 'day': 29}

    And correct year lengths:

    >>> days_to_date(365)
    {'year': 1971, 'month': 1, 'day': 1}
    >>> days_to_date(365 + 365)
    {'year': 1972, 'month': 1, 'day': 1}
    >>> days_to_date(365 + 365 + 365)  # leap year
    {'year': 1972, 'month': 12, 'day': 31}

    They can also be vectorized:

    >>> days_to_date(np.arange(-1, 2))
    {'year': array([1969, 1970, 1970]), 'month': array([12,  1,  1]), 'day': array([31,  1,  2])}
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

def is_leap_year(year: int | array_like) -> bool | array_like:
    """Check if the given year is a leap year according to the proleptic
    Gregorian calendar.

    Can also be used to obtain the length of a particular year.

    Parameters
    ----------
    year : int | array-like
        The year to be checked.

    Returns
    -------
    bool
        `True` if `year` is a leap year.  `False` otherwise.

    Examples
    --------
    Years can be scalar:

    >>> is_leap_year(1971)
    False
    >>> is_leap_year(1972)
    True

    Or vectorized:

    >>> is_leap_year(np.arange(1968, 1973))
    array([ True, False, False, False,  True])
    """
    return (year % 4 == 0) & ((year % 100 != 0) | (year % 400 == 0))


def leaps_between(
    lower: int | array_like,
    upper: int | array_like
) -> int | array_like:
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
    Bounds can be scalar:

    >>> leaps_between(1968, 1970)  # 1968 was a leap year
    1
    >>> leaps_between(1968, 1974)  # 1972 was also a leap year
    2
    >>> leaps_between(0, 2022)
    491

    Or vectorized:

    >>> leaps_between(1970 + np.arange(-6, 6), 2000)
    array([9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6])
    """
    count = lambda x: x // 4 - x // 100 + x // 400
    return count(upper - 1) - count(lower - 1)
