"""This module contains dispatched cast() implementations for string data."""
# pylint: disable=unused-argument
import datetime
import re  # normal python regex for compatibility with pd.Series.str.extract
from functools import partial

import dateutil
import numpy as np
import pandas as pd

from pdcast import types
from pdcast.resolve import resolve_type
from pdcast.util.round import Tolerance
from pdcast.util.string import boolean_match
from pdcast.util import time
from pdcast.util.vector import apply_with_errors

from .base import (
    cast, generic_to_boolean, generic_to_complex
)


@cast.overload("string", "bool")
def string_to_boolean(
    series: pd.Series,
    dtype: types.AtomicType,
    true: set,
    false: set,
    errors: str,
    ignore_case: bool,
    **unused
) -> pd.Series:
    """Convert string data to a boolean data type."""
    lookup = dict.fromkeys(true, 1) | dict.fromkeys(false, 0)
    if "*" in true:
        fill = 1  # KeyErrors become truthy
    elif "*" in false:
        fill = 0  # KeyErrors become falsy
    else:
        fill = -1  # raise

    call = partial(
        boolean_match,
        lookup=lookup,
        ignore_case=ignore_case,
        fill=fill
    )
    series = apply_with_errors(series, call, errors=errors)
    series = series.astype(np.bool_)
    return generic_to_boolean(series, dtype, errors=errors)


@cast.overload("string", "int")
def string_to_integer(
    series: pd.Series,
    dtype: types.AtomicType,
    base: int,
    errors: str,
    **unused
) -> pd.Series:
    """Convert string data to an integer data type with the given base."""
    # 2 step conversion: string -> int[python], int[python] -> int
    series = apply_with_errors(series, partial(int, base=base), errors=errors)
    series = series.astype(resolve_type(int).dtype)
    return cast(
        series,
        dtype,
        base=base,
        errors=errors,
        **unused
    )


@cast.overload("string", "float")
def string_to_float(
    series: pd.Series,
    dtype: types.AtomicType,
    tol: Tolerance,
    errors: str,
    **unused
) -> pd.Series:
    """Convert string data to a floating point data type."""
    # 2 step conversion: string -> decimal, decimal -> float
    series = cast(series, "decimal", errors=errors)
    return cast(series, dtype, tol=tol, errors=errors, **unused)


