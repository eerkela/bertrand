from __future__ import annotations

import numpy as np
import pandas as pd

from pdtypes.check import is_dtype, resolve_dtype
from pdtypes.error import error_trace


def integral_range(dtype: dtype_like) -> tuple[int, int]:
    """Get the integral range of a given integer, float, or complex dtype."""
    dtype = resolve_dtype(dtype)

    # integer case
    if is_dtype(dtype, int):
        # convert to pandas dtype to expose .itemsize attribute
        dtype = pd.api.types.pandas_dtype(dtype)
        bit_size = 8 * dtype.itemsize
        if pd.api.types.is_unsigned_integer_dtype(dtype):
            return (0, 2**bit_size - 1)
        return (-2**(bit_size - 1), 2**(bit_size - 1) - 1)

    # float case
    if is_dtype(dtype, float):
        significand_bits = {
            np.float16: 11,
            np.float32: 24,
            float: 53,
            np.float64: 53,
            np.longdouble: 64
        }
        extreme = 2**significand_bits[dtype]
        return (-extreme, extreme)

    # complex case
    if is_dtype(dtype, complex):
        significand_bits = {
            np.complex64: 24,
            complex: 53,
            np.complex128: 53,
            np.clongdouble: 64
        }
        extreme = 2**significand_bits[dtype]
        return (-extreme, extreme)

    # unrecognized
    err_msg = (f"[{error_trace()}] `dtype` must be int, float, or "
               f"complex-like, not {dtype}")
    raise TypeError(err_msg)
