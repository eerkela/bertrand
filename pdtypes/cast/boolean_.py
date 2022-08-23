from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

from ..check.check import check_dtype, get_dtype, is_dtype, resolve_dtype
from ..error import error_trace
from ..util.type_hints import array_like, dtype_like

from .helpers import _validate_dtype, DEFAULT_STRING_TYPE


class BooleanSeries:
    """test"""

    def __init__(
        self,
        series: bool | array_like,
        validate: bool = True
    ) -> BooleanSeries:
        if validate and not check_dtype(series, bool):
            err_msg = (f"[{error_trace()}] `series` must contain boolean "
                       f"data, not {get_dtype(series)}")
            raise TypeError(err_msg)

        self.series = series

    def to_boolean(
        self,
        dtype: dtype_like = bool
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)  # TODO: erases extension type
        _validate_dtype(dtype, bool)
        return self.series.astype(dtype, copy=True)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        _validate_dtype(dtype, int)

        # if `downcast=True`, return as 8-bit integer dtype
        if downcast:
            dtype = np.uint8 if is_dtype(dtype, "u") else np.int8
        return self.series.astype(dtype, copy=True)

    def to_float(
        self,
        dtype: dtype_like = float,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        _validate_dtype(dtype, float)

        # if `downcast=True`, return as float16
        if downcast:
            dtype = np.float16
        return self.series.astype(dtype, copy=True)

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        _validate_dtype(dtype, complex)

        # if `downcast=True`, return as complex64
        if downcast:
            dtype = np.complex64
        return self.series.astype(dtype, copy=True)

    def to_decimal(self) -> pd.Series:
        """test"""
        return self.series + decimal.Decimal(0)

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """test"""
        resolve_dtype(dtype)  # ensures scalar, resolvable
        _validate_dtype(dtype, str)

        # force string extension type
        if not pd.api.types.is_extension_array_dtype(dtype):
            dtype = DEFAULT_STRING_TYPE

        return self.series.astype(dtype, copy=True)
