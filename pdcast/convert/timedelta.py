"""This module contains dispatched cast() implementations for timedelta data."""
# pylint: disable=unused-argument
import datetime

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


# TODO: timedelta -> float does not retain longdouble precision.  This is due
# to the / operator in convert_unit() defaulting to float64 precision, which is
# probably unfixable.


@cast.overload("timedelta", "bool")
def timedelta_to_boolean(
    series: pd.Series,
    dtype: types.ScalarType,
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


@cast.overload("timedelta[pandas]", "int")
def pandas_timedelta_to_integer(
    series: pd.Series,
    dtype: types.ScalarType,
    rounding: str,
    tol: Tolerance,
    unit: str,
    step_size: int,
    since: time.Epoch,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert pandas Timedeltas to an integer data type."""
    # get integer ns
    series = series.astype(np.int64)

    # convert ns to final unit, step_size
    if unit != "ns":
        series = time.convert_unit(
            series,
            "ns",
            unit,
            since=since,
            rounding=rounding or "down",
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


@cast.overload("timedelta[python]", "int")
def python_timedelta_to_integer(
    series: pd.Series,
    dtype: types.ScalarType,
    rounding: str,
    tol: Tolerance,
    unit: str,
    step_size: int,
    since: time.Epoch,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert python timedeltas to an integer data type."""
    # get integer ns
    series = apply_with_errors(series, time.pytimedelta_to_ns, errors=errors)
    series = series.astype(resolve_type(int).dtype)

    # convert ns to final unit, step_size
    if unit != "ns":
        series = time.convert_unit(
            series,
            "ns",
            unit,
            since=since,
            rounding=rounding or "down",
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


@cast.overload("timedelta[numpy]", "int")
def numpy_timedelta64_to_integer(
    series: pd.Series,
    dtype: types.ScalarType,
    rounding: str,
    tol: Tolerance,
    unit: str,
    step_size: int,
    since: time.Epoch,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert numpy timedelta64s into an integer data type."""
    series_type = detect_type(series)

    # NOTE: using numpy m8 array is ~2x faster than looping through series

    m8_str = f"m8[{series_type.step_size}{series_type.unit}]"
    arr = series.to_numpy(m8_str).view(np.int64).astype(object)
    arr *= series_type.step_size
    arr = time.convert_unit(
        arr,
        series_type.unit,
        unit,
        rounding=rounding or "down",
        since=since
    )
    if step_size != 1:
        arr = round_div(arr, step_size, rule=rounding or "down")

    pd.Series(arr, index=series.index, dtype=resolve_type(int).dtype)
    series, dtype = boundscheck(series, dtype, errors=errors)
    return generic_to_integer(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


@cast.overload("timedelta", "float")
def timedelta_to_float(
    series: pd.Series,
    dtype: types.ScalarType,
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
    # 2 step conversion: timedelta -> ns, ns -> float
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


@cast.overload("timedelta", "complex")
def timedelta_to_complex(
    series: pd.Series,
    dtype: types.ScalarType,
    downcast: types.CompositeType,
    **unused
) -> pd.Series:
    """Convert timedelta data to a complex data type."""
    # 2-step conversion: timedelta -> float, float -> complex
    series = cast(series, dtype.equiv_float, downcast=None, **unused)
    return cast(series, dtype, downcast=downcast, **unused)


@cast.overload("timedelta", "decimal")
def timedelta_to_decimal(
    series: pd.Series,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tol: Tolerance,
    rounding: str,
    errors: str,
    **unused
) -> pd.Series:
    """Convert timedelta data to a decimal data type."""
    # 2-step conversion: timedelta -> ns, ns -> decimal
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


@cast.overload("timedelta", "datetime")
def timedelta_to_datetime(
    series: pd.Series,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    rounding: str,
    since: time.Epoch,
    tz: datetime.tzinfo,
    errors: str,
    **unused
) -> pd.Series:
    """Convert datetime data to another datetime representation."""
    # 2-step conversion: timedelta -> ns, ns -> datetime
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
        tz=tz,
        errors=errors,
        **unused
    )


@cast.overload("timedelta", "timedelta")
def timedelta_to_timedelta(
    series: pd.Series,
    dtype: types.ScalarType,
    unit: str,
    step_size: int,
    downcast: types.CompositeType,
    **unused
) -> pd.Series:
    """Convert timedelta data to a timedelta representation."""
    # trivial case
    if detect_type(series) == dtype:
        return series

    # 2-step conversion: datetime -> ns, ns -> timedelta
    series = cast(
        series,
        "int",
        unit="ns",
        step_size=1,
        downcast=None,
        **unused
    )
    return cast(
        series,
        dtype,
        unit="ns",
        step_size=1,
        downcast=downcast,
        **unused
    )
