from __future__ import annotations
import datetime
import decimal
import functools
import re
import warnings
import zoneinfo

import dateutil
import numpy as np
import pandas as pd
import pytz


from pdtypes import DEFAULT_STRING_DTYPE

from pdtypes.check import (
    check_dtype, extension_type, get_dtype, is_dtype, resolve_dtype
)
from pdtypes.util.loops.string import (
    string_to_boolean, split_complex_strings, string_to_pydatetime,
    string_to_pytimedelta, string_to_numpy_timedelta64, localize
)
from pdtypes.error import ConversionError, error_trace, shorten_list
from pdtypes.util.array import vectorize
from pdtypes.util.time import _to_ns
from pdtypes.util.type_hints import array_like, dtype_like

from .helpers import (
    _validate_datetime_format, _validate_dtype, _validate_errors,
    integral_range, localize_pydatetime, parse_timezone, tolerance
)
from .decimal import DecimalSeries


# TODO: timedelta parsing should be cythonized and go in its own .pyx file


def timedelta_formats_regex():
    """Compile a set of regular expressions to capture and parse recognized
    timedelta strings.

    Matches both abbreviated ('1h22m', '1 hour, 22 minutes', etc.) and
    clock format ('01:22:00', '1:22', '00:01:22:00') strings, with precision up
    to days and/or weeks and down to nanoseconds.
    """
    # capture groups - abbreviated units ('h', 'min', 'seconds', etc.)
    # years = r"(?P<years>\d+)\s*(?:ys?|yrs?.?|years?)"
    # months = r"(?P<months>\d+)\s*(?:mos?.?|mths?.?|months?)"
    W = r"(?P<W>[\d.]+)\s*(?:w|wks?|weeks?)"
    D = r"(?P<D>[\d.]+)\s*(?:d|dys?|days?)"
    h = r"(?P<h>[\d.]+)\s*(?:h|hrs?|hours?)"
    m = r"(?P<m>[\d.]+)\s*(?:m|(mins?)|(minutes?))"
    s = r"(?P<s>[\d.]+)\s*(?:s|secs?|seconds?)"
    ms = r"(?P<ms>[\d.]+)\s*(?:ms|msecs?|millisecs?|milliseconds?)"
    us = r"(?P<us>[\d.]+)\s*(?:us|usecs?|microsecs?|microseconds?)"
    ns = r"(?P<ns>[\d.]+)\s*(?:ns|nsecs?|nanosecs?|nanoseconds?)"

    # capture groups - clock format (':' separated)
    day_clock = r"(?P<D>\d+):(?P<h>\d{2}):(?P<m>\d{2}):(?P<s>\d{2}(?:\.\d+)?)"
    hour_clock = r"(?P<h>\d+):(?P<m>\d{2}):(?P<s>\d{2}(?:\.\d+)?)"
    minute_clock =  r"(?P<m>\d{1,2}):(?P<s>\d{2}(?:\.\d+)?)"
    second_clock = r":(?P<s>\d{2}(?:\.\d+)?)"

    # wrapping functions for capture groups
    separators = r"[,/]"  # these are ignored
    optional = lambda x: rf"(?:{x}\s*(?:{separators}\s*)?)?"

    # compiled timedelta formats
    formats = [
        (rf"{optional(W)}\s*{optional(D)}\s*{optional(h)}\s*{optional(m)}\s*"
         rf"{optional(s)}\s*{optional(ms)}\s*{optional(us)}\s*{optional(ns)}"),
        rf"{optional(W)}\s*{optional(D)}\s*{hour_clock}",
        rf"{day_clock}",
        rf"{minute_clock}",
        rf"{second_clock}"
    ]
    return [re.compile(rf"\s*{fmt}\s*$") for fmt in formats]


timedelta_regex = {
    "sign": re.compile(r"\s*(?P<sign>[+|-])?\s*(?P<unsigned>.*)$"),
    "formats": timedelta_formats_regex()
}


