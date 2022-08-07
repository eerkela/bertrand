from __future__ import annotations
import decimal

import numba
import numpy as np
import pandas as pd

from pdtypes.cast.float import FloatSeries
from pdtypes.cast.helpers import (
    downcast_int_dtype, integral_range, SeriesWrapper, integral_range
)
from pdtypes.check import (
    check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
)
from pdtypes.error import error_trace, shorten_list
from pdtypes.util.type_hints import array_like, dtype_like



class IntegerSeries(SeriesWrapper):
    """test"""

    def __init__(
        self,
        series: int | array_like,
        validate: bool = True
    ) -> IntegerSeries:
        if validate and not check_dtype(series, int):
            err_msg = (f"[{error_trace()}] `series` must contain integer "
                       f"data, not {get_dtype(series)}")
            raise TypeError(err_msg)

        super().__init__(series)
        self._min_val = None
        self._max_val = None

    @property
    def min_val(self) -> int:
        """test"""
        if self._min_val is not None:  # min_val is cached
            return self._min_val

        # compute min_val
        self._min_val = self.series.min()
        return self._min_val

    @property
    def max_val(self) -> int:
        """test"""
        if self._max_val is not None:  # max_val is cached
            return self._max_val

        # compute max_val
        self._max_val = self.series.max()
        return self._max_val

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        errors: str = "raise"
    ) -> pd.Series:
        """_summary_

        Args:
            dtype (dtype_like, optional): _description_. Defaults to bool.
            errors (str, optional): _description_. Defaults to "raise".

        Returns:
            pd.Series: _description_
        """
        # validate input
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, bool)
        SeriesWrapper._validate_errors(errors)

        series = self.series.copy()

        # check series fits within boolean range [0, 1, NA]
        if self.min_val < 0 or self.max_val > 1:
            if errors == "raise":
                bad = series[(series < 0) | (series > 1)].index.values
                err_msg = (f"[{error_trace()}] non-boolean value encountered "
                           f"at index {shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return self.series
            series = series.fillna(pd.NA).abs().clip(0, 1)

        # convert to extension type if series contains missing values
        if self.hasnans:
            dtype = extension_type(dtype)
        return series.astype(dtype, copy=False)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, int)
        SeriesWrapper._validate_errors(errors)

        # copy base parameters
        series = self.series.copy()
        min_val = self.min_val
        max_val = self.max_val
        hasnans = self.hasnans

        # built-in integer special case - can be arbitrarily large
        if is_dtype(dtype, int, exact=True):
            if min_val < -2**63 or max_val > 2**63 - 1:  # >int64
                if min_val >= 0 and max_val <= 2**64 - 1:  # <uint64
                    dtype = pd.UInt64Dtype() if self.hasnans else np.uint64
                    return series.astype(dtype, copy=False)
                # series is >int64 and >uint64, return as built-in python ints
                series = series.astype("O", copy=False)
                return series.fillna(pd.NA, inplace=True)
            dtype = np.int64  # extended range isn't needed, demote to int64

        # check whether min_val, max_val fit within `dtype` range
        min_poss, max_poss = integral_range(dtype)
        if min_val < min_poss or max_val > max_poss:
            if errors == "ignore":
                return self.series
            indices = (series < min_poss) | (series > max_poss)
            if errors == "raise":
                bad = series[indices].index.values
                err_msg = (f"[{error_trace()}] values exceed {dtype.__name__} "
                           f"range at index {shorten_list(bad)}")
                raise OverflowError(err_msg)
            series[indices] = pd.NA
            min_val = series.min()
            max_val = series.max()
            hasnans = True

        # attempt to downcast, if applicable
        if downcast:
            dtype = downcast_int_dtype(dtype, min_val, max_val)

        # convert and return
        if hasnans:
            dtype = extension_type(dtype)
        return series.astype(dtype, copy=False)

    def to_float(
        self,
        dtype: dtype_like = float,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, float)
        SeriesWrapper._validate_errors(errors)

        # astype(float) is unstable for object series with pd.NA as missing val
        series = self.series.copy()
        if pd.api.types.is_object_dtype(series):
            series[~self.not_na] = np.nan
        series = series.astype(dtype, copy=False)

        # check for precision loss.  Can only occur if series vals are outside
        # integral range of `dtype`, as determined by # of bits in significand
        min_precise, max_precise = integral_range(dtype)
        if self.min_val < min_precise or self.max_val > max_precise:
            # series might be imprecise -> confirm by reversing conversion
            reverse = FloatSeries(series, validate=False)
            if reverse.hasinfs:  # infs introduced during coercion (overflow)
                if errors == "raise":
                    bad = series[reverse.infs].index.values
                    err_msg = (f"[{error_trace()}] values exceed "
                               f"{dtype.__name__} range at index "
                               f"{shorten_list(bad)}")
                    raise OverflowError(err_msg)
                if errors == "ignore":
                    return self.series
                series[reverse.infs] = np.nan
            elif errors != "coerce":  # check for precision loss
                reverse_result = reverse.to_integer(errors="coerce")
                if not self.to_integer().equals(reverse_result):
                    if errors == "ignore":
                        return self.series
                    bad = series[(self.series != reverse_result) &
                                 self.not_na].index.values
                    err_msg = (f"[{error_trace()}] precision loss detected at "
                               f"index {shorten_list(bad)} with dtype "
                               f"{repr(dtype.__name__)}")
                    raise ValueError(err_msg)

        # return
        if downcast:
            return FloatSeries(series, validate=False).to_float(downcast=True)
        return series

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, complex)
        SeriesWrapper._validate_errors(errors)

        # astype(complex) is unstable for object series with pd.NA as missing
        series = self.series.copy()
        if pd.api.types.is_object_dtype(series):
            series[~self.not_na] = np.nan
        series = series.astype(dtype, copy=False)

        # TODO: missing values should be (nan+nanj)

        # separate into real and imaginary components
        # TODO: this should be part of ComplexSeries
        real = np.real(series)
        imag = np.imag(series)

        # TODO: change standard back to precision loss

        # check for infinities introduced by coercion
        overflow = np.isinf(real) | np.isinf(imag)
        if overflow.any():
            if errors == "raise":
                bad = series[overflow].index.values
                err_msg = (f"[{error_trace()}] values exceed {dtype.__name__} "
                           f"range at index {shorten_list(bad)}")
                raise OverflowError(err_msg)
            if errors == "ignore":
                return self.series
            series[overflow] = complex(np.nan, np.nan)

        # return
        if downcast:
            return _downcast_complex_series(series, real, imag)
        return series

    def to_decimal(self) -> pd.Series:
        """test"""
        # subset to avoid missing values
        with self.exclude_na(pd.NA) as ctx:
            conv = lambda x: decimal.Decimal(int(x))
            ctx.subset = np.frompyfunc(conv, 1, 1)(ctx.subset)
        return ctx.result

    def to_string(self, dtype: dtype_like = pd.StringDtype()) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, str)

        # TODO: because resolve_dtype doesn't recognize extension types, this
        # always downcasts to str objects rather than pd.StringDtype()

        # do conversion
        if self.hasnans:
            dtype = extension_type(dtype)
        return self.series.astype(dtype, copy=True)
