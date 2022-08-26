from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE

from pdtypes.check import (
    check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
)
from pdtypes.error import ConversionError, error_trace, shorten_list
from pdtypes.util.downcast import integral_range
from pdtypes.util.type_hints import array_like, dtype_like
from pdtypes.util.validate import validate_dtype, validate_errors

from .float import FloatSeries


class IntegerSeries:
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

        self.series = series
        self.min = self.series.min()
        self.max = self.series.max()

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        # validate input
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, bool)
        validate_errors(errors)

        # check series fits within boolean range [0, 1]
        if self.min < 0 or self.max > 1:
            if errors != "coerce":
                bad_vals = series[(series < 0) | (series > 1)]
                err_msg = (f"non-boolean value encountered at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series = self.series.abs().clip(0, 1)  # coerce
        else:
            series = self.series

        return series.astype(dtype)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, int)
        validate_errors(errors)

        # copy base parameters
        series = self.series.copy()
        min_val = self.min
        max_val = self.max

        # built-in integer special case - can be arbitrarily large
        if is_dtype(dtype, int, exact=True):
            if min_val < -2**63 or max_val > 2**63 - 1:  # >int64
                if min_val >= 0 and max_val <= 2**64 - 1:  # <uint64
                    return series.astype(np.uint64)
                # >int64 and >uint64, return as built-in python ints
                return np.frompyfunc(int, 1, 1)(series)
            # extended range isn't needed, demote to int64
            dtype = np.int64

        # NAs may be introduced by coercion
        coerced = False

        # ensure min_val, max_val fit within `dtype` range
        min_poss, max_poss = integral_range(dtype)
        if min_val < min_poss or max_val > max_poss:
            if errors != "coerce":
                bad_vals = series[(series < min_poss) | (series > max_poss)]
                err_msg = (f"values exceed {dtype.__name__} range at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series[(series < min_poss) | (series > max_poss)] = pd.NA  # coerce
            min_val = series.min()
            max_val = series.max()
            coerced = True  # remember to convert to extension type later

        # attempt to downcast, if applicable
        if downcast:  # search for smaller dtypes that can represent series
            if is_dtype(dtype, "unsigned"):
                int_types = [np.uint8, np.uint16, np.uint32, np.uint64]
            else:
                int_types = [np.int8, np.int16, np.int32, np.int64]
            for downcast_type in int_types[:int_types.index(dtype)]:
                min_poss, max_poss = integral_range(downcast_type)
                if min_val >= min_poss and max_val <= max_poss:
                    dtype = downcast_type
                    break  # stop at smallest

        # convert and return
        if coerced:  # convert to extension type early
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
        validate_dtype(dtype, float)
        validate_errors(errors)
        if dtype == float:  # built-in `float` is identical to np.float64
            dtype = np.float64

        # do naive conversion and check for overflow/precision loss afterwards
        series = self.series.astype(dtype, copy=True)

        # check for precision loss
        min_precise, max_precise = integral_range(dtype)
        if self.min < min_precise or self.max > max_precise:
            # series might be imprecise -> confirm by reversing conversion
            reverse = FloatSeries(series, validate=False)

            # err state 1: infs introduced during coercion (overflow)
            if reverse.hasinfs:
                if errors != "coerce":
                    bad_vals = series[reverse.infs].index.values
                    err_msg = (f"values exceed {dtype.__name__} range at "
                               f"index {shorten_list(bad_vals.index.values)}")
                    raise ConversionError(err_msg, bad_vals)
                series[reverse.infs] = np.nan  # coerce

            # err state 2: precision loss (disregard if errors='coerce')
            elif errors != "coerce":  # compute reverse result and assert equal
                reverse_result = reverse.to_integer(errors="coerce")
                if not self.to_integer().equals(reverse_result):
                    bad_vals = series[self.series != reverse_result]
                    err_msg = (f"precision loss detected at index "
                               f"{shorten_list(bad_vals.index.values)} with "
                               f"dtype {repr(dtype.__name__)}")
                    raise ConversionError(err_msg, bad_vals)

        # attempt to downcast, if applicable
        if downcast:  # search for smaller dtypes that can represent series
            float_types = [np.float16, np.float32, np.float64, np.longdouble]
            for downcast_type in float_types[:float_types.index(dtype)]:
                attempt = series.astype(downcast_type, copy=False)
                if (attempt == series).all():
                    return attempt  # stop at smallest

        # return
        return series

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, complex)
        validate_errors(errors)
        if dtype == complex:  # built-in complex is identical to np.complex128
            dtype = np.complex128

        # do naive conversion and check for overflow/precision loss afterwards
        series = self.series.astype(dtype, copy=True)

        # check for precision loss
        min_precise, max_precise = integral_range(dtype)
        if self.min < min_precise or self.max > max_precise:
            # series might be imprecise -> confirm by reversing conversion
            reverse = FloatSeries(np.real(series), validate=False)

            # err state 1: infs introduced during coercion (overflow)
            if reverse.hasinfs:
                if errors != "coerce":
                    bad_vals = series[reverse.infs]
                    err_msg = (f"values exceed {dtype.__name__} range at "
                               f"index {shorten_list(bad_vals.index.values)}")
                    raise ConversionError(err_msg, bad_vals)
                series[reverse.infs] += complex(np.nan, np.nan)  # coerce

            # err state 2: precision loss (disregard if errors='coerce')
            elif errors != "coerce":  # compute reverse result and assert equal
                reverse_result = reverse.to_integer(errors="coerce")
                if not self.to_integer().equals(reverse_result):
                    bad_vals = series[self.series != reverse_result]
                    err_msg = (f"precision loss detected at index "
                               f"{shorten_list(bad_vals.index.values)} with "
                               f"dtype {repr(dtype.__name__)}")
                    raise ConversionError(err_msg, bad_vals)

        # attempt to downcast, if applicable
        if downcast:  # search for smaller dtypes that can represent series
            complex_types = [np.complex64, np.complex128, np.clongdouble]
            for downcast_type in complex_types[:complex_types.index(dtype)]:
                attempt = series.astype(downcast_type, copy=False)
                if (attempt == series).all():
                    return attempt  # stop at smallest

        # return
        return series

    def to_decimal(self) -> pd.Series:
        """test"""
        # decimal.Decimal can't convert numpy integers in an object series
        if pd.api.types.is_object_dtype(self.series):
            conv = lambda x: decimal.Decimal(int(x))
        else:  # use direct conversion (~2x faster)
            conv = decimal.Decimal
        return np.frompyfunc(conv, 1, 1)(self.series)

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """test"""
        resolve_dtype(dtype)  # ensures scalar, resolvable
        validate_dtype(dtype, str)

        # force string extension type
        if not pd.api.types.is_extension_array_dtype(dtype):
            dtype = DEFAULT_STRING_DTYPE

        # do conversion
        return self.series.astype(dtype, copy=True)
