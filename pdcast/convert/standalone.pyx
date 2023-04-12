
from datetime import tzinfo
import decimal
import threading
from typing import Any, Callable, Iterable, Optional

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd
import pytz
import tzlocal

cimport pdcast.resolve as resolve
import pdcast.resolve as resolve
cimport pdcast.detect as detect
import pdcast.detect as detect
import pdcast.patch as patch
cimport pdcast.types as types
import pdcast.types as types

import pdcast.convert.wrapper as wrapper
import pdcast.convert.arguments as arguments

from pdcast.util.round cimport Tolerance
from pdcast.util.round import valid_rules
from pdcast.util.structs import as_series
from pdcast.util.time cimport Epoch, epoch_aliases, valid_units
from pdcast.util.time import timezone
from pdcast.util.type_hints import datetime_like, numeric, type_specifier


######################
####    PUBLIC    ####
######################


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
    attached to each :class:`AtomicType` definition.  Types can override these
    methods to change the behavior of :func:`cast`.  The method that is chosen
    is based on the :attr:`family <AtomicType.family>` of its ``dtype``
    argument.

    Examples
    --------
    This function can be used as an alternative to ``astype()``.
    """
    series = as_series(data)
    kwargs = arguments.CastDefaults(**kwargs)

    # if no target is given, default to series type
    if dtype is None:
        dtype = detect.detect_type(series)
        if dtype is None:
            raise ValueError(
                f"cannot interpret empty series without an explicit `dtype` "
                f"argument: {dtype}"
            )
    else:
        dtype = validate_dtype(dtype)
        if dtype.unwrap() is None:
            dtype.atomic_type = detect.detect_type(series)

    # delegate to AtomicType conversion method
    return do_conversion(
        series,
        f"to_{dtype.family}",
        dtype=dtype,
        **kwargs.default_values
    )


##########################
####    STANDALONE    ####
##########################


def to_boolean(
    data: Any,
    dtype: type_specifier = "bool",
    **kwargs
) -> pd.Series:
    """Convert a to boolean representation."""
    # ensure dtype is bool-like
    dtype = validate_dtype(dtype, types.BooleanType)

    # pass to cast()
    return cast(data, dtype=dtype, **kwargs)


def to_integer(
    data: Any,
    dtype: type_specifier = "int",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    # ensure dtype is int-like
    dtype = validate_dtype(dtype, types.IntegerType)

    # pass to cast()
    return cast(data, dtype=dtype, **kwargs)


def to_float(
    data: Any,
    dtype: type_specifier = "float",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    # ensure dtype is float-like
    dtype = validate_dtype(dtype, types.FloatType)

    # pass to cast()
    return cast(data, dtype=dtype, **kwargs)


def to_complex(
    data: Any,
    dtype: type_specifier = "complex",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to complex representation."""
    # ensure dtype is complex-like
    dtype = validate_dtype(dtype, types.ComplexType)

    # pass to cast()
    return cast(data, dtype=dtype, **kwargs)


def to_decimal(
    data: Any,
    dtype: type_specifier = "decimal",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    # ensure dtype is decimal-like
    dtype = validate_dtype(dtype, types.DecimalType)

    # pass to cast()
    return cast(data, dtype=dtype, **kwargs)


def to_datetime(
    data: Any,
    dtype: type_specifier = "datetime",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to datetime representation."""
    # ensure dtype is datetime-like
    dtype = validate_dtype(dtype, types.DatetimeType)

    # pass to cast()
    return cast(data, dtype=dtype, **kwargs)


def to_timedelta(
    data: Any,
    dtype: type_specifier = "timedelta",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to timedelta representation."""
    # ensure dtype is timedelta-like
    dtype = validate_dtype(dtype, types.TimedeltaType)

    # pass to cast()
    return cast(data, dtype=dtype, **kwargs)


def to_string(
    data: Any,
    dtype: type_specifier = "string",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # ensure dtype is string-like
    dtype = validate_dtype(dtype, types.StringType)

    # pass to cast()
    return cast(data, dtype=dtype, **kwargs)


def to_object(
    data: Any,
    dtype: type_specifier = "object",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # ensure dtype is object-like
    dtype = validate_dtype(dtype, types.ObjectType)

    # pass to cast()
    return cast(data, dtype=dtype, **kwargs)


#######################
####    PRIVATE    ####
#######################

def do_conversion(
    data,
    endpoint: str,
    dtype: types.ScalarType,
    errors: str,
    *args,
    **kwargs
) -> pd.Series:
    # for every registered type, get selected conversion method if it exists.
    submap = {
        k: getattr(k, endpoint) for k in types.AtomicType.registry
        if hasattr(k, endpoint)
    }
    submap[type(None)] = lambda _, series, *args, **kwargs: (
        getattr(dtype, endpoint)(series, *args, **kwargs)
    )

    # convert to series
    data = as_series(data)

    # parse naked adapters ("sparse"/"categorical" without a wrapped type)
    if dtype.unwrap() is None:
        dtype.atomic_type = detect.detect_type(data)

    # create manual dispatch method
    dispatch = patch.DispatchMethod(
        data,
        name="",  # passing empty string causes us to never fall back to pandas
        submap=submap,
        namespace=None,
        wrap_adapters=False  # do not automatically reapply adapters
    )

    # dispatch to conversion method(s)
    try:
        base_type = dtype.unwrap()
        result = dispatch(
            *args,
            dtype=base_type,  # disregard adapters in ``dtype``
            errors=errors,
            **kwargs
        )

        # apply adapters from ``dtype``.  NOTE: this works from the inside out
        for adapter in reversed(list(dtype.adapters)):
            result = adapter.transform(wrapper.SeriesWrapper(result)).series

        return result

    # parse errors
    except (KeyboardInterrupt, MemoryError, SystemError, SystemExit):
        raise  # never ignore these errors
    except Exception as err:
        if errors == "ignore":
            return data
        raise err


def validate_dtype(
    dtype: type_specifier,
    supertype: type_specifier = None
) -> types.ScalarType:
    """Resolve a type specifier and reject it if it is composite or not a
    subtype of the given supertype.
    """
    dtype = resolve.resolve_type(dtype)

    # reject composite
    if isinstance(dtype, types.CompositeType):
        raise ValueError(
            f"`dtype` cannot be composite (received: {dtype})"
        )

    # reject improper subtype
    if supertype is not None and dtype.unwrap() is not None:
        supertype = resolve.resolve_type(supertype)
        if not dtype.unwrap().is_subtype(supertype):
            raise ValueError(
                f"`dtype` must be {supertype}-like, not {dtype}"
            )

    return dtype
