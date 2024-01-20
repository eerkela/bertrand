"""This module contains dispatched cast() implementations for integer data."""
# pylint: disable=unused-argument
from __future__ import annotations
import datetime
from functools import partial

import numpy as np
import pandas as pd

from pdcast import types
from pdcast.detect import detect_type
from pdcast.resolve import resolve_type
from pdcast.util import time
from pdcast.util.numeric import boundscheck
from pdcast.util.round import round_div, Tolerance
from pdcast.util.string import int_to_base
from pdcast.util.vector import apply_with_errors

from .base import (
    cast, generic_to_boolean, generic_to_integer, generic_to_float,
    generic_to_string
)


# TODO: int -> timedelta is unusually slow for some reason


@cast.overload("int", "bool")
def integer_to_boolean(
    series: pd.Series,
    dtype: types.VectorType,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a boolean data type."""
    series = boundscheck(series, dtype=dtype, tol=tol.real, errors=errors)
    return generic_to_boolean(series, dtype, errors=errors)


@cast.overload("int", "int")
def integer_to_integer(
    series: pd.Series,
    dtype: types.VectorType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to another integer data type."""
    # trivial case
    if detect_type(series) == dtype:
        return series

    # check for overflow and upcast if applicable
    try:
        series = boundscheck(series, dtype=dtype, tol=tol.real, errors=errors)
    except OverflowError as err:
        last_err = err
        for typ in dtype.larger:
            try:
                series = boundscheck(
                    series,
                    dtype=typ,
                    tol=tol.real,
                    errors=errors
                )
                dtype = typ
                break
            except OverflowError as exc:
                last_err = exc
        else:
            raise last_err from err

    # use generic implementation
    return generic_to_integer(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


@cast.overload("int", "float")
def integer_to_float(
    series: pd.Series,
    dtype: types.VectorType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a float data type."""
    # NOTE: integers can always be exactly represented as floats as long as
    # their width in bits fits within the significand of the specified floating
    # point type with exponent 1 (as listed in the IEEE 754 specification).

    if int(series.min()) < dtype.min or int(series.max()) > dtype.max:
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
    series: pd.Series,
    dtype: types.VectorType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
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
    series: pd.Series,
    dtype: types.VectorType,
    **unused
) -> pd.Series:
    """Convert integer data to a decimal data type."""
    target = dtype.dtype
    if isinstance(target, types.ObjectDtype):
        series = series + dtype.type_def(0)  # ~2x faster than apply loop
    return series.astype(target)


@cast.overload("int", "datetime")
def integer_to_datetime(
    series: pd.Series,
    dtype: types.VectorType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tz: datetime.tzinfo,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a datetime data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # account for non-utc epoch
    if since:
        series += since.offset

    # check for overflow and upcast if applicable
    series = boundscheck(series, dtype, tol=tol.real, errors=errors)

    # convert to final representation
    return cast(
        series,
        dtype.registry.get_default(dtype),
        unit="ns",
        step_size=1,
        since=time.Epoch("utc"),
        tz=tz,
        errors=errors,
        **unused
    )


@cast.overload("int", "datetime[pandas]")
def integer_to_pandas_timestamp(
    series: pd.Series,
    dtype: types.VectorType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tz: datetime.tzinfo,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a datetime data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # account for non-utc epoch
    if since:
        series += since.offset

    # check for overflow and upcast if applicable
    series = boundscheck(series, dtype, tol=tol.real, errors=errors)

    # reconcile `tz` argument with timezone attached to dtype, if given
    if tz:
        dtype = dtype.replace(tz=tz)

    # convert using pd.to_datetime, accounting for timezone
    if dtype.tz is None:
        series = pd.to_datetime(series, unit="ns")
    else:
        series = pd.to_datetime(series, unit="ns", utc=True)
        if not time.is_utc(dtype.tz):
            series = series.dt.tz_convert(dtype.tz)

    return series


@cast.overload("int", "datetime[python]")
def integer_to_python_datetime(
    series: pd.Series,
    dtype: types.VectorType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tz: datetime.tzinfo,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a datetime data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # account for non-utc epoch
    if since:
        series += since.offset

    # check for overflow and upcast if applicable
    series = boundscheck(series, dtype=dtype, tol=tol.real, errors=errors)

    # reconcile `tz` argument with timezone attached to dtype, if given
    if tz:
        dtype = dtype.replace(tz=tz)

    # convert elementwise
    call = partial(time.ns_to_pydatetime, tz=dtype.tz)
    result = apply_with_errors(series, call, errors=errors)
    return result.astype(dtype.dtype)


@cast.overload("int", "datetime[numpy]")
def integer_to_numpy_datetime64(
    series: pd.Series,
    dtype: types.VectorType,
    unit: str,
    step_size: int,
    rounding: str,
    since: time.Epoch,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a datetime data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # account for non-utc epoch
    if since:
        series += since.offset

    # check for overflow and upcast if applicable
    series = boundscheck(series, dtype=dtype, tol=tol.real, errors=errors)

    # convert from nanoseconds to final representation
    series = time.convert_unit(
        series,
        "ns",
        dtype.unit,
        rounding=rounding or "down"
    )
    if dtype.step_size != 1:
        series = round_div(
            series,
            dtype.step_size,
            rule=rounding or "down"
        )

    M8_str = f"M8[{dtype.step_size}{dtype.unit}]"
    return pd.Series(
        list(series.to_numpy(M8_str)),
        index=series.index,
        dtype=object
    )


@cast.overload("int", "timedelta")
def integer_to_timedelta(
    series: pd.Series,
    dtype: types.VectorType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a timedelta data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # check for overflow and upcast if necessary
    series = boundscheck(series, dtype=dtype, tol=tol.real, errors=errors)

    # convert to final representation
    return cast(
        series,
        dtype.registry.get_default(dtype),
        unit="ns",
        step_size=1,
        since=since,
        errors=errors,
        **unused
    )


@cast.overload("int", "timedelta[pandas]")
def integer_to_pandas_timedelta(
    series: pd.Series,
    dtype: types.VectorType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a timedelta data type."""
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    series = boundscheck(series, dtype=dtype, tol=tol.real, errors=errors)

    # NOTE: pandas.to_timedelta complains when given an ObjectArray as input,
    # so we convert to dtype: object instead.

    return pd.to_timedelta(series.astype(object), unit="ns")


@cast.overload("int", "timedelta[python]")
def integer_to_python_timedelta(
    series: pd.Series,
    dtype: types.VectorType,
    unit: str,
    step_size: int,
    rounding: str,
    since: time.Epoch,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a timedelta data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # check for overflow and upcast if necessary
    series = boundscheck(series, dtype=dtype, tol=tol.real, errors=errors)

    # convert to us
    result = round_div(series, time.as_ns["us"], rule=rounding or "down")

    # NOTE: m8[us].astype(object) implicitly converts to datetime.timedelta

    return pd.Series(
        result.to_numpy("m8[us]").astype(object),
        index=series.index,
        dtype=dtype.dtype
    )


@cast.overload("int", "timedelta[numpy]")
def integer_to_numpy_timedelta64(
    series: pd.Series,
    dtype: types.VectorType,
    unit: str,
    step_size: int,
    rounding: str,
    since: time.Epoch,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a timedelta data type."""
    # convert to ns
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # check for overflow and upcast if necessary
    series = boundscheck(series, dtype=dtype, tol=tol.real, errors=errors)

    # convert from ns to final unit
    series = time.convert_unit(
        series,
        "ns",
        dtype.unit,
        rounding=rounding or "down",
        since=since
    )
    if dtype.step_size != 1:
        series = round_div(
            series,
            dtype.step_size,
            rule=rounding or "down"
        )
    m8_str = f"m8[{dtype.step_size}{dtype.unit}]"
    return pd.Series(
        list(series.to_numpy(m8_str)),
        index=series.index,
        dtype="O"
    )


@cast.overload("int", "string")
def integer_to_string(
    series: pd.Series,
    dtype: types.VectorType,
    base: int,
    format: str,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a string data type in any base."""
    # use non-decimal base in conjunction with format
    if base and base != 10:
        if format:
            call = lambda x: f"{int_to_base(x, base=base):{format}}"
        else:
            call = partial(int_to_base, base=base)

        result = apply_with_errors(series, call, errors=errors)
        return result.astype(dtype.dtype)

    return generic_to_string(
        series,
        dtype,
        format=format,
        errors=errors
    )


#######################
####    PRIVATE    ####
#######################


as_pyint = np.frompyfunc(int, 1, 1)


def to_ns(
    series: pd.Series,
    unit: str,
    step_size: int,
    since: time.Epoch
) -> pd.Series:
    """Convert an integer number of time units into nanoseconds from a given
    epoch.
    """
    series = as_pyint(series).astype(resolve_type(int).dtype)  # overflow-safe

    # trivial case
    if unit == "ns" and step_size == 1:
        return series

    if step_size != 1:
        series *= step_size
    return time.convert_unit(series, unit, "ns", since=since)
