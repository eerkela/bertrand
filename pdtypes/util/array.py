"""This module describes a suite of vectorized utility functions for use on
numpy arrays and similar objects.  They are generically useful, but intended
only for internal pdtypes use.

`vectorize()` takes a sequence and converts it into a 1D array/series.
`broadcast_args()` takes a sequence of scalars/sequences/arrays and applies
    numpy broadcasting rules to standardize their shapes.
`replace_with_dict()` performs vectorized dictionary lookup.
`round_div()` does optimized integer division, with customizable rounding
"""
from __future__ import annotations
from typing import Any, Sequence

import numpy as np
import pandas as pd

from pdtypes.error import error_trace
from pdtypes.util.type_hints import array_like, scalar


def is_scalar(arg: Any) -> bool:
    """Return `True` if `arg` is scalar and `False` if it is array-like."""
    return not np.array(arg).shape


def vectorize(obj: scalar | array_like) -> np.ndarray | pd.Series:
    """Construct an array-like from `obj`.

    If `obj` is scalar, this will construct a 1-dimensional `ndarray` with
    `dtype='O'` and a single element - the original object.  If `obj` is
    list-like, the resulting `ndarray` will have dimensions equal to those of
    the input list.

    If `obj` is already array-like (`np.ndarray` or `pd.Series`), then it is
    returned as-is, with scalar `ndarray`s reshaped to be at least
    1-dimensional.
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
    """Broadcast a set of input arrays/series to equal shape.

    Each input is returned unmodified except for its shape, which is
    standardized according to ordinary numpy broadcasting rules.  If a series
    is provided as input, it is returned as such, with the same properties
    as an array equivalent.  In both cases, array and series dtypes are
    preserved during broadcasting.

    Care should be taken when modifying the objects that are returned by this
    function.  Since each one references a memory view on the original object,
    any changes that are made will propagate to the original.  If one of these
    objects needs to be written to, it is standard practice to copy it first
    using `.copy()`.  This forces reallocation of the underlying memory view
    and prevents unwanted side effects from propagating, at the cost of
    increased overall memory usage.
    """
    # vectorize args
    args = [vectorize(a) for a in args]
    target_shape = np.broadcast_shapes(*[a.shape for a in args])

    # build up result
    result = []
    for array in args:
        if isinstance(array, pd.Series):
            dtype = array.dtype
            index = np.broadcast_to(array.index, target_shape)
            array = np.broadcast_to(array, target_shape)
            result.append(pd.Series(array, dtype=dtype, index=index))
        else:
            result.append(np.broadcast_to(array, target_shape))
    return tuple(result)


def replace_with_dict(
    array: np.ndarray,
    dictionary: dict
) -> np.ndarray:
    """Performs vectorized dictionary lookup, using the provided table.  The
    keys in `dictionary` must be sortable.
    """
    # TODO: this would be a lot easier/more memory efficient/flexible in
    # cython.  Take generic np.ndarray and dict as input.
    keys = np.array(list(dictionary))
    vals = np.array(list(dictionary.values()))

    sorted_indices = keys.argsort()

    keys_sorted = keys[sorted_indices]
    vals_sorted = vals[sorted_indices]
    return vals_sorted[np.searchsorted(keys_sorted, array)]
