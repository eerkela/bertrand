from __future__ import annotations
from typing import Union

import numpy as np
import pandas as pd

from pdtypes.error import error_trace


dtype_like = Union[type, str, np.dtype, pd.api.extensions.ExtensionDtype]
_extension_types = {
    bool: pd.BooleanDtype(),
    np.int8: pd.Int8Dtype(),
    np.int16: pd.Int16Dtype(),
    np.int32: pd.Int32Dtype(),
    np.int64: pd.Int64Dtype(),
    np.uint8: pd.UInt8Dtype(),
    np.uint16: pd.UInt16Dtype(),
    np.uint32: pd.UInt32Dtype(),
    np.uint64: pd.UInt64Dtype(),
    str: pd.StringDtype(),
    np.dtype(bool): pd.BooleanDtype(),
    np.dtype(np.int8): pd.Int8Dtype(),
    np.dtype(np.int16): pd.Int16Dtype(),
    np.dtype(np.int32): pd.Int32Dtype(),
    np.dtype(np.int64): pd.Int64Dtype(),
    np.dtype(np.uint8): pd.UInt8Dtype(),
    np.dtype(np.uint16): pd.UInt16Dtype(),
    np.dtype(np.uint32): pd.UInt32Dtype(),
    np.dtype(np.uint64): pd.UInt64Dtype()
}


_dtype_to_atomic_type = {
    np.dtype(bool): bool,
    pd.BooleanDtype(): bool,
    np.dtype(np.int8): np.int8,
    pd.Int8Dtype(): np.int8,
    np.dtype(np.int16): np.int16,
    pd.Int16Dtype(): np.int16,
    np.dtype(np.int32): np.int32,
    pd.Int32Dtype(): np.int32,
    np.dtype(np.int64): np.int64,
    pd.Int64Dtype(): np.int64,
    np.dtype(np.uint8): np.uint8,
    pd.UInt8Dtype(): np.uint8,
    np.dtype(np.uint16): np.uint16,
    pd.UInt16Dtype(): np.uint16,
    np.dtype(np.uint32): np.uint32,
    pd.UInt32Dtype(): np.uint32,
    np.dtype(np.uint64): np.uint64,
    pd.UInt64Dtype(): np.uint64,
    np.dtype(np.float16): np.float16,
    np.dtype(np.float32): np.float32,
    np.dtype(np.float64): np.float64,
    np.dtype(np.longdouble): np.longdouble,
    np.dtype(np.complex64): np.complex64,
    np.dtype(np.complex128): np.complex128,
    np.dtype(np.clongdouble): np.clongdouble,
    np.dtype("M8[ns]"): pd.Timestamp,
    np.dtype("<M8[ns]"): pd.Timestamp,
    np.dtype(">M8[ns]"): pd.Timestamp,
    np.dtype("m8[ns]"): pd.Timedelta,
    np.dtype(">m8[ns]"): pd.Timedelta,
    np.dtype("<m8[ns]"): pd.Timedelta,
    pd.StringDtype(): str
}


def is_integer_series(series: int | list | np.ndarray | pd.Series) -> bool:
    """Check if a series contains integer data in any form."""
    # vectorize
    series = pd.Series(series)

    # option 1: series is properly formatted -> check dtype directly
    if pd.api.types.is_integer_dtype(series):
        return True

    # series has object dtype -> attempt to infer
    if pd.api.types.is_object_dtype(series):
        return pd.api.types.infer_dtype(series) == "integer"

    # series has some other dtype -> not integer
    return False


def get_dtype(series: int | list | np.ndarray | pd.Series) -> np.ndarray:
    """Get the types of elements present in the given series."""
    # TODO: fix type annotations
    # vectorize
    series = pd.Series(series).infer_objects()

    # check if series has object dtype
    if pd.api.types.is_object_dtype(series):
        type_ufunc = np.frompyfunc(type, 1, 1)
        if series.hasnans:  # do not count missing values
            series = series[series.notna()]
        return type_ufunc(series).unique()

    # series does not have object dtype
    atomic_type = _dtype_to_atomic_type.get(series.dtype, series.dtype)
    return np.array([atomic_type])




# def is_integer_series(
#     series: int | list | np.ndarray | pd.Series,
#     dtype: dtype_like | tuple[dtype_like, ...] = int
# ) -> bool:
#     """Check whether a series contains integer data of the specified dtype.
    
#     Supertype: int, "int", "integer", "i"
#     Subtype: np.int8, np.int16, np.int32, np.int64, np.uint8, np.uint16
#              np.uint32, np.uint64, pd.Int8Dtype(), pd.Int16Dtype(),
#              pd.Int32Dtype(), pd.Int64Dtype(), pd.UInt8Dtype(),
#              pd.UInt16Dtype(), pd.UInt32Dtype(), pd.UInt64Dtype()
#     """
#     # TODO: allow tuples of dtypes, like isinstance
#     supertype = (int, "int", "i", "integer")

#     # vectorize
#     series = pd.Series(series)

#     # option 1: series has integer dtype without extra processing
#     if pd.api.types.is_integer_dtype(series):
#         if dtype in supertype:
#             return True
#         return pd.api.types.pandas_dtype(dtype) == series.dtype

#     # series has object dtype
#     if pd.api.types.is_object_dtype(series):
#         # option 2: consistent non-extension dtype with missing values
#         nans = pd.isna(series)
#         subset = series[~nans].infer_objects()
#         if pd.api.types.is_integer_dtype(subset):
#             if dtype in supertype:
#                 return True
#             if pd.api.types.is_extensionarray_dtype(dtype):
#                 extension_type = _extension_types[subset.dtype]
#                 return pd.api.types.pandas_dtype(dtype) == extension_type
#             return False

#         # option 3: mixed dtypes
#         type_ufunc = np.frompyfunc(type, 1, 1)
#         mixed_types = type_ufunc(subset)
        

#     return False


