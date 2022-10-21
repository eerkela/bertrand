cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from .calendar import *


# np.datetime64
dt64_array = np.arange(-10**6, 10**6, dtype="M8[s]")
dt64_obj_array = np.array(list(dt64_array), dtype="O")
dt64_obj_series = pd.Series(dt64_obj_array, dtype="O")

# pd.Timestamp
ts_series = pd.Series(dt64_array)
ts_obj_series = pd.Series(list(ts_series), dtype="O")
ts_obj_array = ts_obj_series.to_numpy()

# datetime.datetime
dt_obj_array = dt64_array.astype("O")
dt_obj_series = pd.Series(list(dt_obj_array), dtype="O")

# cumulative days per month, starting from March 1st
cdef np.ndarray days_per_month
days_per_month = np.array([0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306,
                           337, 366], dtype="i2")



# Gregorian cycle lengths
cdef unsigned int days_per_400_years = 146097
cdef unsigned short days_per_100_years = 36524
cdef unsigned short days_per_4_years = 1461
cdef unsigned short days_per_year = 365


# cumulative days per month, starting from March 1st
cdef np.ndarray days_per_month_cumsum = np.array(
    [0, 31, 61, 92, 122, 153, 184, 214, 245, 275, 306, 337, 366],
    dtype="O"
)


