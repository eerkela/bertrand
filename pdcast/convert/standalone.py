"""This module defines the ``cast()`` function, as well as several stand-alone
equivalents that allow quick conversion to predefined data types.
"""
from __future__ import annotations
from typing import Any, Optional

import pandas as pd

import pdcast.resolve as resolve
import pdcast.detect as detect
from pdcast.decorators.attachable import attachable
from pdcast.decorators.base import BaseDecorator
from pdcast.decorators.extension import extension_func
from pdcast.decorators.dispatch import dispatch
import pdcast._patch as _patch
import pdcast.types as types

import pdcast.util.wrapper as wrapper

from pdcast.util.type_hints import type_specifier


# TODO: generic implementations just call series.element_type.to_x



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
def cast(
    data: Any,
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
    return dtype.conversion_func(data, dtype, **kwargs)


##########################
####    STANDALONE    ####
##########################


@columnwise
@extension_func
@catch_ignore
@dispatch
def to_boolean(
    data: wrapper.SeriesWrapper,
    dtype: type_specifier = "bool",
    **kwargs
) -> pd.Series:
    """Convert a to boolean representation."""
    return data.element_type.to_boolean(data, dtype, **kwargs)


@columnwise
@extension_func
@catch_ignore
@dispatch
def to_integer(
    data: Any,
    dtype: type_specifier = "int",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    return data.element_type.to_integer(data, dtype, **kwargs)


@columnwise
@extension_func
@catch_ignore
@dispatch
def to_float(
    data: Any,
    dtype: type_specifier = "float",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    return data.element_type.to_float(data, dtype, **kwargs)


@columnwise
@extension_func
@catch_ignore
@dispatch
def to_complex(
    data: Any,
    dtype: type_specifier = "complex",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to complex representation."""
    return data.element_type.to_complex(data, dtype, **kwargs)


@columnwise
@extension_func
@catch_ignore
@dispatch
def to_decimal(
    data: Any,
    dtype: type_specifier = "decimal",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    return data.element_type.to_decimal(data, dtype, **kwargs)


@columnwise
@extension_func
@catch_ignore
@dispatch
def to_datetime(
    data: Any,
    dtype: type_specifier = "datetime",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to datetime representation."""
    return data.element_type.to_datetime(data, dtype, **kwargs)


@columnwise
@extension_func
@catch_ignore
@dispatch
def to_timedelta(
    data: Any,
    dtype: type_specifier = "timedelta",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to timedelta representation."""
    return data.element_type.to_timedelta(data, dtype, **kwargs)


@columnwise
@extension_func
@catch_ignore
@dispatch
def to_string(
    data: Any,
    dtype: type_specifier = "string",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    return data.element_type.to_string(data, dtype, **kwargs)


@columnwise
@extension_func
@catch_ignore
@dispatch
def to_object(
    data: Any,
    dtype: type_specifier = "object",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    return data.element_type.to_object(data, dtype, **kwargs)
