"""This module contains dispatched cast() implementations for boolean data."""
from pdcast import types
from pdcast.util import wrapper

from .base import cast


@cast.overload("bool", "decimal")
def boolean_to_decimal(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert boolean data to a decimal data type."""
    series = series + dtype.type_def(0)  # ~2x faster than loop
    series.element_type = dtype
    return series


# @cast.overload("bool", "datetime")
# def boolean_to_datetime(
#     series: wrapper.SeriesWrapper,
#     dtype: types.ScalarType,
#     **unused
# ) -> wrapper.SeriesWrapper:
#     """Convert boolean data to a datetime format."""
#     # 2-step conversion: bool -> int, int -> datetime
#     series = cast(series, "int", downcast=False, errors="raise")
#     return cast(series, dtype, **unused)


# @cast.overload("bool", "timedelta")
# def boolean_to_timedelta(
#     series: wrapper.SeriesWrapper,
#     dtype: types.ScalarType,
#     **unused
# ) -> wrapper.SeriesWrapper:
#     """Convert boolean data to a timedelta format."""
#     # 2-step conversion: bool -> int, int -> timedelta
#     series = cast(series, "int", downcast=False, errors="raise")
#     return cast(series, dtype=dtype, **unused)
