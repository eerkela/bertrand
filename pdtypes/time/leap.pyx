"""Implements 2 functions, `is_leap_year()` and `leaps_between()`, which help
facilitate vectorized leap year calculations, according to the proleptic
Gregorian calendar.

`is_leap_year()` accepts an integer scalar, array, or Series representing
Gregorian calendar years, returning `True` where the input corresponds to a
leap year and `False` where it does not.

`leaps_between()` accepts a bounded range of integer-based Gregorian calendar
years and returns the number of leap days that occur between them.  Both bounds
can be independently vectorized.
"""
cimport cython
import numpy as np
cimport numpy as np
import pandas as pd


def is_leap_year(
    year: int | pd.Series | np.ndarray
) -> bool | pd.Series | np.ndarray:
    """Returns True if the given year is a leap year according to the proleptic
    Gregorian calendar.
    """
    return (year % 4 == 0) & ((year % 100 != 0) | (year % 400 == 0))


def leaps_between(
    begin: int | pd.Series | np.ndarray,
    end: int | pd.Series | np.ndarray
) -> int | pd.Series | np.ndarray:
    """Return the number of leap days between the years `begin` and `end`.

    This function counts from the beginning of each year.  This means that
    `leaps_between(x, x + 1)` will return 1 if and only if `x` was a leap year.

    Identical to `calendar.leapdays()` from the built-in `calendar` package,
    but avoids an import and is very slightly faster (~10%).
    """
    count = lambda x: x // 4 - x // 100 + x // 400
    return count(end - 1) - count(begin - 1)
