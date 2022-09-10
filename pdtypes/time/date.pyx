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

from .leap import leaps_between


#########################
####    Constants    ####
#########################


cdef unsigned int days_per_400_years = 146097
cdef unsigned short days_per_100_years = 36524
cdef unsigned short days_per_4_years = 1461
cdef unsigned short days_per_year = 365


# cumulative days per month, starting from March 1st
cdef np.ndarray days_per_month = np.array(
    [0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306, 337, 366],
    dtype="i2"
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


@cython.boundscheck(False)
@cython.wraparound(False)
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
    month %= 12  # residual as integer index
    if isinstance(month, (np.ndarray, pd.Series)):
        month = month.astype("i1")  # to be used as index

    # build result
    result = 365 * year + leaps_between(0, year + 1)
    result += days_per_month[month].astype("O")  # convert to python integers
    result += day

    # move origin from March 1st, year 0 to utc
    result -= 719470

    # return
    return result


@cython.boundscheck(False)
@cython.wraparound(False)
def days_to_date(
    days: int | np.ndarray | pd.Series,
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
        https://stackoverflow.com/questions/11609769/efficient-algorithm-for-converting-number-of-days-to-years-including-leap-years
        http://www.algonomicon.org/calendar/gregorian/from_jdn.html

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
    # move origin to March 1st, 2000 (start of 400-year Gregorian cycle)
    days = days - 11017

    # count 400-year cycles
    years = 400 * (days // days_per_400_years) + 2000
    days %= days_per_400_years

    # count 100-year cycles
    temp = days // days_per_100_years
    days %= days_per_100_years
    days += (temp == 4) * days_per_100_years  # put leap day at end of cycle
    temp -= (temp == 4)
    temp *= 100
    years += temp

    # count 4-year cycles
    years += 4 * (days // days_per_4_years)
    days %= days_per_4_years

    # count residual years
    temp = days // days_per_year
    days %= days_per_year
    days += (temp == 4) * days_per_year  # put leap day at end of cycle
    temp -= (temp == 4)
    years += temp

    # get index in days_per_month that matches residual days in last year
    # `searchsorted(..., side="right") - 1` puts ties on the right
    month_index = days_per_month.searchsorted(days, side="right") - 1
    days -= days_per_month[month_index].astype("O")
    days += 1  # 1-indexed

    # convert index to month, accounting for bias toward March 1st
    month_index = month_index.astype("O")  # convert to python integers
    months = (month_index + 2) % 12 + 1  # 1-indexed
    years += (month_index >= 10)   # treat Jan, Feb as belonging to next year

    # return as dict
    return {"year": years, "month": months, "day": days}
