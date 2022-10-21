from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE

from pdtypes.check import (
    check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
)
from pdtypes.error import ConversionError, error_trace, shorten_list
from pdtypes.types import resolve_dtype, ElementType
from pdtypes.util.downcast import integral_range
from pdtypes.util.type_hints import dtype_like

from .util.downcast import (
    demote_integer_supertypes, downcast_integer_dtype, downcast_float_series,
    downcast_complex_series
)
from .util.round import apply_tolerance, round_generic, snap_round
from .util.time import (
    convert_unit_float, epoch, ns_to_datetime, ns_to_numpy_datetime64,
    ns_to_numpy_timedelta64, ns_to_pandas_timedelta, ns_to_pandas_timestamp,
    ns_to_pydatetime, ns_to_pytimedelta, ns_to_timedelta, timezone
)
from .util.validate import (
    tolerance, validate_dtype, validate_errors, validate_rounding,
    validate_series
)

from .base import RealSeries
from .float import FloatSeries



# If DEBUG=True, insert argument checks into FloatSeries conversion methods
DEBUG: bool = True


def reject_non_boolean(
    series: DecimalSeries,
    errors: str
) -> FloatSeries:
    """Reject any DecimalSeries that contains non-boolean values (i.e. not 0
    or 1).
    """
    if ((series != 0) & (series != 1)).any():
        if errors != "coerce":
            bad_vals = series[(series != 0) & (series != 1)]
            err_msg = (f"non-boolean value encountered at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # TODO: np.ceil converts decimals to int

        # coerce to [0, 1]
        series = DecimalSeries(
            np.ceil(series.abs().clip(0, 1)),
            hasnans=series.hasnans
        )

    return series


class DecimalSeries(RealSeries):
    """TODO"""

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        is_na: pd.Series = None,
        min_val: decimal.Decimal = None,
        min_index: int = None,
        max_val: decimal.Decimal = None,
        max_index: int = None,
        hasinfs: bool = None,
        is_inf: np.ndarray = None
    ) -> DecimalSeries:
        if DEBUG:
            validate_series(series, decimal.Decimal)

        super().__init__(
            series=series,
            hasnans=hasnans,
            is_na=is_na,
            min_val=min_val,
            min_index=min_index,
            max_val=max_val,
            max_index=max_index,
            hasinfs=hasinfs,
            is_inf=is_inf
        )

    @property
    def infs(self) -> pd.Series:
        """TODO"""
        # cached
        if self._is_inf is not None:
            return self._is_inf

        # NOTE: np.isinf does not recognize decimal.Decimal objects by default

        # uncached
        comp = (decimal.Decimal("-inf"), decimal.Decimal("inf"))
        self._is_inf = self.series.isin(comp)
        return self._is_inf

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        tol: int | float | decimal.Decimal = 0,
        rounding: None | str = None,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        if DEBUG:
            validate_dtype(dtype, bool)
            validate_rounding(rounding)
            validate_errors(errors)

        tol, _ = tolerance(tol)
        if tol >= 0.5:
            rounding = "half_even"
            tol = 0

        # apply tolerance and rounding rules, if applicable
        series = snap_round(self, tol=tol, rule=rounding)

        # check for precision loss
        series = reject_non_boolean(series=series, errors=errors)

        # return
        if series.hasnans or dtype.nullable:
            return series.astype(dtype.pandas_type)
        return series.astype(dtype.numpy_type)

    def to_integer(
        self,
        dtype: dtype_like = int,
        tol: int | float | decimal.Decimal = 0,
        rounding: None | str = None,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO

        tol=0.5 <==> rounding="half_even"
        errors="coerce" <==> rounding="down" + overflow to nan
        """
        dtype = resolve_dtype(dtype)
        tol, _ = tolerance(tol)
        validate_rounding(rounding)
        validate_errors(errors)
        if tol >= 0.5:
            rounding = "half_even"
            tol = 0

        series = self.series.copy()
        coerced = False  # NAs may be introduced by coercion

        # reject any series that contains infinity
        if self.hasinfs:
            if errors != "coerce":
                bad_vals = series[self.infs]
                err_msg = (f"no integer equivalent for infinity at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series[self.infs] = pd.NA  # coerce
            coerced = True  # remember to convert to extension type later

        # apply tolerance and rounding rules, if applicable
        nearest = ("half_floor", "half_ceiling", "half_down", "half_up",
                   "half_even")
        if tol and rounding not in nearest:
            series = apply_tolerance(series, tol=tol, copy=False)
        if rounding:
            series = round_generic(series, rule=rounding, copy=False)

        # check for precision loss
        if not (rounding or series.equals(round_generic(series))):
            if errors != "coerce":
                bad_vals = series[(series != round_generic(series)) ^
                                  self.infs]
                err_msg = (f"precision loss detected at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            round_generic(series, "down", copy=False)  # coerce toward zero

        # get min/max to evaluate range
        min_val = series.min()
        max_val = series.max()

        # built-in integer special case - can be arbitrarily large
        if is_dtype(dtype, int, exact=True):
            if min_val < -2**63 or max_val > 2**63 - 1:  # >int64
                if min_val >= 0 and max_val <= 2**64 - 1:  # <uint64
                    dtype = pd.UInt64Dtype() if coerced else np.uint64
                    return series.astype(dtype, copy=False)
                # series is >int64 and >uint64, return as built-in python ints
                return np.frompyfunc(int, 1, 1)(series)  # as fast as cython
            # extended range isn't needed, demote to int64
            dtype = np.int64

        # check whether result fits within specified dtype
        min_poss, max_poss = integral_range(dtype)
        if min_val < min_poss or max_val > max_poss:
            if errors != "coerce":
                bad_vals = series[(series < min_poss) | (series > max_poss)]
                err_msg = (f"values exceed {dtype} range at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series[(series < min_poss) | (series > max_poss)] = pd.NA
            min_val = np.longdouble(series.min())
            max_val = np.longdouble(series.max())
            coerced = True  # remember to convert to extension type later

        # attempt to downcast if applicable
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
        tol: int | float | complex | decimal.Decimal = 1e-6,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, float)
        tol, _ = tolerance(tol)
        validate_errors(errors)
        if dtype == float:  # built-in `float` is identical to np.float64
            dtype = np.float64
        if tol == np.inf:  # infinite tolerance is equivalent to "coerce"
            errors = "coerce"

        # do naive conversion, then reverse to detect precision loss/overflow
        series = self.series.astype(dtype)  # naive conversion
        reverse = FloatSeries(series, validate=False)
        if errors == "coerce":
            series[reverse.infs ^ self.infs] = np.nan
        else:  # problem arises when subtracting infinities
            reverse = reverse.to_decimal()[~self.infs]
            outside_tol = np.abs(reverse - self.series[~self.infs]) > tol
            if outside_tol.any():
                bad_vals = series[outside_tol]
                err_msg = (f"precision loss detected at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)

        # downcast if applicable
        if downcast:
            float_types = [np.float16, np.float32, np.float64, np.longdouble]
            for downcast_type in float_types[:float_types.index(dtype)]:
                attempt = series.astype(downcast_type, copy=False)
                if (attempt == series).all():
                    return attempt

        # return
        return series

    def to_complex(
        self,
        dtype: dtype_like = complex,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        tol, _ = tolerance(tol)
        validate_dtype(dtype, complex)
        validate_errors(errors)
        if dtype == complex:  # built-in `complex` is identical to complex128
            dtype = np.complex128

        # 2 steps: decimal -> float, then float -> complex
        equiv_float = {
            np.complex64: np.float32,
            np.complex128: np.float64,
            np.clongdouble: np.longdouble
        }
        series = self.to_float(dtype=equiv_float[dtype], tol=tol, errors=errors)
        series = FloatSeries(series, validate=False)
        return series.to_complex(dtype=dtype, downcast=downcast, errors=errors)

    def to_decimal(self) -> pd.Series:
        """TODO"""
        # decimal.Decimal is the only recognized decimal implementation
        return self.series.copy()

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """TODO"""
        resolve_dtype(dtype)  # ensures scalar, resolvable
        validate_dtype(dtype, str)

        # force string extension type
        if not pd.api.types.is_extension_array_dtype(dtype):
            dtype = DEFAULT_STRING_DTYPE

        # do conversion
        return self.series.astype(dtype, copy=True)
