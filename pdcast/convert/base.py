"""This module defines the ``cast()`` function, as well as several stand-alone
equivalents that allow quick conversion to predefined data types.
"""
# pylint: disable=unused-argument
from __future__ import annotations
from typing import Any, Callable, Optional

import numpy as np
import pandas as pd

from pdcast import types
from pdcast.decorators.attachable import attachable
from pdcast.decorators.base import BaseDecorator
from pdcast.decorators.dispatch import dispatch
from pdcast.decorators.extension import extension_func
from pdcast.decorators.wrapper import SeriesWrapper
from pdcast.patch.round import round as round_generic
from pdcast.util.error import shorten_list
from pdcast.util.round import Tolerance
from pdcast.util.type_hints import type_specifier

from . import arguments


# ignore this file when doing string-based object lookups in resolve_type()
IGNORE_FRAME_OBJECTS = True


class columnwise(BaseDecorator):
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
    # pylint: disable=invalid-name

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
@dispatch(depth=2, cache_size=128, wrap_adapters=False)
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
    This function uses `multiple dispatch <https://en.wikipedia.org/wiki/Multiple_dispatch>`
    
    This function dispatches to one of the
    :ref:`delegated <atomic_type.conversions>` conversion methods that are
    attached to each :class:`AtomicType`.  Types can override these methods to
    change the behavior of :func:`cast`.  The method that is chosen is based on
    the :attr:`family <AtomicType.family>` of its ``dtype`` argument.
    """
    raise NotImplementedError(
        f"no conversion found between {str(series.element_type)} and "
        f"{str(dtype)}"
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
# They are used if no specific implementation is explicitly given for the
# observed data


wildcard = (
    "bool, int, float, complex, decimal, datetime, timedelta, string, object"
)


@cast.overload(wildcard, "bool")
def generic_to_boolean(
    series: SeriesWrapper,
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
    series: SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    errors: str,
    downcast: types.CompositeType,
    **unused
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    if series.hasnans:
        dtype = dtype.make_nullable()

    series = series.astype(dtype, errors=errors)
    if downcast is not None:
        return downcast_integer(series, tol=tol, smallest=downcast)
    return series


@cast.overload(wildcard, "float")
def generic_to_float(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    series = series.astype(dtype, errors=errors)
    if downcast is not None:
        return downcast_float(series, tol=tol, smallest=downcast)
    return series


@cast.overload(wildcard, "complex")
def generic_to_complex(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to complex representation."""
    series = series.astype(dtype, errors=errors)
    if downcast is not None:
        return downcast_complex(series, tol=tol, smallest=downcast)
    return series


@cast.overload(wildcard, "decimal")
def generic_to_decimal(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    return series.astype(dtype, errors=errors)


@cast.overload(wildcard, "datetime")
def generic_to_datetime(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    **unused
) -> pd.Series:
    """Convert arbitrary data to datetime representation."""
    # 2-step conversion: X -> decimal, decimal -> datetime
    series = cast(series, "decimal", errors="raise")
    return cast(series, dtype, **unused)


@cast.overload(wildcard, "timedelta")
def generic_to_timedelta(
    series: SeriesWrapper,
    dtype: types.AtomicType,
    **unused
) -> pd.Series:
    """Convert arbitrary data to timedelta representation."""
    # 2-step conversion: X -> decimal, decimal -> timedelta
    series = cast(series, "decimal", errors="raise")
    return cast(series, dtype=dtype, **unused)


@cast.overload(wildcard, "string")
def generic_to_string(
    series: SeriesWrapper,
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
    series: SeriesWrapper,
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


#######################
####    PRIVATE    ####
#######################


def downcast_integer(
    series: SeriesWrapper,
    tol: Tolerance,
    smallest: types.CompositeType = None
) -> SeriesWrapper:
    """Reduce the itemsize of an integer type to fit the observed range."""
    series_type = series.element_type

    # get downcast candidates
    smaller = series_type.smaller
    if smallest is not None:
        if series_type in smallest:
            smaller = []
        else:
            filtered = []
            for typ in reversed(smaller):
                filtered.append(typ)
                if typ in smallest:
                    break
            smaller = reversed(filtered)

    # convert range to python int for consistent comparison
    if series_type.is_na(series.min):
        min_val = series_type.max  # NOTE: we swap these to maintain upcast()
        max_val = series_type.min  # behavior for upcast-only types
    else:
        min_val = int(series.min)
        max_val = int(series.max)

    # search for smaller data type that fits observed range
    for small in smaller:
        if min_val < small.min or max_val > small.max:
            continue
        return cast(
            series,
            dtype=small,
            downcast=None,
            errors="raise"
        )

    return series


def downcast_float(
    series: SeriesWrapper,
    tol: Tolerance,
    smallest: types.CompositeType
) -> SeriesWrapper:
    """Reduce the itemsize of a float type to fit the observed range."""
    # get downcast candidates
    smaller = series.element_type.smaller
    if smallest is not None:
        filtered = []
        for typ in reversed(smaller):
            filtered.append(typ)
            if typ in smallest:
                break  # stop at largest type contained in `smallest`
        smaller = reversed(filtered)

    # try each candidate in order
    for small in smaller:
        try:
            attempt = cast(
                series,
                dtype=small,
                tol=tol,
                downcast=None,
                errors="raise"
            )
        except Exception:
            continue

        # candidate is valid
        if attempt.within_tol(series, tol=tol.real).all():
            return attempt

    # return original
    return series


def downcast_complex(
    series: SeriesWrapper,
    tol: Tolerance,
    smallest: types.CompositeType
) -> SeriesWrapper:
    """Reduce the itemsize of a complex type to fit the observed range."""
    # downcast real and imaginary component separately
    real = downcast_float(
        series.real,
        tol=tol,
        smallest=smallest
    )
    imag = downcast_float(
        series.imag,
        tol=Tolerance(tol.imag),
        smallest=smallest
    )

    # use whichever type is larger
    largest = max(
        [real.element_type, imag.element_type],
        key=lambda x: x.itemsize or np.inf
    )
    target = largest.equiv_complex
    result = real + imag * 1j
    return SeriesWrapper(
        result.series.astype(target.dtype, copy=False),
        hasnans=real.hasnans or imag.hasnans,
        element_type=target
    )


def snap_round(
    series: SeriesWrapper,
    tol: Tolerance,
    rule: str | None,
    errors: str
) -> SeriesWrapper:
    """Snap a series to the nearest integer within `tol`, and then round
    any remaining results according to the given rule.  Reject any outputs
    that are not integer-like by the end of this process.
    """
    # if rounding to nearest, don't bother applying tolerance
    nearest = (
        "half_floor", "half_ceiling", "half_down", "half_up", "half_even"
    )
    if rule in nearest:
        return round_generic(series, rule=rule)

    # apply tolerance & check for non-integer results
    if tol or rule is None:
        rounded = round_generic(series, rule="half_even")
        outside = ~series.within_tol(rounded, tol=tol)
        if tol:
            element_type = series.element_type
            series = series.where(outside.series, rounded.series)
            series.element_type = element_type

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
