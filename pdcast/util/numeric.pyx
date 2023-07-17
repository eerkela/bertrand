"""This module contains helper functions for numeric conversions."""
import pandas as pd
cimport numpy as np
import numpy as np

from pdcast cimport types
from pdcast.detect import detect_type
from pdcast.util.error import shorten_list
from pdcast.util.round cimport Tolerance


# TODO: downcast_integer should use its tol argument for bounds checks


# TODO: downcast methods should use the target dtype rather than the series
# type to get smaller types.  Right now, the observed series type will always
# be empty.


######################
####    PUBLIC    ####
######################


cpdef object boundscheck(
    object series,
    types.ScalarType dtype,
    object tol,
    str errors
):
    """Ensure that a series fits within the allowable range of a given data
    type.

    If overflow is detected, this function will attempt to upcast the data
    type to fit the series.  If this fails and errors="coerce", then it
    will drop overflowing values from the series to fit the data type instead.

    Parameters
    ----------
    dtype : ScalarType
        An ScalarType whose range will be used for the check.
    errors : str, default "raise"
        The error-handling rule to apply to the range check.  Must be one
        of "raise", "ignore", or "coerce".

    Returns
    -------
    series : pd.Series
        A series whose elements fit within the range of the specified type.
        In most cases, this will be the original series, but if overflow is
        detected and errors="coerce", then it may be a subset of the original.

    Raises
    ------
    OverflowError
        If ``dtype`` cannot fit the observed range of the series, cannot
        be upcasted to fit, and ``errors != "coerce"``
    """
    cdef types.VectorType series_type = detect_type(series)

    # trivial case for empty series
    if series_type is None:
        return series

    # get min/max values as python ints (prevents inconsistent comparison)
    cdef object min_val = series.min()
    cdef object max_val = series.max()
    cdef object index

    min_val = int(min_val) - bool(min_val % 1)
    max_val = int(max_val) + bool(max_val % 1)

    # check for overflow
    if min_val < dtype.min or max_val > dtype.max:
        # clip to within tolerance
        if min_val >= dtype.min - tol and max_val <= dtype.max + tol:
            return series.clip(dtype.min, dtype.max)

        # continue with OverflowError
        index = (series < dtype.min) | (series > dtype.max)
        if errors == "coerce":
            series = series[~index]
        else:
            raise OverflowError(
                f"values exceed {dtype} range at index "
                f"{shorten_list(series[index].index.values)}"
            )

    return series


cpdef object downcast_integer(
    object series,
    Tolerance tol,
    types.CompositeType smallest
):
    """Reduce the itemsize of an integer type to fit the observed range."""
    from pdcast import convert

    cdef types.VectorType series_type = detect_type(series)
    cdef list smaller = list(series_type.smaller)
    cdef object min_val
    cdef object max_val

    # filter based on `smallest`
    if smallest is not None:
        smaller = filter_smallest(smaller, smallest, series_type=series_type)

    min_val = series.min()
    max_val = series.max()

    # get observed range as python ints (prevents inconsistent comparison)
    if pd.isna(min_val):  # series is empty
        # NOTE: we swap min/max to maintain upcast behavior for generic types
        min_val = series_type.max
        max_val = series_type.min
    else:
        min_val = int(min_val)
        max_val = int(max_val)

    # search for smaller data type that fits observed range
    for small in smaller:
        # range check
        if min_val < small.min or max_val > small.max:
            continue

        # dtype is valid
        return convert.cast(
            series,
            dtype=small,
            downcast=None,
            errors="raise"
        )

    # series could not be downcasted
    return series


cpdef object downcast_float(
    object series,
    Tolerance tol,
    types.CompositeType smallest
):
    """Reduce the itemsize of a float type to fit the observed range."""
    from pdcast import convert

    cdef types.VectorType series_type = detect_type(series)
    cdef list smaller = list(series_type.smaller)

    # filter based on `smallest`
    if smallest is not None:
        smaller = filter_smallest(smaller, smallest, series_type=series_type)

    # try converting to each candidate in order (applying tolerance)
    for small in smaller:
        try:
            attempt = convert.cast(
                series,
                dtype=small,
                tol=tol,
                downcast=None,
                errors="raise"
            )
        except Exception:
            continue

        # candidate is valid
        if within_tol(attempt, series, tol=tol.real).all():
            return attempt

    # return original
    return series


