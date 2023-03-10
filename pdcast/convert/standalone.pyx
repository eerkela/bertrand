
from datetime import tzinfo
import decimal
from functools import wraps
import inspect
from typing import Any, Callable, Iterable, Iterator

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd

cimport pdcast.detect as detect
import pdcast.detect as detect
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve
cimport pdcast.types as types
import pdcast.types as types

import pdcast.convert.default as default
import pdcast.convert.wrapper as wrapper

from pdcast.util.error import shorten_list
from pdcast.util.structs import as_series
from pdcast.util.time cimport Epoch, epoch_aliases
from pdcast.util.type_hints import (
    array_like, datetime_like, numeric, type_specifier
)


def cast(
    series: Iterable,
    dtype: type_specifier = None,
    **kwargs
) -> pd.Series:
    """Convert data to an arbitrary data type.

    This function dispatches to one of the standalone conversion functions
    listed below based on the value of its ``dtype`` argument.

    Parameters
    ----------
    

    """
    # if no target is given, default to series type
    if dtype is None:
        series = as_series(series)
        dtype = detect.detect_type(series)

    # validate dtype
    dtype = default.validate_dtype(dtype)

    # delegate to appropriate to_x function below
    return dtype.conversion_func(series, dtype, **kwargs)


def to_boolean(
    series: Iterable,
    dtype: type_specifier = bool,
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    since: str | datetime_like = None,
    true: str | Iterable[str] = None,
    false: str | Iterable[str] = None,
    ignore_case: bool = None,
    call: Callable = None,
    sparse: Any = None,
    categorical: bool = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert a to boolean representation."""
    # validate args
    dtype = default.validate_dtype(dtype, types.BooleanType)
    tol = default.validate_tol(tol)
    rounding = default.validate_rounding(rounding)
    unit = default.validate_unit(unit)
    step_size = default.validate_step_size(step_size)
    since = default.validate_since(since)
    true = default.validate_true(true)
    false = default.validate_false(false)
    ignore_case = default.validate_ignore_case(ignore_case)
    call = default.validate_call(call)
    errors = default.validate_errors(errors)

    # ensure true, false are disjoint
    if not true.isdisjoint(false):
        intersection = true.intersection(false)
        err_msg = f"`true` and `false` must be disjoint "
        if len(intersection) == 1:
            err_msg += (
                f"({repr(intersection.pop())} is present in both sets)"
            )
        else:
            err_msg += f"({intersection} are present in both sets)"
        raise ValueError(err_msg)

    # apply ignore_case logic to true, false
    if ignore_case:
        true = {x.lower() for x in true}
        false = {x.lower() for x in false}

    # delegate to SeriesWrapper.to_boolean
    return do_conversion(
        series,
        "to_boolean",
        dtype=dtype,
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        since=since,
        true=true,
        false=false,
        ignore_case=ignore_case,
        call=call,
        sparse=sparse,
        categorical=categorical,
        errors=errors,
        **kwargs
    )


def to_integer(
    series: Iterable,
    dtype: type_specifier = int,
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    since: str | datetime_like = None,
    base: int = None,
    call: Callable = None,
    downcast: bool | type_specifier = None,
    sparse: Any = None,
    categorical: bool = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    # validate args
    dtype = default.validate_dtype(dtype, types.IntegerType)
    tol = default.validate_tol(tol)
    rounding = default.validate_rounding(rounding)
    unit = default.validate_unit(unit)
    step_size = default.validate_step_size(step_size)
    since = default.validate_since(since)
    base = default.validate_base(base)
    call = default.validate_call(call)
    downcast = default.validate_downcast(downcast)
    errors = default.validate_errors(errors)

    # delegate to SeriesWrapper.to_integer
    return do_conversion(
        series,
        "to_integer",
        dtype=dtype,
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        since=since,
        base=base,
        call=call,
        downcast=downcast,
        sparse=sparse,
        categorical=categorical,
        errors=errors,
        **kwargs
    )


def to_float(
    series: Iterable,
    dtype: type_specifier = float,
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    since: str | datetime_like = None,
    call: Callable = None,
    downcast: bool | type_specifier = None,
    sparse: Any = None,
    categorical: bool = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    # validate args
    dtype = default.validate_dtype(dtype, types.FloatType)
    tol = default.validate_tol(tol)
    rounding = default.validate_rounding(rounding)
    unit = default.validate_unit(unit)
    step_size = default.validate_step_size(step_size)
    since = default.validate_since(since)
    call = default.validate_call(call)
    downcast = default.validate_downcast(downcast)
    errors = default.validate_errors(errors)

    # delegate to SeriesWrapper.to_float
    return do_conversion(
        series,
        "to_float",
        dtype=dtype,
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        since=since,
        call=call,
        downcast=downcast,
        sparse=sparse,
        categorical=categorical,
        errors=errors,
        **kwargs
    )


def to_complex(
    series: Iterable,
    dtype: type_specifier = complex,
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    since: str | datetime_like = None,
    call: Callable = None,
    downcast: bool | type_specifier = None,
    sparse: Any = None,
    categorical: bool = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to complex representation."""
    # validate args
    dtype = default.validate_dtype(dtype, types.ComplexType)
    tol = default.validate_tol(tol)
    rounding = default.validate_rounding(rounding)
    unit = default.validate_unit(unit)
    step_size = default.validate_step_size(step_size)
    since = default.validate_since(since)
    call = default.validate_call(call)
    downcast = default.validate_downcast(downcast)
    errors = default.validate_errors(errors)

    # delegate to SeriesWrapper.to_complex
    return do_conversion(
        series,
        "to_complex",
        dtype=dtype,
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        since=since,
        downcast=downcast,
        sparse=sparse,
        categorical=categorical,
        errors=errors,
        **kwargs
    )


def to_decimal(
    series: Iterable,
    dtype: type_specifier = decimal.Decimal,
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    since: str | datetime_like = None,
    call: Callable = None,
    sparse: Any = None,
    categorical: bool = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    # validate args
    dtype = default.validate_dtype(dtype, types.DecimalType)
    tol = default.validate_tol(tol)
    rounding = default.validate_rounding(rounding)
    unit = default.validate_unit(unit)
    step_size = default.validate_step_size(step_size)
    since = default.validate_since(since)
    call = default.validate_call(call)
    errors = default.validate_errors(errors)

    # delegate to SeriesWrapper.to_decimal
    return do_conversion(
        series,
        "to_decimal",
        dtype=dtype,
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        since=since,
        call=call,
        sparse=sparse,
        categorical=categorical,
        errors=errors,
        **kwargs
    )


def to_datetime(
    series: Iterable,
    dtype: type_specifier = "datetime",
    unit: str = None,
    step_size: int = None,
    tol: numeric = None,
    rounding: str = None,
    since: str | datetime_like = None,
    tz: str | tzinfo = None,
    utc: bool = None,
    format: str = None,
    day_first: bool = None,
    year_first: bool = None,
    call: Callable = None,
    sparse: Any = None,
    categorical: bool = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to datetime representation."""
    # validate args
    dtype = default.validate_dtype(dtype, types.DatetimeType)
    unit = default.validate_unit(unit)
    step_size = default.validate_step_size(step_size)
    tol = default.validate_tol(tol)
    rounding = default.validate_rounding(rounding)
    since = default.validate_since(since)
    tz = default.validate_timezone(tz)
    utc = default.validate_utc(utc)
    format = default.validate_format(format)
    day_first = default.validate_day_first(day_first)
    year_first = default.validate_year_first(year_first)
    call = default.validate_call(call)
    errors = default.validate_errors(errors)

    # delegate to SeriesWrapper.to_datetime
    return do_conversion(
        series,
        "to_datetime",
        dtype=dtype,
        unit=unit,
        step_size=step_size,
        tol=tol,
        rounding=rounding,
        since=since,
        tz=tz,
        utc=utc,
        format=format,
        day_first=day_first,
        year_first=year_first,
        call=call,
        sparse=sparse,
        categorical=categorical,
        errors=errors,
        **kwargs
    )


def to_timedelta(
    series: Iterable,
    dtype: type_specifier = "timedelta",
    unit: str = None,
    step_size: int = None,
    tol: numeric = None,
    rounding: str = None,
    since: str | datetime_like = None,
    as_hours: bool = None,
    call: Callable = None,
    sparse: Any = None,
    categorical: bool = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to timedelta representation."""
    # validate args
    dtype = default.validate_dtype(dtype, types.TimedeltaType)
    unit = default.validate_unit(unit)
    step_size = default.validate_step_size(step_size)
    tol = default.validate_tol(tol)
    rounding = default.validate_rounding(rounding)
    since = default.validate_since(since)
    as_hours = default.validate_as_hours(as_hours)
    call = default.validate_call(call)
    errors = default.validate_errors(errors)

    # delegate to SeriesWrapper.to_timedelta
    return do_conversion(
        series,
        "to_timedelta",
        dtype=dtype,
        unit=unit,
        step_size=step_size,
        tol=tol,
        rounding=rounding,
        since=since,
        as_hours=as_hours,
        call=call,
        sparse=sparse,
        categorical=categorical,
        errors=errors,
        **kwargs
    )


def to_string(
    series: Iterable,
    dtype: type_specifier = str,
    format: str = None,
    base: int = None,
    call: Callable = None,
    sparse: Any = None,
    categorical: bool = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # validate args
    dtype = default.validate_dtype(dtype, types.StringType)
    base = default.validate_base(base)
    format = default.validate_format(format)
    call = default.validate_call(call)
    errors = default.validate_errors(errors)

    # delegate to SeriesWrapper.to_string
    return do_conversion(
        series,
        "to_string",
        dtype=dtype,
        base=base,
        format=format,
        call=call,
        sparse=sparse,
        categorical=categorical,
        errors=errors,
        **kwargs
    )


def to_object(
    series: Iterable,
    dtype: type_specifier = object,
    call: Callable = None,
    sparse: Any = None,
    categorical: bool = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # validate args
    dtype = default.validate_dtype(dtype, types.ObjectType)
    call = default.validate_call(call)
    errors = default.validate_errors(errors)

    # delegate to SeriesWrapper.to_object
    return do_conversion(
        series,
        "to_object",
        dtype=dtype,
        call=call,
        sparse=sparse,
        categorical=categorical,
        errors=errors,
        **kwargs
    )


#######################
####    PRIVATE    ####
#######################


def do_conversion(
    data,
    endpoint: str,
    dtype: types.ScalarType,
    sparse: Any,
    categorical: bool,
    errors: str,
    *args,
    **kwargs
) -> pd.Series:
    # calculate submap
    submap = {
        k: getattr(k, endpoint) for k in types.AtomicType.registry
        if hasattr(k, endpoint)
    }

    # wrap according to adapter settings
    if categorical:
        dtype = types.CategoricalType(dtype)
    if sparse is not None:
        dtype = types.SparseType(dtype, fill_value=sparse)

    try:
        # NOTE: passing dispatch(endpoint="") will never fall back to pandas
        with wrapper.SeriesWrapper(as_series(data)) as series:
            result = series.dispatch(
                "",
                submap,
                None,
                *args,
                dtype=dtype.unwrap(),
                errors=errors,
                **kwargs
            )
            series.series = result.series
            series.hasnans = result.hasnans
            series.element_type = result.element_type

        if isinstance(dtype, types.AdapterType):
            dtype.atomic_type = series.element_type
            return dtype.apply_adapters(series).series

        return series.series

    except (KeyboardInterrupt, MemoryError, SystemError, SystemExit):
        raise  # never ignore these errors
    except Exception as err:
        if errors == "ignore":
            return data
        raise err
