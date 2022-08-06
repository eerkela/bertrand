from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

from pdtypes.cast.float import FloatSeries
from pdtypes.cast.helpers import (
    downcast_int_dtype, int_dtype_range, SeriesWrapper
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
        self.min_val = self.series.min()
        self.max_val = self.series.max()

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

        # copy base series
        series = self.series.copy()
        min_val = self.min_val
        max_val = self.max_val

        # built-in integer special case - can be arbitrarily large
        if is_dtype(dtype, int, exact=True):

            # check if series exceeds int64 range
            if min_val < -2**63 or max_val > 2**63 - 1:

                # check if series fits within uint64 range
                if min_val >= 0 and max_val <= 2**64 - 1:
                    dtype = pd.UInt64Dtype() if self.hasnans else np.uint64
                    return series.astype(dtype, copy=False)

                # series is >int64 and >uint64, return as built-in python ints
                series = series.astype("O", copy=False)
                return series.fillna(pd.NA, inplace=True)

            # python extended integer range isn't needed, reduce to int64
            dtype = np.int64

        # check whether result fits within available range for specified `dtype`
        min_poss, max_poss = int_dtype_range(dtype)
        if min_val < min_poss or max_val > max_poss:
            if errors == "ignore":
                return self.series
            indices = (series < min_poss) | (series > max_poss)
            if errors == "raise":
                bad = series[indices].index.values
                err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                           f"index {shorten_list(bad)}")
                raise OverflowError(err_msg)
            series[indices] = pd.NA
            min_val = series.min()
            max_val = series.max()

        # attempt to downcast if applicable
        if downcast:
            dtype = downcast_int_dtype(dtype, min_val, max_val)

        # convert and return
        if self.hasnans:
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

        # do conversion
        series = self.series.astype(dtype, copy=True)
        reverse = FloatSeries(series)

        # check for precision loss
        back_to_integer = reverse.to_integer(errors="coerce")
        if (self.series - back_to_integer).any():
            if errors == "raise":
                indices = (self.series != back_to_integer) ^ ~self.not_na
                bad = series[indices].index.values
                err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                           f"index {shorten_list(bad)}")
                raise OverflowError(err_msg)
            if errors == "ignore":
                return self.series
            series[reverse.infs] = np.nan
            reverse = FloatSeries(series)

        # return
        if downcast:
            return reverse.to_float(downcast=True)
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

        # do conversion
        series = self.series.astype(dtype, copy=True)
        real = np.real(series)
        imag = np.imag(series)

        # TODO: change standard back to precision loss

        # check for infinities introduced by coercion
        overflow = np.isinf(real) | np.isinf(imag)
        if overflow.any():
            if errors == "raise":
                bad = series[overflow].index.values
                err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                           f"index {shorten_list(bad)}")
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
