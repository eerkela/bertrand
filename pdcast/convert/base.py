"""This module defines the ``cast()`` function, as well as several stand-alone
equivalents that allow quick conversion to predefined data types.
"""
# pylint: disable=unused-argument
from __future__ import annotations
from typing import Any, Callable

import pandas as pd

from pdcast import types
from pdcast.decorators.attachable import attachable
from pdcast.decorators.base import FunctionDecorator
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.detect import detect_type
from pdcast.util.error import shorten_list
from pdcast.util.numeric import (
    downcast_integer, downcast_float, downcast_complex, within_tol
)
from pdcast.util.round import Tolerance
from pdcast.util.type_hints import type_specifier
from pdcast.util.vector import apply_with_errors

from . import arguments


# conversions
# +===========+===+===+===+===+===+===+===+===+===+
# |           | b | i | f | c | d | d | t | s | o |
# +===========+===+===+===+===+===+===+===+===+===+
# | bool      | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | int       | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | float     | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | complex   | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | decimal   | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | datetime  | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | timedelta | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | string    | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+
# | object    | x | x | x | x | x | x | x | x | x |
# +-----------+---+---+---+---+---+---+---+---+---+


# ignore this file when doing string-based object lookups in resolve_type()
IGNORE_FRAME_OBJECTS = True


class columnwise(FunctionDecorator):
    """A basic decorator that breaks up DataFrame inputs into individual
    columns before continuing with a conversion.

    Placing this above ``@extension_func`` allows validators to avoid
    implementing this themselves, and above ``@dispatch`` allows dispatched
    implementations to always work in one dimension.
    """
    # pylint: disable=invalid-name

    def __call__(
        self,
        data: Any,
        dtype: type_specifier | dict[str, type_specifier] = NotImplemented,
        **kwargs
    ):
        """Apply the wrapped function for each column independently."""
        # use default dtype if not given
        if dtype is NotImplemented:
            dtype = self.__wrapped__.dtype  # NOTE: from @extension_func

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
                    **kwargs
                )
            return result

        # base case
        return self.__wrapped__(data, dtype, **kwargs)


