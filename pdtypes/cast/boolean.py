from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

from pdtypes.cast.helpers import SeriesWrapper
from pdtypes.check import (
    check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
)
from pdtypes.error import error_trace
from pdtypes.util.type_hints import array_like, dtype_like


class BooleanSeries(SeriesWrapper):
    """test"""

    def __init__(
        self,
        series: bool | array_like,
        nans: None | bool | array_like = None,
        validate: bool = True
    ) -> BooleanSeries:
        if validate and not check_dtype(series, bool):
            err_msg = (f"[{error_trace()}] `series` must contain boolean "
                       f"data, not {get_dtype(series)}")
            raise TypeError(err_msg)

        super().__init__(series, nans)

    def to_boolean(
        self,
        dtype: dtype_like = bool
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, bool)

        if self.hasnans:
            dtype = extension_type(dtype)
        return self.series.astype(dtype, copy=True)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, int)

        series = self.series.copy()

        # if series has missing values, convert to extension type first
        if self.hasnans and pd.api.types.is_object_dtype(series):
            series = series.astype(pd.BooleanDtype(), copy=False)

        # if `downcast=True`, return as 8-bit integer dtype
        if downcast:
            if pd.api.types.is_unsigned_integer_dtype(dtype):
                if self.hasnans:
                    return series.astype(pd.UInt8Dtype(), copy=False)
                return series.astype(np.uint8, copy=False)
            if self.hasnans:
                return series.astype(pd.Int8Dtype(), copy=False)
            return series.astype(np.int8, copy=False)

        # no extension type for built-in python integers
        if is_dtype(dtype, int, exact=True):
            dtype = np.int64
        if self.hasnans:
            dtype = extension_type(dtype)
        return series.astype(dtype)

    def to_float(
        self,
        dtype: dtype_like = float,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, float)

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
        SeriesWrapper._validate_dtype(dtype, complex)

        # astype(complex) is unstable when converting missing values
        dtype = np.complex64 if downcast else dtype
        with self.exclude_na(complex(np.nan, np.nan), dtype=dtype) as ctx:
            ctx.subset = ctx.subset.astype(dtype, copy=False)
        return ctx.result

    def to_decimal(self) -> pd.Series:
        """test"""
        with self.exclude_na(pd.NA) as ctx:
            ctx.subset += decimal.Decimal(0)
        return ctx.result

    def to_string(
        self,
        dtype: dtype_like = pd.StringDtype()
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, str)

        # use pandas string extension type, if applicable
        if self.hasnans:
            dtype = pd.StringDtype()
        return self.series.astype(dtype, copy=True)
