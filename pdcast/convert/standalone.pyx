
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


def cast(
    series: Any,
    dtype: Optional[type_specifier] = None,
    **kwargs
) -> pd.Series:
    """Cast arbitrary data to the specified data type.

    Parameters
    ----------
    series : Any
        The data to convert.  This can be a scalar or 1D iterable containing
        arbitrary data.
    dtype : type specifier
        The target :doc:`type </content/types/types>` for this conversion.
        This can be in any format recognized by :func:`resolve_type`.
    **kwargs : dict
        Arbitrary keyword :ref:`arguments <cast.arguments>` used to customize
        the conversion.

    Notes
    -----
    This function dispatches to one of the
    :ref:`standalone conversions <cast.stand_alone>` listed below based on the
    value of its ``dtype`` argument.
    """
    # if no target is given, default to series type
    if dtype is None:
        series = as_series(series)
        series_type = detect.detect_type(series)
        if series_type is None:
            raise ValueError(
                f"cannot interpret empty series without an explicit `dtype` "
                f"argument: {dtype}"
            )
        return series_type._conversion_func(series, **kwargs)  # use default

    # delegate to appropriate to_x function below
    dtype = validate_dtype(dtype)
    if dtype.unwrap() is None:
        dtype.atomic_type = detect.detect_type(series)
    return dtype._conversion_func(series, dtype, **kwargs)


def to_boolean(
    series: Any,
    dtype: type_specifier = "bool",
    **kwargs
) -> pd.Series:
    """Convert a to boolean representation."""
    # ensure dtype is bool-like
    dtype = validate_dtype(dtype, types.BooleanType)
    kwargs = arguments.CastDefaults(**kwargs)

    # delegate to SeriesWrapper.to_boolean
    return do_conversion(
        series,
        "to_boolean",
        dtype=dtype,
        **kwargs.default_values
    )


def to_integer(
    series: Any,
    dtype: type_specifier = "int",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    # ensure dtype is int-like
    dtype = validate_dtype(dtype, types.IntegerType)
    kwargs = arguments.CastDefaults(**kwargs)

    # delegate to SeriesWrapper.to_integer
    return do_conversion(
        series,
        "to_integer",
        dtype=dtype,
        **kwargs.default_values
    )


def to_float(
    series: Any,
    dtype: type_specifier = "float",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    # ensure dtype is float-like
    dtype = validate_dtype(dtype, types.FloatType)
    kwargs = arguments.CastDefaults(**kwargs)

    # delegate to SeriesWrapper.to_float
    return do_conversion(
        series,
        "to_float",
        dtype=dtype,
        **kwargs.default_values
    )


def to_complex(
    series: Any,
    dtype: type_specifier = "complex",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to complex representation."""
    # ensure dtype is complex-like
    dtype = validate_dtype(dtype, types.ComplexType)
    kwargs = arguments.CastDefaults(**kwargs)

    # delegate to SeriesWrapper.to_complex
    return do_conversion(
        series,
        "to_complex",
        dtype=dtype,
        **kwargs.default_values
    )


def to_decimal(
    series: Any,
    dtype: type_specifier = "decimal",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    # ensure dtype is decimal-like
    dtype = validate_dtype(dtype, types.DecimalType)
    kwargs = arguments.CastDefaults(**kwargs)

    # delegate to SeriesWrapper.to_decimal
    return do_conversion(
        series,
        "to_decimal",
        dtype=dtype,
        **kwargs.default_values
    )


def to_datetime(
    series: Any,
    dtype: type_specifier = "datetime",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to datetime representation."""
    # ensure dtype is datetime-like
    dtype = validate_dtype(dtype, types.DatetimeType)
    kwargs = arguments.CastDefaults(**kwargs)

    # delegate to SeriesWrapper.to_datetime
    return do_conversion(
        series,
        "to_datetime",
        dtype=dtype,
        **kwargs.default_values
    )


def to_timedelta(
    series: Any,
    dtype: type_specifier = "timedelta",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to timedelta representation."""
    # ensure dtype is timedelta-like
    dtype = validate_dtype(dtype, types.TimedeltaType)
    kwargs = arguments.CastDefaults(**kwargs)

    # delegate to SeriesWrapper.to_timedelta
    return do_conversion(
        series,
        "to_timedelta",
        dtype=dtype,
        **kwargs.default_values
    )


def to_string(
    series: Any,
    dtype: type_specifier = "string",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # ensure dtype is string-like
    dtype = validate_dtype(dtype, types.StringType)
    kwargs = arguments.CastDefaults(**kwargs)

    # delegate to SeriesWrapper.to_string
    return do_conversion(
        series,
        "to_string",
        dtype=dtype,
        **kwargs.default_values
    )


def to_object(
    series: Any,
    dtype: type_specifier = "object",
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # ensure dtype is object-like
    dtype = validate_dtype(dtype, types.ObjectType)
    kwargs = arguments.CastDefaults(**kwargs)

    # delegate to SeriesWrapper.to_object
    return do_conversion(
        series,
        "to_object",
        dtype=dtype,
        **kwargs.default_values
    )


########################
####    DEFAULTS    ####
########################

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
