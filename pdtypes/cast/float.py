from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes.types import get_dtype, resolve_dtype, ElementType, CompositeType
from pdtypes.error import ConversionError, shorten_list
from pdtypes.util.type_hints import datetime_like, dtype_like

from .util.downcast import (
    demote_integer_supertypes, downcast_integer_dtype, downcast_float_series,
    downcast_complex_series
)
from .util.round import round_generic, snap_round
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


# TODO: have to be careful with exact comparisons to integer/boolean dtypes
# due to differing nullable settings.


# If DEBUG=True, insert argument checks into FloatSeries conversion methods
DEBUG: bool = True


def reject_non_boolean(
    series: FloatSeries,
    errors: str
) -> FloatSeries:
    """Reject any FloatSeries that contains non-boolean values (i.e. not 0 or
    1).
    """
    if ((series != 0) & (series != 1)).any():
        if errors != "coerce":
            bad_vals = series[(series != 0) & (series != 1)]
            err_msg = (f"non-boolean value encountered at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # coerce to [0, 1]
        series = FloatSeries(
            np.ceil(series.abs().clip(0, 1)),
            hasnans=series.hasnans
        )

    return series


def reject_infs(
    series: FloatSeries,
    errors: str
) -> FloatSeries:
    """Reject any FloatSeries that contains infinity."""
    if series.hasinfs:
        if errors != "coerce":
            bad_vals = series[series.infs]
            err_msg = (f"no integer equivalent for infinity at index "
                        f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # coerce
        series[series.infs] = np.nan
        series = FloatSeries(
            series=series.series,
            hasinfs=False,
            is_inf=None,
            hasnans=True,
            is_na=series.isna() | series.infs
        )

    return series


def reject_non_integer(
    series: FloatSeries,
    rounding: str,
    errors: str
) -> FloatSeries:
    """Reject any FloatSeries that contains non-integer values."""
    if not rounding:
        rounded = round_generic(series.series, "half_even")
        if not series.equals(rounded):
            if errors != "coerce":
                bad_vals = series[(series != rounded) ^ series.infs]
                err_msg = (f"precision loss detected at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)

            # coerce toward zero
            series = round_generic(series.series, "down", copy=False)

    return series


def reject_integer_overflow(
    series: FloatSeries,
    dtype: ElementType,
    errors: str
) -> FloatSeries:
    """Reject any FloatSeries whose range exceeds the maximum available for
    the given ElementType.
    """
    # NOTE: comparison between floats and ints can be inconsistent when the
    # value exceeds the size of the floating point significand.  Casting to
    # longdouble mitigates this by ensuring a full 64-bit significand.
    min_val = np.longdouble(series.min())
    max_val = np.longdouble(series.max())

    if min_val < dtype.min or max_val > dtype.max:
        if errors != "coerce":
            bad_vals = series[(series < dtype.min) | (series > dtype.max)]
            err_msg = (f"values exceed {str(dtype)} range at index "
                        f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # coerce by replacing with nan
        series[(series < dtype.min) | (series > dtype.max)] = np.nan
        series = FloatSeries(series, hasnans=True)

    return series


def reject_float_precision_loss(
    series: FloatSeries,
    dtype: ElementType,
    tol: int | float | decimal.Decimal,
    errors: str
) -> pd.Series:
    """Reject any FloatSeries whose elements cannot be exactly represented
    in the given float ElementType.
    """
    # do naive conversion
    naive = series.astype(dtype.numpy_type, copy=False)

    # check for precision loss
    if ((naive - series) > tol).any():  # at least one nonzero residual
        # TODO: separate inf (overflow) check from tolerance application

        if errors != "coerce":
            bad_vals = series[(naive != series)]
            err_msg = (f"precision loss detected at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # coerce infs to nans and ignore precision loss
        naive[np.isinf(naive) ^ series.infs] = np.nan

    # return
    return naive


def reject_complex_precision_loss(
    series: FloatSeries,
    dtype: ElementType,
    tol: int | float | decimal.Decimal,
    errors: str
) -> pd.Series:
    """Reject any FloatSeries whose elements cannot be exactly represented
    in the given complex ElementType.
    """
    # do naive conversion
    naive = series.astype(dtype.numpy_type, copy=False)

    # check for precision loss
    if ((naive - series) > tol).any():  # at least one nonzero residual
        if errors != "coerce":
            bad_vals = series[(naive != series)]
            err_msg = (f"precision loss detected at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # coerce infs to nans and ignore precision loss
        naive[np.isinf(naive) ^ series.infs] += complex(np.nan, np.nan)

    # return
    return naive


class FloatSeries(RealSeries):
    """TODO"""

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        is_na: pd.Series = None,
        min_val: float = None,
        min_index: int = None,
        max_val: float = None,
        max_index: int = None,
        hasinfs: bool = None,
        is_inf: np.ndarray = None
    ) -> FloatSeries:
        if DEBUG:
            validate_series(series, float)

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

    #######################
    ####    GENERAL    ####
    #######################

    def rectify(self) -> FloatSeries:
        """Standardize element types of a float series."""
        # rectification is only needed for improperly formatted object series
        if pd.api.types.is_object_dtype(self.series):
            # convert to widest element type in series
            element_types = CompositeType(get_dtype(self.series))
            common = max(t.numpy_type for t in element_types)
            self.series = self.series.astype(common)
        else:  # series is already rectified, return a copy or direct reference
            self.series = self.series.copy()

        # return self, retaining state flags
        return self

    ###########################
    ####    CONVERSIONS    ####
    ###########################

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is boolean-like
        if DEBUG:
            validate_dtype(dtype, bool)
            validate_rounding(rounding)
            validate_errors(errors)

        tol, _ = tolerance(tol)
        if tol >= 0.5:
            rounding = "half_even"
            tol = 0

        # rectify object series
        series = self.rectify()

        # apply tolerance and rounding rules, if applicable
        series = snap_round(series, tol=tol, rule=rounding)

        # check for precision loss
        series = reject_non_boolean(series, errors=errors)

        # return
        if series.hasnans or dtype.nullable:
            return series.astype(dtype.pandas_type)
        return series.astype(dtype.numpy_type)

    def to_integer(
        self,
        dtype: dtype_like = int,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        rounding: None | str = None,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is integer-like
        if DEBUG:
            validate_dtype(dtype, int)
            validate_rounding(rounding)
            validate_errors(errors)

        tol, _ = tolerance(tol)
        if tol >= 0.5:
            rounding = "half_even"
            tol = 0

        # rectify object series
        series = self.rectify()

        # reject any series that contains infinity
        series = reject_infs(series=series, errors=errors)

        # apply tolerance and rounding rules, if applicable
        series = snap_round(series, tol=tol, rule=rounding)

        # check for precision loss
        reject_non_integer(series, rounding=rounding, errors=errors)

        # demote integer supertypes and handle >64-bit special case
        dtype = demote_integer_supertypes(series=series, dtype=dtype)
        exceeds_range = ("int", "signed", "nullable[int]", "nullable[signed]")
        if any(dtype == t for t in exceeds_range):
            return np.frompyfunc(int, 1, 1)(series)

        # ensure series min/max fit within dtype range
        series = reject_integer_overflow(
            series=series,
            dtype=dtype,
            errors=errors
        )

        # attempt to downcast if applicable
        if downcast:  # search for smaller dtypes that can represent series
            dtype = downcast_integer_dtype(series=series, dtype=dtype)

        # convert and return
        if series.hasnans or dtype.nullable:
            return series.astype(dtype.pandas_type)
        return series.astype(dtype.numpy_type)

    def to_float(
        self,
        dtype: dtype_like = float,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is float-like
        if DEBUG:
            validate_dtype(dtype, float)
            validate_errors(errors)

        tol, _ = tolerance(tol)
        if tol == np.inf:
            errors = "coerce"

        # rectify object series
        series = self.rectify()

        # do naive conversion and check for precision loss/overflow afterwards
        if dtype == float:  # preserve precision
            dtype = resolve_dtype(series.dtype)
            series = series.series
        else:
            series = reject_float_precision_loss(
                series=series,
                tol=tol,
                dtype=dtype,
                errors=errors
            )

        # downcast if applicable
        if downcast:
            return downcast_float_series(series=series, dtype=dtype)

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
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is float-like
        if DEBUG:
            validate_dtype(dtype, complex)
            validate_errors(errors)

        tol, _ = tolerance(tol)
        if tol == np.inf:
            errors = "coerce"

        # rectify object series
        series = self.rectify()

        # do naive conversion and check for precision loss/overflow afterwards
        if dtype == complex:  # preserve precision
            dtype = resolve_dtype(series.dtype).equiv_complex
            series = series.astype(dtype.numpy_type, copy=False)
        else:
           series = reject_complex_precision_loss(
                series=series,
                tol=tol,
                dtype=dtype,
                errors=errors
            )

        # attempt to downcast, if applicable
        if downcast:  # search for smaller dtypes that can represent series
            return downcast_complex_series(series=series, dtype=dtype)

        # return
        return series

    def to_datetime(
        self,
        dtype: dtype_like = "datetime",
        unit: str = "ns",
        tz: str | datetime.tzinfo = None
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is datetime-like
        if DEBUG:
            validate_dtype(dtype, "datetime")

        tz = timezone(tz)

        # ElementType objects for each datetime subtype
        pandas_timestamp = resolve_dtype(pd.Timestamp)
        pydatetime = resolve_dtype(datetime.datetime)
        numpy_datetime64 = resolve_dtype(np.datetime64)

        # alias M8[ns] to pd.Timestamp
        if (dtype in numpy_datetime64 and
            dtype.unit == "ns" and
            dtype.step_size == 1
        ):
            dtype = resolve_dtype(
                pd.Timestamp,
                sparse=dtype.sparse,
                categorical=dtype.categorical
            )

        # rectify object series
        series = self.rectify()

        # convert to nanoseconds
        nanoseconds = convert_unit_float(
            series.series,
            unit,
            "ns",
            since=epoch("UTC"),
            rounding="floor"
        )

        # pd.Timestamp
        if dtype in pandas_timestamp:
            return ns_to_pandas_timestamp(nanoseconds, tz=tz)

        # datetime.datetime
        if dtype in pydatetime:
            return ns_to_pydatetime(nanoseconds, tz=tz)

        # np.datetime64
        if dtype in numpy_datetime64:
            # TODO: gather step size
            return ns_to_numpy_datetime64(nanoseconds, unit=dtype.unit)

        # datetime supertype
        return ns_to_datetime(nanoseconds, tz=tz)

    def to_timedelta(
        self,
        dtype: dtype_like = "timedelta",
        unit: str = "ns",
        since: str | datetime_like = "2001-01-01 00:00:00+0000"
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is timedelta-like
        if DEBUG:
            validate_dtype(dtype, "timedelta")

        since=epoch(since)

        # ElementType objects for each timedelta subtype
        pandas_timedelta = resolve_dtype(pd.Timedelta)
        pytimedelta = resolve_dtype(datetime.timedelta)
        numpy_timedelta64 = resolve_dtype(np.timedelta64)

        # alias m8[ns] to pd.Timedelta and gather unit/step size from dtype
        if (dtype in numpy_timedelta64 and
            dtype.unit == "ns" and
            dtype.step_size == 1
        ):
            dtype = resolve_dtype(
                pd.Timedelta,
                sparse=dtype.sparse,
                categorical=dtype.categorical
            )

        # convert to nanoseconds
        nanoseconds = convert_unit_float(
            self.series,
            unit,
            "ns",
            since=since,
            rounding="floor"
        )

        # pd.Timedelta
        if dtype in pandas_timedelta:
            return ns_to_pandas_timedelta(nanoseconds)

        # datetime.timedelta
        if dtype in pytimedelta:
            return ns_to_pytimedelta(nanoseconds)

        # np.timedelta64
        if dtype in numpy_timedelta64:
            # TODO: gather step size
            return ns_to_numpy_timedelta64(
                nanoseconds,
                unit=dtype.unit,
                since=since
            )

        # timedelta supertype
        return ns_to_timedelta(nanoseconds, since=since)

    def to_decimal(self) -> pd.Series:
        """TODO"""
        series = self.rectify()

        # decimal.Decimal can't convert np.longdouble series by default
        if resolve_dtype(series.dtype) == np.longdouble:
            conv = lambda x: decimal.Decimal(str(x))
        else:  # use direct conversion (~2x faster)
            conv = decimal.Decimal

        return np.frompyfunc(conv, 1, 1)(series)

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is boolean-like
        if DEBUG:
            validate_dtype(dtype, str)

        # do conversion
        return self.series.astype(dtype.pandas_type)
