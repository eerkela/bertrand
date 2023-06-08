"""This module contains dispatched cast() implementations for boolean data."""
# pylint: disable=unused-argument
import pandas as pd

from pdcast import types

from .base import cast


# TODO: boolean -> datetime gives infinite recursion
# pdcast.cast([True, False, pd.NA], "datetime")

# -> same with timedelta.  Appears to be related to new abstract interface.

# TODO: bool -> object does not return a dtype: object array
# -> pdcast.cast([True, False], "object")  returns object
# -> pdcast.cast([True, False, None], "object")  returns bool[python]



@cast.overload("bool", "decimal")
def boolean_to_decimal(
    series: pd.Series,
    dtype: types.VectorType,
    **unused
) -> pd.Series:
    """Convert boolean data to a decimal data type."""
    target = dtype.dtype
    if isinstance(target, types.ObjectDtype):
        series = series + dtype.type_def(0)  # ~2x faster than loop
    return series.astype(target)


@cast.overload("bool", "datetime")
def boolean_to_datetime(
    series: pd.Series,
    dtype: types.VectorType,
    **unused
) -> pd.Series:
    """Convert boolean data to a datetime format."""
    # 2-step conversion: bool -> int, int -> datetime
    series = cast(series, "int", downcast=False, errors="raise")
    return cast(series, dtype, **unused)


@cast.overload("bool", "timedelta")
def boolean_to_timedelta(
    series: pd.Series,
    dtype: types.VectorType,
    **unused
) -> pd.Series:
    """Convert boolean data to a timedelta format."""
    # 2-step conversion: bool -> int, int -> timedelta
    series = cast(series, "int", downcast=False, errors="raise")
    return cast(series, dtype, **unused)
