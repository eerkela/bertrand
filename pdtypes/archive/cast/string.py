from __future__ import annotations
import datetime
import decimal
import warnings
import zoneinfo
from typing import Iterable

import dateutil
import numpy as np
import pandas as pd
import pytz

from pdtypes.error import ConversionError, error_trace, shorten_list
from pdtypes.types import resolve_dtype, ElementType
from pdtypes.util.array import vectorize
from pdtypes.util.type_hints import datetime_like, dtype_like

from .util.loops.string import (
    string_to_boolean, string_to_integer, split_complex_strings
)
from .util.validate import (
    tolerance, validate_datetime_format, validate_dtype, validate_errors,
    validate_series
)

from .base import SeriesWrapper
from .integer import IntegerSeries
from .decimal import DecimalSeries



# TODO: swap default tz to UTC?  -> Minor performance increase


# If DEBUG=True, insert argument checks into IntegerSeries conversion methods
DEBUG: bool = True


def parse_boolean_true_false(
    true: str | Iterable,
    false: str | Iterable
) -> tuple[bool, tuple, tuple]:
    """Remove wildcards from `true`/`false` and find an appropriate
    `fill_value` for StringSeries.to_boolean()
    """
    fill_value = pd.NA
    true = [true] if isinstance(true, str) else list(true)
    false = [false] if isinstance(false, str) else list(false)

    # check for '*' wildcard in `true`
    if "*" in true:
        # replace fill value and remove wildcards from `true`
        fill_value = True
        while "*" in true:
            true.remove("*")

    # check for '*' wildcard in `false`
    if "*" in false:
        # check if '*' wildcard is also present in `true`
        if fill_value is True:  # boolean value of pd.NA is ambiguous
            raise ValueError(
                "The wildcard '*' can be in either `true` or `false`, but not "
                "both"
            )

        # replace fill value and remove wildcard from `false`
        fill_value = False
        while "*" in false:
            false.remove("*")

    # convert to tuple and unescape '\*' literals
    true = tuple(s.replace(r"\*", "*") for s in true)
    false = tuple(s.replace(r"\*", "*") for s in false)

    # return
    return (fill_value, true, false)


