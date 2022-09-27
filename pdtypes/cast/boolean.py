from __future__ import annotations
import datetime
import decimal

import numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE

from pdtypes.check import check_dtype, get_dtype, is_dtype, resolve_dtype
from pdtypes.error import error_trace
from pdtypes.time import (
    convert_unit_integer, ns_to_datetime, ns_to_numpy_datetime64,
    ns_to_numpy_timedelta64, ns_to_pandas_timedelta, ns_to_pandas_timestamp,
    ns_to_pydatetime, ns_to_pytimedelta, ns_to_timedelta
)
from pdtypes.util.type_hints import array_like, datetime_like, dtype_like
from pdtypes.util.validate import validate_dtype


# TODO: fix resolve_dtype erasing extension type information, especially when
# it comes to datetime/timedelta types.  "M8[ns]" should point to pd.Timestamp,
# and "m8[ns]" should point to pd.Timedelta, respectively.
# -> this requires adding unit checks


class BooleanSeries:
    """test"""

    def __init__(
        self,
        series: bool | array_like,
        validate: bool = True
    ) -> BooleanSeries:
        if validate and not check_dtype(series, bool):
            err_msg = (f"[{error_trace()}] `series` must contain boolean "
                       f"data, not {get_dtype(series)}")
            raise TypeError(err_msg)

        self.series = series

    def to_boolean(
        self,
        dtype: dtype_like = bool
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)  # TODO: erases extension type
        validate_dtype(dtype, bool)
        return self.series.astype(dtype, copy=True)

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, int)

        # if `downcast=True`, return as 8-bit integer dtype
        if downcast:
            dtype = np.uint8 if is_dtype(dtype, "u") else np.int8
        return self.series.astype(dtype, copy=True)

    def to_float(
        self,
        dtype: dtype_like = float,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, float)

        # if `downcast=True`, return as float16
        if downcast:
            dtype = np.float16
        return self.series.astype(dtype, copy=True)

    def to_complex(
        self,
        dtype: dtype_like = complex,
        downcast: bool = False
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        validate_dtype(dtype, complex)

        # if `downcast=True`, return as complex64
        if downcast:
            dtype = np.complex64
        return self.series.astype(dtype, copy=True)

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
        validate_dtype(dtype, "datetime")
        if not (isinstance(dtype, str) and dtype.lower() == "datetime"):
            dtype = resolve_dtype(dtype)

        # convert to nanoseconds
        nanoseconds = convert_unit_integer(self.to_integer(), unit, "ns")

        # return as a subtype, if directed
        if dtype == pd.Timestamp:
            return ns_to_pandas_timestamp(nanoseconds, tz=tz)
        if dtype == datetime.datetime:
            return ns_to_pydatetime(nanoseconds, tz=tz)
        if dtype == np.datetime64:
            return ns_to_numpy_datetime64(nanoseconds, unit=unit)

        # return as arbitrary datetime objects
        return ns_to_datetime(nanoseconds, tz=tz)

    def to_timedelta(
        self,
        dtype: dtype_like = "timedelta",
        unit: str = "ns",
        since: str | datetime_like = "2001-01-01 00:00:00+0000"
    ) -> pd.Series:
        """TODO"""
        validate_dtype(dtype, "timedelta")
        if not (isinstance(dtype, str) and dtype.lower() == "timedelta"):
            dtype = resolve_dtype(dtype)

        # convert to nanoseconds
        nanoseconds = convert_unit_integer(self.to_integer(), unit, "ns")

        # return as a subtype, if directed
        if dtype == pd.Timedelta:
            return ns_to_pandas_timedelta(nanoseconds)
        if dtype == datetime.timedelta:
            return ns_to_pytimedelta(nanoseconds)
        if dtype == np.datetime64:
            return ns_to_numpy_timedelta64(nanoseconds, since=since, unit=unit)

        # return as arbitrary timedelta objects
        return ns_to_timedelta(nanoseconds, since=since)

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """test"""
        resolve_dtype(dtype)  # ensures scalar, resolvable
        validate_dtype(dtype, str)

        # force string extension type
        if not pd.api.types.is_extension_array_dtype(dtype):
            dtype = DEFAULT_STRING_DTYPE

        return self.series.astype(dtype, copy=True)