def date_to_days_with_lookup(
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
    result += days_per_month_cumsum[month]
    result += day
    result -= 719470  # move origin from March 1st, year 0 to utc
    return result



def days_to_date_reference(
    days: int | np.ndarray | pd.Series
) -> dict[str, int] | dict[str, np.ndarray]:
    """This is a reference function to test the accuracy of `days_to_date` and
    `days_to_date_straight_division`.  It uses a reliable `numpy.datetime64` approach,
    and could be used in place of either of the other two, except that it
    is ~2x slower than `days_to_date` and ~4x slower than
    `days_to_date_straight_division`.
    """
    cycles = days // 146097
    days = days % 146097

    if isinstance(days, np.ndarray):
        days = days.astype("M8[D]", copy=False)
    elif isinstance(days, pd.Series):
        days = days.to_numpy(dtype="M8[D]")
    else:
        days = np.datetime64(days, "D")

    result = decompose_date(days)
    result["year"] += 400 * cycles
    return result


def days_to_date_with_lookup(
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

    # get index in days_per_month_cumsum that matches residual days in last year
    # `searchsorted(..., side="right") - 1` puts ties on the right
    month_index = days_per_month_cumsum.searchsorted(days, side="right") - 1
    days -= days_per_month_cumsum[month_index]
    days += 1  # 1-indexed

    # convert index to month, accounting for bias toward March 1st
    month_index = month_index.astype("O")  # convert to python integers
    months = (month_index + 2) % 12 + 1  # 1-indexed
    years += (month_index >= 10)   # treat Jan, Feb as belonging to next year

    # return as dict
    return {"year": years, "month": months, "day": days}


def days_to_date_straight_division(
    days: int | np.ndarray | pd.Series,
    offset: int
) -> dict[str, int] | dict[str, np.ndarray]:
    """Identical to `days_to_date`, but attempts to find a direct conversion
    from days to years.  This works almost perfectly, but fails due to rounding
    errors for certain dates (<0.1% of results).

    I've included this in the hope that at some point, a solution can be found
    that will make it consistent.  If it is, this function would be about 2x
    faster than the original `days_to_date`.

    This approach essentially works by multiplying `days` by the continued
    fraction for `1 / 365.2425`, the length of a standard gregorian year.  To
    do this in purely integer math, we multiply the numerator and denominator
    by 10,000 and simplify to get `400 // 146097`.  This reflects the length
    of a standard Gregorian 400-year cycle.

    The trouble with this is that the modeled leap year cycles don't reflect
    the real-world equivalents.  As modeled, each 400-year cycle starts with
    a long century (36525 days) followed by 3 short centuries (36524 days),
    rather than the other way around (short, short, short, long).  If you begin
    counting from March 1st, this is corrected automatically.

    Unfortunately, there is an additional problem of inconsistent 4-year cycles.
    This can be illuminated with the `search_leaps` testing function, which
    reveals an occasional 5-year cycle for reasons I still don't fully
    understand.  I believe it arises because the bias towards March 1st lacks
    the fractional component that accompanies each day.  In order to account
    for this, a fraction of `days * 0.2425 / 365.2425` must be applied for each
    bias term.  Doing so would require squaring the denominator, in order to
    make room for the needed correction and maintain integer accuracy.  The
    formula would then look like this:

    (146097 * 400 * (days + bias) + bias_correction) // 146097**2

    In essence, we want a formula that replicates the real-world distribution
    for the 400 years above and below year 0.  Then, we simply bias our
    calculation toward year -30 and add 2000 to simulate starting from 1970.
    """
    # this is essentially the same as starting from year -100
    # centuries = lambda days: (400 * (days) + 300) // 146097

    # this is accurate for year calculations, but the residual isn't reliable
    # from_march = lambda days: (400 * (days + 60)) // 146097

    # this is simulating starting from March 1st, -30
    # from_neg30 = lambda days: (400 * (days - 59 - 10957)) // 146097 + 2000

    days = days - 59 - 10957  # from March 1st, -30
    # days = days + 719468  # utc epoch from March 1st, year 0
    days *= 400
    days += offset
    year = days // 146097 + 2000
    days %= 146097
    days //= 400

    month_index = days_per_month.searchsorted(days, side="right") - 1
    year += month_index >= 10
    month = (month_index + 2) % 12 + 1
    days -= days_per_month[month_index]

    return {"year": year, "month": month, "day": days + 1}





def residual(arg):
    if isinstance(arg, tuple):
        days = date_to_days(arg[0], arg[1], arg[2]) - date_to_days(0, 1, 1)
        days *= 400
    else:
        days = 400 * arg
    return days % 146097


def test(arg, threshold=200):
    if isinstance(arg, tuple):
        days = date_to_days(arg[0], arg[1], arg[2]) - date_to_days(0, 1, 1)
        days *= 400
    else:
        days = 400 * arg

    result = days // 146097
    residual = days % 146097
    result -= (residual != 0) & (residual < threshold)
    return result





def search_years(int high):
    """Compare the output of `days_to_date_straight_division` against
    `days_to_date_reference`, returning any inputs that return an unequal
    year.
    """
    cdef int i
    cdef list different = []

    for i in range(0, high):
        i -= 719528  # bias from utc epoch to year 0
        expected = days_to_date_reference(i)
        result = days_to_date_straight_division(i)
        if expected["year"] != result["year"]:
            different.append(i)

    return np.array(different)


def search_offsets(int up_to = 10**4):
    """Attempt to find an offset that brings the output of
    `days_to_date_straight_division` into alignment with `days_to_date_reference`.
    """
    cdef int i
    cdef np.ndarray[long int] test = np.arange(-10**4, 10**4) - 719528

    expected = days_to_date_reference(test)["year"]

    for i in range(up_to):
        result = days_to_date_straight_division(test, i)["year"]
        if (result == expected).all():
            return i

    return None


def search_leaps(int years_up, int test_offset = 0):
    """Identify the years that `days_to_date_straight_division` thinks are leap years.
    """
    cdef int i
    cdef list leap_years = []
    cdef set normal_years = set()
    cdef int offset = 0
    cdef int result

    for i in range(years_up):
        result = days_to_date_straight_division(365 * i + offset, test_offset)["year"]
        if result in normal_years:
            leap_years.append(result)
            offset += 1
        else:
            normal_years.add(result)

    return np.array(leap_years)


def search_leaps_basic(int years_up, threshold):
    """Identify the years that a simple continued fraction are leap years.

    A discontinuity is observed at year 37, which comes 5 years after the
    previous leap year, the correctly-identified year 32.  These discntinuities
    appear to be non-linear and not tied to an identifiable gregorian cycle.
    The 5-year gap appears to repeat on a 33-year cycle, reoccuring at year
    70, 103, 136, 169, 202, 235, 268, 301, etc.

    If this can be remedied, then hope remains for days_to_date_straight_division.
    """
    cdef int i
    cdef list leap_years = []
    cdef set normal_years = set()
    cdef int offset = 0
    cdef int result

    for i in range(years_up):
        result = test(365 * i + offset, threshold)
        if result in normal_years:
            leap_years.append(result)
            offset += 1
        else:
            normal_years.add(result)
    return np.array(leap_years)
