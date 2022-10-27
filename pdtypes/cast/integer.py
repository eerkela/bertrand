from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes.delegate import delegates
from pdtypes.types import resolve_dtype, ElementType
from pdtypes.error import ConversionError, shorten_list
from pdtypes.util.type_hints import datetime_like, dtype_like

from .util.downcast import (
    demote_integer_supertypes, downcast_integer_dtype, downcast_float_series,
    downcast_complex_series
)
from .util.time import (
    convert_unit_integer, epoch, ns_to_datetime, ns_to_numpy_datetime64,
    ns_to_numpy_timedelta64, ns_to_pandas_timedelta, ns_to_pandas_timestamp,
    ns_to_pydatetime, ns_to_pytimedelta, ns_to_timedelta, timezone
)
from .util.validate import validate_dtype, validate_errors

from .base import NumericSeries
from .float import FloatSeries


# TODO: have to be careful with exact comparisons to integer/boolean dtypes
# due to differing nullable settings.


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


@delegates()
class IntegerSeries(NumericSeries):
    """TODO"""

    def __init__(self, series: pd.Series, **kwargs) -> IntegerSeries:
        super().__init__(series=series, **kwargs)

    ##################################
    ####    CONVERSION METHODS    ####
    ##################################

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, bool)
        validate_errors(errors)

        # check series fits within boolean range [0, 1]
        if self.min() < 0 or self.max() > 1:
            if errors != "coerce":
                bad_vals = self[(self < 0) | (self > 1)]
                err_msg = (f"non-boolean value encountered at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)

            # coerce
            self.series = self.abs().clip(0, 1)

        # do conversion
        if self.hasnans or dtype.nullable:
            return self.astype(dtype.pandas_type)
        return self.astype(dtype.numpy_type)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, int)
        validate_errors(errors)

        # demote integer supertypes and handle >64-bit special case
        dtype = demote_integer_supertypes(series=self, dtype=dtype)
        exceeds_range = ("int", "signed", "nullable[int]", "nullable[signed]")
        if any(dtype == t for t in exceeds_range):
            # for each nonmissing value, convert to pyint and fill with pd.NA
            with self.exclude_na(pd.NA):
                self.series = np.frompyfunc(int, 1, 1)(self.series)
            return self.series

        # ensure series min/max fit within dtype range
        if self.min() < dtype.min or self.max() > dtype.max:
            index = (self < dtype.min) | (self > dtype.max)
            if errors != "coerce":
                err_msg = (f"values exceed {str(dtype)} range at index "
                           f"{shorten_list(self.index[index].values)}")
                raise OverflowError(err_msg)

            # coerce by replacing with NA
            extension_type = resolve_dtype(self.dtype).pandas_type
            if extension_type is not None:
                self.series = self.astype(extension_type)
            self.series[index] = pd.NA
            self._hasnans = True

        # attempt to downcast, if applicable
        if downcast:
            dtype = downcast_integer_dtype(series=self, dtype=dtype)

        # convert and return
        if self.hasnans or dtype.nullable:
            return self.astype(dtype.pandas_type)
        return self.astype(dtype.numpy_type)

    def to_float(
        self,
        dtype: dtype_like = float,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, float)
        validate_errors(errors)

        # do naive conversion
        result = self.astype(dtype.numpy_type)

        # backtrack to check for overflow/precision loss
        if self.min() < dtype.min or self.max() > dtype.max:
            reverse = FloatSeries(result)

            # overflow - infs introduced during coercion
            if reverse.hasinfs:
                if errors != "coerce":
                    indices = self.index[reverse.infs].values
                    err_msg = (f"values exceed {str(dtype)} range at index "
                               f"{shorten_list(indices)}")
                    raise OverflowError(err_msg)

                # coerce
                result[reverse.infs] = np.nan

            # precision loss - cast reverse to int and compare with original
            elif errors != "coerce":  # ignore if errors == "coerce"
                reverse = reverse.to_integer(errors="coerce")
                original = self.to_integer()

                # TODO: (result - self).abs() > tol

                # assert reverse == original
                if not reverse.equals(original):
                    indices = (reverse != original)
                    err_msg = (f"precision loss detected at index "
                               f"{shorten_list(indices)} with dtype "
                               f"{repr(str(dtype))}")
                    raise ValueError(err_msg)

        # attempt to downcast if applicable
        if downcast:
            return downcast_float_series(series=result, dtype=dtype)

        # return
        return result

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, complex)
        validate_errors(errors)

        # do naive conversion
        result = self.astype(dtype.numpy_type)

        # backtrack to check for overflow/precision loss
        if self.min() < dtype.min or self.max() > dtype.max:
            reverse = FloatSeries(pd.Series(np.real(result), copy=False))  # TODO: ComplexSeries?

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

        # check for overflow/precision loss
        series = maybe_backtrack_int_to_complex(
            series=series,
            original=self,
            dtype=dtype,
            errors=errors
        )

        # attempt to downcast if applicable
        if downcast:
            return downcast_complex_series(series=series, dtype=dtype)

        # return
        return series

    def to_decimal(self) -> pd.Series:
        """TODO"""
        # for each nonmissing value, convert to decimal.Decimal
        with self.exclude_na(pd.NA):
            # decimal.Decimal can't convert numpy integers in an object series
            if pd.api.types.is_object_dtype(self.series):
                conv = lambda x: decimal.Decimal(int(x))
            else:  # use direct conversion (~2x faster)
                conv = decimal.Decimal

            self.series = np.frompyfunc(conv, 1, 1)(self.series)

        return self.series

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

        # TODO: exclude_na automatically converts to pd.Timestamp

        # convert nonmissing values to ns, then ns to datetime
        with self.exclude_na(pd.NaT):
            # convert to nanoseconds
            nanoseconds = convert_unit_integer(
                self.series,
                unit,
                "ns",
                since=epoch("UTC")
            )

            # pd.Timestamp
            if dtype in pandas_timestamp:
                self.series = ns_to_pandas_timestamp(nanoseconds, tz=tz)

            # datetime.datetime
            elif dtype in pydatetime:
                self.series = ns_to_pydatetime(nanoseconds, tz=tz)

            # np.datetime64
            elif dtype in numpy_datetime64:
                # TODO: gather step size
                self.series = ns_to_numpy_datetime64(nanoseconds, unit=dtype.unit)

            # datetime supertype
            else:
                self.series = ns_to_datetime(nanoseconds, tz=tz)

        return self.series

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

        # TODO: exclude_na automatically casts to pd.Timedelta

        # convert nonmissing values to ns, then ns to timedelta
        with self.exclude_na(pd.NaT):
            # convert to nanoseconds
            self.series = convert_unit_integer(
                self.series,
                unit,
                "ns",
                since=since
            )

            # pd.Timedelta
            if dtype in pandas_timedelta:
                self.series = ns_to_pandas_timedelta(self.series)

            # datetime.timedelta
            elif dtype in pytimedelta:
                self.series = ns_to_pytimedelta(self.series)

            # np.timedelta64
            elif dtype in numpy_timedelta64:
                # TODO: gather step size
                self.series = ns_to_numpy_timedelta64(
                    self.series,
                    unit=dtype.unit,
                    since=since
                )

            # timedelta supertype
            else:
                self.series = ns_to_timedelta(self.series, since=since)

        return self.series

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, str)

        # do conversion
        return self.series.astype(dtype.pandas_type)
