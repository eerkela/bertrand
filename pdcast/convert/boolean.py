"""This module contains overloaded conversion logic for boolean data types.
"""
from pdcast import types
from pdcast.util import wrapper

from .base import to_integer, to_decimal, to_datetime, to_timedelta


#######################
####    DECIMAL    ####
#######################


@to_decimal.overload("bool")
def boolean_to_decimal(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert boolean data to a decimal format."""
    series = series + dtype.type_def(0)  # ~2x faster than loop
    series.element_type = dtype
    return series


########################
####    DATETIME    ####
########################


@to_datetime.overload("bool")
def boolean_to_datetime(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert boolean data to a datetime format."""
    # 2-step conversion: bool -> int, int -> datetime
    series = to_integer(series, "int", downcast=False, errors="raise")
    return to_datetime(series, dtype, **unused)


#########################
####    TIMEDELTA    ####
#########################


@to_timedelta.overload("bool")
def boolean_to_timedelta(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert boolean data to a timedelta format."""
    # 2-step conversion: bool -> int, int -> timedelta
    series = to_integer(series, "int", downcast=False, errors="raise")
    return to_timedelta(series, dtype=dtype, **unused)
