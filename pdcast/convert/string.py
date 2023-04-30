"""This module contains dispatched cast() implementations for string data."""
# pylint: disable=unused-argument
import re  # normal python regex for compatibility with pd.Series.str.extract
from functools import partial

from pdcast import types
from pdcast.decorators.wrapper import SeriesWrapper
from pdcast.util.round import Tolerance

from .base import (
    cast, generic_to_boolean, generic_to_complex
)


@cast.overload("string", "bool")
def string_to_boolean(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    true: set,
    false: set,
    errors: str,
    ignore_case: bool,
    **unused
) -> SeriesWrapper:
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
        element_type="bool"
    )
    return generic_to_boolean(series, dtype, errors=errors)


@cast.overload("string", "int")
def string_to_integer(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    base: int,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert string data to an integer data type with the given base."""
    # 2 step conversion: string -> int[python], int[python] -> int
    series = series.apply_with_errors(
        partial(int, base=base),
        errors=errors,
        element_type=int
    )
    return cast(
        series,
        dtype,
        base=base,
        errors=errors,
        **unused
    )


@cast.overload("string", "float")
def string_to_float(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert string data to a floating point data type."""
    # 2 step conversion: string -> decimal, decimal -> float
    series = cast(series, "decimal", errors=errors)
    return cast(series, dtype, tol=tol, errors=errors, **unused)


@cast.overload("string", "complex")
def string_to_complex(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert string data to a complex data type."""
    # NOTE: this is technically a 3-step conversion: (1) str -> str (split
    # real/imag), (2) str -> float, (3) float -> complex.  This allows for full
    # precision loss/overflow/downcast checks for both real + imag.

    # (1) separate real, imaginary components via regex
    components = series.str.extract(complex_pattern)
    real = SeriesWrapper(
        components["real"],
        hasnans=series.hasnans,
        element_type=series.element_type
    )
    imag = SeriesWrapper(
        components["imag"],
        hasnans=series.hasnans,
        element_type=series.element_type
    )

    # (2) convert real, imag to float, applying checks independently
    real = cast(
        real,
        dtype.equiv_float,
        tol=Tolerance(tol.real),
        downcast=None,
        errors="raise"
    )
    imag = cast(
        imag,
        dtype.equiv_float,
        tol=Tolerance(tol.imag),
        downcast=None,
        errors="raise"
    )

    # (3) combine floats into complex result
    series = real + imag * 1j
    series.element_type = dtype
    return generic_to_complex(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


# @cast.overload("string", "datetime")
# def string_to_datetime(
#     series: SeriesWrapper,
#     dtype: types.AtomicType,
#     **unused
# ) -> SeriesWrapper:
#     """Convert string data into a datetime data type."""
#     return dtype.from_string(series, dtype=dtype, **unused)


# @cast.overload("string", "timedelta")
# def string_to_timedelta(
#     series: SeriesWrapper,
#     dtype: types.AtomicType,
#     unit: str,
#     step_size: int,
#     since: Epoch,
#     as_hours: bool,
#     errors: str,
#     **unused
# ) -> SeriesWrapper:
#     """Convert string data into a timedelta representation."""
#     # 2-step conversion: str -> int, int -> timedelta
#     transfer_type = resolve.resolve_type("int[python]")
#     series = series.apply_with_errors(
#         partial(timedelta_string_to_ns, as_hours=as_hours, since=since),
#         errors=errors,
#         element_type=transfer_type
#     )
#     return transfer_type.to_timedelta(
#         series,
#         dtype=dtype,
#         unit="ns",
#         step_size=1,
#         since=since,
#         errors=errors,
#         **unused
#     )


#######################
####    PRIVATE    ####
#######################


complex_pattern = re.compile(
    r"\(?(?P<real>[+-]?[0-9.]+)(?P<imag>[+-][0-9.]+)?j?\)?"
)