class catch_errors(FunctionDecorator):
    """A basic decorator that enforces the ``errors="ignore"`` rule during
    conversions.

    Placing this above ``@dispatch`` allows dispatched implementations to
    avoid implementing this themselves.
    """
    # pylint: disable=invalid-name

    def __call__(
        self,
        data: Any,
        dtype: types.VectorType,
        *args,
        errors: str = NotImplemented,
        **kwargs
    ):
        """Call the wrapped function in a try/except block."""
        if errors is NotImplemented:
            errors = self.__wrapped__.errors  # NOTE: from @extension_func

        try:
            return self.__wrapped__(
                data,
                dtype,
                *args,
                errors=errors,
                **kwargs
            )

        # never ignore these errors
        except (KeyboardInterrupt, MemoryError, SystemError, SystemExit):
            raise

        # process according to `errors` arg
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
@catch_errors
@dispatch("series", "dtype", cache_size=128, convert_mixed=True)
def cast(
    series: Any,
    dtype: type_specifier | None = None,
    **unused
) -> pd.Series | pd.DataFrame:
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
    This function uses `multiple dispatch <https://en.wikipedia.org/wiki/Multiple_dispatch>`
    
    This function dispatches to one of the
    :ref:`delegated <atomic_type.conversions>` conversion methods that are
    attached to each :class:`ScalarType`.  Types can override these methods to
    change the behavior of :func:`cast`.  The method that is chosen is based on
    the :attr:`family <ScalarType.family>` of its ``dtype`` argument.
    """
    series_type = detect_type(series)

    # recursively unwrap decorators and retry
    # NOTE: These operate like recursive stacks.  If decorators are detected,
    # they are popped off the stack in FIFO order before recurring.  We do this
    # first to the data, and then to the target data type, allowing conversions
    # to safely ignore decorators in all their implementations.

    for before in getattr(series_type, "decorators", ()):
        series = before.inverse_transform(series)
        return cast(series, dtype, **kwargs)

    for before in getattr(dtype, "decorators", ()):
        series = cast(series, dtype.wrapped, **kwargs)
        return before.transform(series)

    raise NotImplementedError(
        f"no conversion found between {str(series_type)} and {str(dtype)}"
    )


def to_boolean(
    data: Any,
    dtype: type_specifier = "bool",
    **kwargs
) -> pd.Series:
    """Stand-alone conversion to boolean data types."""
    dtype = arguments.dtype(dtype, {}, supertype="bool")
    return cast(data, dtype, **kwargs)


def to_integer(
    data: Any,
    dtype: type_specifier = "int",
    **kwargs
) -> pd.Series:
    """Stand-alone conversion to integer data types."""
    dtype = arguments.dtype(dtype, {}, supertype="int")
    return cast(data, dtype, **kwargs)


def to_float(
    data: Any,
    dtype: type_specifier = "float",
    **kwargs
) -> pd.Series:
    """Stand-alone conversion to floating point data types."""
    dtype = arguments.dtype(dtype, {}, supertype="float")
    return cast(data, dtype, **kwargs)


def to_complex(
    data: Any,
    dtype: type_specifier = "complex",
    **kwargs
) -> pd.Series:
    """Stand-alone conversion to complex data types."""
    dtype = arguments.dtype(dtype, {}, supertype="complex")
    return cast(data, dtype, **kwargs)


def to_decimal(
    data: Any,
    dtype: type_specifier = "decimal",
    **kwargs
) -> pd.Series:
    """Stand-alone conversion to decimal data types."""
    dtype = arguments.dtype(dtype, {}, supertype="decimal")
    return cast(data, dtype, **kwargs)


def to_datetime(
    data: Any,
    dtype: type_specifier = "datetime",
    **kwargs
) -> pd.Series:
    """Stand-alone conversion to datetime data types."""
    dtype = arguments.dtype(dtype, {}, supertype="datetime")
    return cast(data, dtype, **kwargs)


def to_timedelta(
    data: Any,
    dtype: type_specifier = "timedelta",
    **kwargs
) -> pd.Series:
    """Stand-alone conversion to timedelta data types."""
    dtype = arguments.dtype(dtype, {}, supertype="timedelta")
    return cast(data, dtype, **kwargs)


def to_string(
    data: Any,
    dtype: type_specifier = "string",
    **kwargs
) -> pd.Series:
    """Stand-alone conversion to string data types."""
    dtype = arguments.dtype(dtype, {}, supertype="string")
    return cast(data, dtype, **kwargs)


#######################
####    GENERIC    ####
#######################


# NOTE: the following are base conversions for each of the major data types.
# They are used if no specific implementation is given for the observed data.


wildcard = types.registry.roots


@cast.overload(wildcard, "bool")
def generic_to_boolean(
    series: pd.Series,
    dtype: types.ScalarType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert a to boolean representation."""
    # trivial case
    if detect_type(series) == dtype:
        return series

    target = dtype.dtype

    # NOTE: pandas complains about converting arbitrary objects to nullable
    # boolean.  Luckily, NAs are dropped, so we can do a two-step conversion.

    if isinstance(target, types.ObjectDtype):
        series = apply_with_errors(series, dtype.type_def, errors=errors)
    elif target == pd.BooleanDtype():
        series = series.astype(target.numpy_dtype, copy=False)
    return series.astype(target)


