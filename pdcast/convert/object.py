"""This module contains dispatched cast() implementations for data stored as
raw Python objects.
"""
# pylint: disable=unused-argument
from typing import Callable

from pdcast import types
from pdcast.decorators.wrapper import SeriesWrapper

from .base import cast, generic_to_object, safe_apply


@cast.overload("object", "bool")
def object_to_boolean(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a boolean data type."""
    # if no callable is given, hook into object's __bool__ dunder
    if call is None:
        call = bool
        element_type = bool
    else:
        element_type = None  # don't know what output of `call` will be

    # apply callable
    series = series.apply_with_errors(
        call,
        errors=errors,
        element_type=element_type
    )

    # check result is boolean
    if not series.element_type.is_subtype("bool"):
        raise TypeError(
            f"`call` must produce boolean output, not "
            f"{str(series.element_type)}"
        )

    # convert int[python] -> final repr
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "int")
def object_to_integer(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to an integer data type."""
    # if no callable is given, hook into object's __int__ dunder
    if call is None:
        call = int
        element_type = int
    else:
        element_type = None

    # apply callable
    series = series.apply_with_errors(
        call,
        errors=errors,
        element_type=element_type
    )

    # check result is integer
    if not series.element_type.is_subtype("int"):
        raise TypeError(
            f"`call` must produce integer output, not "
            f"{str(series.element_type)}"
        )

    # apply final conversion
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "float")
def object_to_float(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a float data type."""
    # if no callable is given, hook into object's __float__ dunder
    if call is None:
        call = float
        element_type = float
    else:
        element_type = None

    # apply callable
    series = series.apply_with_errors(
        call,
        errors=errors,
        element_type=element_type
    )

    # check result is float
    if not series.element_type.is_subtype("float"):
        raise TypeError(
            f"`call` must produce float output, not "
            f"{str(series.element_type)}"
        )

    # convert float[python] -> final repr
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "complex")
def object_to_complex(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a complex data type."""
    # if no callable is given, hook into object's __complex__ dunder
    if call is None:
        call = complex
        element_type = complex
    else:
        element_type = None

    # apply callable
    series = series.apply_with_errors(
        call,
        errors=errors,
        element_type=element_type
    )

    # check result is complex
    if not series.element_type.is_subtype("complex"):
        raise TypeError(
            f"`call` must produce complex output, not "
            f"{str(series.element_type)}"
        )

    # convert complex[python] -> final repr
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "decimal")
def object_to_decimal(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a decimal data type."""
    # if no callable is given, hook into object's __float__ dunder
    if call is None:
        call = lambda x: dtype.type_def(float(x))

    # check callable output matches dtype at every index
    return safe_apply(series=series, dtype=dtype, call=call, errors=errors)


@cast.overload("object", "datetime")
def object_to_datetime(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a datetime data type."""
    # if callable is given, convert directly
    if call:
        return safe_apply(series=series, dtype=dtype, call=call, errors=errors)

    # 2-step conversion: object -> float, float -> datetime
    series = cast(series, float, call=float, errors=errors)
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "timedelta")
def object_to_timedelta(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a timedelta data type."""
    # if callable is given, convert directly
    if call:
        return safe_apply(
            series=series,
            dtype=dtype,
            call=call,
            errors=errors
        )

    # 2-step conversion: object -> float, float -> timedelta
    series = cast(series, float, call=float, errors=errors)
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "string")
def object_to_string(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a string data type."""
    # if no callable is given, hook into object's __str__ dunder
    if call is None:
        if dtype.type_def is str:
            call = str
        else:
            call = lambda x: dtype.type_def(str(x))

    # check callable output matches dtype at every index
    return safe_apply(series=series, dtype=dtype, call=call, errors=errors)


@cast.overload("object", "object")
def object_to_object(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    **unused
) -> SeriesWrapper:
    """Convert arbitrary data to an object data type."""
    # trivial case
    if dtype == series.element_type:
        return series.rectify()

    # fall back to generic implementation
    return generic_to_object(series, dtype=dtype, **unused)
