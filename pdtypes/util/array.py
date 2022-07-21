from __future__ import annotations

import numpy as np
import pandas as pd

from pdtypes.error import error_trace
from pdtypes.util.type_hints import array_like


type_ufunc = np.frompyfunc(type, 1, 1)


def vectorize(obj) -> np.ndarray | pd.Series:
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
    *args: np.ndarray | pd.Series
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
    return [pd.Series(a, dtype=d) if s else a for a, s, d in generator]


def replace_with_dict(array: np.ndarray, dictionary: dict) -> np.ndarray:
    """Vectorized dictionary lookup."""
    keys = np.array(list(dictionary))
    vals = np.array(list(dictionary.values()))

    sorted_indices = keys.argsort()

    keys_sorted = keys[sorted_indices]
    vals_sorted = vals[sorted_indices]
    return vals_sorted[np.searchsorted(keys_sorted, array)]


def round_div(
    val: int | array_like,
    divisor: int | array_like,
    rounding: str = "floor"
) -> np.ndarray:
    """Divide integers and integer arrays with specified rounding rule."""
    # vectorize input
    val = vectorize(val)
    divisor = vectorize(divisor)

    # broadcast to same size
    val, divisor = np.broadcast_arrays(val, divisor)

    # round towards -infinity
    if rounding == "floor":
        return val // divisor

    # round towards zero
    if rounding == "truncate":
        neg = (val < 0) ^ (divisor < 0)
        result = val.copy()
        result[neg] = (val[neg] + divisor[neg] - 1) // divisor[neg]  # ceiling
        result[~neg] //= divisor[~neg]  # floor
        return result

    # round towards closest integer
    if rounding == "round":
        return (val + divisor // 2) // divisor

    # round towards +infinity
    if rounding == "ceiling":
        return (val + divisor - 1) // divisor

    # error - rounding not recognized
    err_msg = (f"[{error_trace()}] `rounding` must be one of ['floor', "
               f"'truncate', 'round', 'ceiling'], not {repr(rounding)}")
    raise ValueError(err_msg)


def object_types(array: np.ndarray | pd.Series) -> np.array | pd.Series:
    """Get the type of each element in a given object array."""
    if pd.api.types.is_object_dtype(array):
        return type_ufunc(array)
    err_msg = (f"[{error_trace()}] elementwise type detection is only valid "
               f"for object arrays, not {array.dtype}")
    raise TypeError(err_msg)
