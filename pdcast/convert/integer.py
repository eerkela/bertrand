"""This module contains dispatched cast() implementations for integer data."""
# pylint: disable=unused-argument
from __future__ import annotations
from functools import partial

import numpy as np
import pandas as pd
import pytz

from pdcast import types
from pdcast.decorators.wrapper import SeriesWrapper
from pdcast.util.round import round_div, Tolerance
from pdcast.util import time

from .base import (
    cast, generic_to_boolean, generic_to_integer, generic_to_float,
    generic_to_string
)


@cast.overload("int", "bool")
def integer_to_boolean(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert integer data to a boolean data type."""
    series, dtype = series.boundscheck(dtype, errors=errors)
    return generic_to_boolean(series, dtype, errors=errors)


@cast.overload("int", "int")
def integer_to_integer(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert integer data to another integer data type."""
    series, dtype = series.boundscheck(dtype, errors=errors)
    return generic_to_integer(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


@cast.overload("int", "float")
def integer_to_float(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
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
    series: SeriesWrapper,
    dtype: types.ScalarType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
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
    series: SeriesWrapper,
    dtype: types.ScalarType,
    **unused
) -> SeriesWrapper:
    """Convert integer data to a decimal data type."""
    result = series + dtype.type_def(0)  # ~2x faster than apply loop
    result.element_type = dtype
    return result


@cast.overload("int", "datetime")
def integer_to_datetime(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tz: pytz.BaseTzInfo,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert integer data to a datetime data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # account for non-utc epoch
    if since:
        series.series += since.offset

    # check for overflow and upcast if applicable
    series, dtype = series.boundscheck(dtype, errors=errors)

    # convert to final representation
    return cast(
        series,
        dtype,
        unit="ns",
        step_size=1,
        since=time.Epoch("utc"),
        tz=tz,
        errors=errors,
        **unused
    )


@cast.overload("int", "datetime[pandas]")
def integer_to_pandas_timestamp(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tz: pytz.BaseTzInfo,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert integer data to a datetime data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # account for non-utc epoch
    if since:
        series.series += since.offset

    # check for overflow and upcast if applicable
    series, dtype = series.boundscheck(dtype, errors=errors)

    # reconcile `tz` argument with timezone attached to dtype, if given
    if tz:
        dtype = dtype.replace(tz=tz)

    # convert using pd.to_datetime, accounting for timezone
    if dtype.tz is None:
        result = pd.to_datetime(series.series, unit="ns")
    else:
        result = pd.to_datetime(series.series, unit="ns", utc=True)
        if dtype.tz != pytz.utc:
            result = result.dt.tz_convert(dtype.tz)

    return SeriesWrapper(
        result,
        hasnans=series.hasnans,
        element_type=dtype
    )


@cast.overload("int", "datetime[python]")
def integer_to_python_datetime(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tz: pytz.BaseTzInfo,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert integer data to a datetime data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # account for non-utc epoch
    if since:
        series.series += since.offset

    # check for overflow and upcast if applicable
    series, dtype = series.boundscheck(dtype, errors=errors)

    # reconcile `tz` argument with timezone attached to dtype, if given
    if tz:
        dtype = dtype.replace(tz=tz)

    # convert elementwise
    call = partial(time.ns_to_pydatetime, tz=dtype.tz)
    return series.apply_with_errors(call, element_type=dtype)


@cast.overload("int", "datetime[numpy]")
def integer_to_numpy_datetime64(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    rounding: str,
    since: time.Epoch,
    tz: pytz.BaseTzInfo,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert integer data to a datetime data type."""
    if tz and tz != pytz.utc:
        raise TypeError(
            "np.datetime64 objects do not carry timezone information "
            "(must be UTC)"
        )

    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # account for non-utc epoch
    if since:
        series.series += since.offset

    # check for overflow and upcast if applicable
    series, dtype = series.boundscheck(dtype, errors=errors)

    # convert from nanoseconds to final representation
    series.series = time.convert_unit(
        series.series,
        "ns",
        dtype.unit,
        rounding=rounding or "down"
    )
    if dtype.step_size != 1:
        series.series = round_div(
            series.series,
            dtype.step_size,
            rule=rounding or "down"
        )

    M8_str = f"M8[{dtype.step_size}{dtype.unit}]"
    return SeriesWrapper(
        pd.Series(
            list(series.series.to_numpy(M8_str)),
            index=series.series.index,
            dtype="O"
        ),
        hasnans=series.hasnans,
        element_type=dtype
    )


@cast.overload("int", "timedelta")
def integer_to_timedelta(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert integer data to a timedelta data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # check for overflow and upcast if necessary
    series, dtype = series.boundscheck(dtype, errors=errors)

    # convert to final representation
    return cast(
        series,
        dtype,
        unit="ns",
        step_size=1,
        since=since,
        errors=errors,
        **unused
    )


@cast.overload("int", "timedelta[pandas]")
def integer_to_pandas_timedelta(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert integer data to a timedelta data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # check for overflow and upcast if necessary
    series, dtype = series.boundscheck(dtype, errors=errors)

    # convert to final representation
    return SeriesWrapper(
        pd.to_timedelta(series.series.astype(object), unit="ns"),
        hasnans=series.hasnans,
        element_type=dtype
    )


@cast.overload("int", "timedelta[python]")
def integer_to_python_timedelta(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    rounding: str,
    since: time.Epoch,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert integer data to a timedelta data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # check for overflow and upcast if necessary
    series, dtype = series.boundscheck(dtype, errors=errors)

    # convert to us
    result = round_div(series.series, time.as_ns["us"], rule=rounding or "down")

    # NOTE: m8[us].astype("O") implicitly converts to datetime.timedelta
    return SeriesWrapper(
        pd.Series(
            result.to_numpy("m8[us]").astype("O"),
            index=series.series.index,
            dtype="O"
        ),
        hasnans=series.hasnans,
        element_type=dtype
    )


@cast.overload("int", "timedelta[numpy]")
def integer_to_numpy_timedelta64(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    rounding: str,
    since: time.Epoch,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert integer data to a timedelta data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # check for overflow and upcast if necessary
    series, dtype = series.boundscheck(dtype, errors=errors)

    # convert from ns to final unit
    series.series = time.convert_unit(
        series.series,
        "ns",
        dtype.unit,
        rounding=rounding or "down",
        since=since
    )
    if dtype.step_size != 1:
        series.series = round_div(
            series.series,
            dtype.step_size,
            rule=rounding or "down"
        )
    m8_str = f"m8[{dtype.step_size}{dtype.unit}]"
    return SeriesWrapper(
        pd.Series(
            list(series.series.to_numpy(m8_str)),
            index=series.series.index,
            dtype="O"
        ),
        hasnans=series.hasnans,
        element_type=dtype
    )


@cast.overload("int", "string")
def integer_to_string(
    series: SeriesWrapper,
    dtype: types.ScalarType,
    base: int,
    format: str,
    errors: str,
    **unused
) -> SeriesWrapper:
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
    series: SeriesWrapper,
    unit: str,
    step_size: int,
    since: time.Epoch
) -> SeriesWrapper:
    """Convert an integer number of time units into nanoseconds from a given
    epoch.
    """
    # TODO: use np.frompyfunc(int, 1, 1) rather than full cast() op

    # convert to python int to avoid overflow
    series = cast(series, int, downcast=False, errors="raise")

    # trivial case
    if unit == "ns" and step_size == 1:
        return series

    # account for step size
    if step_size != 1:
        series.series *= step_size

    # convert to ns
    return SeriesWrapper(
        time.convert_unit(series.series, unit, "ns", since=since),
        hasnans=series.hasnans,
        element_type=int
    )
