"""Simple Cython loops for typing-related operations on numpy arrays.

Further reading:
    https://blog.paperspace.com/faster-np-array-processing-ndarray-cython/
"""
cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from .resolve cimport _resolve_dtype


cdef dict atomic_to_extension_type = {
    # maps atomic types to their associated pandas extension type, if they
    # have one.

    # boolean
    bool: pd.BooleanDtype(),

    # integer
    np.int8: pd.Int8Dtype(),
    np.int16: pd.Int16Dtype(),
    np.int32: pd.Int32Dtype(),
    np.int64: pd.Int64Dtype(),
    np.uint8: pd.UInt8Dtype(),
    np.uint16: pd.UInt16Dtype(),
    np.uint32: pd.UInt32Dtype(),
    np.uint64: pd.UInt64Dtype(),

    # string
    str: pd.StringDtype()  # TODO: replace with DEFAULT_STRING_DTYPE
}


@cython.boundscheck(False)
@cython.wraparound(False)
@cython.nonecheck(False)
cdef object _extension_type(object dtype):
    """Internal C interface for public-facing extension_type() function."""
    dtype = _resolve_dtype(dtype)
    return atomic_to_extension_type.get(dtype, dtype)


@cython.boundscheck(False)
@cython.wraparound(False)
@cython.nonecheck(False)
def extension_type(object dtype) -> type | pd.api.extensions.ExtensionDtype:
    """Return the pandas extension type associated with a given dtype.  If none
    exists, return the original (resolved) dtype.

    Pandas offers a selection of extension types that allow for nullable
    integers/booleans and custom string storage backends.  This function allows
    for simple lookup in the event that such extension types are needed.
    """
    return _extension_type(dtype)