class StringSeries(SeriesWrapper):
    """TODO"""

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        is_na: pd.Series = None
    ) -> StringSeries:
        if DEBUG:
            validate_series(series, str)

        super().__init__(series=series, hasnans=hasnans, is_na=is_na)

    ###########################
    ####    CONVERSIONS    ####
    ###########################

    def to_boolean(
        self,
        dtype: dtype_like = bool,
        true: str | Iterable = ("y", "yes", "t", "true", "on", "1"),
        false: str | Iterable = ("n", "no", "f", "false", "off", "0"),
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: raise to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: confirm dtype is bool-like, errors is valid
        validate_dtype(dtype, bool)
        validate_errors(errors)

        # get an appropriate fill value and trim wildcards from `true`/`false`
        fill_value, true, false = parse_boolean_true_false(
            true=true,
            false=false
        )
        if fill_value is not pd.NA:
            errors = "coerce"

        # for each nonmissing element, attempt boolean coercion
        with self.exclude_na(pd.NA, dtype.pandas_type):
            try:
                series = string_to_boolean(
                    self.to_numpy(),
                    true=true,
                    false=false,
                    errors=errors,
                    fill_value=fill_value
                )
            except ValueError as err:
                raise err

            if (
                self.hasnans or
                dtype.nullable or
                (errors == "coerce" and pd.isna(series).any())
            ):
                self.series = pd.Series(
                    series,
                    index=self.index,
                    dtype=dtype.pandas_type
                )
            else:
                self.series = pd.Series(
                    series,
                    index=self.index,
                    dtype=dtype.numpy_type
                )

        return self.series

    def to_integer(
        self,
        dtype: dtype_like = int,
        base: int = 10,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        rounding: str = None,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: raise this to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: confirm dtype is integer-like, errors is valid
        validate_dtype(dtype, int)
        # TODO: validate_integer_base (>= 2 and <= 36)
        validate_errors(errors)

        # for each nonmissing element, attempt integer coercion
        with self.exclude_na(pd.NA, dtype.pandas_type):
            try:
                series = string_to_integer(
                    self.to_numpy(),
                    base=base,
                    errors=errors
                )
            except ValueError as err:
                if base == 10:  # try decimal conversion instead
                    series = DecimalSeries(self.to_decimal(errors=errors))
                    return series.to_integer(
                        dtype=dtype,
                        tol=tol,
                        rounding=rounding,
                        downcast=downcast,
                        errors=errors
                    )

                raise err

            # use IntegerSeries.to_integer() to sort out dtype, downcast
            series = IntegerSeries(
                series=pd.Series(series, index=self.index),
                hasnans=None if errors == "coerce" else False
            )
            self.series = series.to_integer(
                dtype=dtype,
                downcast=downcast,
                errors=errors
            )

        return self.series

    def to_float(
        self,
        dtype: dtype_like = float,
        tol: int | float | complex | decimal.Decimal = 1e-6,
        downcast: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        # TODO: raise this to ConversionSeries
        dtype = resolve_dtype(dtype)

        # DEBUG: confirm dtype is float-like, errors is valid
        validate_dtype(dtype, float)
        validate_errors(errors)

        tol, _ = tolerance(tol)

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
        """TODO"""
        dtype = resolve_dtype(dtype)
        real_tol, imag_tol = tolerance(tol)
        validate_dtype(dtype, complex)
        validate_errors(errors)
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
        """TODO"""
        validate_errors(errors)

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
        """TODO"""
        validate_datetime_format(format, day_first, year_first)
        tz = timezone(tz)
        validate_errors(errors)

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
        """TODO"""
        validate_datetime_format(format, day_first, year_first)
        tz = timezone(tz)
        validate_errors(errors)

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
        """TODO"""
        validate_errors(errors)

        # TODO: can't replicate errors="coerce"

        # TODO: this can silently overflow if unit is too small for value
        # -> cython warnings might be good here.

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
        """TODO"""
        validate_dtype(dtype, "datetime")
        if not (isinstance(dtype, str) and dtype.lower() == "datetime"):
            dtype = resolve_dtype(dtype)
        # validate_dtype(dtype, "datetime")  # TODO: fix in check.py
        validate_datetime_format(format, day_first, year_first)
        tz = timezone(tz)
        validate_errors(errors)

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
        """TODO"""
        validate_errors(errors)

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
        """TODO"""
        validate_errors(errors)

        # implemented as a cython loop
        result = string_to_pytimedelta(self.series.to_numpy(),
                                       as_hours=as_hours, errors=errors)
        return pd.Series(result, index=self.series.index, dtype="O")

    def _to_numpy_timedelta64(
        self,
        as_hours: bool = False,
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        validate_errors(errors)

        # implemented as a cython loop
        result = string_to_numpy_timedelta64(self.series.to_numpy(),
                                             as_hours=as_hours, errors=errors)
        return pd.Series(result, index=self.series.index, dtype="O")

    def to_timedelta(
        self,
        dtype: dtype_like = "timedelta",
        errors: str = "raise"
    ) -> pd.Series:
        """TODO"""
        if dtype != "timedelta":  # can't directly resolve timedelta supertype
            dtype = resolve_dtype(dtype)
        # validate_dtype(dtype, "datetime")  # TODO: fix in check.py
        validate_errors(errors)

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
        """TODO"""
        resolve_dtype(dtype)  # ensures scalar, resolveable
        validate_dtype(dtype, str)

        # force string extension type
        if not pd.api.types.is_extension_array_dtype(dtype):
            dtype = DEFAULT_STRING_DTYPE

        return self.series.astype(dtype, copy=True)
