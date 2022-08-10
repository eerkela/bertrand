from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

from pdtypes.cast.float import FloatSeries
from pdtypes.cast.helpers import integral_range, SeriesWrapper
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
        nans: None | bool | array_like = None,
        validate: bool = True
    ) -> IntegerSeries:
        if validate and not check_dtype(series, int):
            err_msg = (f"[{error_trace()}] `series` must contain integer "
                       f"data, not {get_dtype(series)}")
            raise TypeError(err_msg)

        super().__init__(series, nans)
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
                # TODO: astype("O") doesn't actually coerce to python integer
                series = series.astype("O", copy=False)
                series[self.is_na] = pd.NA
                return series
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
            if is_dtype(dtype, "unsigned"):
                int_types = [np.uint8, np.uint16, np.uint32, np.uint64]
            else:
                int_types = [np.int8, np.int16, np.int32, np.int64]
            for downcast_type in int_types[:int_types.index(dtype)]:
                min_poss, max_poss = integral_range(downcast_type)
                if min_val >= min_poss and max_val <= max_poss:
                    dtype = downcast_type
                    break

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
        if dtype == float:  # built-in `float` is identical to np.float64
            dtype = np.float64

        # astype(float) is unstable for object series with pd.NA as missing val
        series = self.series.copy()
        if pd.api.types.is_object_dtype(series):
            series[self.is_na] = np.nan
        series = series.astype(dtype, copy=False)

        # check for precision loss.  Can only occur if series vals are outside
        # integral range of `dtype`, as determined by # of bits in significand
        min_precise, max_precise = integral_range(dtype)
        if self.min_val < min_precise or self.max_val > max_precise:
            # series might be imprecise -> confirm by reversing conversion
            reverse = FloatSeries(series, validate=False, nans=self.is_na)
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
                    bad = series[(self.series != reverse_result) ^
                                 self.is_na].index.values
                    err_msg = (f"[{error_trace()}] precision loss detected at "
                               f"index {shorten_list(bad)} with dtype "
                               f"{repr(dtype.__name__)}")
                    raise ValueError(err_msg)

        # return
        if downcast:
            float_types = [np.float16, np.float32, np.float64, np.longdouble]
            for downcast_type in float_types[:float_types.index(dtype)]:
                attempt = series.astype(downcast_type, copy=False)
                if not (attempt - series).any():
                    return attempt
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
        if dtype == complex:  # built-in complex is identical to np.complex128
            dtype = np.complex128

        # astype(complex) is unstable when used on pd.NA
        series = self.series.copy()
        if self.hasnans:
            if pd.api.types.is_object_dtype(series):  # replace nans directly
                series[self.is_na] = np.nan
            else:  # cast to float, then to complex
                float_equivalent = {
                    np.complex64: np.float32,
                    np.complex128: np.float64,
                    np.clongdouble: np.longdouble
                }
                series = series.astype(float_equivalent[dtype], copy=False)
        series = series.astype(dtype, copy=False)
        series[self.is_na] += complex(0, np.nan)  # (nan+0j) -> (nan+nanj)

        # check for precision loss.  Can only occur if series vals are outside
        # integral range of `dtype`, as determined by # of bits in significand
        min_precise, max_precise = integral_range(dtype)
        if self.min_val < min_precise or self.max_val > max_precise:
            # series might be imprecise -> confirm by reversing conversion
            reverse = FloatSeries(np.real(series), validate=False,
                                  nans=self.is_na)
            if reverse.hasinfs:  # infs introduced during coercion (overflow)
                if errors == "raise":
                    bad = series[reverse.infs].index.values
                    err_msg = (f"[{error_trace()}] values exceed "
                               f"{dtype.__name__} range at index "
                               f"{shorten_list(bad)}")
                    raise OverflowError(err_msg)
                if errors == "ignore":
                    return self.series
                series[reverse.infs] += complex(np.nan, np.nan)
            elif errors != "coerce":  # check for precision loss
                reverse_result = reverse.to_integer(errors="coerce")
                if not self.to_integer().equals(reverse_result):
                    if errors == "ignore":
                        return self.series
                    bad = series[(self.series != reverse_result) ^
                                 self.is_na].index.values
                    err_msg = (f"[{error_trace()}] precision loss detected at "
                               f"index {shorten_list(bad)} with dtype "
                               f"{repr(dtype.__name__)}")
                    raise ValueError(err_msg)

        # return
        if downcast:
            complex_types = [np.complex64, np.complex128, np.clongdouble]
            for downcast_type in complex_types[:complex_types.index(dtype)]:
                attempt = series.astype(downcast_type, copy=False)
                if not (attempt - series).any():
                    return attempt
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
