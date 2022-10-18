from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE

# from pdtypes.check import check_dtype, get_dtype, is_dtype, resolve_dtype
from pdtypes.types import check_dtype, get_dtype, resolve_dtype
from pdtypes.error import error_trace
from pdtypes.time import (
    convert_unit_integer, epoch, ns_to_datetime, ns_to_numpy_datetime64,
    ns_to_numpy_timedelta64, ns_to_pandas_timedelta, ns_to_pandas_timestamp,
    ns_to_pydatetime, ns_to_pytimedelta, ns_to_timedelta
)
from pdtypes.util.type_hints import array_like, datetime_like, dtype_like
# from pdtypes.util.validate import validate_dtype


from .validate import validate_dtype


# TODO: add step size to datetime/timedelta conversions
# -> has to be added in ns_to_numpy_datetime64, ns_to_numpy_timedelta64


# TODO: "M8[ns]" should point to pd.Timestamp, and "m8[ns]" should point to
# pd.Timedelta, respectively.


class BooleanSeries:
    """test"""

    def __init__(
        self,
        series: bool | array_like,
        hasnans: bool = False,
        validate: bool = True
    ) -> BooleanSeries:
        if validate and not check_dtype(series, bool):
            err_msg = (f"[{error_trace()}] `series` must contain boolean "
                       f"data, not {get_dtype(series)}")
            raise TypeError(err_msg)

        self.series = series
        self.hasnans = hasnans

    def to_boolean(
        self,
        dtype: dtype_like = bool
    ) -> pd.Series:
        """test"""
        dtype = validate_dtype(dtype, bool)

        # do conversion
        if self.hasnans or dtype.nullable:
            return self.series.astype(dtype.pandas_type)
        return self.series.astype(dtype.numpy_dtype)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = validate_dtype(dtype, int)

        # if downcasting, return as 8-bit integer
        if downcast:
            if dtype in resolve_dtype("unsigned"):
                dtype = resolve_dtype(np.uint8)
            else:
                dtype = resolve_dtype(np.int8)
        elif dtype == int or dtype == "signed":
            dtype = resolve_dtype(np.int64)
        elif dtype == "unsigned":
            dtype = resolve_dtype(np.uint64)

        # do conversion
        if self.hasnans or dtype.nullable:
            return self.series.astype(dtype.pandas_type)
        return self.series.astype(dtype.numpy_type)

    def to_float(
        self,
        dtype: dtype_like = float,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = validate_dtype(dtype, float)

        # if downcasting, return as 16-bit float
        if downcast:
            dtype = resolve_dtype(np.float16)

        # do conversion
        return self.series.astype(dtype.numpy_type)

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = validate_dtype(dtype, complex)

        # if downcasting, return as 64-bit complex
        if downcast:
            dtype = resolve_dtype(np.complex64)

        # do conversion
        return self.series.astype(dtype.numpy_type)

    def to_decimal(self) -> pd.Series:
        """test"""
        return self.series + decimal.Decimal(0)

    def to_datetime(
        self,
        dtype: dtype_like = "datetime",
        unit: str = "ns",
        tz: str | datetime.tzinfo = None
    ) -> pd.Series:
        """TODO"""
        dtype = validate_dtype(dtype, "datetime")
        if dtype in resolve_dtype(np.datetime64):
            if dtype.unit == "ns" and dtype.step_size == 1:
                dtype = resolve_dtype(pd.Timestamp)
            elif dtype.unit is not None:
                unit = dtype.unit

        # convert to nanoseconds
        nanoseconds = convert_unit_integer(self.to_integer(), unit, "ns")

        # pd.Timestamp
        if dtype in resolve_dtype(pd.Timestamp):
            return ns_to_pandas_timestamp(nanoseconds, tz=tz)

        # datetime.datetime
        if dtype in resolve_dtype(datetime.datetime):
            return ns_to_pydatetime(nanoseconds, tz=tz)

        # np.datetime64
        if dtype in resolve_dtype(np.datetime64):
            return ns_to_numpy_datetime64(nanoseconds, unit=unit)

        # datetime supertype
        return ns_to_datetime(nanoseconds, tz=tz)

    def to_timedelta(
        self,
        dtype: dtype_like = "timedelta",
        unit: str = "ns",
        since: str | datetime_like = "2001-01-01 00:00:00+0000"
    ) -> pd.Series:
        """TODO"""
        dtype = validate_dtype(dtype, "timedelta")
        if dtype in resolve_dtype(np.timedelta64):
            if dtype.unit == "ns" and dtype.step_size == 1:
                dtype = resolve_dtype(pd.Timedelta)
            elif dtype.unit is not None:
                unit = dtype.unit

        # convert to nanoseconds
        nanoseconds = convert_unit_integer(
            self.to_integer(),
            unit,
            "ns",
            since=epoch(since)
        )

        # pd.Timedelta
        if dtype in resolve_dtype(pd.Timedelta):
            return ns_to_pandas_timedelta(nanoseconds)

        # datetime.timedelta
        if dtype in resolve_dtype(datetime.timedelta):
            return ns_to_pytimedelta(nanoseconds)

        # np.timedelta64
        if dtype in resolve_dtype(np.datetime64):
            return ns_to_numpy_timedelta64(nanoseconds, since=since, unit=unit)

        # timedelta supertype
        return ns_to_timedelta(nanoseconds, since=since)

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """test"""
        dtype = validate_dtype(dtype, str)
        return self.series.astype(dtype.pandas_type)
