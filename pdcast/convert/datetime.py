"""This module contains dispatched cast() implementations for datetime data."""
# pylint: disable=unused-argument
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
from pdcast.util.vector import apply_with_errors

from .base import cast, generic_to_integer


# TODO: pdcast.cast("1883-11-18 12:00:00", "datetime", tz="US/Pacific")
# induces an `AmbiguousTimeError: Cannot infer dst time from 1883-11-18
# 12:00:00, try using the 'ambiguous' argument`

# -> have to pass in ambiguous_tz, nonexistent_tz arguments to cast()


@cast.overload("datetime", "bool")
def datetime_to_boolean(
    series: pd.Series,
    dtype: types.AtomicType,
    tol: Tolerance,
    rounding: str,
    unit: str,
    step_size: int,
    since: time.Epoch,
    errors: str,
    **unused
) -> pd.Series:
    """Convert timedelta data to a boolean data type."""
    # 2-step conversion: timedelta -> decimal, decimal -> bool
    series = cast(
        series,
        "decimal",
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        since=since,
        errors=errors
    )
    return cast(
        series,
        dtype,
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        since=since,
        errors=errors,
        **unused
    )


@cast.overload("datetime[pandas]", "int")
def pandas_timestamp_to_integer(
    series: pd.Series,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tz: datetime.tzinfo,
    downcast: types.CompositeType,
    errors: str,
    **kwargs
) -> pd.Series:
    """Convert pandas Timestamps into an integer data type."""
    # apply tz if naive
    if tz and detect_type(series).tz is None:
        series = series.dt.tz_localize(tz)

    # convert to ns (overflow-safe)
    series = series.astype(np.int64)

    # apply epoch
    if since:
        series = series.astype(resolve_type(int).dtype)  # overflow-safe
        series -= since.offset

    # convert ns to final unit, step_size
    if unit != "ns":
        series = time.convert_unit(
            series,
            "ns",
            unit,
            rounding=rounding or "down"
        )
    if step_size != 1:
        series = round_div(
            series,
            step_size,
            rule=rounding or "down"
        )

    # check for overflow
    series, dtype = boundscheck(series, dtype, errors=errors)

    # delegate to generic conversion
    return generic_to_integer(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


@cast.overload("datetime[python]", "int")
def python_datetime_to_integer(
    series: pd.Series,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tz: datetime.tzinfo,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert python datetimes into an integer data type."""
    series_type = detect_type(series)

    # apply tz if naive
    if tz and series_type.tz is None:
        series = apply_with_errors(
            series,
            partial(time.localize_pydatetime_scalar, tz=tz),
            errors="raise"
        )

    # convert to ns
    series = apply_with_errors(series, time.pydatetime_to_ns, errors="raise")
    series = series.astype(resolve_type(int).dtype)

    # apply epoch
    if since:
        series -= since.offset

    # convert ns to final unit, step_size
    if unit != "ns":
        series = time.convert_unit(
            series,
            "ns",
            unit,
            rounding=rounding or "down"
        )
    if step_size != 1:
        series = round_div(
            series,
            step_size,
            rule=rounding or "down"
        )

    # check for overflow
    series, dtype = boundscheck(series, dtype, errors=errors)

    # delegate to generic conversion
    return generic_to_integer(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


@cast.overload("datetime[numpy]", "int")
def numpy_datetime64_to_integer(
    series: pd.Series,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    unit: str,
    step_size: int,
    since: time.Epoch,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert numpy datetime64s into an integer data type."""
    series_type = detect_type(series)

    # NOTE: using numpy M8 array is ~2x faster than looping through series
    M8_str = f"M8[{series_type.step_size}{series_type.unit}]"
    arr = series.to_numpy(M8_str).view(np.int64).astype(object)

    # correct for M8 step size
    arr *= series_type.step_size

    # convert to final unit
    if since:
        # convert to ns, subtract epoch, and then convert to final unit
        arr = time.convert_unit(
            arr,
            series_type.unit,
            "ns"
        )
        arr -= since.offset
        arr = time.convert_unit(
            arr,
            "ns",
            unit,
            rounding=rounding or "down"
        )

    else:
        # skip straight to final unit
        arr = time.convert_unit(
            arr,
            series_type.unit,
            unit,
            rounding=rounding or "down"
        )

    # apply final step size
    if step_size != 1:
        arr = round_div(arr, step_size, rule=rounding or "down")

    # re-wrap as pandas Series
    series = pd.Series(arr, index=series.index)

    # check for overflow
    series, dtype = boundscheck(series, dtype, errors=errors)

    # delegate to generic conversion
    return generic_to_integer(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


@cast.overload("datetime", "float")
def datetime_to_float(
    series: pd.Series,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tol: Tolerance,
    rounding: str,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert timedelta data to a floating point data type."""
    # 2 step conversion: datetime -> ns, ns -> float
    series = cast(
        series,
        int,
        unit="ns",
        step_size=1,
        since=since,
        rounding=None,
        downcast=None,
        errors=errors
    )

    # convert ns to final unit
    if unit != "ns":
        series = time.convert_unit(
            series,
            "ns",
            unit,
            rounding=rounding,
            since=since
        )
        if rounding is None:
            series = series.astype(resolve_type(float).dtype)

    # apply final step size
    if step_size != 1:
        series /= step_size
        series = series.astype(resolve_type(float).dtype)

    # integer/float -> float
    return cast(
        series,
        dtype,
        unit=unit,
        step_size=step_size,
        since=since,
        tol=tol,
        rounding=rounding,
        downcast=downcast,
        errors=errors,
        **unused
    )


@cast.overload("datetime", "complex")
def datetime_to_complex(
    series: pd.Series,
    dtype: types.AtomicType,
    downcast: types.CompositeType,
    **unused
) -> pd.Series:
    """Convert timedelta data to a complex data type."""
    # 2-step conversion: datetime -> float, float -> complex
    series = cast(series, dtype.equiv_float, downcast=None, **unused)
    return cast(series, dtype, downcast=downcast, **unused)


@cast.overload("datetime", "decimal")
def datetime_to_decimal(
    series: pd.Series,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tol: Tolerance,
    rounding: str,
    errors: str,
    **unused
) -> pd.Series:
    """Convert timedelta data to a decimal data type."""
    # 2-step conversion: datetime -> ns, ns -> decimal
    series = cast(
        series,
        "int",
        unit="ns",
        step_size=1,
        since=since,
        rounding=None,
        downcast=None,
        errors=errors
    )
    series = cast(
        series,
        dtype,
        unit=unit,
        step_size=step_size,
        since=since,
        tol=tol,
        rounding=rounding,
        errors=errors,
        **unused
    )

    # convert decimal ns to final unit
    if unit != "ns":
        series = time.convert_unit(
            series,
            "ns",
            unit,
            rounding=rounding,
            since=since
        )

    # apply final step size
    if step_size != 1:
        series /= step_size

    return series


@cast.overload("datetime", "datetime")
def datetime_to_datetime(
    series: pd.Series,
    dtype: types.AtomicType,
    rounding: str,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tz: datetime.tzinfo,
    errors: str,
    **unused
) -> pd.Series:
    """Convert datetime data to another datetime representation."""
    series_type = detect_type(series)

    # trivial case
    if dtype == series_type:
        return series

    # 2-step conversion: datetime -> ns, ns -> datetime
    series = cast(
        series,
        "int",
        rounding=rounding,
        unit="ns",
        step_size=1,
        since=time.Epoch("utc"),
        tz=tz,
        downcast=None,
        errors=errors
    )
    return cast(
        series,
        dtype,
        rounding=rounding,
        unit="ns",
        step_size=1,
        since=time.Epoch("utc"),
        tz=tz,
        errors=errors,
        **unused
    )


@cast.overload("datetime[pandas]", "datetime[pandas]")
def pandas_timestamp_to_pandas_timestamp(
    series: pd.Series,
    dtype: types.AtomicType,
    tz: datetime.tzinfo,
    **unused
) -> pd.Series:
    """Fastpath for same-class pandas timestamp conversions."""
    series_type = detect_type(series)

    # reconcile time zones
    if tz:
        dtype = dtype.replace(tz=tz)

    # trivial case
    if series_type == dtype:
        return series

    # localize/convert tim ezones
    if not series_type.tz:
        return series.dt.tz_localize(dtype.tz)
    return series.dt.tz_convert(dtype.tz)


@cast.overload("datetime[python]", "datetime[python]")
def python_datetime_to_python_datetime(
    series: pd.Series,
    dtype: types.AtomicType,
    tz: datetime.tzinfo,
    **unused
) -> pd.Series:
    """Fastpath for same-class pandas timestamp conversions."""
    # reconcile time zones
    if tz:
        dtype = dtype.replace(tz=tz)

    # trivial case: time zones are identical
    if detect_type(series) == dtype:
        return series

    # localize/convert time zones
    call = partial(time.localize_pydatetime_scalar, tz=tz)
    result = series.apply(call, convert_dtype=False)
    return result.astype(dtype.dtype)


@cast.overload("datetime", "timedelta")
def datetime_to_timedelta(
    series: pd.Series,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    rounding: str,
    since: time.Epoch,
    errors: str,
    **unused
) -> pd.Series:
    """Convert datetime data to a timedelta representation."""
    # 2-step conversion: datetime -> ns, ns -> timedelta
    series = cast(
        series,
        "int",
        unit="ns",
        step_size=1,
        rounding=rounding,
        since=since,
        downcast=None,
        errors=errors
    )
    return cast(
        series,
        dtype,
        unit="ns",
        step_size=1,
        rounding=rounding,
        since=since,
        errors=errors,
        **unused
    )
