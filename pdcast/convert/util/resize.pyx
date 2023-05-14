"""This module contains helper functions to resize numeric data types and
series objects to fit an observed range.
"""
import pandas as pd

from pdcast cimport types
from pdcast.detect import detect_type
from pdcast.util.error import shorten_list


cpdef tuple boundscheck(
    object series,
    types.AtomicType dtype,
    str errors = "raise"
):
    """Ensure that a series fits within the allowable range of a given data
    type.

    If overflow is detected, this function will attempt to upcast the data
    type to fit the series.  If this fails and errors="coerce", then it
    will drop overflowing values from the series to fit the data type instead.

    Parameters
    ----------
    dtype : AtomicType
        An AtomicType whose range will be used for the check.
    errors : str, default "raise"
        The error-handling rule to apply to the range check.  Must be one
        of "raise", "ignore", or "coerce".

    Returns
    -------
    series : pd.Series
        A series whose elements fit within the range of the specified type.
        In most cases, this will be the original series, but if overflow is
        detected and errors="coerce", then it may be a subset of the original.
    dtype : AtomicType
        A type that fits the observed range of the series.  In most cases,
        this will be the original data type, but if overflow is detected
        and the type is upcastable, then it may be larger.

    Raises
    ------
    OverflowError
        If ``dtype`` cannot fit the observed range of the series, cannot
        be upcasted to fit, and ``errors != "coerce"``
    """
    cdef types.ScalarType series_type = detect_type(series)

    # trivial case for empty series
    if series_type is None:
        return series, dtype

    # get min/max values as python ints (prevents inconsistent comparison)
    cdef object min_val = int(series.min) - bool(series.min % 1)
    cdef object max_val = int(series.max) + bool(series.max % 1)
    cdef object index

    # check for overflow
    if min_val < dtype.min or max_val > dtype.max:
        # attempt to upcast dtype to fit series
        try:
            return series, dtype.upcast(series)
        except OverflowError:
            pass

        # TODO: clip to within tolerance?
        # if min_val > dtype.min - tol and max_val < dtype.max + tol:
        #     series.clip(dtype.min, dtype.max)
        # else:
        #     see below

        # continue with OverflowError
        index = (series < dtype.min) | (series > dtype.max)
        if errors == "coerce":
            series = series[~index]
        else:
            raise OverflowError(
                f"values exceed {dtype} range at index "
                f"{shorten_list(series[index].index.values)}"
            )

    return series, dtype
