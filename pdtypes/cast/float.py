from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE

# from pdtypes.check import (
#     check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
# )
from pdtypes.types import check_dtype, get_dtype, resolve_dtype
from pdtypes.error import ConversionError, error_trace, shorten_list
from pdtypes.round import apply_tolerance, round_generic
from pdtypes.util.array import vectorize
from pdtypes.util.downcast import integral_range
from pdtypes.util.type_hints import array_like, dtype_like
# from pdtypes.util.validate import (
#     validate_dtype, validate_errors, validate_rounding, tolerance
# )

from .validate import (
    validate_dtype, validate_errors, validate_rounding, tolerance
)


class FloatSeries:
    """test"""

    def __init__(
        self,
        series: float | array_like,
        validate: bool = True
    ) -> FloatSeries:
        if validate and not check_dtype(series, float):
            err_msg = (f"[{error_trace()}] `series` must contain float data, "
                       f"not {get_dtype(series)}")
            raise TypeError(err_msg)

        self.series = series
        self._infs = None
        self._hasinfs = None

    @property
    def infs(self) -> pd.Series:
        """test"""
        if self._infs is not None:  # infs is cached
            return self._infs

        # infs must be computed
        self._infs = np.isinf(self.rectify(copy=True))
        self._hasinfs = self._infs.any()
        return self._infs

    @property
    def hasinfs(self) -> bool:
        """test"""
        if self._hasinfs is not None:  # hasinfs is cached
            return self._hasinfs

        # hasinfs must be computed
        self._hasinfs = self.infs.any()
        return self._hasinfs

    def rectify(self, copy: bool = True) -> pd.Series:
        """Standardize element types of a float series."""
        # rectification is only needed for improperly formatted object series
        if pd.api.types.is_object_dtype(self.series):
            # get largest element type in series
            element_types = get_dtype(self.series)
            common = max(np.dtype(t) for t in vectorize(element_types))
            return self.series.astype(common, copy=copy)

        # series is already rectified, return a copy or direct reference
        return self.series.copy() if copy else self.series

    def round(
        self,
        rule: str = "half_even",
        decimals: int = 0,
        copy: bool = True
    ) -> pd.Series:
        """test"""
        # TODO: this can be attached directly to pd.Series
        series = self.rectify(copy=copy)
        return round_generic(series, rule=rule, decimals=decimals, copy=copy)

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = validate_dtype(dtype, bool)
        tol, _ = tolerance(tol)
        validate_rounding(rounding)
        validate_errors(errors)
        if tol >= 0.5:
            rounding = "half_even"
            tol = 0

        # rectify object series
        series = self.rectify(copy=True)

        # apply tolerance and rounding rules, if applicable
        nearest = ("half_floor", "half_ceiling", "half_down", "half_up",
                   "half_even")
        if tol and rounding not in nearest:
            series = apply_tolerance(series, tol=tol, copy=False)
        if rounding:
            series = round_generic(series, rule=rounding, copy=False)

        # check for precision loss
        if ((series != 0) & (series != 1)).any():
            if errors != "coerce":
                bad_vals = series[(series != 0) & (series != 1)]
                err_msg = (f"non-boolean value encountered at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series = np.ceil(series.abs().clip(0, 1))  # coerce to [0, 1]

        # return
        return series.astype(dtype.pandas_type)

    def to_integer(
        self,
        dtype: dtype_like = int,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = validate_dtype(dtype, int)
        tol, _ = tolerance(tol)
        validate_rounding(rounding)
        validate_errors(errors)
        if tol >= 0.5:
            rounding = "half_even"
            tol = 0

        # rectify object series
        series = self.rectify(copy=True)
        # coerced = False  # NAs may be introduced by coercion

        # reject any series that contains infinity
        if self.hasinfs:
            if errors != "coerce":
                bad_vals = series[self.infs]
                err_msg = (f"no integer equivalent for infinity at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series[self.infs] = np.nan  # coerce
            # coerced = True  # remember to convert to extension type later

        # apply tolerance and rounding rules, if applicable
        nearest = ("half_floor", "half_ceiling", "half_down", "half_up",
                   "half_even")
        if tol and rounding not in nearest:
            series = apply_tolerance(series, tol=tol, copy=False)
        if rounding:
            series = round_generic(series, rule=rounding, copy=False)

        # check for precision loss
        if not (rounding or series.equals(round_generic(series, "half_even"))):
            if errors != "coerce":
                bad_vals = series[(series != round_generic(series)) ^ self.infs]
                err_msg = (f"precision loss detected at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            round_generic(series, "down", copy=False)  # coerce toward zero

        # get min/max to evaluate range - longdouble maintains integer
        # precision for entire 64-bit range, prevents inconsistent comparison
        min_val = np.longdouble(series.min())
        max_val = np.longdouble(series.max())

        # built-in integer special case - can be arbitrarily large
        if dtype == int:
            if min_val < -2**63 or max_val > 2**63 - 1:  # >int64
                if min_val >= 0 and max_val <= np.uint64(2**64 - 1):  # <uint64
                    dtype = resolve_dtype("uint64")
                    return series.astype(dtype.pandas_type)
                # series is >int64 and >uint64, return as built-in python ints
                return np.frompyfunc(int, 1, 1)(series)  # as fast as cython
            # extended range isn't needed, demote to int64
            dtype = resolve_dtype(np.int64)

        # check whether result fits within specified dtype
        if min_val < dtype.min or max_val > dtype.max:
            if errors != "coerce":
                bad_vals = series[(series < dtype.min) | (series > dtype.max)]
                err_msg = (f"values exceed {str(dtype)} range at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series[(series < dtype.min) | (series > dtype.max)] = np.nan
            min_val = np.longdouble(series.min())
            max_val = np.longdouble(series.max())
            # coerced = True  # remember to convert to extension type later

        # attempt to downcast if applicable
        if downcast:  # search for smaller dtypes that can represent series
            if dtype in resolve_dtype("unsigned"):
                smaller = [np.uint8, np.uint16, np.uint32, np.uint64]
            else:
                smaller = [np.int8, np.int16, np.int32, np.int64]
            smaller = [resolve_dtype(t) for t in smaller]

            for small in smaller[:smaller.index(dtype)]:
                if min_val >= small.min and max_val <= small.max:
                    dtype = small
                    break  # stop at smallest

        # convert and return
        # if coerced:  # convert to extension type early
        #     dtype = extension_type(dtype)
        return series.astype(dtype.pandas_type)

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

        # rectify object series
        series = self.rectify(copy=True)

        # do naive conversion and check for precision loss/overflow afterwards
        if is_dtype(dtype, float, exact=True):  # preserve precision
            dtype = resolve_dtype(series.dtype)
        else:
            series = series.astype(dtype, copy=False)  # naive conversion
            if (series - self.series).any():  # precision loss detected
                if errors != "coerce":
                    bad_vals = series[(series != self.series)]
                    err_msg = (f"precision loss detected at index "
                               f"{shorten_list(bad_vals.index.values)}")
                    raise ConversionError(err_msg, bad_vals)
                # coerce infs to nans and ignore precision loss
                series[np.isinf(series) ^ self.infs] = np.nan

        # downcast if applicable
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

        # rectify object series
        series = self.rectify(copy=True)

        # do naive conversion and check for precision loss/overflow afterwards
        if is_dtype(dtype, complex, exact=True):  # preserve precision
            equiv_complex = {
                np.dtype(np.float16): np.complex64,
                np.dtype(np.float32): np.complex64,
                np.dtype(np.float64): np.complex128,
                np.dtype(np.longdouble): np.clongdouble
            }
            dtype = equiv_complex[series.dtype]
            series = series.astype(dtype, copy=False)
        else:
            series = series.astype(dtype, copy=False)  # naive conversion
            if (series - self.series).any():
                if errors != "coerce":
                    bad_vals = series[(series != self.series)]
                    err_msg = (f"precision loss detected at index "
                               f"{shorten_list(bad_vals.index.values)}")
                    raise ConversionError(err_msg, bad_vals)
                # coerce infs to nans and ignore precision loss
                series[np.isinf(series) ^ self.infs] += complex(np.nan, np.nan)

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
        series = self.rectify(copy=True)

        # decimal.Decimal can't parse np.longdouble series by default
        if is_dtype(series.dtype, np.longdouble):
            conv = lambda x: decimal.Decimal(str(x))
        else:
            conv = decimal.Decimal

        return np.frompyfunc(conv, 1, 1)(series)

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """test"""
        resolve_dtype(dtype)  # ensures scalar, resolvable
        validate_dtype(dtype, str)

        # force string extension type
        if not pd.api.types.is_extension_array_dtype(dtype):
            dtype = DEFAULT_STRING_DTYPE

        # do conversion
        return self.series.astype(dtype, copy=True)