cpdef object downcast_complex(
    object series,
    Tolerance tol,
    types.CompositeType smallest
):
    """Reduce the itemsize of a complex type to fit the observed range."""
    cdef object real_part
    cdef object imag_part

    # downcast real and imaginary component separately
    real_part = downcast_float(
        real(series),
        tol=tol,
        smallest=smallest
    )
    imag_part = downcast_float(
        imag(series),
        tol=Tolerance(tol.imag),
        smallest=smallest
    )

    # use whichever type is larger
    return combine_real_imag(real_part, imag_part)


cpdef np.ndarray[np.uint8_t, cast=True] isinf(object series):
    """Return a boolean mask indicating the position of infinities in the
    series.

    Parameters
    ----------
    series : pd.Series
        A pandas series to check for infinities.

    Returns
    -------
    np.ndarray
        A numpy boolean array indicating the index of each infinity within the
        series.
    """
    return np.isin(series, (np.inf, -np.inf))


cpdef object real(object series):
    """Get the real component of a series.

    This is a convenience function that mimics the behavior of `np.real()`, but
    supports complex values stored in object arrays.

    Parameters
    ----------
    series : pd.Series
        A pandas series to decompose.

    Returns
    -------
    pd.Series
        The real component of the series.
    """
    cdef types.VectorType series_type
    cdef types.VectorType target

    # NOTE: np.real() fails when applied to object arrays that may contain
    # complex values.  In this case, we reduce it to a loop.

    # object array
    if pd.api.types.is_object_dtype(series):
        series_type = detect_type(series)
        target = getattr(series_type, "equiv_float", series_type)
        return elementwise_real(series).astype(target.dtype)

    # use np.real() directly
    return pd.Series(np.real(series), index=series.index)


cpdef object imag(object series):
    """Get the imaginary component of the wrapped series.

    This is a convenience function that mimics the behavior of `np.imag()`, but
    support complex values stored in object arrays.

    Parameters
    ----------
    series : pd.Series
        A pandas series to decompose.

    Returns
    -------
    pd.Series
        The imaginary component of the series.
    """
    cdef types.VectorType series_type
    cdef types.VectorType target

    # NOTE: np.imag() fails when applied to object arrays that may contain
    # complex values.  In this case, we reduce it to a loop.

    # object array
    if pd.api.types.is_object_dtype(series):
        series_type = detect_type(series)
        target = getattr(series_type, "equiv_float", series_type)
        return elementwise_real(series).astype(target.dtype)

    # use np.imag() directly
    return pd.Series(np.imag(series), index=series.index)


cpdef np.ndarray[np.uint8_t, cast=True] within_tol(
    object series1,
    object series2,
    object tol
):
    """Return a boolean mask indicating where the elements of two series are
    within the specified tolerance of one another.

    Parameters
    ----------
    series1 : pd.Series
        A series to compare against ``series2``.
    series2 : pd.Series
        A series to compare against ``series1``.
    tol : numeric
        The maximum tolerance between values.  If any elements of the provided
        series objects differ by more than this amount, then the corresponding
        result will be set ``False``.

    Returns
    -------
    np.ndarray
        A boolean mask indicating which elements of ``series1`` are within
        tolerance of ``series2``.
    """
    cdef object result

    # fastpath if no tolerance is given
    if not tol:
        result = (series1 == series2)
    else:
        result = ~((series1 - series2).abs() > tol)

    return result.to_numpy()


#######################
####    PRIVATE    ####
#######################


cdef object elementwise_real = np.frompyfunc(np.real, 1, 1)
cdef object elementwise_imag = np.frompyfunc(np.imag, 1, 1)


cdef list filter_smallest(
    list smaller,
    types.CompositeType smallest,
    types.VectorType series_type
):
    """Filter a list of downcast candidates based on a set of lower limits."""
    # trivial case: series type is directly contained in lower limits
    if series_type in smallest:
        return []

    cdef list filtered = []

    # iterate from largest to smallest
    for typ in reversed(smaller):
        filtered.append(typ)

        # if candidate is in lower limits, break
        if typ in smallest:
            break

    return list(reversed(filtered))


cdef object combine_real_imag(
    object real_part,
    object imag_part
):
    cdef types.VectorType real_type = detect_type(real_part)
    cdef types.VectorType imag_type = detect_type(imag_part)
    cdef types.VectorType largest
    cdef types.VectorType target
    cdef object result

    largest = max([real_type, imag_type])
    target = largest.equiv_complex
    result = (real_part + imag_part * 1j)
    return result.astype(target.dtype, copy=False)

