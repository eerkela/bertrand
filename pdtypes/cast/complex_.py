from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

from pdtypes.check.check import check_dtype, get_dtype, is_dtype, resolve_dtype
from pdtypes.error import ConversionError, error_trace, shorten_list
from pdtypes.util.array import vectorize
from pdtypes.util.type_hints import array_like, dtype_like

from .float_ import FloatSeries
from .helpers import (
    _validate_dtype, _validate_errors, _validate_rounding, tolerance, DEFAULT_STRING_TYPE
)

# TODO: in the case of (1+nanj)/(nan+1j), retain non-nan real/imag component
# -> pd.isna() considers both of these to be NA


class ComplexSeries:
    """test"""

    def __init__(
        self,
        series: complex | array_like,
        validate: bool = True
    ) -> ComplexSeries:
        if validate and not check_dtype(series, complex):
            err_msg = (f"[{error_trace()}] `series` must contain complex "
                       f"data, not {get_dtype(series)}")
            raise TypeError(err_msg)

        self.series = series
        self._real = None
        self._imag = None

    @property
    def real(self) -> pd.Series:
        """test"""
        if self._real is None:
            rectified = self.rectify(copy=True)
            self._real = pd.Series(np.real(rectified), copy=False)
            self._real.index = self.series.index  # match index
        return self._real

    @property
    def imag(self) -> pd.Series:
        """test"""
        if self._imag is None:
            rectified = self.rectify(copy=True)
            self._imag = pd.Series(np.imag(rectified), copy=False)
            self._imag.index = self.series.index  # match index
        return self._imag

    def rectify(self, copy: bool = True) -> pd.Series:
        """Standardize element types of a complex series."""
        # rectification is only needed for improperly formatted object series
        if pd.api.types.is_object_dtype(self.series):
            # get largest element type in series
            element_types = get_dtype(self.series)
            common = max(np.dtype(t) for t in vectorize(element_types))
            return self.series.astype(common, copy=copy)

        # series is already rectified, return a copy or direct reference
        return self.series.copy() if copy else self.series

    def to_boolean(
        self,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        dtype: dtype_like = bool,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        real_tol, imag_tol = tolerance(tol)
        _validate_rounding(rounding)
        _validate_dtype(dtype, bool)
        _validate_errors(errors)

        # 2 steps: complex -> float, then float -> boolean
        series = self.to_float(tol=imag_tol, errors=errors)
        series = FloatSeries(series, validate=False)
        return series.to_boolean(tol=real_tol, rounding=rounding, dtype=dtype,
                                 errors=errors)

    def to_integer(
        self,
        dtype: dtype_like = int,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        real_tol, imag_tol = tolerance(tol)
        _validate_rounding(rounding)
        _validate_dtype(dtype, int)
        _validate_errors(errors)

        # 2 steps: complex -> float, then float -> integer
        series = self.to_float(tol=imag_tol, errors=errors)
        series = FloatSeries(series, validate=False)
        return series.to_integer(tol=real_tol, rounding=rounding, dtype=dtype,
                                 downcast=downcast, errors=errors)

    def to_float(
        self,
        dtype: dtype_like = float,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        _, imag_tol = tolerance(tol)
        _validate_dtype(dtype, float)
        _validate_errors(errors)
        if imag_tol == np.inf:
            errors = "coerce"

        # split series into real and imaginary components
        real = self.real
        imag = self.imag

        # check imaginary component for information loss
        if errors != "coerce" and (np.abs(imag) > imag_tol).any():
            bad_vals = imag[np.abs(imag) > imag_tol]
            err_msg = (f"imaginary component exceeds tolerance ({imag_tol}) "
                       f"at index {shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        real = FloatSeries(real, validate=False)
        return real.to_float(dtype=dtype, downcast=downcast, errors=errors)

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        _validate_dtype(dtype, complex)
        _validate_errors(errors)

        # rectify object series
        series = self.rectify(copy=True)

        # do naive conversion and check for precision loss/overflow afterwards
        if is_dtype(dtype, complex, exact=True):  # preserve precision
            dtype = resolve_dtype(series.dtype)
        else:
            old_infs = np.isinf(series)
            series = series.astype(dtype, copy=False)  # naive conversion
            if (series - self.series).any():  # precision loss detected
                if errors != "coerce":
                    bad_vals = series[series != self.series]
                    err_msg = (f"precision loss detected at index "
                               f"{shorten_list(bad_vals.index.values)}")
                    raise ConversionError(err_msg, bad_vals)
                # coerce infs into nans and ignore precision loss
                series[np.isinf(series) ^ old_infs] += complex(np.nan, np.nan)

        # downcast, if applicable
        if downcast:  # search for smaller dtypes that can represent series
            complex_types = [np.complex64, np.complex128, np.clongdouble]
            for downcast_type in complex_types[:complex_types.index(dtype)]:
                attempt = series.astype(downcast_type, copy=False)
                if (attempt == series).all():
                    return attempt

        # return
        return series

    def to_decimal(
        self,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        _, imag_tol = tolerance(tol)
        _validate_errors(errors)

        # 2 steps: complex -> float, then float -> decimal
        series = self.to_float(tol=imag_tol, errors=errors)
        return FloatSeries(series, validate=False).to_decimal()

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """test"""
        resolve_dtype(dtype)  # ensure scalar, resolvable
        _validate_dtype(dtype, str)

        # force string extension type
        if not pd.api.types.is_extension_array_dtype(dtype):
            dtype = DEFAULT_STRING_TYPE

        # do conversion
        return self.series.astype(dtype, copy=True)
