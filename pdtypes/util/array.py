"""This module describes a suite of vectorized utility functions for use on
numpy arrays and similar objects.  They are generically useful, but intended
only for internal pdtypes use.

`vectorize()` takes a sequence and converts it into a 1D array/series.
`broadcast_args()` takes a sequence of scalars/sequences/arrays and applies
    numpy broadcasting rules to standardize their shapes.
`replace_with_dict()` performs vectorized dictionary lookup.
`round_div()` does optimized integer division, with customizable rounding
`object_types()` applies a vectorized `type` function across an object array's
    constituent elements, returning their types.
"""
from __future__ import annotations

import numpy as np
import pandas as pd

from pdtypes.error import error_trace
from pdtypes.util.type_hints import array_like, scalar


type_ufunc = np.frompyfunc(type, 1, 1)


def vectorize(obj: scalar | array_like) -> np.ndarray | pd.Series:
    """Convert any object into an array-like with at least 1 dimension.
    `pandas.Series` objects are returned immediately.  `numpy.ndarray` objects
    are reshaped to be at least 1 dimensional, but are otherwise unaffected.
    All other inputs are converted to 1D object arrays.
    """
    if isinstance(obj, pd.Series):
        return obj
    if isinstance(obj, np.ndarray):
        return np.atleast_1d(obj)
    if isinstance(obj, set):
        obj = list(obj)
    return np.atleast_1d(np.array(obj, dtype="O"))


def broadcast_args(
    *args: scalar | array_like
) -> tuple[np.ndarray | pd.Series, ...]:
    """Broadcast a set of input arrays/series to equal size.  Series inputs
    are returned as like objects, which reference a memory view of the given
    input.

    Array and series dtypes are preserved during broadcasting.

    None of the objects that are returned by this function should be written to
    without first copying them.  This forces reallocation of the underlying
    memory view and prevents unwanted side-effects.
    """
    # identify which args are series and note their original dtype
    arrays = [vectorize(a) for a in args]
    is_series = [isinstance(a, pd.Series) for a in arrays]
    series_dtypes = [a.dtype if s else None for a, s in zip(arrays, is_series)]

    # broadcast args, then convert back to series where appropriate
    arrays = np.broadcast_arrays(*arrays)
    generator = zip(arrays, is_series, series_dtypes)
    return tuple(pd.Series(a, dtype=d) if s else a for a, s, d in generator)


def replace_with_dict(array: np.ndarray, dictionary: dict) -> np.ndarray:
    """Vectorized dictionary lookup."""
    keys = np.array(list(dictionary))
    vals = np.array(list(dictionary.values()))

    sorted_indices = keys.argsort()

    keys_sorted = keys[sorted_indices]
    vals_sorted = vals[sorted_indices]
    return vals_sorted[np.searchsorted(keys_sorted, array)]


def round_div(
    num: int | np.ndarray | pd.Series,
    div: int | np.ndarray | pd.Series,
    rounding: str = "floor"
) -> int | np.array | pd.Series:
    """Vectorized integer division with customizable rounding."""
    switch = {  # C-style switch statement with lazy evaluation
        # round toward -infinity
        "floor": lambda: num // div,

        # round toward +infinity
        "ceiling": lambda: (num + div - 1) // div,

        # round toward zero
        "down": lambda: (num + ((num < 0) ^ (div < 0)) * (div - 1)) // div,

        # round away from zero
        "up": lambda: (num + ((num > 0) ^ (div < 0)) * (div - 1)) // div,

        # round toward nearest integer, half toward -infinity
        "half_floor": lambda: (num + div // 2 - 1) // div,

        # round toward nearest integer, half toward +infinity
        "half_ceiling": lambda: (num + div // 2) // div,

        # round toward nearest integer, half toward zero
        "half_down": lambda: (num + div // 2 - ((num > 0) ^ (div < 0))) // div,

        # round toward nearest integer, half away from zero
        "half_up": lambda: (num + div // 2 - ((num < 0) ^ (div < 0))) // div,

        # round toward nearest integer, half to even (~5x slower than others)
        "half_even": lambda: (num + div // 2 + (num // div % 2 - 1)) // div
    }
    try:
        return switch[rounding]()
    except KeyError as err:  # error - rounding not recognized
        err_msg = (f"[{error_trace()}] `rounding` must be one of ['floor', "
                   f"'ceiling', 'down', 'up', 'half_floor', 'half_ceiling', "
                   f"'half_down', 'half_up', 'half_even'], not "
                   f"{repr(rounding)}")
        raise ValueError(err_msg) from err


def object_types(array: np.ndarray | pd.Series) -> type | array_like:
    """Get the type of each element in a given object array."""
    if pd.api.types.is_object_dtype(array):
        return type_ufunc(array)
    err_msg = (f"[{error_trace()}] elementwise type detection is only valid "
               f"for object arrays, not {array.dtype}")
    raise TypeError(err_msg)
