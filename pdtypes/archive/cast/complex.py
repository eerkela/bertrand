from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes.delegate import delegates
from pdtypes.types import get_dtype, resolve_dtype, ElementType, CompositeType
from pdtypes.error import ConversionError, shorten_list
from pdtypes.util.type_hints import datetime_like, dtype_like

from .util.downcast import downcast_complex_series
from .util.validate import (
    tolerance, validate_dtype, validate_errors, validate_rounding
)

from .base import SeriesWrapper
from .float import FloatSeries


# TODO: in the case of (1+nanj)/(nan+1j), retain non-nan real/imag component
# -> pd.isna() considers both of these to be NA
# -> do same with infs

# TODO: for complex series, treat infs/nans in real, imaginary components
# separately.


def reject_nonreal(
    imag: FloatSeries,
    imag_tol: int | float | decimal.Decimal,
    errors: str
) -> None:
    """Reject any imaginary ComplexSeries that exceeds `imag_tol` distance from
    zero.
    """
    if errors != "coerce" and (np.abs(imag) > imag_tol).any():
        bad_vals = imag[np.abs(imag) > imag_tol]
        err_msg = (f"imaginary component exceeds tolerance ({imag_tol}) "
                    f"at index {shorten_list(bad_vals.index.values)}")
        raise ConversionError(err_msg, bad_vals)


