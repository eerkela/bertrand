from __future__ import annotations


import numpy as np
import pandas as pd

from pdtypes.error import error_trace


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
    arrays = []
    is_series = []
    dtypes = []
    for arg in args:
        if isinstance(arg, np.ndarray):  # make 1D, preserving dtype
            arrays.append(np.atleast_1d(arg))
            is_series.append(False)
            dtypes.append(None)
        elif isinstance(arg, pd.Series):  # mark as series and note dtype
            arrays.append(arg)
            is_series.append(True)
            dtypes.append(arg.dtype)
        else:  # convert to 1D array with dtype="O"
            arrays.append(np.atleast_1d(np.array(arg, dtype="O")))
            is_series.append(False)
            dtypes.append(None)
    arrays = np.broadcast_arrays(*arrays)
    return [pd.Series(a, dtype=d) if s else a
            for a, s, d in zip(arrays, is_series, dtypes)]


def replace_with_dict(array: np.ndarray, dictionary: dict) -> np.ndarray:
    """Vectorized dictionary lookup."""
    keys = np.array(list(dictionary))
    vals = np.array(list(dictionary.values()))

    sorted_indices = keys.argsort()

    keys_sorted = keys[sorted_indices]
    vals_sorted = vals[sorted_indices]
    return vals_sorted[np.searchsorted(keys_sorted, array)]


def round_div(
    val: int | list | np.ndarray | pd.Series,
    divisor: int | np.ndarray | pd.Series,
    rounding: str = "floor"
) -> np.ndarray:
    """Divide integers and integer arrays with specified rounding rule."""
    # vectorize input
    val = np.atleast_1d(np.array(val))
    divisor = np.atleast_1d(np.array(divisor))

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
