from __future__ import annotations

import numpy as np
import pandas as pd

from pdtypes.cast.helpers import SeriesWrapper
from pdtypes.check import extension_type, resolve_dtype
from pdtypes.util.type_hints import array_like, dtype_like


class EmptySeries:
    """test"""

    def __init__(self, series: None | array_like, nans, validate) -> EmptySeries:
        self.series = series

    def to_boolean(self, dtype: dtype_like = pd.BooleanDtype()) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, bool)

        dtype = pd.BooleanDtype() if dtype == bool else pd.BooleanDtype()
        series = np.full(self.series.shape, pd.NA, dtype="O")
        series = pd.Series(series, dtype=dtype)
        series.index = self.series.index
        return series

    def to_integer(self, dtype: dtype_like = pd.Int64Dtype()) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, int)

        dtype = pd.Int64Dtype() if dtype == int else extension_type(dtype)
        series = np.full(self.series.shape, pd.NA, dtype="O")
        series = pd.Series(series, dtype=dtype)
        series.index = self.series.index
        return series

    def to_float(self, dtype: dtype_like = np.float64) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, float)

        dtype = np.float64 if dtype == float else dtype
        series = pd.Series(np.full(self.series.shape, np.nan, dtype=dtype))
        series.index = self.series.index
        return series

    def to_complex(self, dtype: dtype_like = np.complex128) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, complex)

        dtype = np.complex128 if dtype == complex else dtype
        series = pd.Series(np.full(self.series.shape, None, dtype=dtype))
        series.index = self.series.index
        return series

    def to_decimal(self) -> pd.Series:
        """test"""
        series = pd.Series(np.full(self.series.shape, pd.NA, dtype="O"))
        series.index = self.series.index
        return series

    def to_datetime(self) -> pd.Series:
        """test"""
        series = pd.Series(np.full(self.series.shape, None, dtype="M8[ns]"))
        series.index = self.series.index
        return series

    def to_timedelta(self) -> pd.Series:
        """test"""
        series = pd.Series(np.full(self.series.shape, None, dtype="m8[ns]"))
        series.index = self.series.index
        return series

    def to_string(self) -> pd.Series:
        """test"""
        series = np.full(self.series.shape, pd.NA, dtype="O")
        series = pd.Series(series, dtype=pd.StringDtype())
        series.index = self.series.index
        return series
