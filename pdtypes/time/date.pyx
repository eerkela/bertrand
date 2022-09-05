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


# constants for `days_to_date`
cdef unsigned int days_per_400_years = 146097
cdef unsigned short days_per_100_years = 36524
cdef unsigned short days_per_4_years = 1461
cdef unsigned short days_per_year = 365


# cumulative days per month, starting from March 1st
cdef np.ndarray days_per_month
days_per_month = np.array([0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306,
                           337, 366], dtype="i2")


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

    # case 1: pd.Timestamp
    if dtype == pd.Timestamp:
        if isinstance(date, np.ndarray):
            return decompose_datelike_objects(date)
        if isinstance(date, pd.Series):
            if pd.api.types.is_datetime64_ns_dtype(date):
                return {
                    "year": date.dt.year,
                    "month": date.dt.month,
                    "day": date.dt.day
                }
            index = date.index
            result = decompose_datelike_objects(date.to_numpy())
            return {k: pd.Series(v, index=index, copy=False)
                    for k, v in result.items()}
        return {"year": date.year, "month": date.month, "day": date.day}

    # case 2: np.datetime64
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
        if is_series:
            index = date.index
            return {
                "year": pd.Series(year, index=index, copy=False),
                "month": pd.Series(month, index=index, copy=False),
                "day": pd.Series(day, index=index, copy=False)
            }
        return {"year": year, "month": month, "day": day}

    # case 3: datetime.datetime-like (duck-type)
    if isinstance(date, np.ndarray):
        return decompose_datelike_objects(date)
    if isinstance(date, pd.Series):
        return decompose_datelike_objects(date.to_numpy())
    return {"year": date.year, "month": date.month, "day": date.day}



# TODO: fractional years, months


@cython.boundscheck(False)
@cython.wraparound(False)
def date_to_days(
    year: int | np.ndarray | pd.Series,
    month: int | np.ndarray | pd.Series,
    day: int | np.ndarray | pd.Series,
    epoch: str = "utc"
) -> int | np.ndarray | pd.Series:
    """Convert a (proleptic) Gregorian calendar date `(year, month, day)` into
    a day offset from the given epoch.

    The available epochs are as follows (earliest - latest):
        - `'julian'`: indexes from the start of the Julian period, which
            corresponds to the historical date January 1st, 4713 BC (according
            to the proleptic Julian calendar) or November 24, 4714 BC
            (according to the proleptic Gregorian calendar).  Commonly used in
            astronomical applications.
        - `'gregorian'`: indexes from October 14th, 1582, the date at which
            Pope Gregory XIII instituted the Gregorian calendar.
        - `'reduced_julian'`: indexes from November 16th, 1858, which drops the
            first two leading digits of the corresponding `'julian'` day
            number.
        - `'lotus'`: indexes from December 31st, 1899, which was incorrectly
            identified as January 0, 1900 in the original Lotus 1-2-3
            implementation.  Still used in a variety of spreadsheet
            applications, including Microsoft Excel, Google Sheets, and
            LibreOffice.
        - `'sas'`: indexes from January 1st, 1960.  Used by the SAS statistical
            software.
        - `'utc'`: indexes from January 1st, 1970.  Represents Unix/POSIX day
            number.
        - `'gps'`: indexes from January 6th, 1980.  Used by most GPS systems,
            which normally count weeks instead of days, as returned by this
            function.
        - `'cocoa'`: indexes from January 1st, 2001.  Used in Apple's Cocoa
            framework, which drives macOS and related mobile devices.

    Note: by convention, `'julian'` and `'reduced_julian'` dates increment at
    noon (12:00:00 UTC) on the corresponding day.  For these to be proper
    *Julian dates* (JD), a fractional component between -0.5 and +0.5 would
    normally be added to the integer *Julian day number* (JDN, as returned by
    this function).  -0.5 represents midnight (00:00:00 UTC) on the same day,
    and +0.5 represents midnight (00:00:00 UTC) on the next day.

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
    epoch (str):
        Epoch for the returned day offset.  See the descriptions above for more
        details.  Defaults to `'utc'`.

    Returns
    -------
    int | np.ndarray | pd.Series:
        The integer day offset from the given epoch.

    Raises
    ------
    ValueError:
        If `epoch` is not one of the accepted epochs `('julian', 'gregorian',
        'reduced_julian', 'lotus', 'sas', 'utc', 'gps', 'cocoa')`.
    """
    # normalize months to start with March 1st, indexed from 1 (January)
    month = month - 3
    year = year + month // 12
    month %= 12  # residual as integer index

    # build result
    result = 365 * year + leaps_between(0, year + 1)
    result += days_per_month[month]
    result += day

    # move origin from March 1st, year 0 toward specified epoch
    cdef dict epoch_bias = {
        "julian": 1721118,            # (-4713, 11, 24)
        "gregorian": -578042,         # (1582, 10, 14)
        "reduced_julian": -678882,    # (1858, 11, 16)
        "lotus": -693901,             # (1899, 12, 30)
        "sas": -715817,               # (1960, 1, 1)
        "utc": -719470,               # (1970, 1, 1)
        "gps": -723127,               # (1980, 1, 6)
        "cocoa": -730793              # (2001, 1, 1)
    }
    try:
        result += epoch_bias[epoch]
    except KeyError as err:
        err_msg = f"`epoch` must be one of {tuple(epoch_bias)}, not {epoch}"
        raise ValueError(err_msg) from err

    # return
    return result


