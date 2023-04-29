"""This module contains dispatched cast() implementations for integer data."""
from __future__ import annotations
from functools import partial

import numpy as np
import pytz

from pdcast import types
from pdcast.util import wrapper
from pdcast.util.round import Tolerance
from pdcast.util.time import Epoch, convert_unit

from .base import (
    cast, generic_to_boolean, generic_to_integer, generic_to_float,
    generic_to_string
)


@cast.overload("int", "bool")
def integer_to_boolean(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a boolean data type."""
    series, dtype = series.boundscheck(dtype, errors=errors)
    return generic_to_boolean(series, dtype, errors=errors)


@cast.overload("int", "int")
def integer_to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to another integer data type."""
    series, dtype = series.boundscheck(dtype, errors=errors)
    return generic_to_integer(
        series=series,
        dtype=dtype,
        downcast=downcast,
        errors=errors
    )


@cast.overload("int", "float")
def integer_to_float(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a float data type."""
    # NOTE: integers can always be exactly represented as floats as long as
    # their width in bits fits within the significand of the specified floating
    # point type with exponent 1 (as listed in the IEEE 754 specification).
    if int(series.min) < dtype.min or int(series.max) > dtype.max:
        # 2-step conversion: int -> decimal, decimal -> float
        series = cast(series, "decimal", errors=errors)
        return cast(
            series,
            dtype,
            tol=tol,
            downcast=downcast,
            errors=errors,
            **unused
        )

    return generic_to_float(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


@cast.overload("int", "complex")
def integer_to_complex(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a complex data type."""
    # 2-step conversion: int -> float, float -> complex
    series = cast(
        series,
        "float",
        tol=tol,
        downcast=None,
        errors=errors
    )
    return cast(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors,
        **unused
    )


@cast.overload("int", "decimal")
def integer_to_decimal(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a decimal data type."""
    result = series + dtype.type_def(0)  # ~2x faster than apply loop
    result.element_type = dtype
    return result


# @cast.overload("int", "datetime")
# def integer_to_datetime(
#     series: wrapper.SeriesWrapper,
#     dtype: types.ScalarType,
#     unit: str,
#     step_size: int,
#     rounding: str,
#     since: Epoch,
#     tz: pytz.BaseTzInfo,
#     errors: str,
#     **unused
# ) -> wrapper.SeriesWrapper:
#     """Convert integer data to a datetime data type."""
#     # convert to ns
#     series = to_ns(series, unit=unit, step_size=step_size, since=since)

#     # account for non-utc epoch
#     if since:
#         series.series += since.offset

#     # check for overflow and upcast if applicable
#     series, dtype = series.boundscheck(dtype, errors=errors)

#     # convert to final representation
#     return dtype.from_ns(
#         series,
#         unit=unit,
#         step_size=step_size,
#         rounding=rounding,
#         since=since,
#         tz=tz,
#         errors=errors,
#         **unused
#     )


# @to_timedelta.overload("int")
# def integer_to_timedelta(
#     series: wrapper.SeriesWrapper,
#     dtype: types.ScalarType,
#     unit: str,
#     step_size: int,
#     since: Epoch,
#     errors: str,
#     **unused
# ) -> wrapper.SeriesWrapper:
#     """Convert integer data to a timedelta data type."""
#     # convert to ns
#     series = to_ns(series, unit=unit, step_size=step_size, since=since)

#     # check for overflow and upcast if necessary
#     series, dtype = series.boundscheck(dtype, errors=errors)

#     # convert to final representation
#     return dtype.from_ns(
#         series,
#         unit=unit,
#         step_size=step_size,
#         since=since,
#         errors=errors,
#         **unused
#     )


@cast.overload("int", "string")
def integer_to_string(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    base: int,
    format: str,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a string data type in any base."""
    # use non-decimal base in conjunction with format
    if base and base != 10:
        if format:
            call = lambda x: f"{int_to_base(x, base=base):{format}}"
        else:
            call = partial(int_to_base, base=base)
        return series.apply_with_errors(
            call,
            errors=errors,
            element_type=dtype
        )

    return generic_to_string(
        series=series,
        dtype=dtype,
        format=format,
        errors=errors
    )


#######################
####    PRIVATE    ####
#######################


base_lookup = np.array(
    [chr(ord("0") + i) for i in range(10)] + 
    [chr(ord("A") + i) for i in range(26)]
)


def int_to_base(val: str, base: int):
    """Convert an integer into a string with the given base."""
    if not val:
        return "0"

    negative = val < 0
    if negative:
        val = abs(val)

    chars = []
    while val:
        chars.append(base_lookup[val % base])
        val //= base

    result = "".join(chars[::-1])
    if negative:
        result = "-" + result
    return result


def to_ns(
    series: wrapper.SeriesWrapper,
    unit: str,
    step_size: int,
    since: Epoch
) -> wrapper.SeriesWrapper:
    """Convert an integer number of time units into nanoseconds from a given
    epoch.
    """
    # convert to python int to avoid overflow
    series = to_integer(series, int, downcast=False, errors="raise")

    # trivial case
    if unit == "ns" and step_size == 1:
        return series

    # convert to ns
    if step_size != 1:
        series.series *= step_size
    return wrapper.SeriesWrapper(
        convert_unit(series.series, unit, "ns", since=since),
        hasnans=series.hasnans,
        element_type=int
    )