def string_to_ns(delta: str, as_hours: bool = False) -> int:
    """Parse a timedelta string, returning its associated value as an integer
    number of nanoseconds.

    See also: https://github.com/wroberts/pytimeparse

    Parameters
    ----------
    delta (str):
        Timedelta string to parse.  Can be in either abbreviated ('1h22m',
        '1 hour, 22 minutes', ...) or clock format ('01:22:00', '1:22',
        '00:01:22:00', ...), with precision up to days/weeks and down to
        nanoseconds.  Can be either signed or unsigned.
    as_hours (bool):
        Whether to parse ambiguous timedelta strings of the form '1:22' as
        containing hours and minutes (`True`), or minutes and seconds
        (`False`).  Does not affect any other string format.

    Returns
    -------
    int:
        An integer number of nanoseconds associated with the given timedelta
        string.  If the string contains digits below nanosecond precision,
        they are destroyed.

    Raises
    ------
    ValueError:
        If the passed timedelta string does not match any of the recognized
        formats.

    Examples
    --------
    >>> parse_timedelta('1:24')
    84000000000
    >>> parse_timedelta(':22')
    22000000000
    >>> parse_timedelta('1 minute, 24 secs')
    84000000000
    >>> parse_timedelta('1m24s')
    84000000000
    >>> parse_timedelta('1.2 minutes')
    72000000000
    >>> parse_timedelta('1.2 seconds')
    1200000000

    Time expressions can be signed.
    >>> parse_timedelta('- 1 minute')
    -60000000000
    >>> parse_timedelta('+ 1 minute')
    60000000000

    If `as_hours=True`, then ambiguous digits following a colon will be
    interpreted as minutes; otherwise they are considered to be seconds.
    >>> timeparse('1:30', as_hours=False)
    90000000000
    >>> timeparse('1:30', as_hours=True)
    5400000000000
    """
    # get sign if present
    match = timedelta_regex["sign"].match(delta)
    sign = -1 if match.groupdict()["sign"] == "-" else 1

    # strip sign and test all possible formats
    delta = match.groupdict()["unsigned"]
    for time_format in timedelta_regex["formats"]:

        # attempt match
        match = time_format.match(delta)
        if match and match.group().strip():  # match found and not empty

            # get dict of named subgroups (?P<...>) and associated values
            groups = match.groupdict()

            # strings of the form '1:22' are ambiguous.  By default, we assume
            # minutes and seconds, but if `as_hours=True`, we interpret as
            # hours and minutes instead
            if (as_hours and delta.count(":") == 1 and "." not in delta and
                not any(groups.get(x, None) for x in ["h", "D", "W"])):
                groups["h"] = groups["m"]
                groups["m"] = groups["s"]
                groups.pop("s")

            # build result
            result = sum(_to_ns[k] * decimal.Decimal(v)
                         for k, v in groups.items() if v)
            return int(sign * result)

    # string could not be matched
    raise ValueError(f"could not parse timedelta string: {repr(delta)}")


def parse_iso_8601_strings(arr: np.ndarray) -> np.ndarray:
    """test"""
    arr = arr.astype("M8[us]")

    # TODO: have to manually check for overflow

    min_val = arr.min()
    max_val = arr.max()
    min_datetime = np.datetime64(datetime.datetime.min)
    max_datetime = np.datetime64(datetime.datetime.max)

    if min_val < min_datetime or max_val > max_datetime:
        raise ValueError()

    return arr.astype("O")


