"""This module contains dispatched cast() implementations for decimal data."""
# pylint: disable=unused-argument
import datetime

import numpy as np
import pandas as pd

from pdcast import types
from pdcast.detect import detect_type
from pdcast.resolve import resolve_type
from pdcast.util.error import shorten_list
from pdcast.util.round import Tolerance
from pdcast.util import time
from pdcast.util.vector import apply_with_errors

from .base import (
    cast, generic_to_boolean, generic_to_integer, snap_round
)
from .util import boundscheck, downcast_float, isinf, within_tol


# TODO: pdcast.cast(pdcast.cast([1., 2., np.inf], "decimal"), "float")
# OverflowError: cannot convert Infinity to integer

# -> insert series.min == inf checks


@cast.overload("decimal", "bool")
def decimal_to_boolean(
    series: pd.Series,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert decimal data to a boolean data type."""
    series = snap_round(
        series,
        tol=tol.real,
        rule=rounding,
        errors=errors
    )
    series, dtype = boundscheck(series, dtype, errors=errors)
    return generic_to_boolean(series, dtype, errors=errors)


@cast.overload("decimal", "integer")
def decimal_to_integer(
    series: pd.Series,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert decimal data to an integer data type."""
    series = snap_round(
        series,
        tol=tol.real,
        rule=rounding,
        errors=errors
    )
    series, dtype = boundscheck(series, dtype, errors=errors)
    return generic_to_integer(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


@cast.overload("decimal", "float")
def decimal_to_float(
    series: pd.Series,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert decimal data to a floating point data type."""
    # do naive conversion
    if dtype.itemsize > 8:
        # NOTE: series.astype() implicitly calls Decimal.__float__(), which
        # is limited to 64-bits.  Converting to an intermediate string
        # representation avoids this and maintains full precision
        result = series.astype(str).astype(dtype)
    else:
        result = series.astype(dtype)

    # check for overflow
    if int(series.min()) < dtype.min or int(series.max()) > dtype.max:
        infs = isinf(result) ^ isinf(series)
        if infs.any():
            if errors == "coerce":
                result = result[~infs]
                series = series[~infs]  # mirror on original
            else:
                raise OverflowError(
                    f"values exceed {dtype} range at index "
                    f"{shorten_list(series[infs].index.values)}"
                )

    # backtrack to check for precision loss
    if errors != "coerce":  # coercion ignores precision loss
        series_type = detect_type(series)
        bad = ~within_tol(
            series,
            cast(result, dtype=series_type, errors="raise"),
            tol=tol.real
        )
        if bad.any():
            raise ValueError(
                f"precision loss exceeds tolerance {float(tol.real):g} at "
                f"index {shorten_list(series[bad].index.values)}"
            )

    if downcast is not None:
        return downcast_float(result, tol=tol, smallest=downcast)
    return result


@cast.overload("decimal", "complex")
def decimal_to_complex(
    series: pd.Series,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert decimal data to a complex data type."""
    # 2-step conversion: decimal -> float, float -> complex
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


@cast.overload("decimal", "decimal")
def decimal_to_decimal(
    series: pd.Series,
    dtype: types.AtomicType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert boolean data to a decimal data type."""
    # trivial case
    if detect_type(series) == dtype:
        return series

    target = dtype.dtype
    if isinstance(dtype.dtype, types.AbstractDtype):
        series = apply_with_errors(series, dtype.type_def, errors=errors)
    return series.astype(target)


@cast.overload("decimal", "datetime")
def decimal_to_datetime(
    series: pd.Series,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    tz: datetime.tzinfo,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a datetime data type."""
    # 2-step conversion: decimal -> ns, ns -> datetime
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # account for non-utc epoch
    if since:
        series += since.offset

    # check for overflow and upcast if applicable
    series, dtype = boundscheck(series, dtype, errors=errors)

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


@cast.overload("decimal", "timedelta")
def decimal_to_timedelta(
    series: pd.Series,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    errors: str,
    **unused
) -> pd.Series:
    """Convert integer data to a timedelta data type."""
    # 2-step conversion: decimal -> ns, ns -> timedelta
    series = to_ns(series, unit=unit, step_size=step_size, since=since)

    # check for overflow and upcast if necessary
    series, dtype = boundscheck(series, dtype, errors=errors)

    # convert to final representation
    return cast(
        series,
        dtype,
        unit="ns",
        step_size=1,
        since=since,
        errors=errors,
        **unused,
    )


#######################
####    PRIVATE    ####
#######################


def to_ns(
    series: pd.Series,
    unit: str,
    step_size: int,
    since: time.Epoch
) -> pd.Series:
    """Convert an integer number of time units into nanoseconds from a given
    epoch.
    """
    # round fractional inputs to the nearest nanosecond
    if unit == "Y":
        result = time.round_years_to_ns(series * step_size, since=since)
    elif unit == "M":
        result = time.round_months_to_ns(series * step_size, since=since)
    else:
        as_pyint = np.frompyfunc(int, 1, 1)
        result = series
        if step_size != 1:
            result *= step_size
        if unit != "ns":
            result *= time.as_ns[unit]
        result = as_pyint(result)

    return result.astype(resolve_type(int).dtype)
