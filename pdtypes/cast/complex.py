from __future__ import annotations
import decimal

import numpy as np
import pandas as pd

import pdtypes.cast.float  # absolute path prevents circular ImportError
from pdtypes.cast.helpers import SeriesWrapper
from pdtypes.check import check_dtype, get_dtype, is_dtype, resolve_dtype
from pdtypes.error import error_trace, shorten_list
from pdtypes.util.array import vectorize
from pdtypes.util.type_hints import array_like, dtype_like


class ComplexSeries(SeriesWrapper):
    """test"""

    def __init__(
        self,
        series: complex | array_like,
        nans: None | bool | array_like = None,
        validate: bool = True
    ) -> ComplexSeries:
        if validate and not check_dtype(series, complex):
            err_msg = (f"[{error_trace()}] `series` must contain complex "
                       f"data, not {get_dtype(series)}")
            raise TypeError(err_msg)

        super().__init__(series, nans)
        self._real = None
        self._imag = None

    @property
    def real(self) -> pd.Series:
        """test"""
        if self._real is None:
            self._real = pd.Series(np.real(self.rectify(copy=True)))
        return self._real

    @property
    def imag(self) -> pd.Series:
        """test"""
        if self._imag is None:
            self._imag = pd.Series(np.imag(self.rectify(copy=True)))
        return self._imag

    def rectify(self, copy: bool = True) -> pd.Series:
        """Standardize element types of a complex series."""
        # rectification is only needed for improperly formatted object series
        if pd.api.types.is_object_dtype(self.series):
            # get largest element type in series
            element_types = get_dtype(self.series)
            common = max(np.dtype(t) for t in vectorize(element_types))
            complex_nan = resolve_dtype(common)("nan+nanj")

            # if returning a copy, use series.where(..., inplace=False)
            if copy:
                series = self.series.where(~self.is_na, complex_nan)
                return series.astype(common, copy=False)

            # modify in-place and return result
            self.series.where(~self.is_na, complex_nan, inplace=True)
            self.series = self.series.astype(common, copy=False)
            return self.series

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
        if isinstance(tol, (complex, np.complexfloating)):
            real_tol = np.real(tol)
            imag_tol = np.imag(tol)
        else:
            real_tol, imag_tol = (tol, tol)

        # TODO: imaginary tolerances > 0.5 are now supported
        SeriesWrapper._validate_tolerance(real_tol)
        SeriesWrapper._validate_tolerance(imag_tol)
        SeriesWrapper._validate_rounding(rounding)
        SeriesWrapper._validate_dtype(dtype, bool)
        SeriesWrapper._validate_errors(errors)

        # 2 steps: complex -> float, then float -> boolean
        series = self.to_float(tol=imag_tol, errors=errors)
        series = pdtypes.cast.float.FloatSeries(series, nans=self.is_na,
                                                validate=False)
        return series.to_boolean(tol=real_tol, rounding=rounding, dtype=dtype,
                                 errors=errors)

    def to_integer(
        self,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        dtype: dtype_like = int,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        if isinstance(tol, (complex, np.complexfloating)):
            real_tol = np.real(tol)
            imag_tol = np.imag(tol)
        else:
            real_tol, imag_tol = (tol, tol)

        SeriesWrapper._validate_tolerance(real_tol)
        SeriesWrapper._validate_tolerance(imag_tol)
        SeriesWrapper._validate_rounding(rounding)
        SeriesWrapper._validate_dtype(dtype, int)
        SeriesWrapper._validate_errors(errors)

        # 2 steps: complex -> float, then float -> integer
        series = self.to_float(tol=imag_tol, errors=errors)
        series = pdtypes.cast.float.FloatSeries(series, nans=self.is_na,
                                               validate=False)
        return series.to_integer(tol=real_tol, rounding=rounding, dtype=dtype,
                                 downcast=downcast, errors=errors)

    def to_float(
        self,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        dtype: dtype_like = float,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        if isinstance(tol, (complex, np.complexfloating)):
            real_tol = np.real(tol)
            imag_tol = np.imag(tol)
        else:
            real_tol, imag_tol = (tol, tol)

        SeriesWrapper._validate_tolerance(real_tol)
        SeriesWrapper._validate_dtype(dtype, float)
        SeriesWrapper._validate_errors(errors)

        # split series into real and imaginary components
        real = self.real
        imag = self.imag

        # check imaginary component for information loss
        if (np.abs(imag) > imag_tol).any():
            if errors == "raise":
                bad = imag[np.abs(imag) > imag_tol].index.values
                err_msg = (f"[{error_trace()}] imaginary component exceeds "
                           f"tolerance ({imag_tol}) at index "
                           f"{shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return self.series

        # TODO: can't pass nans because real nans might not match imag nans
        real = pdtypes.cast.float.FloatSeries(real, validate=False)
        return real.to_float(dtype=dtype, downcast=downcast, errors=errors)

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
        series = self.rectify(copy=True)
        old_infs = np.isinf(series)  # TODO: make this a lazy-loaded property
        if is_dtype(dtype, complex, exact=True):  # preserve precision
            dtype = resolve_dtype(series.dtype)
        else:
            series = series.astype(dtype, copy=False)
            if (series - self.series).any():  # precision loss detected
                if errors == "ignore":
                    return self.series
                if errors == "raise":
                    indices = (series != self.series) ^ self.is_na
                    bad = series[indices].index.values
                    err_msg = (f"[{error_trace()}] precision loss detected at "
                               f"index {shorten_list(bad)}")
                    raise OverflowError(err_msg)
                # coerce infs introduced by coercion into nans
                complex_na = dtype("nan+nanj")
                series[np.isinf(series) ^ old_infs] = complex_na

        # downcast, if applicable
        if downcast:
            complex_types = [np.complex64, np.complex128, np.clongdouble]
            smaller = complex_types[:complex_types.index(dtype)]
            for downcast_type in smaller:
                try:
                    return self.to_complex(dtype=downcast_type)
                except OverflowError:
                    pass
        return series

    def to_decimal(
        self,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        if isinstance(tol, (complex, np.complexfloating)):
            real_tol = np.real(tol)
            imag_tol = np.imag(tol)
        else:
            real_tol, imag_tol = (tol, tol)

        SeriesWrapper._validate_tolerance(real_tol)
        SeriesWrapper._validate_errors(errors)

        # 2 steps: complex -> float, then float -> decimal
        series = self.to_float(tol=imag_tol, errors=errors)
        series = pdtypes.cast.float.FloatSeries(series, nans=self.is_na,
                                                validate=False)
        return series.to_decimal()

    def to_string(self, dtype: dtype_like = pd.StringDtype()) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        SeriesWrapper._validate_dtype(dtype, str)

        if self.hasnans:
            dtype = pd.StringDtype()
        return self.series.astype(dtype, copy=True)
