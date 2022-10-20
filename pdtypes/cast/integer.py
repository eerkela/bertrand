from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE

# from pdtypes.check import (
#     check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
# )
from pdtypes.types import check_dtype, get_dtype, resolve_dtype, ElementType
from pdtypes.error import ConversionError, error_trace, shorten_list
from pdtypes.util.downcast import integral_range
from pdtypes.util.type_hints import datetime_like, dtype_like
# from pdtypes.util.validate import validate_dtype, validate_errors



from pdtypes.time import (
    convert_unit_integer, epoch, ns_to_datetime, ns_to_numpy_datetime64,
    ns_to_numpy_timedelta64, ns_to_pandas_timedelta, ns_to_pandas_timestamp,
    ns_to_pydatetime, ns_to_pytimedelta, ns_to_timedelta, timezone
)

from .base import NumericSeries
from .float import FloatSeries

from .util.downcast import (
    demote_integer_supertypes, downcast_integer_dtype, downcast_float_series,
    downcast_complex_series
)
from .util.validate import validate_dtype, validate_errors, validate_series


# TODO: have to be careful with exact comparisons to integer/boolean dtypes
# due to differing nullable settings.


# If DEBUG=True, insert argument checks into BooleanSeries conversion methods
DEBUG = True


def reject_non_boolean(
    series: IntegerSeries,
    errors: str
) -> IntegerSeries:
    """Reject any IntegerSeries that contains non-boolean values (i.e. not 0
    or 1).
    """
    if series.min() < 0 or series.max() > 1:
        if errors != "coerce":
            bad_vals = series[(series < 0) | (series > 1)]
            err_msg = (f"non-boolean value encountered at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # coerce
        series = IntegerSeries(
            series.abs().clip(0, 1),
            hasnans=series.hasnans
        )

    return series


def reject_integer_overflow(
    series: IntegerSeries,
    dtype: ElementType,
    errors: str
) -> IntegerSeries:
    """Reject any IntegerSeries whose range exceeds the maximum available for
    the given ElementType.
    """
    if series.min() < dtype.min or series.max() > dtype.max:
        if errors != "coerce":
            bad_vals = series[(series < dtype.min) | (series > dtype.max)]
            err_msg = (f"values exceed {str(dtype)} range at index "
                        f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # make series nullable to prepare for NA coercion
        series = series.series
        extension_type = resolve_dtype(series.dtype).pandas_type
        if extension_type is not None:
            series = series.astype(extension_type)

        # coerce by replacing with NA
        series[(series < dtype.min) | (series > dtype.max)] = pd.NA
        series = IntegerSeries(series, hasnans=True)

    return series


def maybe_backtrack_int_to_float(
    series: pd.Series,
    original: IntegerSeries,
    dtype: ElementType,
    errors: str
) -> pd.Series:
    """Reverse an integer to float conversion, checking for overflow/precision
    loss and applying the selected error-handling rule.
    """
    if original.min() < dtype.min or original.max() > dtype.max:
        # series might be imprecise -> confirm by backtracking conversion
        reverse = FloatSeries(series)

        # err state 1: infs introduced during coercion (overflow)
        if reverse.hasinfs:
            if errors != "coerce":
                bad_vals = series[reverse.infs]
                err_msg = (f"values exceed {str(dtype)} range at index "
                        f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)

            # coerce
            series[reverse.infs] = np.nan

        # err state 2: precision loss due to floating point rounding
        elif errors != "coerce":
            # compute reverse result + original equivalent
            reverse = reverse.to_integer(errors="coerce")
            original = original.to_integer()

            # assert reverse == original
            if not reverse.equals(original):
                bad_vals = series[reverse != original]
                err_msg = (f"precision loss detected at index "
                        f"{shorten_list(bad_vals.index.values)} with dtype "
                        f"{repr(str(dtype))}")
                raise ConversionError(err_msg, bad_vals)

    return series


def maybe_backtrack_int_to_complex(
    series: pd.Series,
    original: IntegerSeries,
    dtype: ElementType,
    errors: str
) -> pd.Series:
    """Reverse an integer to float conversion, checking for overflow/precision
    loss and applying the selected error-handling rule.
    """
    if original.min() < dtype.min or original.max() > dtype.max:
        # series might be imprecise -> confirm by backtracking conversion
        reverse = FloatSeries(pd.Series(np.real(series), copy=False))

        # err state 1: infs introduced during coercion (overflow)
        if reverse.hasinfs:
            if errors != "coerce":
                bad_vals = series[reverse.infs]
                err_msg = (f"values exceed {str(dtype)} range at index "
                        f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)

            # coerce
            series[reverse.infs] += complex(np.nan, np.nan)

        # err state 2: precision loss due to floating point rounding
        elif errors != "coerce":
            # compute reverse result + original equivalent
            reverse = reverse.to_integer(errors="coerce")
            original = original.to_integer()

            # assert reverse == original
            if not reverse.equals(original):
                bad_vals = series[reverse != original]
                err_msg = (f"precision loss detected at index "
                            f"{shorten_list(bad_vals.index.values)} with dtype "
                            f"{repr(str(dtype))}")
                raise ConversionError(err_msg, bad_vals)

    return series


class IntegerSeries(NumericSeries):
    """TODO"""

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        is_na: pd.Series = None,
        min_val: int = None,
        min_index: int = None,
        max_val: int = None,
        max_index: int = None
    ) -> IntegerSeries:
        if DEBUG:
            validate_series(series, int)

        super().__init__(
            series=series,
            hasnans=hasnans,
            is_na=is_na,
            min_val=min_val,
            min_index=min_index,
            max_val=max_val,
            max_index=max_index
        )

    ##################################
    ####    CONVERSION METHODS    ####
    ##################################

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is boolean-like
        if DEBUG:
            validate_dtype(dtype, bool)
            validate_errors(errors)

        # series might change depending on coercion
        series = self

        # check series fits within boolean range [0, 1]
        series = reject_non_boolean(series=series, errors=errors)

        # do conversion
        if series.hasnans or dtype.nullable:
            return series.astype(dtype.pandas_type)
        return series.astype(dtype.numpy_type)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is boolean-like
        if DEBUG:
            validate_dtype(dtype, int)
            validate_errors(errors)

        # series might change depending on coercion
        series = self

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

        # attempt to downcast, if applicable
        if downcast:
            dtype = downcast_integer_dtype(series=series, dtype=dtype)

        # convert and return
        if series.hasnans or dtype.nullable:
            return series.astype(dtype.pandas_type)
        return series.astype(dtype.numpy_type)

    def to_float(
        self,
        dtype: dtype_like = float,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is boolean-like
        if DEBUG:
            validate_dtype(dtype, float)
            validate_errors(errors)

        # do naive conversion
        series = self.astype(dtype.numpy_type)

        # check for overflow/precision loss
        series = maybe_backtrack_int_to_float(
            series=series,
            original=self,
            dtype=dtype,
            errors=errors
        )

        # attempt to downcast if applicable
        if downcast:
            return downcast_float_series(series=series, dtype=dtype)

        # return
        return series

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is boolean-like
        if DEBUG:
            validate_dtype(dtype, complex)
            validate_errors(errors)

        # do naive conversion and check for overflow/precision loss afterwards
        series = self.series.astype(dtype.numpy_type)

        # check for precision loss
        series = maybe_backtrack_int_to_complex(
            series=series,
            original=self,
            dtype=dtype,
            errors=errors
        )

        # attempt to downcast, if applicable
        if downcast:  # search for smaller dtypes that can represent series
            return downcast_complex_series(series=series, dtype=dtype)

        # return
        return series

    def to_decimal(self) -> pd.Series:
        """TODO"""
        # decimal.Decimal can't convert numpy integers in an object series
        if pd.api.types.is_object_dtype(self.series):
            conv = lambda x: decimal.Decimal(int(x))
        else:  # use direct conversion (~2x faster)
            conv = decimal.Decimal

        return np.frompyfunc(conv, 1, 1)(self)

    def to_datetime(
        self,
        dtype: dtype_like = "datetime",
        unit: str = "ns",
        tz: str | datetime.tzinfo = None
    ) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)
        tz = timezone(tz)

        # DEBUG: assert `dtype` is datetime-like
        if DEBUG:
            validate_dtype(dtype, "datetime")

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
        nanoseconds = convert_unit_integer(
            self.series,
            unit,
            "ns",
            since=epoch("UTC")
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
        since=epoch(since)

        # DEBUG: assert `dtype` is timedelta-like
        if DEBUG:
            validate_dtype(dtype, "timedelta")

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
        nanoseconds = convert_unit_integer(
            self.series,
            unit,
            "ns",
            since=since
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

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """TODO"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is boolean-like
        if DEBUG:
            validate_dtype(dtype, str)

        # do conversion
        return self.series.astype(dtype.pandas_type)
