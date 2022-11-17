from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes.delegate import delegates
from pdtypes.types import resolve_dtype, ElementType
from pdtypes.util.type_hints import datetime_like, dtype_like

from .util.time import (
    convert_unit_integer, epoch, epoch_ns, is_utc, ns_to_datetime,
    ns_to_numpy_datetime64, ns_to_numpy_timedelta64, ns_to_pandas_timedelta,
    ns_to_pandas_timestamp, ns_to_pydatetime, ns_to_pytimedelta,
    ns_to_timedelta, timezone
)
from .util.validate import validate_dtype, validate_series

from .base import SeriesWrapper


# TODO: add step size to datetime/timedelta conversions
# -> has to be added in ns_to_numpy_datetime64, ns_to_numpy_timedelta64

# TODO: have to be careful with exact comparisons to integer/boolean dtypes
# due to differing nullable settings.


@delegates()
class BooleanSeries(SeriesWrapper):
    """TODO"""

    def __init__(self, series: pd.Series, **kwargs) -> BooleanSeries:
        super().__init__(series=series, **kwargs)

    def to_boolean(
        self,
        dtype: dtype_like = bool
    ) -> pd.Series:
        """TODO"""
        # TODO: errors on scalar array input
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, bool)

        # do conversion
        if self.hasnans or dtype.nullable:
            return self.astype(dtype.pandas_type, copy=False)
        return self.astype(dtype.numpy_type, copy=False)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, int)

        # if downcasting, return as 8-bit integer
        if downcast:
            if dtype in resolve_dtype("unsigned"):
                dtype = resolve_dtype(
                    np.uint8,
                    sparse=dtype.sparse,
                    categorical=dtype.categorical,
                    nullable=dtype.nullable
                )
            else:
                dtype = resolve_dtype(
                    np.int8,
                    sparse=dtype.sparse,
                    categorical=dtype.categorical,
                    nullable=dtype.nullable
                )

        # do conversion
        self.series = self.to_boolean()
        if self.hasnans or dtype.nullable:
            return self.astype(dtype.pandas_type)
        return self.astype(dtype.numpy_type)

    def to_float(
        self,
        dtype: dtype_like = float,
        downcast: bool = False
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, float)

        # if downcasting, return as 16-bit float
        if downcast:
            dtype = resolve_dtype(np.float16)

        # do conversion
        return self.to_boolean().astype(dtype.numpy_type)

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, complex)

        # if downcasting, return as 64-bit complex
        if downcast:
            dtype = resolve_dtype(
                np.complex64,
                sparse=dtype.sparse,
                categorical=dtype.categorical
            )

        # do conversion - NOTE: direct astype(complex) fails on pd.NA
        with self.exclude_na(complex("nan+nanj"), dtype.numpy_type):
            self.series = self.series.astype(dtype.numpy_type)

        return self.series

    def to_decimal(
        self,
        dtype: dtype_like = "decimal",
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, "decimal")

        with self.exclude_na(pd.NA):
            self.series = self + decimal.Decimal(0)

        return self.series

    def to_datetime(
        self,
        dtype: dtype_like = "datetime",
        unit: str = "ns",
        tz: str | datetime.tzinfo = None,
        since: str | datetime_like = "UTC"
    ) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, "datetime")
        tz = timezone(tz)
        since = epoch(since)

        # ElementType objects for each datetime subtype
        pandas_timestamp = resolve_dtype(pd.Timestamp)
        pydatetime = resolve_dtype(datetime.datetime)
        numpy_datetime64 = resolve_dtype(np.datetime64)

        # convert nonmissing values to ns, then ns to datetime
        with self.exclude_na(pd.NaT):
            self.series = convert_unit_integer(
                self.astype(np.int64).astype("O"),
                unit,
                "ns",
                since=since
            )
            self.series += epoch_ns(since)

            # pd.Timestamp
            if dtype in pandas_timestamp:
                self.series = ns_to_pandas_timestamp(self.series, tz=tz)

            # datetime.datetime
            elif dtype in pydatetime:
                self.series = ns_to_pydatetime(self.series, tz=tz)

            # np.datetime64
            elif dtype in numpy_datetime64:
                # ensure tz is UTC or None
                if tz and not is_utc(tz):
                    err_msg = (
                        "numpy.datetime64 objects do not carry timezone "
                        "information (must be UTC or None)"
                    )
                    raise RuntimeError(err_msg)
 
                # TODO: gather step size
                self.series = ns_to_numpy_datetime64(
                    self.series,
                    unit=dtype.unit,
                    step_size=dtype.step_size
                )

            # datetime supertype
            else:
                self.series = ns_to_datetime(self.series, tz=tz)

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

        # convert nonmissing values to ns, then ns to timedelta
        with self.exclude_na(pd.NaT):
            nanoseconds = convert_unit_integer(
                self.series.astype(np.uint8),
                unit,
                "ns",
                since=since
            )

            # pd.Timedelta
            if dtype in pandas_timedelta:
                self.series = ns_to_pandas_timedelta(nanoseconds)

            # datetime.timedelta
            elif dtype in pytimedelta:
                self.series = ns_to_pytimedelta(nanoseconds)

            # np.timedelta64
            elif dtype in numpy_timedelta64:
                # TODO: gather step size
                self.series = ns_to_numpy_timedelta64(
                    nanoseconds,
                    unit=dtype.unit,
                    since=since
                )

            # timedelta supertype
            else:
                self.series = ns_to_timedelta(nanoseconds, since=since)

        return self.series

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """TODO"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, str)

        return self.astype(dtype.pandas_type)