@cast.overload(wildcard, "int")
def generic_to_integer(
    series: pd.Series,
    dtype: types.ScalarType,
    tol: Tolerance,
    errors: str,
    downcast: types.CompositeType,
    **unused
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    if detect_type(series) != dtype:  # ignore trivial
        target = dtype.dtype

        # NOTE: pandas complains about converting arbitrary objects to nullable
        # int. Luckily, NAs are dropped, so we can do a two-step conversion.

        if isinstance(target, types.ObjectDtype):
            series = apply_with_errors(series, dtype.type_def, errors=errors)
        elif target in pandas_integer_dtypes:
            series = series.astype(target.numpy_dtype, copy=False)
        series = series.astype(target)

    # downcast if applicable
    if downcast is not None:
        return downcast_integer(series, tol=tol, smallest=downcast)
    return series


@cast.overload(wildcard, "float")
def generic_to_float(
    series: pd.Series,
    dtype: types.ScalarType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    if detect_type(series) != dtype:  # ignore trivial
        target = dtype.dtype
        if isinstance(target, types.ObjectDtype):
            series = apply_with_errors(series, dtype.type_def, errors=errors)
        series = series.astype(target)

    if downcast is not None:
        return downcast_float(series, tol=tol, smallest=downcast)
    return series


@cast.overload(wildcard, "complex")
def generic_to_complex(
    series: pd.Series,
    dtype: types.ScalarType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to complex representation."""
    if detect_type(series) != dtype:  # ignore trivial
        target = dtype.dtype
        if isinstance(target, types.ObjectDtype):
            series = apply_with_errors(series, dtype.type_def, errors=errors)
        series = series.astype(target)

    if downcast is not None:
        return downcast_complex(series, tol=tol, smallest=downcast)
    return series


@cast.overload(wildcard, "decimal")
def generic_to_decimal(
    series: pd.Series,
    dtype: types.ScalarType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    # trivial case
    if detect_type(series) == dtype:
        return series

    target = dtype.dtype
    if isinstance(target, types.ObjectDtype):
        series = apply_with_errors(series, dtype.type_def, errors=errors)
    return series.astype(target)


@cast.overload(wildcard, "datetime")
def generic_to_datetime(
    series: pd.Series,
    dtype: types.ScalarType,
    **unused
) -> pd.Series:
    """Convert arbitrary data to datetime representation."""
    # 2-step conversion: X -> decimal, decimal -> datetime
    series = cast(series, "decimal", errors="raise")
    return cast(series, dtype, **unused)


@cast.overload(wildcard, "timedelta")
def generic_to_timedelta(
    series: pd.Series,
    dtype: types.ScalarType,
    **unused
) -> pd.Series:
    """Convert arbitrary data to timedelta representation."""
    # 2-step conversion: X -> decimal, decimal -> timedelta
    series = cast(series, "decimal", errors="raise")
    return cast(series, dtype=dtype, **unused)


@cast.overload(wildcard, "string")
def generic_to_string(
    series: pd.Series,
    dtype: types.ScalarType,
    format: str,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    if detect_type(series) == dtype:
        return series

    if format:
        call = lambda x: f"{x:{format}}"
        series = apply_with_errors(series, call, errors=errors)

    target = dtype.dtype
    if isinstance(target, types.ObjectDtype):
        series = apply_with_errors(series, dtype.type_def, errors=errors)
    return series.astype(target, copy=False)


@cast.overload(wildcard, "object")
def generic_to_object(
    series: pd.Series,
    dtype: types.ScalarType,
    call: Callable,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # trivial case
    if detect_type(series) == dtype:
        return series

    if call is None:
        call = dtype.type_def

    # trivial case: object root type
    if call is object:
        return series.astype(object, copy=False)

    return safe_apply(series=series, dtype=dtype, call=call, errors=errors)


#######################
####    PRIVATE    ####
#######################


pandas_integer_dtypes = {
    pd.Int8Dtype(), pd.Int16Dtype(), pd.Int32Dtype(), pd.Int64Dtype(),
    pd.UInt8Dtype(), pd.UInt16Dtype(), pd.UInt32Dtype(), pd.UInt64Dtype()
}


def safe_apply(
    series: pd.Series,
    dtype: types.ScalarType,
    call: Callable,
    errors: str
) -> pd.Series:
    """Apply a callable over the input series that produces objects of the
    appropriate type.

    This is only used for conversions to or from
    :class:`ObjectTypes <pdcast.ObjectType>`.
    """
    def safe_call(val):
        result = call(val)
        output_type = type(result)
        if output_type != dtype.type_def:
            raise TypeError(
                f"`call` must return an object of type {dtype.type_def}"
            )
        return result

    # apply `safe_call` over series and pass to delegated conversion
    series = apply_with_errors(series, safe_call, errors=errors)
    return series.astype(dtype.dtype)


def snap_round(
    series: pd.Series,
    tol: Tolerance,
    rule: str | None,
    errors: str
) -> pd.Series:
    """Snap a series to the nearest integer within `tol`, and then round
    any remaining results according to the given rule.  Reject any outputs
    that are not integer-like by the end of this process.
    """
    from pdcast.patch.round import round as round_generic

    # if rounding to nearest, don't bother applying tolerance
    nearest = (
        "half_floor", "half_ceiling", "half_down", "half_up", "half_even"
    )
    if rule in nearest:
        return round_generic(series, rule=rule)

    # apply tolerance & check for non-integer results
    if tol or rule is None:
        rounded = round_generic(series, rule="half_even")
        outside = ~within_tol(series, rounded, tol=tol)
        if tol:
            series = series.where(outside, rounded)

        # check for non-integer (ignore if rounding in next step)
        if rule is None and outside.any():
            if errors == "coerce":
                series = round_generic(series, "down")  # emulate int()
            else:
                raise ValueError(
                    f"precision loss exceeds tolerance {float(tol):g} at "
                    f"index {shorten_list(outside[outside].index.values)}"
                )

    # apply final rounding
    if rule:
        series = round_generic(series, rule=rule)

    return series
