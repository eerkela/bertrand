from __future__ import annotations
import datetime
import decimal
import functools
import warnings
import zoneinfo

import dateutil
import numpy as np
import pandas as pd
import pytz

from ..check import check_dtype, get_dtype, is_dtype, resolve_dtype
from ..cython.loops import string_to_boolean
from ..error import ConversionError, error_trace, shorten_list
from ..util.array import vectorize
from ..util.type_hints import array_like, dtype_like

from .helpers import (
    _validate_dtype, _validate_errors, _validate_rounding, _validate_tolerance
)
from .integer import IntegerSeries


class StringSeries:
    """test"""

    def __init__(
        self,
        series: pd.Series,
        validate: bool = True
    ) -> StringSeries:
        if validate and not check_dtype(series, str):
            err_msg = (f"[{error_trace()}] `series` must contain decimal "
                       f"data, not {get_dtype(series)}")
            raise TypeError(err_msg)

        self.series = series

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        _validate_dtype(dtype, bool)
        _validate_errors(errors)

        # for each element, attempt boolean coercion and note errors
        series, invalid = string_to_boolean(self.series.to_numpy())
        if invalid.any():
            if errors != "coerce":
                bad_vals = self.series[invalid]
                err_msg = (f"non-boolean values detected at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series = pd.Series(series, dtype=pd.BooleanDtype())
        else:
            series = pd.Series(series, dtype=bool)

        # replace index and return
        series.index = self.series.index
        return series

    def to_integer(
        self,
        dtype: dtype_like = int,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        _validate_dtype(dtype, int)
        _validate_errors(errors)

        # for each element, attempt integer coercion and note errors
        def transcribe(element: str) -> tuple[int, bool]:
            try:
                return (int(element.replace(" ", "")), False)
            except ValueError:
                return (pd.NA, True)

        # TODO: series might have NAs

        series, invalid = np.frompyfunc(transcribe, 1, 2)(self.series)
        if invalid.any():
            if errors != "coerce":
                bad_vals = self.series[invalid]
                err_msg = (f"non-integer values detected at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)

            # TODO: series has NAs.  Strip them out before passing to
            # IntegerSeries, then merge them back in by constructing a new
            # result series, as with SeriesWrapper.





def string_to_integer(
    series: str | array_like,
    dtype: dtype_like = int,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a string series into integers."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_string_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_integer_dtype(dtype)
    _validate_errors(errors)

    # for each element, attempt integer coercion and note errors
    def transcribe(element: str) -> tuple[int, bool]:
        try:
            return (int(element.replace(" ", "")), False)
        except ValueError:
            return (pd.NA, True)

    # subset to avoid missing values
    with ExcludeNA(series, pd.NA) as ctx:
        ctx.subset, invalid = np.frompyfunc(transcribe, 1, 2)(ctx.subset)
        if invalid.any():
            if errors == "raise":
                bad = ctx.subset[invalid].index.values
                err_msg = (f"[{error_trace()}] non-integer values detected at "
                           f"index {_shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return series

    # pass through integer_to_integer to sort out dtype, downcast args
    return integer_to_integer(ctx.result, dtype=dtype, downcast=downcast,
                              errors=errors, validate=False)


def string_to_float(
    series: str | array_like,
    dtype: dtype_like = float,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a string series into floats."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_string_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_float_dtype(dtype)
    _validate_errors(errors)

    # subset to avoid missing values
    with ExcludeNA(series, np.nan, dtype) as ctx:
        ctx.subset = ctx.subset.str.replace(" ", "").str.lower()
        old_infs = ctx.subset.isin(("-inf", "-infinity", "inf", "infinity"))

        # attempt conversion
        try:  # all elements are valid
            ctx.subset = ctx.subset.astype(dtype, copy=False)
        except ValueError as err:  # parsing errors, apply error-handling rule
            if errors == "ignore":
                return series

            # find indices of elements that cannot be parsed
            def transcribe(element: str) -> bool:
                try:
                    float(element)
                    return False
                except ValueError:
                    return True

            invalid = np.frompyfunc(transcribe, 1, 1)(ctx.subset)
            if errors == "raise":
                bad = ctx.subset[invalid].index.values
                err_msg = (f"[{error_trace()}] non-numeric values detected at "
                           f"index {_shorten_list(bad)}")
                raise ValueError(err_msg) from err

            ctx.subset[invalid] = np.nan
            ctx.subset = ctx.subset.astype(dtype, copy=False)

        # scan for new infinities introduced by coercion
        new_infs = np.isinf(ctx.subset) ^ old_infs
        if new_infs.any():
            if errors == "raise":
                bad = ctx.subset[new_infs].index.values
                err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                           f"index {_shorten_list(bad)}")
                raise OverflowError(err_msg)
            if errors == "ignore":
                return series
            ctx.subset[new_infs] = np.nan

    # return
    if downcast:
        return _downcast_float_series(ctx.result)
    return ctx.result


def string_to_complex(
    series: str | array_like,
    dtype: dtype_like = complex,
    downcast: bool = False,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a string series into complex numbers."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_string_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_complex_dtype(dtype)
    _validate_errors(errors)

    # subset to avoid missing values
    with ExcludeNA(series, complex(np.nan, np.nan), dtype) as ctx:
        ctx.subset = ctx.subset.str.replace(" ", "").str.lower()
        old_infs = ctx.subset.str.match(r".*(inf|infinity).*")

        # attempt conversion
        try:  # all elements are valid
            ctx.subset = ctx.subset.astype(dtype, copy=False)
        except ValueError as err:  # parsing errors, apply error-handling rule
            if errors == "ignore":
                return series

            # find indices of elements that cannot be parsed
            def transcribe(element: str) -> bool:
                try:
                    complex(element)
                    return False
                except ValueError:
                    return True

            invalid = np.frompyfunc(transcribe, 1, 1)(ctx.subset)
            if errors == "raise":
                bad = ctx.subset[invalid].index.values
                err_msg = (f"[{error_trace()}] non-numeric values detected at "
                           f"index {_shorten_list(bad)}")
                raise ValueError(err_msg) from err

            # coerce
            ctx.subset[invalid] = complex(np.nan, np.nan)
            ctx.subset = ctx.subset.astype(dtype, copy=False)

        # scan for new infinities introduced by coercion
        real = np.real(ctx.subset)
        imag = np.imag(ctx.subset)
        new_infs = (np.isinf(real) | np.isinf(imag)) ^ old_infs
        if new_infs.any():
            if errors == "raise":
                bad = ctx.subset[new_infs].index.values
                err_msg = (f"[{error_trace()}] values exceed {dtype} range at "
                           f"index {_shorten_list(bad)}")
                raise OverflowError(err_msg)
            if errors == "ignore":
                return series
            ctx.subset[new_infs] = np.nan

    # TODO: python doesn't like the `real = real or x` construction when
    # downcast=True

    # return
    if downcast:
        return _downcast_complex_series(ctx.result, real, imag)
    return ctx.result


def string_to_decimal(
    series: str | array_like,
    dtype: dtype_like = decimal.Decimal,
    errors: str = "raise",
    *,
    validate: bool = True
) -> pd.Series:
    """convert strings to arbitrary precision decimal objects."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_string_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_decimal_dtype(dtype)
    _validate_errors(errors)

    # for each element, attempt decimal coercion and note errors
    def transcribe(element: str) -> tuple[decimal.Decimal, bool]:
        try:
            return (decimal.Decimal(element.replace(" ", "")), False)
        except ValueError:
            return (pd.NA, True)

    # subset to avoid missing values
    with ExcludeNA(series, pd.NA) as ctx:
        ctx.subset, invalid = np.frompyfunc(transcribe, 1, 2)(ctx.subset)
        if invalid.any():
            if errors == "raise":
                bad = ctx.subset[invalid].index.values
                err_msg = (f"[{error_trace()}] non-decimal values detected at "
                           f"index {_shorten_list(bad)}")
                raise ValueError(err_msg)
            if errors == "ignore":
                return series

    return ctx.result


def _string_to_pandas_timestamp(
    series: str | array_like,
    format: None | str = None,
    tz: None | str | datetime.tzinfo = "local",
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    errors: str = "raise"
) -> pd.Series:
    """Convert a datetime string series into `pandas.Timestamp` objects."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_string_series(series)
    _validate_format(format, day_first=day_first, year_first=year_first)
    tz = _parse_timezone(tz)
    _validate_errors(errors)

    # do conversion -> use pd.to_datetime with appropriate args
    if format:  # use specified format string
        result = pd.to_datetime(series, utc=True, format=format,
                                exact=not fuzzy, errors=errors)
    else:  # infer format
        result = pd.to_datetime(series, utc=True, dayfirst=day_first,
                                yearfirst=year_first,
                                infer_datetime_format=True, errors=errors)

    # catch immediate return from pd.to_datetime(..., errors="ignore")
    if errors == "ignore" and result.equals(series):
        return series

    # TODO: this last localize step uses LMT (local mean time) for dates prior
    # to 1902 for some reason.  This appears to be a known pytz limitation.
    # https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
    # https://github.com/pandas-dev/pandas/issues/41834
    # solution: use zoneinfo.ZoneInfo instead once pandas supports it
    # https://github.com/pandas-dev/pandas/pull/46425
    return result.dt.tz_convert(tz)


def _string_to_pydatetime(
    series: str | array_like,
    format: None | str = None,
    tz: None | str | datetime.tzinfo = "local",
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    cache: bool = True,
    errors: str = "raise"
) -> pd.Series:
    """Convert a datetime string series into `datetime.datetime` objects."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_string_series(series)
    _validate_format(format, day_first=day_first, year_first=year_first)
    tz = _parse_timezone(tz)
    _validate_errors(errors)

    # subset to avoid missing values
    not_na = series.notna()
    subset = series[not_na]

    # gather resources for elementwise conversion
    def localize(date: datetime.datetime) -> datetime.datetime:
        if not tz and not date.tzinfo:
            return date
        if not tz:
            date = date.astimezone(datetime.timezone.utc)
            return date.replace(tzinfo=None)
        if not date.tzinfo:
            date = date.replace(tzinfo=datetime.timezone.utc)
        return date.astimezone(tz)
    # TODO: this last localize step uses LMT (local mean time) for dates prior
    # to 1902 for some reason.  This appears to be a known pytz limitation.
    # https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
    # https://github.com/pandas-dev/pandas/issues/41834
    # solution: use zoneinfo.ZoneInfo instead once pandas supports it
    # https://github.com/pandas-dev/pandas/pull/46425

    if format is None:  # attempt to infer format
        infer = pd.core.tools.datetimes._guess_datetime_format_for_array
        format = infer(np.array(subset))
    parser_info = dateutil.parser.parserinfo(dayfirst=day_first,
                                             yearfirst=year_first)

    # TODO: this is actually almost exactly as fast as pd.to_datetime when
    # parsing is required, but significantly slower when it is not.

    # do conversion -> use an elementwise conversion func + dateutil
    if format:  # format is given or can be inferred
        def transcribe(date_string: str) -> datetime.datetime:
            date_string = date_string.strip()
            try:
                try:
                    result = datetime.datetime.strptime(date_string, format)
                    return localize(result)
                except ValueError:  # attempt flexible parse
                    result = dateutil.parser.parse(date_string, fuzzy=fuzzy,
                                                   parserinfo=parser_info)
                    return localize(result)
            except (dateutil.parser.ParserError, OverflowError) as err:
                if errors == "raise":
                    raise err
                if errors == "ignore":
                    raise RuntimeError() from err  # used as kill signal
                return pd.NaT
    else:  # format cannot be inferred -> skip strptime step
        def transcribe(date_string: str) -> datetime.datetime:
            date_string = date_string.strip()
            try:
                result = dateutil.parser.parse(date_string, fuzzy=fuzzy,
                                               parserinfo=parser_info)
                return localize(result)
            except (dateutil.parser.ParserError, OverflowError) as err:
                if errors == "raise":
                    raise err
                if errors == "ignore":
                    raise RuntimeError() from err  # used as kill signal
                return pd.NaT

    # apply caching if directed
    if cache:
        transcribe = functools.cache(transcribe)

    # attempt conversion
    try:
        subset = np.frompyfunc(transcribe, 1, 1)(subset)
    except RuntimeError:
        return series
    except (dateutil.parser.ParserError, OverflowError) as err:
        err_msg = (f"[{error_trace()}] unable to interpret string "
                   f"{repr(err.args[1])}")
        raise ValueError(err_msg) from err

    # reassign subset to series, accounting for missing values
    series = pd.Series(np.full(series.shape, pd.NaT, dtype="O"))
    series[not_na] = subset
    return series


def _string_to_numpy_datetime64(
    series: str | array_like,
    errors: str = "raise"
) -> pd.Series:
    """Convert a datetime string series into `numpy.datetime64` objects."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_string_series(series)
    _validate_errors(errors)

    # subset to avoid missing values
    not_na = series.notna()
    subset = series[not_na]

    # do conversion -> requires ISO format and does not carry tzinfo
    try:
        subset = pd.Series(list(subset.array.astype("M8")), dtype="O")
    except ValueError as err:
        # TODO: replicate "coerce" behavior
        if errors == "ignore":
            return series
        raise err

    series = pd.Series(np.full(series.shape, pd.NaT, dtype="O"))
    series[not_na] = subset
    return series


def string_to_datetime(
    series: str | array_like,
    format: None | str = None,
    tz: None | str | datetime.tzinfo = "local",
    day_first: bool = False,
    year_first: bool = False,
    fuzzy: bool = False,
    dtype: dtype_like = "datetime",
    errors: str = "raise"
) -> pd.Series:
    """Convert a string series to datetimes of the specified dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    _validate_string_series(series)
    _validate_format(format, day_first, year_first)
    tz = _parse_timezone(tz)
    _validate_dtype_is_scalar(dtype)
    _validate_datetime_dtype(dtype)
    _validate_errors(errors)

    args = {
        "format": format,
        "tz": tz,
        "day_first": day_first,
        "year_first": year_first,
        "fuzzy": fuzzy,
        "errors": errors
    }

    # if dtype is a subtype of "datetime", return directly
    if is_dtype(dtype, pd.Timestamp):
        return _string_to_pandas_timestamp(series, **args)
    if is_dtype(dtype, datetime.datetime):
        return _string_to_pydatetime(series, **args)
    if is_dtype(dtype, np.datetime64):
        return _string_to_numpy_datetime64(series, errors=errors)

    # dtype is datetime superclass.  Try each and return most accurate.

    # try pd.Timestamp
    try:
        return _string_to_pandas_timestamp(series, **args)
    except (OverflowError, pd._libs.tslibs.np_datetime.OutOfBoundsDatetime,
            dateutil.parser.ParserError):
        pass

    # try datetime.datetime
    try:
        return _string_to_pydatetime(series, **args)
    except (ValueError, OverflowError, dateutil.parser.ParserError):
        pass

    # try np.datetime64
    if any((format, fuzzy, day_first, year_first)):
        err_msg = (f"[{error_trace()}] `numpy.datetime64` objects do not "
                   f"support arbitrary string parsing (string must be ISO "
                   f"8601-compliant)")
        raise TypeError(err_msg)
    if tz and tz not in ("UTC", datetime.timezone.utc, pytz.utc,
                         zoneinfo.ZoneInfo("UTC")):
        warn_msg = ("`numpy.datetime64` objects do not carry timezone "
                    "information - returned time is UTC")
        warnings.warn(warn_msg, RuntimeWarning)
    try:
        return _string_to_numpy_datetime64(series, errors=errors)
    except Exception as err:
        err_msg = (f"[{error_trace()}] could not convert string to any form "
                   f"of datetime object")
        raise ValueError(err_msg) from err


def _string_to_pandas_timedelta(
    series: str | array_like,
    errors: str = "raise"
) -> pd.Series:
    """Convert a string series into pd.Timedelta objects."""


def string_to_string(
    series: str | array_like,
    dtype: dtype_like = pd.StringDtype(),
    *,
    validate: bool = True
) -> pd.Series:
    """Convert a string series to another string dtype."""
    # vectorize input
    series = pd.Series(vectorize(series))

    # validate input
    if validate:
        _validate_string_series(series)
        _validate_dtype_is_scalar(dtype)
        _validate_string_dtype(dtype)

    # do conversion
    if series.hasnans:
        return series.astype(pd.StringDtype())
    return series.astype(dtype)
