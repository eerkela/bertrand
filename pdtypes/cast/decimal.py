from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes.delegate import delegates
from pdtypes.error import ConversionError, shorten_list
from pdtypes.types import resolve_dtype, ElementType
from pdtypes.util.type_hints import datetime_like, dtype_like

from .util.downcast import (
    demote_integer_supertypes, downcast_integer_dtype, downcast_float_series
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
from .float import FloatSeries


# TODO: to_boolean/to_integer cannot convert decimal objects to pandas
# extension types.


def reject_non_boolean(
    series: DecimalSeries,
    errors: str
) -> FloatSeries:
    """Reject any DecimalSeries that contains non-boolean values (i.e. not 0
    or 1).
    """
    # NOTE: astype() cannot convert decimal.Decimal objects to pandas extension
    # types - have to manually convert to int first

    non_boolean = series.notna() & (series != 0) & (series != 1)

    if non_boolean.any():
        if errors != "coerce":
            bad_vals = series[non_boolean]
            err_msg = (f"non-boolean value encountered at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # coerce to [0, 1]
        with series.exclude_na(pd.NA):
            # NOTE: np.ceil automatically casts to integer
            series.series = np.ceil(series.abs().clip(0, 1))

    else:
        with series.exclude_na(pd.NA):
            series.series = np.frompyfunc(bool, 1, 1)(series.series)

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
        series[series.infs] = pd.NA
        series = FloatSeries(
            series,
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
            round_generic(series.series, "down", copy=False)

    return series


def reject_integer_overflow(
    series: FloatSeries,
    dtype: ElementType,
    errors: str
) -> FloatSeries:
    """Reject any FloatSeries whose range exceeds the maximum available for
    the given ElementType.
    """
    if series.min() < dtype.min or series.max() > dtype.max:
        index = (series < dtype.min) | (series > dtype.max)

        if errors != "coerce":
            bad_vals = series[index]
            err_msg = (f"values exceed {str(dtype)} range at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # coerce by replacing with nan
        series[index] = np.nan
        series = FloatSeries(series, hasnans=True)

    return series


def reject_float_precision_loss(
    series: pd.Series,
    original: DecimalSeries,
    tol: int | float | decimal.Decimal,
    errors: str
) -> pd.Series:
    """Reverse a decimal to float conversion to ensure that precision loss
    does not exceed `tol`.
    """
    # check that results did not shift by more than `tol`
    reverse = FloatSeries(series)

    if errors == "coerce":
        series[reverse.infs ^ original.infs] = np.nan
    else:
        # compute reverse result
        reverse = reverse.to_decimal()[~original.infs]

        outside_tol = (np.abs(reverse - original[~original.infs]) > tol)
        if outside_tol.any():
            bad_vals = original[outside_tol]
            err_msg = (f"precision loss detected at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

    return series


@delegates()
class DecimalSeries(RealSeries):
    """TODO"""

    def __init__(
        self,
        series: pd.Series,
        **kwargs
    ) -> DecimalSeries:
        super().__init__(series=series, **kwargs)

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        tol: int | float | decimal.Decimal = 0,
        rounding: None | str = None,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, bool)
        validate_rounding(rounding)
        validate_errors(errors)

        tol, _ = tolerance(tol)
        if tol >= 0.5:
            rounding = "half_even"
            tol = 0

        # apply tolerance and rounding rules, if applicable
        snap_round(self.series, tol=tol, rule=rounding, copy=False)

        # check for precision loss
        series = reject_non_boolean(series=self, errors=errors)

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
        validate_dtype(dtype, int)
        validate_rounding(rounding)
        validate_errors(errors)

        tol, _ = tolerance(tol)
        if tol >= 0.5:
            rounding = "half_even"
            tol = 0

        # reject any series that contains infinity
        series = reject_infs(series=self, errors=errors)

        # apply tolerance and rounding rules, if applicable
        snap_round(series.series, tol=tol, rule=rounding, copy=False)

        # check for precision loss
        series = reject_non_integer(series, rounding=rounding, errors=errors)

        # demote integer supertypes and handle >64-bit special case
        dtype = demote_integer_supertypes(series=series, dtype=dtype)
        exceeds_range = ("int", "signed", "nullable[int]", "nullable[signed]")
        if any(dtype == t for t in exceeds_range):
            # TODO: exclude na
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
            # NOTE: cannot astype() decimal.Decimal objects directly to pandas
            # extension type.  Reduce to int first, then astype() to final.
            return np.frompyfunc(int, 1, 1)(series).astype(dtype.pandas_type)
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
        validate_dtype(dtype, float)
        validate_errors(errors)

        tol, _ = tolerance(tol)

        # do naive conversion
        series = self.astype(dtype.numpy_type)

        # check for overflow/precision loss
        series = reject_float_precision_loss(
            series=series,
            original=self,
            tol=tol,
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
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, complex)
        validate_errors(errors)

        tol, _ = tolerance(tol)

        # 2 steps: decimal -> float, then float -> complex
        series = FloatSeries(
            self.to_float(dtype=dtype.equiv_float, tol=tol, errors=errors),
            hasinfs = self.hasinfs,
            is_inf = self.infs
        )
        return series.to_complex(
            dtype=dtype,
            downcast=downcast,
            errors=errors
        )

    def to_datetime(
        self,
        dtype: dtype_like = "datetime",
        unit: str = "ns",
        tz: str | datetime.tzinfo = None
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
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

        # convert to nanoseconds
        nanoseconds = convert_unit_float(
            self.series,
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
        dtype = resolve_dtype(dtype)
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
        # decimal.Decimal is the only recognized decimal implementation
        return self.series.copy()

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """TODO"""
        resolve_dtype(dtype)  # ensures scalar, resolvable
        validate_dtype(dtype, str)

        return self.astype(dtype.pandas_type)
