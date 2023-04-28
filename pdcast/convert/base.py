"""This module defines the ``cast()`` function, as well as several stand-alone
equivalents that allow quick conversion to predefined data types.
"""
from __future__ import annotations
from typing import Any, Callable, Optional

import pandas as pd

from pdcast import types
from pdcast.decorators.attachable import attachable
from pdcast.decorators.base import BaseDecorator
from pdcast.decorators.extension import extension_func
from pdcast.decorators.dispatch import dispatch
from pdcast.type_hints import type_specifier
from pdcast.util import wrapper
from pdcast.util.round import Tolerance

# ignore this file when doing string-based object lookups in resolve_type()
IGNORE_FRAME_OBJECTS = True


class columnwise(BaseDecorator):
    """A basic decorator that breaks up DataFrame inputs into individual
    columns before continuing with a conversion.

    Placing this above ``@extension_func`` allows validators to avoid
    implementing this themselves, and above ``@dispatch`` allows dispatched
    implementations to always work in one dimension.
    """

    def __call__(
        self,
        data: Any,
        dtype: type_specifier | dict[str, type_specifier] = NotImplemented,
        *args,
        **kwargs
    ):
        """Apply the wrapped function for each column independently."""
        # use default dtype if not given
        if dtype is NotImplemented:
            dtype = self.__wrapped__.dtype

        # recursive DataFrame case
        if isinstance(data, pd.DataFrame):
            # broadcast across columns
            columns = data.columns
            if isinstance(dtype, dict):
                bad = [col for col in dtype if col not in columns]
                if bad:
                    raise ValueError(f"column not found: {repr(bad)}")
            else:
                dtype = dict.fromkeys(columns, dtype)

            # pass each column individually
            result = data.copy()
            for col, typespec in dtype.items():
                result[col] = self.__wrapped__(
                    result[col],
                    typespec,
                    *args,
                    **kwargs
                )
            return result

        # base case
        return self.__wrapped__(data, dtype, *args, **kwargs)


class catch_ignore(BaseDecorator):
    """A basic decorator that enforces the ``errors="ignore"`` rule during
    conversions.

    Placing this above ``@dispatch`` allows dispatched implementations to
    avoid implementing this themselves.
    """

    def __call__(
        self,
        data: Any,
        *args,
        errors: str = NotImplemented,
        **kwargs
    ):
        """Call the wrapped function in a try/except block."""
        if errors is NotImplemented:
            errors = self.__wrapped__.errors

        try:
            return self.__wrapped__(data, *args, errors=errors, **kwargs)

        # parse errors
        except (KeyboardInterrupt, MemoryError, SystemError, SystemExit):
            raise  # never ignore these errors
        except Exception as err:
            if errors == "ignore":
                return data
            raise err


######################
####    PUBLIC    ####
######################


@attachable
@columnwise
@extension_func
@dispatch(depth=2)  # dispatch on data and dtype
def cast(
    series: Any,
    dtype: Optional[type_specifier] = None,
    **kwargs
) -> pd.Series:
    """Cast arbitrary data to the specified data type.

    Parameters
    ----------
    data : Any
        The data to convert.  This can be a scalar or 1D iterable containing
        arbitrary data.
    dtype : type specifier
        The target :doc:`type </content/types/types>` for this conversion.
        This can be in any format recognized by :func:`resolve_type`.  It can
        also be omitted to perform an :ref:`anonymous <cast.anonymous>`
        conversion.
    **kwargs : dict
        Arbitrary keyword :ref:`arguments <cast.arguments>` used to customize
        the conversion.

    Notes
    -----
    This function dispatches to one of the
    :ref:`delegated <atomic_type.conversions>` conversion methods that are
    attached to each :class:`AtomicType`.  Types can override these methods to
    change the behavior of :func:`cast`.  The method that is chosen is based on
    the :attr:`family <AtomicType.family>` of its ``dtype`` argument.
    """
    return dtype.conversion_func(series, dtype, **kwargs)


#######################
####    GENERIC    ####
#######################


# NOTE: the following are base conversions for each of the major data types.
# They are used if no specific implementation is explicitly given for the
# observed data


# wildcard = (
#     "bool, int, float, complex, decimal, datetime, timedelta, string, object"
# )


wildcard = (
    "bool, int, float, complex, decimal, string, object"
)


@cast.overload(wildcard, "bool")
def generic_to_boolean(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert a to boolean representation."""
    if series.hasnans:
        dtype = dtype.make_nullable()
    return series.astype(dtype, errors=errors)


@cast.overload(wildcard, "int")
def generic_to_integer(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    errors: str,
    downcast: types.CompositeType,
    **unused
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    if series.hasnans:
        dtype = dtype.make_nullable()

    series = series.astype(dtype, errors=errors)
    if downcast is not None:
        return dtype.downcast(series, smallest=downcast)
    return series


@cast.overload(wildcard, "float")
def generic_to_float(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    series = series.astype(dtype, errors=errors)
    if downcast is not None:
        return dtype.downcast(series, smallest=downcast, tol=tol)
    return series


@cast.overload(wildcard, "complex")
def generic_to_complex(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to complex representation."""
    series = series.astype(dtype, errors=errors)
    if downcast is not None:
        return dtype.downcast(series, smallest=downcast, tol=tol)
    return series


@cast.overload(wildcard, "decimal")
def generic_to_decimal(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    return series.astype(dtype, errors=errors)


# @cast.overload(wildcard, "datetime")
# def generic_to_datetime(
#     series: wrapper.SeriesWrapper,
#     dtype: types.AtomicType,
#     **unused
# ) -> pd.Series:
#     """Convert arbitrary data to datetime representation."""
#     # 2-step conversion: X -> decimal, decimal -> datetime
#     series = cast(series, "decimal", errors="raise")
#     return cast(series, dtype, **unused)


# @cast.overload(wildcard, "timedelta")
# def generic_to_timedelta(
#     series: wrapper.SeriesWrapper,
#     dtype: types.AtomicType,
#     **unused
# ) -> pd.Series:
#     """Convert arbitrary data to timedelta representation."""
#     # 2-step conversion: X -> decimal, decimal -> timedelta
#     series = cast(series, "decimal", errors="raise")
#     return cast(series, dtype=dtype, **unused)


@cast.overload(wildcard, "string")
def generic_to_string(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    format: str,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    if format:
        series = series.apply_with_errors(
            lambda x: f"{x:{format}}",
            errors=errors,
            element_type=str
        )
    return series.astype(dtype, errors=errors)


@cast.overload(wildcard, "object")
def generic_to_object(
    series: wrapper.SeriesWrapper,
    dtype: types.AtomicType,
    call: Callable,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    direct = call is None
    if direct:
        call = dtype.type_def

    # object root type
    if call is object:
        return series.astype("O")

    def wrapped_call(val):
        result = call(val)
        if direct:
            return result
        output_type = type(result)
        if output_type != dtype.type_def:
            raise ValueError(
                f"`call` must return an object of type {dtype.type_def}"
            )
        return result

    return series.apply_with_errors(
        call=wrapped_call,
        errors=errors,
        element_type=dtype
    )
