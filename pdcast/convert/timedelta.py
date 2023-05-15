"""This module contains dispatched cast() implementations for timedelta data."""
# pylint: disable=unused-argument
import datetime

import numpy as np
import pandas as pd

from pdcast import types
from pdcast.decorators.wrapper import SeriesWrapper
from pdcast.detect import detect_type
from pdcast.util import time
from pdcast.util.round import round_div, Tolerance

from .base import cast, generic_to_integer
from .util import boundscheck


@cast.overload("timedelta", "bool")
def timedelta_to_boolean(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    rounding: str,
    unit: str,
    step_size: int,
    since: time.Epoch,
    errors: str,
    **unused
) -> SeriesWrapper:
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
    series: SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    unit: str,
    step_size: int,
    since: time.Epoch,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert pandas Timedeltas to an integer data type."""
    # get integer ns
    series = series.rectify().astype(np.int64)
    
    # convert ns to final unit, step_size
    if unit != "ns":
        series.series = time.convert_unit(
            series.series,
            "ns",
            unit,
            since=since,
            rounding=rounding or "down",
        )
    if step_size != 1:
        series.series = round_div(
            series.series,
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
    series: SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    unit: str,
    step_size: int,
    since: time.Epoch,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert python timedeltas to an integer data type."""
    # get integer ns
    series = series.apply_with_errors(
        time.pytimedelta_to_ns,
        element_type=int
    )

    # convert ns to final unit, step_size
    if unit != "ns":
        series.series = time.convert_unit(
            series.series,
            "ns",
            unit,
            since=since,
            rounding=rounding or "down",
        )
    if step_size != 1:
        series.series = round_div(
            series.series,
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
    series: SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    unit: str,
    step_size: int,
    since: time.Epoch,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert numpy timedelta64s into an integer data type."""
    series_type = detect_type(series)

    # NOTE: using numpy m8 array is ~2x faster than looping through series
    m8_str = f"m8[{series_type.step_size}{series_type.unit}]"
    arr = series.series.to_numpy(m8_str).view(np.int64).astype(object)

    # correct for m8 step size
    arr *= series_type.step_size

    # convert to final unit
    arr = time.Epochconvert_unit(
        arr,
        series_type.unit,
        unit,
        rounding=rounding or "down",
        since=since
    )

    # apply final step size
    if step_size != 1:
        arr = round_div(arr, step_size, rule=rounding or "down")

    # re-wrap as SeriesWrapper
    series = SeriesWrapper(
        pd.Series(arr, index=series.series.index),
        element_type=int
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


# TODO: remove assignment to .element_type


@cast.overload("timedelta", "float")
def timedelta_to_float(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tol: Tolerance,
    rounding: str,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
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
        series.series = time.convert_unit(
            series.series,
            "ns",
            unit,
            rounding=rounding,
            since=since
        )
        # if rounding is None:
        #     series.element_type = float

    # apply final step size
    if step_size != 1:
        series.series /= step_size
        # series.element_type = float

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
    series: SeriesWrapper,
    dtype: types.AtomicType,
    downcast: types.CompositeType,
    **unused
) -> SeriesWrapper:
    """Convert timedelta data to a complex data type."""
    # 2-step conversion: timedelta -> float, float -> complex
    series = cast(series, dtype.equiv_float, downcast=None, **unused)
    return cast(series, dtype, downcast=downcast, **unused)


@cast.overload("timedelta", "decimal")
def timedelta_to_decimal(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tol: Tolerance,
    rounding: str,
    errors: str,
    **unused
) -> SeriesWrapper:
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
        series.series = time.convert_unit(
            series.series,
            "ns",
            unit,
            rounding=rounding,
            since=since
        )

    # apply final step size
    if step_size != 1:
        series.series /= step_size

    return series


@cast.overload("timedelta", "datetime")
def timedelta_to_datetime(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    rounding: str,
    since: time.Epoch,
    tz: datetime.tzinfo,
    errors: str,
    **unused
) -> SeriesWrapper:
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
    series: SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    downcast: types.CompositeType,
    **unused
) -> SeriesWrapper:
    """Convert timedelta data to a timedelta representation."""
    # trivial case
    if dtype == detect_type(series):
        return series.astype(dtype.dtype, copy=False)

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
