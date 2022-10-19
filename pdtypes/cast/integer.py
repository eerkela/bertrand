from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE

# from pdtypes.check import (
#     check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
# )
from pdtypes.types import check_dtype, get_dtype, resolve_dtype
from pdtypes.error import ConversionError, error_trace, shorten_list
from pdtypes.util.downcast import integral_range
from pdtypes.util.type_hints import datetime_like, dtype_like
# from pdtypes.util.validate import validate_dtype, validate_errors


from pdtypes.time import (
    convert_unit_integer, epoch, ns_to_datetime, ns_to_numpy_datetime64,
    ns_to_numpy_timedelta64, ns_to_pandas_timedelta, ns_to_pandas_timestamp,
    ns_to_pydatetime, ns_to_pytimedelta, ns_to_timedelta, timezone
)

from .base import SeriesWrapper

from .float import FloatSeries
from .validate import validate_dtype, validate_errors, validate_series


# If DEBUG=True, insert argument checks into BooleanSeries conversion methods
DEBUG = True


class IntegerSeries(SeriesWrapper):
    """TODO"""

    unwrapped = SeriesWrapper.unwrapped + (
        "_min", "_max", "_idxmin", "_idxmax"
    )

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

        super().__init__(series=series, hasnans=hasnans, is_na=is_na)
        self._min = min_val
        self._idxmin = min_index
        self._max = max_val
        self._idxmax = max_index

    #############################
    ####    CACHE MIN/MAX    ####
    #############################

    def min(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.min()."""
        # cached
        if self._min is not None:
            return self._min

        # uncached
        self._min = self._series.min(*args, **kwargs)
        return self._min

    def argmin(self, *args, **kwargs) -> int:
        """Alias for IntegerSeries.min()."""
        return self.min(*args, **kwargs)

    def max(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.max()."""
        # cached
        if self._max is not None:
            return self._max

        # uncached
        self._max = self._series.max(*args, **kwargs)
        return self._max

    def argmax(self, *args, **kwargs) -> int:
        """Alias for IntegerSeries.max()."""
        return self.max(*args, **kwargs)

    def idxmin(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.idxmin()."""
        # cached
        if self._idxmin is not None:
            return self._idxmin

        # uncached
        self._idxmin = self._series.idxmin(*args, **kwargs)
        return self._idxmin

    def idxmax(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.idxmax()."""
        # cached
        if self._idxmax is not None:
            return self._idxmax

        # uncached
        self._idxmax = self._series.idxmax(*args, **kwargs)
        return self._idxmax

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

        # integer supertype - can be arbitrarily large
        if dtype == int:
            if series.min() < -2**63 or series.max() > 2**63 - 1:  # > int64
                if series.min() >= 0 and series.max() <= 2**64 - 1:  # < uint64
                    return series.astype(np.uint64)

                # > int64 and > uint64, return as built-in python ints
                return np.frompyfunc(int, 1, 1)(series)

            # extended range isn't needed, demote to int64
            dtype = resolve_dtype(np.int64)

        # signed integer supertype - suppress conversion to uint64
        elif dtype == "signed":
            if series.min() < -2**63 or series.max() > 2**63 - 1:  # > int64
                return np.frompyfunc(int, 1, 1)(series)

            # extended range isn't needed, demote to int64
            dtype = resolve_dtype(np.int64)

        # unsigned integer supertype - demote to uint64
        elif dtype == "unsigned":
            dtype = resolve_dtype(np.uint64)

        # ensure series min/max fit within dtype range
        if series.min() < dtype.min or series.max() > dtype.max:
            if errors != "coerce":
                bad_vals = series[(series < dtype.min) | (series > dtype.max)]
                err_msg = (f"values exceed {str(dtype)} range at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)

            # coerce by replacing series
            series = IntegerSeries(series.series, hasnans=True)
            series[(series < dtype.min) | (series > dtype.max)] = pd.NA

        # attempt to downcast, if applicable
        if downcast:
            # resolve all possible dtypes that are smaller than given
            if dtype in resolve_dtype("unsigned"):
                smaller = [np.uint8, np.uint16, np.uint32, np.uint64]
            else:
                smaller = [np.int8, np.int16, np.int32, np.int64]
            smaller = [resolve_dtype(t) for t in smaller]

            # search for smaller dtypes that can represent series
            for small in smaller[:smaller.index(dtype)]:
                if series.min() >= small.min and series.max() <= small.max:
                    dtype = small
                    break  # stop at smallest

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

        # do naive conversion and check for overflow/precision loss afterwards
        series = self.astype(dtype.numpy_type)

        # check for precision loss
        if self.min() < dtype.min or self.max() > dtype.max:
            # series might be imprecise -> confirm by reversing conversion
            reverse = FloatSeries(series)  # TODO: revisit with completed FloatSeries

            # err state 1: infs introduced during coercion (overflow)
            if reverse.hasinfs:
                if errors != "coerce":
                    bad_vals = series[reverse.infs].index.values
                    err_msg = (f"values exceed {str(dtype)} range at "
                               f"index {shorten_list(bad_vals.index.values)}")
                    raise ConversionError(err_msg, bad_vals)

                # coerce
                series[reverse.infs] = np.nan

            # err state 2: encountered rounding errors (precision loss)
            elif errors != "coerce":
                # compute reverse result
                reverse_result = reverse.to_integer(errors="coerce")

                # assert equal
                if not self.to_integer().equals(reverse_result):
                    bad_vals = series[self.series != reverse_result]
                    err_msg = (f"precision loss detected at index "
                               f"{shorten_list(bad_vals.index.values)} with "
                               f"dtype {repr(str(dtype))}")
                    raise ConversionError(err_msg, bad_vals)

        # attempt to downcast, if applicable
        if downcast:  # search for smaller dtypes that can represent series
            smaller = [
                resolve_dtype(t) for t in (
                    np.float16, np.float32, np.float64, np.longdouble
                )
            ]
            for small in smaller[:smaller.index(dtype)]:
                attempt = series.astype(small.numpy_type)
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
        if self.min() < dtype.min or self.max() > dtype.max:
            # series might be imprecise -> confirm by reversing conversion
            reverse = FloatSeries(np.real(series))  # TODO: revist with completed FloatSeries

            # err state 1: infs introduced during coercion (overflow)
            if reverse.hasinfs:
                if errors != "coerce":
                    bad_vals = series[reverse.infs]
                    err_msg = (f"values exceed {str(dtype)} range at "
                               f"index {shorten_list(bad_vals.index.values)}")
                    raise ConversionError(err_msg, bad_vals)

                # coerce
                series[reverse.infs] += complex(np.nan, np.nan)

            # err state 2: encountered rounding errors (precision loss)
            elif errors != "coerce":
                # compute reverse result
                reverse_result = reverse.to_integer(errors="coerce")

                # assert equal
                if not self.to_integer().equals(reverse_result):
                    bad_vals = series[self.series != reverse_result]
                    err_msg = (f"precision loss detected at index "
                               f"{shorten_list(bad_vals.index.values)} with "
                               f"dtype {repr(str(dtype))}")
                    raise ConversionError(err_msg, bad_vals)

        # attempt to downcast, if applicable
        if downcast:  # search for smaller dtypes that can represent series
            smaller = [
                resolve_dtype(t) for t in (
                    np.complex64, np.complex128, np.clongdouble
                )
            ]
            for small in smaller[:smaller.index(dtype)]:
                attempt = series.astype(small.numpy_type)
                if (attempt == series).all():
                    return attempt  # stop at smallest

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
            dtype = resolve_dtype(pd.Timestamp)
            # TODO: retain sparse, categorical flags
            # -> might be superseded if sparse, categorical handled in cast()
            # dtype = resolve_dtype(
            #     pd.Timestamp,
            #     sparse=dtype.sparse,
            #     categorical=dtype.categorical
            # )

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
            dtype = resolve_dtype(pd.Timedelta)
            # TODO: retain sparse, categorical flags
            # -> might be superseded if sparse, categorical handled in cast()
            # dtype = resolve_dtype(
            #     pd.Timedelta,
            #     sparse=dtype.sparse,
            #     categorical=dtype.categorical
            # )

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
