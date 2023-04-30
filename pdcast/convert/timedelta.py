"""This module contains dispatched cast() implementations for timedelta data."""
# pylint: disable=unused-argument
from pdcast import types
from pdcast.decorators.wrapper import SeriesWrapper
from pdcast.util.round import Tolerance

from .base import (
    to_boolean, to_integer, to_float, to_decimal, to_complex, to_datetime,
    to_timedelta
)



#######################
####    BOOLEAN    ####
#######################


@to_boolean.overload("timedelta")
def timedelta_to_boolean(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    rounding: str,
    unit: str,
    step_size: int,
    since: Epoch,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert timedelta data to a boolean data type."""
    # 2-step conversion: timedelta -> decimal, decimal -> bool
    series = to_decimal(
        series,
        dtype="decimal",
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        since=since,
        errors=errors
    )
    return to_boolean(
        series,
        dtype=dtype,
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        since=since,
        errors=errors,
        **unused
    )


#######################
####    INTEGER    ####
#######################


@to_integer.oeverload("timedelta[numpy]")
def numpy_datetime64_to_integer(
    series: SeriesWrapper,
    dtype: AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    rounding: str,
    downcast: CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert numpy timedelta64s into an integer data type."""
    series_type = series.element_type

    # NOTE: using numpy m8 array is ~2x faster than looping through series
    m8_str = f"m8[{series_type.step_size}{series_type.unit}]"
    arr = series.series.to_numpy(m8_str).view(np.int64).astype(object)
    arr *= series_type.step_size
    arr = convert_unit(
        arr,
        series_type.unit,
        unit,
        rounding=rounding or "down",
        since=since
    )
    series = SeriesWrapper(
        pd.Series(arr, index=series.series.index),
        hasnans=series.hasnans,
        element_type=resolve.resolve_type("int[python]")
    )

    series, dtype = series.boundscheck(dtype, errors=errors)
    return to_integer.generic(
        series,
        dtype,
        downcast=downcast,
        errors=errors
    )


@to_integer.overload("timedelta[pandas]")
def pandas_timestamp_to_integer(
    series: SeriesWrapper,
    dtype: AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    rounding: str,
    downcast: CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert pandas Timedeltas to an integer data type."""
    series = series.rectify().astype(np.int64)
    if unit != "ns" or step_size != 1:
        convert_ns_to_unit(
            series,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
            since=since
        )

    series, dtype = series.boundscheck(dtype, errors=errors)
    return to_integer.generic(
        series,
        dtype,
        downcast=downcast,
        errors=errors
    )


@to_integer.overload("timedelta[python]")
def pytimedelta_to_integer(
    series: SeriesWrapper,
    dtype: AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    rounding: str,
    downcast: CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert python timedeltas to an integer data type."""
    series = series.apply_with_errors(
        pytimedelta_to_ns,
        element_type=resolve.resolve_type("int[python]")
    )

    if unit != "ns" or step_size != 1:
        convert_ns_to_unit(
            series,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
            since=since
        )

    series, dtype = series.boundscheck(dtype, errors=errors)
    return to_integer.generic(
        series,
        dtype,
        downcast=downcast,
        errors=errors
    )


#####################
####    FLOAT    ####
#####################


@to_float.overload("timedelta")
def timedelta_to_float(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    tol: Tolerance,
    rounding: str,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert timedelta data to a floating point data type."""
    # convert to nanoseconds, then from nanoseconds to final unit
    series = to_integer(
        series,
        dtype="int",
        unit="ns",
        step_size=1,
        since=since,
        rounding=None,
        downcast=None,
        errors=errors
    )
    if unit != "ns" or step_size != 1:
        series.series = convert_unit(
            series.series,
            "ns",
            unit,
            rounding=rounding,
            since=since
        )
        if step_size != 1:
            series.series /= step_size
        series.element_type = "float[python]"

    return to_float(
        series,
        dtype=dtype,
        unit=unit,
        step_size=step_size,
        since=since,
        tol=tol,
        rounding=rounding,
        downcast=downcast,
        errors=errors,
        **unused
    )


#######################
####    COMPLEX    ####
#######################


@to_complex.overload("timedelta")
def timedelta_to_complex(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    tol: Tolerance,
    rounding: str,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert timedelta data to a complex data type."""
    # 2-step conversion: timedelta -> float, float -> complex
    series = to_float(
        series,
        dtype=series.element_type.equiv_float,
        unit=unit,
        step_size=step_size,
        since=since,
        rounding=rounding,
        downcast=None,
        errors=errors
    )
    return to_complex(
        series,
        dtype=dtype,
        unit=unit,
        step_size=step_size,
        since=since,
        tol=tol,
        rounding=rounding,
        downcast=downcast,
        errors=errors,
        **unused
    )


#######################
####    DECIMAL    ####
#######################


@to_decimal.overload("timedelta")
def timedelta_to_decimal(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    tol: Tolerance,
    rounding: str,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert timedelta data to a decimal data type."""
    # 2-step conversion: timedelta -> ns, ns -> decimal
    series = to_integer(
        series,
        dtype="int",
        unit="ns",
        step_size=1,
        since=since,
        rounding=None,
        downcast=None,
        errors=errors
    )
    series = to_decimal(
        series,
        dtype=dtype,
        unit=unit,
        step_size=step_size,
        since=since,
        tol=tol,
        rounding=rounding,
        errors=errors,
        **unused
    )
    if unit != "ns" or step_size != 1:
        series.series = convert_unit(
            series.series,
            "ns",
            unit,
            rounding=rounding,
            since=since
        )
        if step_size != 1:
            series.series /= step_size

    return series


########################
####    DATETIME    ####
########################


@to_datetime.overload("timedelta")
def timedelta_to_datetime(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    rounding: str,
    since: Epoch,
    tz: pytz.BaseTzInfo,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert datetime data to another datetime representation."""
    # 2-step conversion: datetime -> ns, ns -> datetime
    series = to_integer(
        series,
        dtype="int",
        unit="ns",
        step_size=1,
        rounding=rounding,
        since=since,
        downcast=None,
        errors=errors
    )
    return to_datetime(
        series,
        dtype=dtype,
        unit="ns",
        step_size=1,
        rounding=rounding,
        since=since,
        tz=tz,
        errors=errors,
        **unused
    )


#########################
####    TIMEDELTA    ####
#########################


@to_timedelta.overload("timedelta")
def timedelta_to_timedelta(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    rounding: str,
    since: Epoch,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert timedelta data to a timedelta representation."""
    # trivial case
    if dtype == self:
        return series.rectify()

    # 2-step conversion: datetime -> ns, ns -> timedelta
    transfer_type = resolve.resolve_type("int")
    series = self.to_integer(
        series,
        dtype=transfer_type,
        unit="ns",
        step_size=1,
        rounding=rounding,
        since=since,
        downcast=None,
        errors=errors
    )
    return transfer_type.to_timedelta(
        series,
        dtype=dtype,
        unit="ns",
        step_size=1,
        rounding=rounding,
        since=since,
        errors=errors,
        **unused
    )




# def convert_ns_to_unit(
#     series: SeriesWrapper,
#     unit: str,
#     step_size: int,
#     rounding: str,
#     since: Epoch
# ) -> None:
#     """Helper for converting between integer time units."""
#     series.series = convert_unit(
#         series.series,
#         "ns",
#         unit,
#         since=since,
#         rounding=rounding or "down",
#     )
#     if step_size != 1:
#         series.series = round_div(
#             series.series,
#             step_size,
#             rule=rounding or "down"
#         )
