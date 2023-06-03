"""This module contains dispatched cast() implementations for data stored as
raw Python objects.
"""
# pylint: disable=unused-argument
from typing import Callable

import pandas as pd

from pdcast import types
from pdcast.resolve import resolve_type
from pdcast.detect import detect_type
from pdcast.util.vector import apply_with_errors

from .base import cast, generic_to_object, safe_apply


@cast.overload("object", "bool")
def object_to_boolean(
    series: pd.Series,
    dtype: types.ScalarType,
    call: Callable,
    errors: str,
    **unused
) -> pd.Series:
    """Convert unstructured objects to a boolean data type."""
    # if no callable is given, hook into object's __bool__ dunder
    use_dunder = call is None or call is bool
    if use_dunder:
        call = bool

    # apply callable
    series = apply_with_errors(series, call, errors=errors)
    if use_dunder:
        series = series.astype(resolve_type(bool).dtype)
    else:
        output_type = detect_type(series)
        if not output_type.is_subtype("bool"):
            raise TypeError(
                f"`call` must produce boolean output, not {str(output_type)}"
            )
        series = series.astype(output_type.dtype)

    # convert bool[python] -> final repr
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "int")
def object_to_integer(
    series: pd.Series,
    dtype: types.ScalarType,
    call: Callable,
    errors: str,
    **unused
) -> pd.Series:
    """Convert unstructured objects to an integer data type."""
    # if no callable is given, hook into object's __int__ dunder
    use_dunder = call is None or call is int
    if use_dunder:
        call = int

    # apply callable
    series = apply_with_errors(series, call, errors=errors)
    if use_dunder:
        series = series.astype(resolve_type(int).dtype)
    else:
        output_type = detect_type(series)
        if not output_type.is_subtype("int"):
            raise TypeError(
                f"`call` must produce integer output, not {str(output_type)}"
            )
        series = series.astype(output_type.dtype)

    # convert int[python] -> final repr
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "float")
def object_to_float(
    series: pd.Series,
    dtype: types.ScalarType,
    call: Callable,
    errors: str,
    **unused
) -> pd.Series:
    """Convert unstructured objects to a float data type."""
    # if no callable is given, hook into object's __float__ dunder
    use_dunder = call is None or call is float
    if use_dunder:
        call = float

    # apply callable
    series = apply_with_errors(series, call, errors=errors)
    if use_dunder:
        series = series.astype(resolve_type(float).dtype)
    else:
        output_type = detect_type(series)
        if not output_type.is_subtype("float"):
            raise TypeError(
                f"`call` must produce float output, not {str(output_type)}"
            )
        series = series.astype(output_type.dtype)

    # convert float[python] -> final repr
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "complex")
def object_to_complex(
    series: pd.Series,
    dtype: types.ScalarType,
    call: Callable,
    errors: str,
    **unused
) -> pd.Series:
    """Convert unstructured objects to a complex data type."""
    # if no callable is given, hook into object's __complex__ dunder
    use_dunder = call is None or call is complex
    if use_dunder:
        call = complex

    # apply callable
    series = apply_with_errors(series, call, errors=errors)
    if use_dunder:
        series = series.astype(resolve_type(complex).dtype)
    else:
        output_type = detect_type(series)
        if not output_type.is_subtype("complex"):
            raise TypeError(
                f"`call` must produce complex output, not {str(output_type)}"
            )
        series = series.astype(output_type.dtype)

    # convert complex[python] -> final repr
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "decimal")
def object_to_decimal(
    series: pd.Series,
    dtype: types.ScalarType,
    call: Callable,
    errors: str,
    **unused
) -> pd.Series:
    """Convert unstructured objects to a decimal data type."""
    # if no callable is given, hook into object's __float__ dunder
    if call is None:
        call = lambda x: dtype.type_def(float(x))

    # check callable output matches dtype at every index
    return safe_apply(series=series, dtype=dtype, call=call, errors=errors)


@cast.overload("object", "datetime")
def object_to_datetime(
    series: pd.Series,
    dtype: types.ScalarType,
    call: Callable,
    errors: str,
    **unused
) -> pd.Series:
    """Convert unstructured objects to a datetime data type."""
    # if callable is given, convert directly
    if call:
        return safe_apply(series=series, dtype=dtype, call=call, errors=errors)

    # 2-step conversion: object -> float, float -> datetime
    series = cast(series, float, call=float, errors=errors)
    return cast(series, dtype, call=call, errors=errors, **unused)


@cast.overload("object", "timedelta")
def object_to_timedelta(
    series: pd.Series,
    dtype: types.ScalarType,
    call: Callable,
    errors: str,
    **unused
) -> pd.Series:
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
    series: pd.Series,
    dtype: types.ScalarType,
    call: Callable,
    errors: str,
    **unused
) -> pd.Series:
    """Convert unstructured objects to a string data type."""
    # if no callable is given, hook into object's __str__ dunder
    if call is None:
        if dtype.type_def is str:
            call = str
        else:
            call = lambda x: dtype.type_def(str(x))

    # check callable output matches dtype at every index
    return safe_apply(series=series, dtype=dtype, call=call, errors=errors)