@cython.boundscheck(False)
@cython.wraparound(False)
def days_to_date(
    days: int | np.ndarray | pd.Series,
    epoch: str = "utc"
) -> dict[str, int] | dict[str, np.ndarray] | dict[str, pd.Series]:
    """Convert a day offset from the given epoch into the corresponding
    (proleptic) Gregorian calendar date `(year, month, day)`.

    The available epochs are as follows (earliest - latest):
        - `'julian'`: indexes from the start of the Julian period, which
            corresponds to the historical date January 1st, 4713 BC (according
            to the proleptic Julian calendar) or November 24, 4714 BC
            (according to the proleptic Gregorian calendar).  Commonly used in
            astronomical applications.
        - `'gregorian'`: indexes from October 14th, 1582, the date at which
            Pope Gregory XIII instituted the Gregorian calendar.
        - `'reduced_julian'`: indexes from November 16th, 1858, which drops the
            first two leading digits of the corresponding `'julian'` day
            number.
        - `'lotus'`: indexes from December 31st, 1899, which was incorrectly
            identified as January 0, 1900 in the original Lotus 1-2-3
            implementation.  Still used in a variety of spreadsheet
            applications, including Microsoft Excel, Google Sheets, and
            LibreOffice.
        - `'sas'`: indexes from January 1st, 1960.  Used by the SAS statistical
            software.
        - `'utc'`: indexes from January 1st, 1970.  Represents Unix/POSIX day
            number.
        - `'gps'`: indexes from January 6th, 1980.  Used by most GPS systems,
            which normally count weeks instead of days, like this function.
        - `'cocoa'`: indexes from January 1st, 2001.  Used in Apple's Cocoa
            framework, which drives macOS and related mobile devices.

    Note: by convention, `'julian'` and `'reduced_julian'` dates increment at
    noon (12:00:00 UTC) on the corresponding day.  For these to be proper
    *Julian dates* (JD), a fractional component between -0.5 and +0.5 would
    normally be added to the integer *Julian day number* (JDN, as accepted by
    this function).  -0.5 represents midnight (00:00:00 UTC) on the same day,
    and +0.5 represents midnight (00:00:00 UTC) on the next day.

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
        Integer day offset from the given `epoch`.  
    epoch (str):
        Epoch for the given day offset.  See the descriptions above for more
        details.  Defaults to `'utc'`.

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

    Raises
    ------
    ValueError:
        If `epoch` is not one of the accepted epochs `('julian', 'gregorian',
        'reduced_julian', 'lotus', 'sas', 'utc', 'gps', 'cocoa')`.
    """
    # move origin to March 1st, 2000 (start of 400-year Gregorian cycle)
    cdef dict epoch_bias = {
        "julian": -2451605,          # (-4713, 11, 24)
        "gregorian": -152445,        # (1582, 10, 14)
        "reduced_julian": -51605,    # (1858, 11, 16)
        "lotus": -36586,             # (1899, 12, 30)
        "sas": -14670,               # (1960, 1, 1)
        "utc": -11017,               # (1970, 1, 1)
        "gps": -7360,                # (1980, 1, 6)
        "cocoa": 306                 # (2001, 1, 1)
    }
    try:
        days = days + epoch_bias[epoch]
    except KeyError as err:
        err_msg = f"`epoch` must be one of {tuple(epoch_bias)}, not {epoch}"
        raise ValueError(err_msg) from err

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

    # convert residual days (ordinal) in last year to months
    # searchsorted(..., side="right") - 1 puts ties on the right
    month_index = days_per_month.searchsorted(days, side="right") - 1
    months = (month_index + 2) % 12 + 1  # undo bias toward March 1st
    years += (month_index >= 10)  # treat Jan, Feb as belonging to next year

    # subtract off months to get final days in last month
    days -= days_per_month[month_index]
    days += 1  # index from 1

    # return as dict
    return {"year": years, "month": months, "day": days}