# TODO: swap default tz to UTC?  -> Minor performance increase


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
        # TODO: `invalid` may be replaced by (series == pd.NA).any()
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
        base: int = 10,
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
            element = element.replace(" ", "")
            try:  # attempt conversion
                return (int(element, base=base), False)
            except ValueError:
                return (pd.NA, True)

        # TODO: `invalid` can be replaced by series == pd.NA
        series, invalid = np.frompyfunc(transcribe, 1, 2)(self.series)
        coerced = invalid.any()
        if coerced and errors != "coerce":
            bad_vals = self.series[invalid]
            err_msg = (f"invalid literal for int() with base {base} at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # get min/max to evaluate range
        min_val = series.min()
        max_val = series.max()

        # built-in integer special case - can be arbitrarily large
        if is_dtype(dtype, int, exact=True):
            if min_val < -2**63 or max_val > 2**63 - 1:  # >int64
                if min_val >= 0 and max_val <= 2**64 - 1:  # <uint64
                    dtype = pd.UInt64Dtype() if coerced else np.uint64
                    return series.astype(dtype, copy=False)
                # series is >int64 and >uint64, return as built-in python ints
                return series
            # extended range isn't needed, demote to int64
            dtype = np.int64

        # check whether min_val, max_val fit within `dtype` range
        min_poss, max_poss = integral_range(dtype)
        if min_val < min_poss or max_val > max_poss:
            if errors != "coerce":
                bad_vals = series[(series < min_poss) | (series > max_poss)]
                err_msg = (f"values exceed {dtype.__name__} range at index "
                           f"{shorten_list(bad_vals.index.values)}")
                raise ConversionError(err_msg, bad_vals)
            series[(series < min_poss) | (series > max_poss)] = pd.NA  # coerce
            min_val = series.min()
            max_val = series.max()
            coerced = True  # remember to convert to extension type later

        # attempt to downcast, if applicable
        if downcast:  # search for smaller dtypes that can represent series
            if is_dtype(dtype, "unsigned"):
                int_types = [np.uint8, np.uint16, np.uint32, np.uint64]
            else:
                int_types = [np.int8, np.int16, np.int32, np.int64]
            for downcast_type in int_types[:int_types.index(dtype)]:
                min_poss, max_poss = integral_range(downcast_type)
                if min_val >= min_poss and max_val <= max_poss:
                    dtype = downcast_type
                    break  # stop at smallest

        # convert and return
        if coerced:  # convert to extension type early
            dtype = extension_type(dtype)
        return series.astype(dtype, copy=False)

    def to_float(
        self,
        dtype: dtype_like = float,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        tol, _ = tolerance(tol)
        _validate_dtype(dtype, float)
        _validate_errors(errors)

        # 2 steps: string -> decimal, then decimal -> float
        series = self.to_decimal(errors=errors)
        if errors == "coerce":  # series might contain missing vals
            valid = pd.notna(series)
            if not valid.all():
                # compute decimal -> float on valid subset
                series = DecimalSeries(series[valid], validate=False)
                series = series.to_float(dtype=dtype, tol=tol,
                                         downcast=downcast, errors=errors)

                # replace nans
                result = pd.Series(np.full(self.series.shape, np.nan,
                                            dtype=series.dtype))
                result[valid] = series
                result.index = self.series.index
                return result

        # no missing vals to replace, compute decimal -> float directly
        series = DecimalSeries(series, validate=False)
        return series.to_float(dtype=dtype, tol=tol, downcast=downcast,
                               errors=errors)

    def to_complex(
        self,
        dtype: dtype_like = complex,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        dtype = resolve_dtype(dtype)
        real_tol, imag_tol = tolerance(tol)
        _validate_dtype(dtype, complex)
        _validate_errors(errors)
        if dtype == complex:  # built-in complex is identical to np.complex128
            dtype = np.complex128

        # split strings into real, imaginary components
        real, imag, invalid = split_complex_strings(self.series.to_numpy())

        # check for conversion errors
        if errors != "coerce" and invalid.any():
            bad_vals = self.series[invalid]
            err_msg = (f"non-complex value detected at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        # convert real, imag to StringSeries and replace index
        real = StringSeries(pd.Series(real, index=self.series.index),
                            validate=False)
        imag = StringSeries(pd.Series(imag, index=self.series.index),
                            validate=False)

        # call .to_float() on real and imaginary components separately
        equiv_float = {
            np.complex64: np.float32,
            np.complex128: np.float64,
            np.clongdouble: np.longdouble
        }
        real = real.to_float(equiv_float[dtype], tol=real_tol, errors=errors)
        imag = imag.to_float(equiv_float[dtype], tol=imag_tol, errors=errors)

        # combine real and imaginary components
        series = imag * 1j  # coerces nans/infs to (nan+nanj), (nan+infj)
        series.to_numpy().real[np.isnan(imag) | np.isinf(imag)] = 0  # correct
        series += real

        # downcast, if applicable
        if downcast:  # search for smaller dtypes that can represent series
            complex_types = [np.complex64, np.complex128, np.clongdouble]
            for downcast_type in complex_types[:complex_types.index(dtype)]:
                attempt = series.astype(downcast_type)
                if (attempt == series).all():
                    return attempt

        return series

    def to_decimal(
        self,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        _validate_errors(errors)

        # for each element, attempt decimal coercion and note errors
        def transcribe(element: str) -> tuple[decimal.Decimal, bool]:
            element = element.replace(" ", "")
            try:
                return (decimal.Decimal(element), False)
            except (ValueError, decimal.InvalidOperation):
                return (pd.NA, True)

        series, invalid = np.frompyfunc(transcribe, 1, 2)(self.series)
        if errors != "coerce" and invalid.any():
            bad_vals = series[invalid]
            err_msg = (f"non-decimal value detected at index "
                       f"{shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals)

        return series

    def _to_pandas_timestamp(
        self,
        format: None | str = None,
        tz: None | str | datetime.tzinfo = "local",
        day_first: bool = False,
        year_first: bool = False,
        fuzzy: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        _validate_datetime_format(format, day_first, year_first)
        tz = parse_timezone(tz)
        _validate_errors(errors)

        # set up pd.to_datetime args
        if errors == "ignore":  # can't catch unraised error
            errors = "raise"
        if format:  # use specified format string
            kwargs = {"format": format, "exact": not fuzzy}
        else:  # infer format
            kwargs = {"dayfirst": day_first, "yearfirst": year_first,
                      "infer_datetime_format": True}

        # do conversion and catch any errors
        try:
            result = pd.to_datetime(self.series, utc=True, errors=errors,
                                    **kwargs)
        except (OverflowError, pd._libs.tslibs.np_datetime.OutOfBoundsDatetime,
                dateutil.parser.ParserError) as err:
            raise ConversionError(str(err), None) from err

        # TODO: this last localize step uses LMT (local mean time) for dates
        # prior to 1902 for some reason.  This appears to be a known pytz
        # limitation.
        # https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
        # https://github.com/pandas-dev/pandas/issues/41834
        # solution: use zoneinfo.ZoneInfo instead once pandas supports it
        # https://github.com/pandas-dev/pandas/pull/46425
        return result.dt.tz_convert(tz)

    def _to_pydatetime(
        self,
        format: None | str = None,
        tz: None | str | datetime.tzinfo = "local",
        day_first: bool = False,
        year_first: bool = False,
        fuzzy: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        _validate_datetime_format(format, day_first, year_first)
        tz = parse_timezone(tz)
        _validate_errors(errors)

        # TODO: localize step uses LMT (local mean time) for dates prior
        # to 1902 for some reason.  This appears to be a known pytz limitation.
        # https://stackoverflow.com/questions/24188060/in-pandas-why-does-tz-convert-change-the-timezone-used-from-est-to-lmt
        # https://github.com/pandas-dev/pandas/issues/41834
        # solution: use zoneinfo.ZoneInfo instead once pandas supports it
        # https://github.com/pandas-dev/pandas/pull/46425

        # fastpath for ISO 8601 strings
        # TODO: might need to filter based on day_first, year_first, format
        # if they do not match ISO 8601
        try:
            series = parse_iso_8601_strings(self.series.to_numpy())
            series = np.frompyfunc(localize, 2, 1)(series, tz)
            series = pd.Series(series, dtype="O")
            series.index = self.series.index
            return series
        except ValueError:
            pass

        # attempt conversion
        kwargs = {"format": format, "tz": tz, "day_first": day_first,
                  "year_first": year_first, "fuzzy": fuzzy, "errors": errors}
        try:
            series = string_to_pydatetime(self.series.to_numpy(), **kwargs)
        except dateutil.parser.ParserError as err:
            bad_string = err.args[1]
            bad_vals = self.series[self.series == bad_string]
            err_msg = (f"unable to interpret string {repr(bad_string)} at "
                       f"index {shorten_list(bad_vals.index.values)}")
            raise ConversionError(err_msg, bad_vals) from err

        # reassign subset to series, accounting for missing values
        series = pd.Series(series, dtype="O")
        series.index = self.series.index
        return series

    def _to_numpy_datetime64(self, errors: str = "raise") -> pd.Series:
        """test"""
        _validate_errors(errors)

        # TODO: can't replicate errors="coerce"

        # TODO: this can silently overflow if unit is too small for value

        try:
            series = self.series.to_numpy().astype("M8")
        except ValueError as err:
            err_msg = ("np.datetime64 objects do not support arbitrary string "
                       "parsing (string must be ISO 8601-compliant)")
            raise ConversionError(err_msg, pd.Series()) from err

        series = pd.Series(list(series), dtype="O")
        series.index = self.series.index
        return series

    def to_datetime(
        self,
        dtype: dtype_like = "datetime",
        format: None | str = None,
        tz: None | str | datetime.tzinfo = "local",
        day_first: bool = False,
        year_first: bool = False,
        fuzzy: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        if dtype != "datetime":
            dtype = resolve_dtype(dtype)
        # _validate_dtype(dtype, "datetime")  # TODO: fix in check.py
        _validate_datetime_format(format, day_first, year_first)
        tz = parse_timezone(tz)
        _validate_errors(errors)

        # TODO: new type identifiers, in same pattern as string extension type
        # "datetime"
        # "datetime[pandas]"/"pandas.timestamp"/"pd.timestamp"
        # "datetime[python]"/"pydatetime"/"datetime.datetime"
        # "datetime[numpy]"/"numpy.datetime64"/"np.datetime64"/"M8"
        # TODO: ^ add optional unit information ("M8[ns]" -> pd.Timestamp)

        kwargs = {"format": format, "tz": tz, "day_first": day_first,
                  "year_first": year_first, "fuzzy": fuzzy}

        # if dtype is a subtype of "datetime", return directly
        if dtype == pd.Timestamp:
            return self._to_pandas_timestamp(**kwargs, errors=errors)
        if dtype == datetime.datetime:
            return self._to_pydatetime(**kwargs, errors=errors)
        if dtype == np.datetime64:
            # TODO: reject kwargs
            return self._to_numpy_datetime64(errors=errors)

        # dtype is "datetime" superclass.  Try each and return most precise

        # pd.Timestamp
        try:
            return self._to_pandas_timestamp(**kwargs, errors="raise")
        except ConversionError:
            pass

        # datetime.datetime
        try:
            return self._to_pydatetime(**kwargs, errors="raise")
        except (OverflowError, ConversionError):
            pass

        # np.datetime64
        if any((format, fuzzy, day_first, year_first)):
            err_msg = ("`numpy.datetime64` objects do not support arbitrary "
                       "string parsing (string must be ISO 8601-compliant)")
            raise TypeError(err_msg)
        if tz and tz not in ("UTC", datetime.timezone.utc, pytz.utc,
                            zoneinfo.ZoneInfo("UTC")):
            warn_msg = ("`numpy.datetime64` objects do not carry timezone "
                        "information - returned time is UTC")
            warnings.warn(warn_msg, RuntimeWarning)
        try:
            return self._to_numpy_datetime64(errors="raise")
        except ConversionError:
            pass

        # TODO: handle errors="coerce"
        # -> distinguish between overflow and parsing errors, and only carry
        # to next highest precision on overflow

        # could not parse
        err_msg = (f"could not convert string to any form of datetime object")
        raise ConversionError(err_msg, pd.Series())

    def _to_pandas_timedelta(self, errors: str = "raise") -> pd.Series:
        """test"""
        _validate_errors(errors)

        # set up pd.to_timedelta args
        if errors == "ignore":  # can't catch unraised error
            errors = "raise"

        try:
            return pd.to_timedelta(self.series, errors=errors)
        except (OverflowError, ValueError) as err:
            raise ConversionError(str(err), None) from err

    def _to_pytimedelta(
        self,
        as_hours: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        _validate_errors(errors)

        # implemented as a cython loop
        result = string_to_pytimedelta(self.series.to_numpy(),
                                       as_hours=as_hours, errors=errors)
        return pd.Series(result, index=self.series.index, dtype="O")

    def _to_numpy_timedelta64(
        self,
        as_hours: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        _validate_errors(errors)

        # implemented as a cython loop
        result = string_to_numpy_timedelta64(self.series.to_numpy(),
                                             as_hours=as_hours, errors=errors)
        return pd.Series(result, index=self.series.index, dtype="O")

    def to_timedelta(
        self,
        dtype: dtype_like = "timedelta",
        errors: str = "raise"
    ) -> pd.Series:
        """test"""
        if dtype != "timedelta":  # can't directly resolve timedelta supertype
            dtype = resolve_dtype(dtype)
        # _validate_dtype(dtype, "datetime")  # TODO: fix in check.py
        _validate_errors(errors)

        # TODO: new type identifiers, in same pattern as string extension type
        # "timedelta"
        # "timedelta[pandas]"/"pandas.timedelta"/"pd.timedelta"
        # "timedelta[python]"/"pytimedelta"/"datetime.timedelta"
        # "timedelta[numpy]"/"numpy.timedelta64"/"np.timedelta64"/"m8"
        # TODO: ^ add optional unit information ("m8[ns]" -> pd.Timedelta)

        # if dtype is a subtype of "datetime", return directly
        if dtype == pd.Timedelta:
            return self._to_pandas_timedelta(errors=errors)
        if dtype == datetime.timedelta:
            return self._to_pytimedelta(errors=errors)
        if dtype == np.timedelta64:
            return self._to_numpy_timedelta64(errors=errors)

        # dtype is "timedelta" superclass.  Try each and return most precise

        # pd.Timestamp
        try:
            return self._to_pandas_timedelta(errors="raise")
        except ConversionError:
            pass

        # datetime.timedelta
        try:
            return self._to_pytimedelta(errors="raise")
        except (OverflowError, ConversionError):
            pass

        # np.timedelta64
        try:
            return self._to_numpy_timedelta64(errors="raise")
        except ConversionError:
            pass

        # TODO: handle errors="coerce"
        # -> distinguish between overflow and parsing errors, and only carry
        # to next highest precision on overflow

        # could not parse
        err_msg = ("could not convert string to any form of datetime object")
        raise ConversionError(err_msg, pd.Series())

    def to_string(self, dtype: dtype_like = str) -> pd.Series:
        """test"""
        resolve_dtype(dtype)  # ensures scalar, resolveable
        _validate_dtype(dtype, str)

        # force string extension type
        if not pd.api.types.is_extension_array_dtype(dtype):
            dtype = DEFAULT_STRING_DTYPE

        return self.series.astype(dtype, copy=True)
