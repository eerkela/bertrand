"""This module contains dispatched cast() implementations for datetime data."""
from pdcast import types
from pdcast.util import wrapper
from pdcast.util.round import Tolerance
from pdcast.util.time import Epoch

from .base import (
    to_boolean, to_integer, to_float, to_decimal, to_complex, to_datetime,
    to_timedelta
)



#######################
####    BOOLEAN    ####
#######################


@to_boolean.overload("datetime")
def datetime_to_boolean(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    rounding: str,
    unit: str,
    step_size: int,
    since: Epoch,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
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


@to_integer.overload("datetime[numpy]")
def numpy_datetime64_to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    unit: str,
    step_size: int,
    since: Epoch,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert numpy datetime64s into an integer data type."""
    series_type = series.element_type

    # NOTE: using numpy M8 array is ~2x faster than looping through series
    M8_str = f"M8[{series_type.step_size}{series_type.unit}]"
    arr = series.series.to_numpy(M8_str).view(np.int64).astype(object)
    arr *= series_type.step_size
    if since:  # apply epoch offset if not utc
        arr = convert_unit(
            arr,
            series_type.unit,
            "ns"
        )
        arr -= since.offset  # retains full ns precision from epoch
        arr = convert_unit(
            arr,
            "ns",
            unit,
            rounding=rounding or "down"
        )
    else:  # skip straight to final unit
        arr = convert_unit(
            arr,
            series_type.unit,
            unit,
            rounding=rounding or "down"
        )
    series = wrapper.SeriesWrapper(
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


@to_integer.overload("datetime[pandas]")
def pandas_timestamp_to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    unit: str,
    step_size: int,
    since: Epoch,
    naive_tz: pytz.BaseTzInfo,
    downcast: types.CompositeType,
    errors: str,
    **kwargs
) -> wrapper.SeriesWrapper:
    """Convert pandas Timestamps into an integer data type."""
    # convert to ns
    series = series.rectify()
    if naive_tz:
        series.series = series.series.dt.tz_localize(naive_tz)
    series = series.astype(np.int64)

    # apply epoch
    if since:
        series.series = series.series.astype("O")  # overflow-safe
        series.series -= since.offset

    # convert ns to final unit
    if unit != "ns" or step_size != 1:
        convert_ns_to_unit(
            series,
            unit=unit,
            step_size=step_size,
            rounding=rounding
        )

    # boundscheck and convert to final integer representation
    series, dtype = series.boundscheck(dtype, errors=errors)
    return super().to_integer(
        series,
        dtype,
        downcast=downcast,
        errors=errors
    )


@to_integer.overload("datetime[python]")
def to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    rounding: str,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert python datetimes into an integer data type."""
    series = series.apply_with_errors(
        pydatetime_to_ns,
        element_type=resolve.resolve_type("int[python]")
    )
    if since:
        series.series -= since.offset

    if unit != "ns" or step_size != 1:
        convert_ns_to_unit(
            series,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
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


@to_float.overload("datetime")
def datetime_to_float(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    tol: Tolerance,
    rounding: str,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
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
            series.series.astype(object),
            "ns",
            unit,
            rounding=rounding,
            since=since
        )
        if step_size != 1:
            series.series /= step_size
            series.element_type = float

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


@to_complex.overload("datetime")
def datetime_to_complex(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    tol: Tolerance,
    rounding: str,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert timedelta data to a complex data type."""
    # 2-step conversion: timedelta -> float, float -> complex
    series = to_float(
        series,
        dtype=series.element_type.equiv_float,
        unit=unit,
        step_size=step_size,
        since=since,
        tol=tol,
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


@to_decimal.overload("datetime")
def datetime_to_decimal(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    tol: Tolerance,
    rounding: str,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert timedelta data to a decimal data type."""
    # 2-step conversion: datetime -> ns, ns -> decimal
    series = self.to_integer(
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


@to_datetime.overload("datetime")
def datetime_to_datetime(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    unit: str,
    step_size: int,
    since: Epoch,
    tz: pytz.BaseTzInfo,
    naive_tz: pytz.BaseTzInfo,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert datetime data to another datetime representation."""
    # trivial case
    if dtype == self:
        return series.rectify()

    if tz and hasattr(dtype, "tz"):
        dtype = dtype.replace(tz=tz)
    if naive_tz is None:
        if hasattr(dtype, "tz"):
            naive_tz = dtype.tz
        else:
            naive_tz = tz

    # 2-step conversion: datetime -> ns, ns -> datetime
    series = to_integer(
        series,
        dtype="int",
        rounding=rounding,
        unit="ns",
        step_size=1,
        since=Epoch("utc"),
        naive_tz=naive_tz,
        downcast=None,
        errors=errors
    )
    return to_datetime(
        series,
        dtype=dtype,
        rounding=rounding,
        unit="ns",
        step_size=1,
        since=Epoch("utc"),
        tz=tz,
        naive_tz=naive_tz,
        errors=errors,
        **unused
    )


@to_datetime.overload("datetime[pandas]")
def pandas_timestamp_to_datetime(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tz: pytz.BaseTzInfo,
    naive_tz: pytz.BaseTzInfo,
    **unused
) -> wrapper.SeriesWrapper:
    """Specialized for same-type conversions."""
    series_type = series.element_type

    # fastpath for same-class datetime conversions
    if type(dtype) == type(series_type):
        series = series.rectify()
        if tz:
            dtype = dtype.replace(tz=tz)
        if dtype.tz != series_type.tz:
            if not series_type.tz:
                if not naive_tz:
                    result = series.series.dt.tz_localize(dtype.tz)
                else:
                    result = series.series.dt.tz_localize(naive_tz)
                series = wrapper.SeriesWrapper(
                    result,
                    hasnans=series.hasnans,
                    element_type=series_type.replace(tz=naive_tz)
                )
            series = wrapper.SeriesWrapper(
                series.series.dt.tz_convert(dtype.tz),
                hasnans=series.hasnans,
                element_type=dtype
            )
        return series

    return to_datetime.generic(
        series,
        dtype=dtype,
        tz=tz,
        naive_tz=naive_tz,
        **unused
    )


@to_datetime.overload("datetime[python]")
def pydatetime_to_datetime(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tz: pytz.BaseTzInfo,
    **unused
) -> wrapper.SeriesWrapper:
    """Specialized for same-type conversions."""
    series_type = series.element_type

    # fastpath for same-class datetime conversions
    if type(dtype) == type(series_type):
        if tz:
            dtype = dtype.replace(tz=tz)
        if dtype.tz != series_type.tz:
            if not series_type.tz:
                series = series_type.tz_localize(series, "UTC")
            return series_type.tz_convert(series, dtype.tz)

    return to_datetime.generic(series, dtype=dtype, tz=tz, **unused)


#########################
####    TIMEDELTA    ####
#########################


@to_timedelta.overload("datetime")
def datetime_to_timedelta(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    rounding: str,
    since: Epoch,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert datetime data to a timedelta representation."""
    # 2-step conversion: datetime -> ns, ns -> timedelta
    series = self.to_integer(
        series,
        dtype="int",
        unit="ns",
        step_size=1,
        rounding=rounding,
        since=since,
        downcast=None,
        errors=errors
    )
    return to_timedelta(
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
#     series: wrapper.SeriesWrapper,
#     unit: str,
#     step_size: int,
#     rounding: str
# ) -> None:
#     """Helper for converting between integer time units."""
#     series.series = convert_unit(
#         series.series,
#         "ns",
#         unit,
#         rounding=rounding or "down",
#     )
#     if step_size != 1:
#         series.series = round_div(
#             series.series,
#             step_size,
#             rule=rounding or "down"
#         )