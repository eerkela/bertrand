"""This module contains general vector manipulations for 1D numpy arrays and
pandas Series objects.
"""
cimport cython
cimport numpy as np
import numpy as np
import pandas as pd


######################
####    PUBLIC    ####
######################


cpdef np.ndarray as_array(object data):
    """Convert an arbitrary object into a corresponding 1D numpy array.

    Scalars and non-numpy/pandas iterables are converted into dtype: object
    array.
    """
    if isinstance(data, pd.Series):
        return data.to_numpy()

    if isinstance(data, np.ndarray):
        return np.atleast_1d(data)

    return np.atleast_1d(np.array(data, dtype="O"))


cpdef object as_series(object data):
    """Convert an arbitrary object into a corresponding pandas Series.

    Scalars and non-numpy/pandas iterables are converted into dtype: object
    series.
    """
    if isinstance(data, pd.Series):
        return data

    if isinstance(data, np.ndarray):
        return pd.Series(np.atleast_1d(data))

    return pd.Series(data, dtype="O")


cpdef object apply_with_errors(object series, object call, str errors):
    """Apply a callable over a series using the specified error handling rule.

    This always returns a `dtype: object` series.

    Parameters
    ----------
    series : pd.Series
        A Series to iterate over.
    call : Callable
        A callable to apply at each index.
    errors : str
        The rule to use if errors are encountered.  Must be one of "raise",
        "ignore", or "coerce".  Both "raise" and "ignore" propagate errors up
        the stack, while "coerce" drops the offending values from the output.

    Returns
    -------
    pd.Series
        The result of applying ``call`` at each index.
    """
    cdef np.ndarray[object] result
    cdef bint has_errors
    cdef np.ndarray[np.uint8_t, cast=True] error_index
    cdef object series_index

    # loop over numpy array
    result, has_errors, error_index = _apply_with_errors(
        series.to_numpy(dtype=object),
        call=call,
        errors=errors
    )

    # remove coerced NAs.  NOTE: has_errors=True only when errors="coerce"
    series_index = series.index
    if has_errors:
        result = result[~error_index]
        series_index = series_index[~error_index]

    return pd.Series(
        result,
        index=series_index,
        name=series.name,
        dtype=object
    )


#######################
####    PRIVATE    ####
#######################


@cython.boundscheck(False)
@cython.wraparound(False)
cdef tuple _apply_with_errors(
    np.ndarray[object] arr,
    object call,
    str errors
):
    """Helper for the apply step of `apply_with_errors()`."""
    cdef unsigned int arr_length = arr.shape[0]
    cdef unsigned int i
    cdef np.ndarray[object] result = np.full(arr_length, None, dtype="O")
    cdef bint has_errors = False
    cdef np.ndarray[np.uint8_t, cast=True] index

    # index is only necessary if errors="coerce"
    if errors == "coerce":
        index = np.full(arr_length, False)
    else:
        index = None

    # apply `call` at every index of array and record errors
    for i in range(arr_length):
        try:
            result[i] = call(arr[i])
        except (KeyboardInterrupt, MemoryError, SystemError, SystemExit):
            raise  # never coerce on these error types
        except Exception as err:
            if errors == "coerce":
                has_errors = True
                index[i] = True
                continue
            raise err

    return result, has_errors, index
