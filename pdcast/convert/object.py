from typing import Callable

from pdcast import types
from pdcast.util import wrapper

from .base import (
    to_boolean, to_integer, to_float, to_decimal, to_complex, to_datetime,
    to_timedelta, to_string, to_object
)


@to_boolean.overload(object)
def object_to_boolean(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert unstructured objects to a boolean data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_boolean,
        **unused
    )


@to_integer.overload(object)
def object_to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert unstructured objects to an integer data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_integer,
        **unused
    )


@to_float.overload(object)
def object_to_float(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert unstructured objects to a float data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=convert.to_float,
        **unused
    )


@to_complex.overload(object)
def object_to_complex(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert unstructured objects to a complex data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_complex,
        **unused
    )


@to_decimal.overload(object)
def object_to_decimal(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert unstructured objects to a decimal data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_decimal,
        **unused
    )


@to_datetime.overload(object)
def object_to_datetime(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert unstructured objects to a datetime data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_datetime,
        **unused
    )


@to_timedelta.overload(object)
def object_to_timedelta(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert unstructured objects to a timedelta data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_timedelta,
        **unused
    )


@to_string.overload(object)
def object_to_string(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert unstructured objects to a string data type."""
    return two_step_conversion(
        series=series,
        dtype=dtype,
        call=dtype.type_def if call is None else call,
        errors=errors,
        conv_func=dtype.to_string,
        **unused
    )


@to_object.overload(object)
def object_to_object(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    **unused
) -> wrapper.SeriesWrapper:
    """Convert arbitrary data to an object data type."""
    # trivial case
    if dtype == series.element_type:
        return series.rectify()
    return to_object.generic(series, dtype=dtype, **unused)


#######################
####    PRIVATE    ####
#######################


def two_step_conversion(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    conv_func: Callable,
    **unused
) -> wrapper.SeriesWrapper:
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
