"""This module contains dispatched cast() implementations for data stored as
raw Python objects.
"""
from typing import Callable

from pdcast import types
from pdcast.decorators.wrapper import SeriesWrapper

from .base import cast


@cast.overload("object", "bool")
def object_to_boolean(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a boolean data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_boolean,
        **unused
    )


@cast.overload("object", "int")
def object_to_integer(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to an integer data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_integer,
        **unused
    )


@cast.overload("object", "float")
def object_to_float(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a float data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=convert.to_float,
        **unused
    )


@cast.overload("object", "complex")
def object_to_complex(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a complex data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_complex,
        **unused
    )


@cast.overload("object", "decimal")
def object_to_decimal(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a decimal data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_decimal,
        **unused
    )


@cast.overload("object", "datetime")
def object_to_datetime(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a datetime data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_datetime,
        **unused
    )


@cast.overload("object", "timedelta")
def object_to_timedelta(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a timedelta data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_timedelta,
        **unused
    )


@cast.overload("object", "string")
def object_to_string(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> SeriesWrapper:
    """Convert unstructured objects to a string data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_string,
        **unused
    )


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
    return to_object.generic(series, dtype=dtype, **unused)


#######################
####    PRIVATE    ####
#######################


def two_step_conversion(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    conv_func: Callable,
    **unused
) -> SeriesWrapper:
    """A conversion in two parts."""
    def safe_call(val):
        result = call(val)
        output_type = type(result)
        if output_type != dtype.type_def:
            raise TypeError(
                f"`call` must return an object of type {dtype.type_def}"
            )
        return result

    # apply `safe_call` over series and pass to delegated conversion
    series = series.apply_with_errors(
        call=safe_call,
        errors=errors,
        element_type=dtype
    )
    return conv_func(series, dtype=dtype, errors=errors, **unused)
