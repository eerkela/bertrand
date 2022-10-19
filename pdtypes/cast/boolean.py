from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes.types import resolve_dtype, ElementType
from pdtypes.time import (
    convert_unit_integer, epoch, ns_to_datetime, ns_to_numpy_datetime64,
    ns_to_numpy_timedelta64, ns_to_pandas_timedelta, ns_to_pandas_timestamp,
    ns_to_pydatetime, ns_to_pytimedelta, ns_to_timedelta, timezone
)
from pdtypes.util.type_hints import datetime_like, dtype_like

from .base import SeriesWrapper
from .validate import validate_dtype, validate_series


# TODO: add step size to datetime/timedelta conversions
# -> has to be added in ns_to_numpy_datetime64, ns_to_numpy_timedelta64


# If DEBUG=True, insert argument checks into BooleanSeries conversion methods
DEBUG = True


class BooleanSeries(SeriesWrapper):
    """test"""

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        is_na: pd.Series = None
    ) -> BooleanSeries:
        # DEBUG: assert `series` contains boolean data
        if DEBUG:
            validate_series(series, bool)

        super().__init__(series=series, hasnans=hasnans, is_na=is_na)

    def to_boolean(
        self,
        dtype: dtype_like = bool
    ) -> pd.Series:
        """test"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is boolean-like
        if DEBUG:
            validate_dtype(dtype, bool)

        # do conversion
        if self.hasnans or dtype.nullable:
            return self.astype(dtype.pandas_type)
        return self.astype(dtype.numpy_type)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is integer-like
        if DEBUG:
            validate_dtype(dtype, int)

        # if downcasting, return as 8-bit integer
        if downcast:
            if dtype in resolve_dtype("unsigned"):
                dtype = resolve_dtype(np.uint8)
            else:
                dtype = resolve_dtype(np.int8)

        # do conversion
        if self.hasnans or dtype.nullable:
            return self.astype(dtype.pandas_type)
        return self.astype(dtype.numpy_type)

    def to_float(
        self,
        dtype: dtype_like = float,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is float-like
        if DEBUG:
            validate_dtype(dtype, float)

        # if downcasting, return as 16-bit float
        if downcast:
            dtype = resolve_dtype(np.float16)

        # do conversion
        return self.astype(dtype.numpy_type)

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is complex-like
        if DEBUG:
            validate_dtype(dtype, complex)

        # if downcasting, return as 64-bit complex
        if downcast:
            dtype = resolve_dtype(np.complex64)

        # do conversion
        return self.astype(dtype.numpy_type)

    def to_decimal(self) -> pd.Series:
        """test"""
        return self + decimal.Decimal(0)

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
        """test"""
        # TODO: move this up to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: assert `dtype` is string-like
        if DEBUG:
            validate_dtype(dtype, str)

        return self.astype(dtype.pandas_type)