@cast.overload("string", "complex")
def string_to_complex(
    series: pd.Series,
    dtype: types.AtomicType,
    tol: Tolerance,
    downcast: types.CompositeType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert string data to a complex data type."""
    # NOTE: this is technically a 3-step conversion: (1) str -> str (split
    # real/imag), (2) str -> float (x2), (3) float -> complex.  This allows for
    # full precision loss/overflow/downcast checks for both real and imag.

    # (1) separate real, imaginary components via regex
    components = series.str.extract(complex_pattern)
    real_part = components["real"]
    imag_part = components["imag"]

    # (2) convert real, imag to float, applying checks independently
    real_part = cast(
        real_part,
        dtype.equiv_float,
        tol=Tolerance(tol.real),
        downcast=None,
        errors="raise"
    )
    imag_part = cast(
        imag_part,
        dtype.equiv_float,
        tol=Tolerance(tol.imag),
        downcast=None,
        errors="raise"
    )

    # (3) combine floats into complex result
    series = (real_part + imag_part * 1j).astype(dtype.dtype, copy=False)
    return generic_to_complex(
        series,
        dtype,
        tol=tol,
        downcast=downcast,
        errors=errors
    )


@cast.overload("string", "datetime")
def string_to_datetime(
    series: pd.Series,
    dtype: types.AtomicType,
    errors: str,
    **unused
) -> pd.Series:
    """Convert string data into a datetime data type."""
    # NOTE: because this type has no associated scalars, it will never be given
    # as the result of a detect_type() operation.  It can only be specified
    # manually, as the target of a resolve_type() call.

    last_err = None
    for candidate in dtype.larger:
        try:
            return cast(series, candidate, errors="raise", **unused)
        except OverflowError as err:
            last_err = err

    # every candidate overflows - pick the last one and coerce
    if errors == "coerce":
        return cast(
            series,
            dtype,
            errors=errors,
            **unused
        )
    raise last_err


@cast.overload("string", "datetime[pandas]")
def string_to_pandas_timestamp(
    series: pd.Series,
    dtype: types.AtomicType,
    tz: datetime.tzinfo,
    format: str,
    day_first: bool,
    year_first: bool,
    errors: str,
    **unused
) -> pd.Series:
    """Convert datetime strings into pandas Timestamps."""
    # reconcile `tz` argument with timezone attached to dtype, if given
    if tz:
        dtype = dtype.replace(tz=tz)

    # configure kwargs for pd.to_datetime
    kwargs = {
        "dayfirst": day_first,
        "yearfirst": year_first,
        "utc": dtype.tz is not None and time.is_utc(dtype.tz),
        "errors": "raise" if errors == "ignore" else errors
    }
    if format:
        kwargs |= {"format": format, "exact": False}
    else:
        kwargs |= {"format": "mixed"}

    # NOTE: pd.to_datetime() can throw several kinds of errors, some of which
    # are ambiguous.  For simplicity, we catch and re-raise these only as
    # ValueErrors or OverflowErrors.
    try:
        result = pd.to_datetime(series, **kwargs)

    # exception 1: outside pd.Timestamp range, but within datetime.datetime
    except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime as err:
        raise OverflowError(str(err)) from None  # truncate stack

    # exception 2: bad string or outside datetime.datetime range
    except dateutil.parser.ParserError as err:
        raise time.filter_dateutil_parser_error(err) from None

    # account for missing values introduced during coercion
    if errors == "coerce":
        isna = result.isna()
        result = result[~isna]

    # localize to final timezone
    try:
        # NOTE: if utc=False and there are mixed timezones and/or mixed
        # aware/naive strings in the input series, the output of
        # pd.to_datetime() could be malformed.
        if not kwargs["utc"]:
            # homogenous - either naive or consistent timezone
            if pd.api.types.is_datetime64_ns_dtype(result):
                if not result.dt.tz:  # naive
                    result = result.dt.tz_localize(dtype.tz)
                else:  # aware
                    result = result.dt.tz_convert(dtype.tz)

            # non-homogenous - either mixed timezone or mixed aware/naive
            else:  # NOTE: pd.to_datetime() sacrifices ns precision here
                localize = partial(
                    time.localize_pydatetime_scalar,
                    tz=dtype.tz
                )
                # NOTE: np.frompyfunc() implicitly casts to pd.Timestamp
                result = np.frompyfunc(localize, 1, 1)(result)

    # exception 3: overflow induced by timezone localization
    except pd._libs.tslibs.np_datetime.OutOfBoundsDatetime as err:
        raise OverflowError(str(err)) from None

    return result


@cast.overload("string", "datetime[python]")
def string_to_python_datetime(
    series: pd.Series,
    dtype: types.ScalarType,
    tz: datetime.tzinfo,
    day_first: bool,
    year_first: bool,
    format: str,
    errors: str,
    **unused
) -> pd.Series:
    """Convert strings into datetime objects."""
    # reconcile `tz` argument with timezone attached to dtype, if given
    if tz:
        dtype = dtype.replace(tz=tz)

    # set up dateutil parserinfo
    parser_info = dateutil.parser.parserinfo(
        dayfirst=day_first,
        yearfirst=year_first
    )

    # apply elementwise
    call = partial(
        time.string_to_pydatetime,
        format=format,
        parser_info=parser_info,
        tz=dtype.tz,
        errors=errors
    )
    series = apply_with_errors(series, call, errors=errors)
    return series.astype(dtype.dtype)


@cast.overload("string", "datetime[numpy]")
def string_to_numpy_datetime64(
    series: pd.Series,
    dtype: types.ScalarType,
    format: str,
    tz: datetime.tzinfo,
    errors: str,
    **unused
) -> pd.Series:
    """Convert ISO 8601 strings to a numpy datetime64 data type."""
    if format and not time.is_iso_8601_format_string(format):
        raise TypeError(
            "np.datetime64 strings must be in ISO 8601 format"
        )
    if tz and not time.is_utc(tz):
        raise TypeError(
            "np.datetime64 objects do not carry timezone information"
        )

    # 2-step conversion: string -> ns, ns -> datetime64
    series = apply_with_errors(series, time.iso_8601_to_ns, errors=errors)
    series = series.astype(resolve_type(int).dtype)
    return cast(
        series,
        dtype,
        format=format,
        tz=tz,
        errors=errors,
        **unused
    )


@cast.overload("string", "timedelta")
def string_to_timedelta(
    series: pd.Series,
    dtype: types.AtomicType,
    unit: str,
    step_size: int,
    since: time.Epoch,
    as_hours: bool,
    errors: str,
    **unused
) -> pd.Series:
    """Convert string data into a timedelta representation."""
    # 2-step conversion: str -> int, int -> timedelta
    call = partial(time.timedelta_string_to_ns, as_hours=as_hours, since=since)
    series = apply_with_errors(series, call, errors=errors)
    series = series.astype(resolve_type(int).dtype)
    return cast(
        series,
        dtype,
        unit="ns",
        step_size=1,
        since=since,
        errors=errors,
        **unused
    )


#######################
####    PRIVATE    ####
#######################


complex_pattern = re.compile(
    r"\(?(?P<real>[+-]?[0-9.]+)(?P<imag>[+-][0-9.]+)?j?\)?"
)