def reject_complex_precision_loss(
    series: ComplexSeries,
    dtype: ElementType,
    real_tol: int | float | decimal.Decimal,
    imag_tol: int | float | decimal.Decimal,
    errors: str
) -> pd.Series:
    """Reject any ComplexSeries whose elements cannot be exactly represented
    in the given complex ElementType.
    """
    # do naive conversion
    naive = series.astype(dtype.numpy_type, copy=False)
    real = np.real(naive)
    imag = np.imag(naive)

    # check for precision loss
    if (
        ((real - series.real) > real_tol).any() or
        ((imag - series.imag) > imag_tol).any()
    ):  # at least one nonzero residual, either real or complex
        # TODO: separate inf (overflow) check from tolerance application

        if errors != "coerce":
            bad_vals = series[(naive != series)]
            err_msg = (f"precision loss detected at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # coerce infs to nans and ignore precision loss
        naive[(np.isinf(real) ^ series.real.infs)] += complex(np.nan, 0)
        naive[(np.isinf(imag) ^ series.imag.infs)] += complex(0, np.nan)

    # return
    return naive


@delegates()
class ComplexSeries(SeriesWrapper):
    """TODO"""

    unwrapped = SeriesWrapper.unwrapped + (
        "_real_kwargs", "_imag_kwargs", "_real", "_imag"
    )

    def __init__(
        self,
        series: pd.Series,
        real_kwargs: dict[str, bool | pd.Series | np.ndarray] = None,
        imag_kwargs: dict[str, bool | pd.Series | np.ndarray] = None,
        **kwargs
    ) -> ComplexSeries:
        super().__init__(series=series, **kwargs)
        self._real_kwargs = {} if real_kwargs is None else real_kwargs
        self._imag_kwargs = {} if imag_kwargs is None else real_kwargs
        self._real = None
        self._imag = None

    #######################
    ####    GENERAL    ####
    #######################

    @property
    def real(self) -> FloatSeries:
        """TODO"""
        # cached
        if self._real is not None:
            return self._real

        # uncached
        real = pd.Series(
            np.real(self.rectify().series),
            index=self.index,
            copy=False
        )
        self._real = FloatSeries(series=real, **self._real_kwargs)
        return self._real

    @property
    def imag(self) -> FloatSeries:
        """TODO"""
        # cached
        if self._imag is not None:
            return self._imag

        # uncached
        imag = pd.Series(
            np.imag(self.rectify().series),
            index=self.index,
            copy=False
        )
        self._imag = FloatSeries(series=imag, **self._imag_kwargs)
        return self._imag

    def rectify(self) -> ComplexSeries:
        """Standardize element types of a complex series."""
        # rectification is only needed for improperly formatted object series
        if pd.api.types.is_object_dtype(self.series):
            # get largest element type in series
            element_types = CompositeType(get_dtype(self.series))
            common = max(t.numpy_type for t in element_types)
            self.series = self.series.astype(common)

        return self

    ###########################
    ####    CONVERSIONS    ####
    ###########################

    def to_boolean(
        self,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        dtype: dtype_like = bool,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, bool)
        validate_rounding(rounding)
        validate_errors(errors)

        real_tol, imag_tol = tolerance(tol)

        # assert series is real
        reject_nonreal(imag=self.imag, imag_tol=imag_tol, errors=errors)

        # convert real component to boolean
        return self.real.to_boolean(
            tol=real_tol,
            rounding=rounding,
            dtype=dtype,
            errors=errors
        )

    def to_integer(
        self,
        dtype: dtype_like = int,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, int)
        validate_rounding(rounding)
        validate_errors(errors)

        real_tol, imag_tol = tolerance(tol)

        # assert series is real
        reject_nonreal(imag=self.imag, imag_tol=imag_tol, errors=errors)

        # convert real component to integer
        return self.real.to_integer(
            tol=real_tol,
            rounding=rounding,
            dtype=dtype,
            downcast=downcast,
            errors=errors
        )

    def to_float(
        self,
        dtype: dtype_like = float,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, float)
        validate_errors(errors)

        real_tol, imag_tol = tolerance(tol)

        # assert series is real
        reject_nonreal(imag=self.imag, imag_tol=imag_tol, errors=errors)

        # return real component
        return self.real.to_float(
            dtype=dtype,
            tol=real_tol,
            downcast=downcast,
            errors=errors
        )

    def to_complex(
        self,
        dtype: dtype_like = complex,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, complex)
        validate_errors(errors)

        # rectify object series
        series = self.rectify()

        real_tol, imag_tol = tolerance(tol)

        # do naive conversion and check for precision loss/overflow afterwards
        if dtype == complex:  # preserve precision
            dtype = resolve_dtype(series.dtype)
            series = series.series
        else:
            series = reject_complex_precision_loss(
                series=series,
                dtype=dtype,
                real_tol=real_tol,
                imag_tol=imag_tol,
                errors=errors
            )

        # downcast, if applicable
        if downcast:
            return downcast_complex_series(series, dtype=dtype)

        # return
        return series

    def to_decimal(
        self,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        _, imag_tol = tolerance(tol)
        validate_errors(errors)

        # assert series is real
        reject_nonreal(imag=self.imag, imag_tol=imag_tol, errors=errors)

        # return real component
        return self.real.to_decimal()

    def to_datetime(
        self,
        dtype: dtype_like = "datetime",
        unit: str = "ns",
        tz: str | datetime.tzinfo = None,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, "datetime")

        _, imag_tol = tolerance(tol)

        # assert series is real
        reject_nonreal(imag=self.imag, imag_tol=imag_tol, errors=errors)

        # convert real component
        return self.real.to_datetime(
            dtype=dtype,
            unit=unit,
            tz=tz
        )

    def to_timedelta(
        self,
        dtype: dtype_like = "timedelta",
        unit: str = "ns",
        since: str | datetime_like = "2001-01-01 00:00:00+0000",
        tol: int | float | complex | decimal.Decimal = 1e-6,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, "timedelta")

        _, imag_tol = tolerance(tol)

        # assert series is real
        reject_nonreal(imag=self.imag, imag_tol=imag_tol, errors=errors)

        # convert real component
        return self.real.to_datetime(
            dtype=dtype,
            unit=unit,
            since=since
        )

    def to_string(
        self,
        dtype: dtype_like = str
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)  # ensure scalar, resolvable
        validate_dtype(dtype, str)

        # do conversion
        return self.series.astype(dtype.pandas_type)
