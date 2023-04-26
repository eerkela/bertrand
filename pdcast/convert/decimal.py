from pdcast import types
from pdcast.util import wrapper

from pdcast.util.round import snap_round, Tolerance


from .base import (
    to_boolean, to_integer, to_float, to_decimal, to_complex, to_datetime,
    to_timedelta
)



@to_boolean.overload("decimal")
def decimal_to_boolean(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert decimal data to a boolean data type."""
    series = snap_round(
        series,
        tol=tol.real,
        rule=rounding,
        errors=errors
    )
    series, dtype = series.boundscheck(dtype, errors=errors)
    return to_boolean.generic(series, dtype=dtype, errors=errors)


@to_integer.overload("decimal")
def decimal_to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    rounding: str,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert decimal data to an integer data type."""
    series = snap_round(
        series,
        tol=tol.real,
        rule=rounding,
        errors=errors
    )
    series, dtype = series.boundscheck(dtype, errors=errors)
    return to_integer.generic(
        series,
        dtype,
        downcast=downcast,
        errors=errors
    )


@to_float.overload("decimal")
def decimal_to_float(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert decimal data to a floating point data type."""
    # TODO: remove self references

    # do naive conversion
    if dtype.itemsize > 8:
        # NOTE: series.astype() implicitly calls Decimal.__float__(), which
        # is limited to 64-bits.  Converting to an intermediate string
        # representation avoids this.
        result = series.astype(str).astype(dtype)
    else:
        result = series.astype(dtype)

    # check for overflow
    if int(series.min) < dtype.min or int(series.max) > dtype.max:
        infs = result.isinf() ^ series.isinf()
        if infs.any():
            if errors == "coerce":
                result = result[~infs]
                result.hasnans = True
                series = series[~infs]  # mirror on original
            else:
                raise OverflowError(
                    f"values exceed {dtype} range at index "
                    f"{shorten_list(infs[infs].index.values)}"
                )

    # backtrack to check for precision loss
    if errors != "coerce":  # coercion ignores precision loss
        bad = ~series.within_tol(
            dtype.to_decimal(result, dtype=self, errors="raise"),
            tol=tol.real
        )
        if bad.any():
            raise ValueError(
                f"precision loss exceeds tolerance {float(tol.real):g} at "
                f"index {shorten_list(bad[bad].index.values)}"
            )

    if downcast is not None:
        return dtype.downcast(result, smallest=downcast, tol=tol)
    return result


@to_complex.overload("decimal")
def decimal_to_complex(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert decimal data to a complex data type."""
    # 2-step conversion: decimal -> float, float -> complex
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


@to_decimal.overload("decimal")
def decimal_to_decimal(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert boolean data to a decimal data type."""
    return series.astype(dtype, errors=errors)


@to_datetime.overload("decimal")
def decimal_to_datetime(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    tz: pytz.BaseTzInfo,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a datetime data type."""
    # round fractional inputs to the nearest nanosecond
    if unit == "Y":
        ns = round_years_to_ns(series.series * step_size, since=since)
    elif unit == "M":
        ns = round_months_to_ns(series.series * step_size, since=since)
    else:
        ns = series.series * step_size * as_ns[unit]
    ns = np.frompyfunc(int, 1, 1)(ns).astype("O", copy=False)

    # account for non-utc epoch
    if since:
        ns += since.offset

    series = wrapper.SeriesWrapper(
        ns,
        hasnans=series.hasnans,
        element_type=resolve.resolve_type("int[python]")
    )

    # check for overflow and upcast if applicable
    series, dtype = series.boundscheck(dtype, errors=errors)

    # convert to final representation
    return dtype.from_ns(
        series,
        dtype=dtype,
        unit=unit,
        step_size=step_size,
        since=since,
        tz=tz,
        errors=errors,
        **unused
    )


@to_timedelta.overload("decimal")
def decimal_to_timedelta(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert integer data to a timedelta data type."""
    # round fractional inputs to the nearest nanosecond
    if unit == "Y":  # account for leap days
        ns = round_years_to_ns(series.series * step_size, since=since)
    elif unit == "M":  # account for irregular lengths
        ns = round_months_to_ns(series.series * step_size, since=since)
    else:
        cast_to_int = np.frompyfunc(int, 1, 1)
        ns = cast_to_int(series.series * step_size * as_ns[unit])

    series = wrapper.SeriesWrapper(
        ns,
        hasnans=series.hasnans,
        element_type=resolve.resolve_type("int[python]")
    )

    # check for overflow and upcast if necessary
    series, dtype = series.boundscheck(dtype, errors=errors)

    # convert to final representation
    return dtype.from_ns(
        series,
        dtype=dtype,
        unit=unit,
        step_size=step_size,
        since=since,
        errors=errors,
        **unused,
    )

