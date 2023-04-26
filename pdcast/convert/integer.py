"""This module contains overloaded conversion logic for integer data types.
"""
from __future__ import annotations
from functools import partial

import numpy as np
import pytz

from pdcast import types
from pdcast.util import wrapper
from pdcast.util.round import Tolerance
from pdcast.util.time import Epoch, convert_unit

from .base import (
    to_boolean, to_integer, to_float, to_decimal, to_complex, to_datetime,
    to_timedelta, to_string
)


# with targetable conversions, maybe the whole suite could just be recreated
# that way

# cast.overload("int", "bool")
# cast.overload("int", "float")
# cast.overload("int", "complex")
# cast.overload("int", "decimal")
# cast.overload("int", "datetime")
# cast.overload("int", "datetime[pandas]")
# cast.overload("int", "datetime[numpy]")
# cast.overload("int", "datetime[python]")
# cast.overload("int", "timedelta")


#######################
####    BOOLEAN    ####
#######################


@to_boolean.overload("int")
def integer_to_boolean(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a boolean data type."""
    series, dtype = series.boundscheck(dtype, errors=errors)
    return to_boolean.generic(series, dtype, errors=errors)


#######################
####    INTEGER    ####
#######################


@to_integer.overload("int")
def integer_to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to another integer data type."""
    series, dtype = series.boundscheck(dtype, errors=errors)
    return to_integer.generic(
        series=series,
        dtype=dtype,
        downcast=downcast,
        errors=errors
    )


#####################
####    FLOAT    ####
#####################


@to_float.overload("int")
def integer_to_float(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a float data type."""
    # NOTE: integers can always be exactly represented as long as their
    # width in bits fits within the significand of the specified floating
    # point type with exponent 1 (listed in the IEEE 754 specification).

    if int(series.min) < dtype.min or int(series.max) > dtype.max:
        # 2-step conversion: int -> decimal, decimal -> float
        series = to_decimal(series, dtype="decimal", errors=errors)
        return to_float(
            series,
            dtype=dtype,
            tol=tol,
            downcast=downcast,
            errors=errors,
            **unused
        )

    # do naive conversion
    return to_float.generic(
        series,
        dtype=dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


#######################
####    COMPLEX    ####
#######################


@to_complex.overload("int")
def integer_to_complex(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a complex data type."""
    series = to_float(
        series,
        dtype="float",
        tol=tol,
        downcast=None,
        errors=errors
    )
    return to_complex(
        series,
        dtype=dtype,
        tol=tol,
        downcast=downcast,
        errors=errors,
        **unused
    )


#######################
####    DECIMAL    ####
#######################


@to_decimal.overload("int")
def integer_to_decimal(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a decimal data type."""
    result = series + dtype.type_def(0)  # ~2x faster than apply loop
    result.element_type = dtype
    return result


########################
####    DATETIME    ####
########################


@to_datetime.overload("int")
def integer_to_datetime(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    rounding: str,
    since: Epoch,
    tz: pytz.BaseTzInfo,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a datetime data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # account for non-utc epoch
    if since:
        series.series += since.offset

    # check for overflow and upcast if applicable
    series, dtype = series.boundscheck(dtype, errors=errors)

    # convert to final representation
    return dtype.from_ns(
        series,
        unit=unit,
        step_size=step_size,
        rounding=rounding,
        since=since,
        tz=tz,
        errors=errors,
        **unused
    )


#########################
####    TIMEDELTA    ####
#########################


@to_timedelta.overload("int")
def integer_to_timedelta(
    series: wrapper.SeriesWrapper,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    since: Epoch,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a timedelta data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # check for overflow and upcast if necessary
    series, dtype = series.boundscheck(dtype, errors=errors)

    # convert to final representation
    return dtype.from_ns(
        series,
        unit=unit,
        step_size=step_size,
        since=since,
        errors=errors,
        **unused
    )


######################
####    STRING    ####
######################


@to_string.overload("int")
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

    return to_string.generic(
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
