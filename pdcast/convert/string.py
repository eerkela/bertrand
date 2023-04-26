from pdcast import types
from pdcast.util import wrapper
from pdcast.util.round import snap_round, Tolerance

from .base import (
    to_boolean, to_integer, to_float, to_complex, to_datetime, to_timedelta
)


@to_boolean.overload("string")
def string_to_boolean(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    true: set,
    false: set,
    errors: str,
    ignore_case: bool,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert string data to a boolean data type."""
    # configure lookup dict
    lookup = dict.fromkeys(true, 1) | dict.fromkeys(false, 0)
    if "*" in true:
        fill = 1  # KeyErrors become truthy
    elif "*" in false:
        fill = 0  # KeyErrors become falsy
    else:
        fill = -1  # raise

    # apply lookup function with specified errors
    series = series.apply_with_errors(
        partial(
            boolean_apply,
            lookup=lookup,
            ignore_case=ignore_case,
            fill=fill
        ),
        errors=errors,
        element_type=resolve.resolve_type("bool")
    )
    return super().to_boolean(series, dtype, errors=errors)


@to_integer.overload("string")
def string_to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    base: int,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert string data to an integer data type with the given base."""
    transfer_type = resolve.resolve_type("int[python]")
    series = series.apply_with_errors(
        partial(int, base=base),
        errors=errors,
        element_type=transfer_type
    )
    return transfer_type.to_integer(
        series,
        dtype=dtype,
        base=base,
        errors=errors,
        **unused
    )


@to_float.overload("string")
def string_to_float(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert string data to a floating point data type."""
    transfer_type = resolve.resolve_type("decimal")
    series = self.to_decimal(series, transfer_type, errors=errors)
    return transfer_type.to_float(
        series,
        dtype=dtype,
        tol=tol,
        errors=errors,
        **unused
    )


@to_complex.overload("string")
def string_to_complex(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: bool | BaseType,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert string data to a complex data type."""
    # NOTE: this is technically a 3-step conversion: (1) str -> str,
    # (2) str -> float, (3) float -> complex.  This allows for full
    # precision loss/overflow/downcast checks for both real + imag.

    # (1) separate real, imaginary components via regex
    components = series.str.extract(complex_pattern)
    real = wrapper.SeriesWrapper(
        components["real"],
        hasnans=series.hasnans,
        element_type=self
    )
    imag = wrapper.SeriesWrapper(
        components["imag"],
        hasnans=series.hasnans,
        element_type=self
    )

    # (2) convert real, imag to float, applying checks independently
    real = self.to_float(
        real,
        dtype=dtype.equiv_float,
        tol=Tolerance(tol.real),
        downcast=None,
        errors="raise"
    )
    imag = self.to_float(
        imag,
        dtype=dtype.equiv_float,
        tol=Tolerance(tol.imag),
        downcast=None,
        errors="raise"
    )

    # (3) combine floats into complex result
    series = real + imag * 1j
    series.element_type = dtype
    return super().to_complex(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


@to_datetime.overload("string")
def string_to_datetime(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert string data into a datetime data type."""
    return dtype.from_string(series, dtype=dtype, **unused)


@to_timedelta.overload("string")
def string_to_timedelta(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: Epoch,
    as_hours: bool,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert string data into a timedelta representation."""
    # 2-step conversion: str -> int, int -> timedelta
    transfer_type = resolve.resolve_type("int[python]")
    series = series.apply_with_errors(
        partial(timedelta_string_to_ns, as_hours=as_hours, since=since),
        errors=errors,
        element_type=transfer_type
    )
    return transfer_type.to_timedelta(
        series,
        dtype=dtype,
        unit="ns",
        step_size=1,
        since=since,
        errors=errors,
        **unused
    )
