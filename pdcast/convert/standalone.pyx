
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

from pdcast.util.round cimport Tolerance
from pdcast.util.round import valid_rules
from pdcast.util.structs import as_series
from pdcast.util.time cimport Epoch, epoch_aliases, valid_units
from pdcast.util.type_hints import datetime_like, numeric, type_specifier


class GetDefault:
    """A dummy value signaling ``pdcast`` to get the default value for a
    given argument.
    """

    pass


get_default = GetDefault()


def cast(
    series: Iterable,
    dtype: Optional[type_specifier] = None,
    **kwargs
) -> pd.Series:
    """Cast arbitrary data to the specified data type.

    This function dispatches to one of the
    :ref:`standalone conversions <cast.stand_alone>` listed below based on the
    value of its ``dtype`` argument.

    Parameters
    ----------
    dtype : type specifier
        The target :doc:`type </content/types/types>` for this conversion.
        This can be in any format recognized by :func:`resolve_type`.
    tol : numeric
        The maximum amount of :ref:`precision loss <cast.arguments.tol>` that
        can occur before an error is raised.
    rounding : str
        The :ref:`rounding <cast.arguments.rounding>` rule to use for numeric
        conversions.
    unit : str
        The :ref:`unit <cast.arguments.unit>` to use for datetime/timedelta
        conversions.
    step_size : int
        The :ref:`step size <cast.arguments.step_size>` to use for each
        :ref:`unit <cast.arguments.unit>`.
    tz : str | datetime.tzinfo
        The :ref:`time zone <cast.arguments.tz>` to use for datetime/timedelta
        conversions.
    since : str | datetime-like
        The :ref:`epoch <cast.arguments.since>` to use for datetime/timedelta
        conversions.
    true : str | set[str]
        TODO
    false : str | set[str]
        TODO
    utc : bool
        Controls whether to consider naive datetimes as
        :ref:`UTC timestamps <cast.arguments.utc>` (``True``), or in the
        system's local timezone (``False``).
    day_first : bool
        TODO
    year_first : bool
        TODO
    as_hours : bool
        TODO
    format : str
        TODO
    base : int
        TODO
    downcast : bool
        TODO
    call : Callable
        TODO
    errors : str
        TODO
    **kwargs : dict
        TODO

    Notes
    -----
    See the :ref:`arguments <cast.arguments>` section for more details on how
    to use each argument.
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
    series: Iterable,
    dtype: type_specifier = "bool",
    tol: Optional[numeric] = get_default,
    rounding: Optional[str] = get_default,
    unit: Optional[str] = get_default,
    step_size: Optional[int] = get_default,
    since: Optional[str | datetime_like] = get_default,
    true: Optional[str | Iterable[str]] = get_default,
    false: Optional[str | Iterable[str]] = get_default,
    ignore_case: Optional[bool] = get_default,
    call: Optional[Callable] = get_default,
    errors: Optional[str] = get_default,
    **kwargs
) -> pd.Series:
    """Convert a to boolean representation."""
    # ensure dtype is bool-like
    dtype = validate_dtype(dtype, types.BooleanType)

    # validate default args
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    since = validate_since(since)
    ignore_case = validate_ignore_case(ignore_case)
    true, false = validate_true_false(
        true,
        false,
        ignore_case=ignore_case
    )
    call = validate_call(call)
    errors = validate_errors(errors)

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
        errors=errors,
        **kwargs
    )


def to_integer(
    series: Iterable,
    dtype: type_specifier = "int",
    tol: Optional[numeric] = get_default,
    rounding: Optional[str] = get_default,
    unit: Optional[str] = get_default,
    step_size: Optional[int] = get_default,
    since: Optional[str | datetime_like] = get_default,
    base: Optional[int] = get_default,
    call: Optional[Callable] = get_default,
    downcast: Optional[bool | type_specifier] = get_default,
    errors: Optional[str] = get_default,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    # ensure dtype is int-like
    dtype = validate_dtype(dtype, types.IntegerType)

    # validate args
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    since = validate_since(since)
    base = validate_base(base)
    call = validate_call(call)
    downcast = validate_downcast(downcast)
    errors = validate_errors(errors)

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
        errors=errors,
        **kwargs
    )


def to_float(
    series: Iterable,
    dtype: type_specifier = "float",
    tol: Optional[numeric] = get_default,
    rounding: Optional[str] = get_default,
    unit: Optional[str] = get_default,
    step_size: Optional[int] = get_default,
    since: Optional[str | datetime_like] = get_default,
    call: Optional[Callable] = get_default,
    downcast: Optional[bool | type_specifier] = get_default,
    errors: Optional[str] = get_default,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    # ensure dtype is float-like
    dtype = validate_dtype(dtype, types.FloatType)

    # validate args
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    since = validate_since(since)
    call = validate_call(call)
    downcast = validate_downcast(downcast)
    errors = validate_errors(errors)

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
        errors=errors,
        **kwargs
    )


def to_complex(
    series: Iterable,
    dtype: type_specifier = "complex",
    tol: Optional[numeric] = get_default,
    rounding: Optional[str] = get_default,
    unit: Optional[str] = get_default,
    step_size: Optional[int] = get_default,
    since: Optional[str | datetime_like] = get_default,
    call: Optional[Callable] = get_default,
    downcast: Optional[bool | type_specifier] = get_default,
    errors: Optional[str] = get_default,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to complex representation."""
    # ensure dtype is complex-like
    dtype = validate_dtype(dtype, types.ComplexType)

    # validate args
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    since = validate_since(since)
    call = validate_call(call)
    downcast = validate_downcast(downcast)
    errors = validate_errors(errors)

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
        errors=errors,
        **kwargs
    )


def to_decimal(
    series: Iterable,
    dtype: type_specifier = "decimal",
    tol: Optional[numeric] = get_default,
    rounding: Optional[str] = get_default,
    unit: Optional[str] = get_default,
    step_size: Optional[int] = get_default,
    since: Optional[str | datetime_like] = get_default,
    call: Optional[Callable] = get_default,
    errors: Optional[str] = get_default,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    # ensure dtype is decimal-like
    dtype = validate_dtype(dtype, types.DecimalType)

    # validate args
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    since = validate_since(since)
    call = validate_call(call)
    errors = validate_errors(errors)

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
        errors=errors,
        **kwargs
    )


def to_datetime(
    series: Iterable,
    dtype: type_specifier = "datetime",
    unit: Optional[str] = get_default,
    step_size: Optional[int] = get_default,
    tol: Optional[numeric] = get_default,
    rounding: Optional[str] = get_default,
    since: Optional[str | datetime_like] = get_default,
    tz: Optional[str | tzinfo] = get_default,
    utc: Optional[bool] = get_default,
    format: Optional[str] = get_default,
    day_first: Optional[bool] = get_default,
    year_first: Optional[bool] = get_default,
    call: Optional[Callable] = get_default,
    errors: Optional[str] = get_default,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to datetime representation."""
    # ensure dtype is datetime-like
    dtype = validate_dtype(dtype, types.DatetimeType)

    # validate args
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    since = validate_since(since)
    tz = validate_tz(tz)
    utc = validate_utc(utc)
    format = validate_format(format)
    day_first = validate_day_first(day_first)
    year_first = validate_year_first(year_first)
    call = validate_call(call)
    errors = validate_errors(errors)

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
        errors=errors,
        **kwargs
    )


def to_timedelta(
    series: Iterable,
    dtype: type_specifier = "timedelta",
    unit: Optional[str] = get_default,
    step_size: Optional[int] = get_default,
    tol: Optional[numeric] = get_default,
    rounding: Optional[str] = get_default,
    since: Optional[str | datetime_like] = get_default,
    as_hours: Optional[bool] = get_default,
    call: Optional[Callable] = get_default,
    errors: Optional[str] = get_default,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to timedelta representation."""
    # ensure dtype is timedelta-like
    dtype = validate_dtype(dtype, types.TimedeltaType)

    # validate args
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    since = validate_since(since)
    as_hours = validate_as_hours(as_hours)
    call = validate_call(call)
    errors = validate_errors(errors)

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
        errors=errors,
        **kwargs
    )


def to_string(
    series: Iterable,
    dtype: type_specifier = "string",
    format: Optional[str] = get_default,
    base: Optional[int] = get_default,
    call: Optional[Callable] = get_default,
    errors: Optional[str] = get_default,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # ensure dtype is string-like
    dtype = validate_dtype(dtype, types.StringType)

    # validate args
    base = validate_base(base)
    format = validate_format(format)
    call = validate_call(call)
    errors = validate_errors(errors)

    # delegate to SeriesWrapper.to_string
    return do_conversion(
        series,
        "to_string",
        dtype=dtype,
        base=base,
        format=format,
        call=call,
        errors=errors,
        **kwargs
    )


def to_object(
    series: Iterable,
    dtype: type_specifier = "object",
    call: Optional[Callable] = get_default,
    errors: Optional[str] = get_default,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # ensure dtype is object-like
    dtype = validate_dtype(dtype, types.ObjectType)

    # validate args
    call = validate_call(call)
    errors = validate_errors(errors)

    # delegate to SeriesWrapper.to_object
    return do_conversion(
        series,
        "to_object",
        dtype=dtype,
        call=call,
        errors=errors,
        **kwargs
    )


########################
####    DEFAULTS    ####
########################


class CastDefaults(threading.local):
    """A thread-local configuration object containing default values for
    :func:`cast` operations.

    Notes
    -----
    This object allows users to globally modify the default arguments for
    :func:`cast`\-related functionality.  It also performs some basic
    validation for each argument, ensuring that they are compatible.
    """

    def __init__(self):
        self._vals = {
            "tol": Tolerance(1e-6),
            "rounding": None,
            "unit": "ns",
            "step_size": 1,
            "since": Epoch("utc"),
            "tz": None,
            "true": {"true", "t", "yes", "y", "on", "1"},
            "false": {"false", "f", "no", "n", "off", "0"},
            "ignore_case": True,
            "day_first": False,
            "year_first": False,
            "utc": False,
            "as_hours": False,
            "format": None,
            "base": 0,
            "call": None,
            "downcast": None,
            "errors": "raise"
        }

    #######################
    ####    GETTERS    ####
    #######################

    @property
    def tol(self) -> Tolerance:
        """The maximum amount of precision loss that can occur before an error
        is raised.

        Returns
        -------
        Tolerance
            A ``Tolerance`` object that consists of two ``Decimal`` values, one
            for both the real and imaginary components.  This maintains the
            highest possible precision in both cases.

        Notes
        -----
        Precision loss is defined using a 2-sided window around each of the
        observed values.  The size of this window is directly controlled by
        this argument.  If a conversion causes any value to be coerced outside
        this window, then a ``ValueError`` will be raised.

        This argument only affects numeric conversions.

        Examples
        --------
        The input to this argument must be a positive numeric that is
        coercible to ``Decimal``.

        .. doctest::

            >>> pdcast.cast(1.001, "int", tol=0.01)
            0    1
            dtype: int64
            >>> pdcast.cast(1.001, "int", tol=0)
            Traceback (most recent call last):
                ...
            ValueError: precision loss exceeds tolerance 0 at index [0]

        If a complex value is given, then its real and imaginary components
        will be considered separately.

        .. doctest::

            >>> pdcast.cast(1.001+0.001j, "int", tol=0.01+0.01j)
            0    1
            dtype: int64
            >>> pdcast.cast(1.001+0.001j, "int", tol=0.01+0j)
            Traceback (most recent call last):
                ...
            ValueError: imaginary component exceeds tolerance 0 at index [0]

        This argument also has special behavior around the min/max of bounded
        numerics, like integers and booleans.  If a value would normally
        overflow, but falls within tolerance of these bounds, then it will be
        clipped to fit rather than raise an ``OverflowError``.

        .. doctest::

            >>> pdcast.cast(129, "int8", tol=2)
            0    127
            dtype: int8
            >>> pdcast.cast(129, "int8", tol=0)
            Traceback (most recent call last):
                ...
            OverflowError: values exceed int8 range at index [0]

        Additionally, this argument controls the maximum amount of precision
        loss that can occur when :attr:`downcasting <CastDefaults.downcast>`
        numeric values.

        .. doctest::

            >>> pdcast.cast(1.1, "float", tol=0, downcast=True)
            0    1.1
            dtype: float64
            >>> pdcast.cast(1.1, "float", tol=0.001, downcast=True)
            0    1.099609
            dtype: float16

        Setting this to infinity ignores precision loss entirely.

        .. doctest::

            >>> pdcast.cast(1.5, "int", tol=np.inf)
            0    2
            dtype: int64
            >>> pdcast.cast(np.inf, "int64", tol=np.inf)
            0    9223372036854775807
            dtype: int64

        .. note::

            For integer conversions, this is equivalent to setting
            :attr:`rounding <CastDefaults.rounding>` to ``"half_even"``, with
            additional clipping around the minimum and maximum values.
        """
        return self._vals["tol"]

    @property
    def rounding(self) -> str:
        return self._vals["rounding"]

    @property
    def unit(self) -> str:
        return self._vals["unit"]

    @property
    def step_size(self) -> int:
        return self._vals["step_size"]

    @property
    def since(self) -> np.datetime64:
        return self._vals["since"]

    @property
    def tz(self) -> pytz.BaseTzInfo:
        return self._vals["tz"]

    @property
    def true(self) -> set:
        return self._vals["true"]

    @property
    def false(self) -> set:
        return self._vals["false"]

    @property
    def ignore_case(self) -> bool:
        return self._vals["ignore_case"]

    @property
    def day_first(self) -> bool:
        return self._vals["day_first"]

    @property
    def year_first(self) -> bool:
        return self._vals["year_first"]

    @property
    def utc(self) -> bool:
        return self._vals["utc"]

    @property
    def as_hours(self) -> bool:
        return self._vals["as_hours"]

    @property
    def format(self) -> str:
        return self._vals["format"]

    @property
    def base(self) -> int:
        return self._vals["base"]

    @property
    def call(self) -> Callable:
        return self._vals["call"]

    @property
    def downcast(self) -> types.CompositeType:
        return self._vals["downcast"]

    @property
    def errors(self) -> str:
        return self._vals["errors"]

    #######################
    ####    SETTERS    ####
    #######################

    @tol.setter
    def tol(self, val: numeric) -> None:
        self._vals["tol"] = validate_tol(val)

    @rounding.setter
    def rounding(self, val: str) -> None:
        self._vals["rounding"] = validate_rounding(val)

    @unit.setter
    def unit(self, val: str) -> None:
        self._vals["unit"] = validate_unit(val)

    @step_size.setter
    def step_size(self, val: int) -> None:
        self._vals["step_size"] = validate_step_size(val)

    @since.setter
    def since(self, val: str | datetime_like) -> None:
        self._vals["since"] = validate_since(val)

    @tz.setter
    def tz(self, val: str | tzinfo) -> None:
        self._vals["tz"] = validate_tz(val)

    @true.setter
    def true(self, val: str | set) -> None:
        try:
            self._vals["true"], _ = validate_true_false(
                val,
                self.false,
                ignore_case=self.ignore_case
            )
        except Exception as err:
            raise TypeError(f"`true` must contain only strings")

    @false.setter
    def false(self, val: str | set) -> None:
        try:
            _, self._vals["false"] = validate_true_false(
                self.true,
                val,
                ignore_case=self.ignore_case
            )
        except Exception as err:
            raise TypeError(f"`true` must contain only strings")

    @ignore_case.setter
    def ignore_case(self, val: bool) -> None:
        self._vals["ignore_case"] = validate_ignore_case(val)

    @day_first.setter
    def day_first(self, val: bool) -> None:
        self._vals["day_first"] = validate_day_first(val)

    @year_first.setter
    def year_first(self, val: bool) -> None:
        self._vals["year_first"] = validate_year_first(val)

    @utc.setter
    def utc(self, val: bool) -> None:
        self._vals["utc"] = validate_utc(val)

    @as_hours.setter
    def as_hours(self, val: bool) -> None:
        self._vals["as_hours"] = validate_as_hours(val)

    @format.setter
    def format(self, val: str) -> None:
        self._vals["format"] = validate_format(val)

    @base.setter
    def base(self, val: int) -> None:
        self._vals["base"] = validate_base(val)

    @call.setter
    def call(self, val: Callable) -> None:
        self._vals["call"] = validate_call(val)

    @downcast.setter
    def downcast(self, val: bool | type_specifier) -> None:
        self._vals["downcast"] = validate_downcast(val)

    @errors.setter
    def errors(self, val: str) -> None:
        self._vals["errors"] = validate_errors(val)


defaults = CastDefaults()

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


def validate_tol(val) -> Tolerance:
    """Ensure that a tolerance is a positive numeric."""
    if isinstance(val, GetDefault):
        return defaults.tol

    try:
        return Tolerance(val)
    except Exception as err:
        raise ValueError(f"invalid tol: {val}") from err


def validate_rounding(val) -> str:
    """Ensure that a rounding rule is valid."""
    if isinstance(val, GetDefault):
        return defaults.rounding

    if val not in valid_rules:
        raise ValueError(
            f"`rounding` must be one of {valid_rules}, not {repr(val)}"
        )
    return val


def validate_unit(val) -> str:
    """Ensure that a time unit is valid."""
    if isinstance(val, GetDefault):
        return defaults.unit

    if val not in valid_units:
        raise ValueError(
            f"`unit` must be one of {valid_units}, not {repr(val)}"
        )
    return val


def validate_step_size(val) -> int:
    """Ensure that a step size is an integer >= 1."""
    if isinstance(val, GetDefault):
        return defaults.step_size

    if not isinstance(val, int) or val < 1:
        raise ValueError(f"`step_size` must be an integer >= 1, not {val}")
    return val


def validate_since(val) -> Epoch:
    """Ensure that a datetime epoch is valid."""
    if isinstance(val, GetDefault):
        return defaults.since

    try:
        # convert datetime-like string input
        if isinstance(val, str) and val not in epoch_aliases:
            val = cast(val, "datetime")
            if len(val) != 1:
                raise ValueError(f"`since` must be scalar")
            val = val[0]

        return Epoch(val)

    except Exception as err:
        raise ValueError(f"invalid epoch: {val}") from err


def validate_tz(val) -> pytz.BaseTzInfo:
    """Ensure that a time zone is IANA-recognized."""
    if isinstance(val, GetDefault):
        return defaults.tz

    # get system local timezone
    if val == "local":
        return pytz.timezone(tzlocal.get_localzone_name())
    return None if val is None else pytz.timezone(val)


def validate_true_false(true, false, ignore_case) -> tuple[set[str], set[str]]:
    """Ensure that a pair of boolean comparison string sets are valid."""

    def as_set(val: str | set[str]) -> set[str]:
        if isinstance(true, str):
            return {true}
        if hasattr(true, "__iter__"):
            if not all(isinstance(x, str) for x in true):
                raise TypeError(
                    f"input must consist only of strings: {true}"
                )
            return set(true)
        return {str(true)}

    # convert to set
    true = defaults.true if isinstance(true, GetDefault) else as_set(true)
    false = defaults.false if isinstance(false, GetDefault) else as_set(false)

    # ensure true, false are disjoint
    if not true.isdisjoint(false):
        intersection = true.intersection(false)
        err_msg = f"`true` and `false` must be disjoint "
        if len(intersection) == 1:  # singular
            err_msg += (
                f"({repr(intersection.pop())} is present in both sets)"
            )
        else:  # plural
            err_msg += f"({intersection} are present in both sets)"
        raise ValueError(err_msg)

    # apply ignore_case logic to true, false
    if isinstance(ignore_case, GetDefault):
        ignore_case = defaults.ignore_case
    if ignore_case:
        true = {x.lower() for x in true}
        false = {x.lower() for x in false}

    return true, false


def validate_ignore_case(val) -> bool:
    """Ensure that an `ignore_case` flag is valid."""
    if isinstance(val, GetDefault):
        return defaults.ignore_case

    return bool(val)


def validate_day_first(val) -> bool:
    """Ensure that a `day_first` flag is valid."""
    if isinstance(val, GetDefault):
        return defaults.day_first

    return bool(val)


def validate_year_first(val) -> bool:
    """Ensure that a `year_first` flag is valid."""
    if isinstance(val, GetDefault):
        return defaults.year_first

    return bool(val)


def validate_utc(val) -> bool:
    """Ensure that a `utc` flag is valid."""
    if isinstance(val, GetDefault):
        return defaults.utc

    return bool(val)


def validate_as_hours(val) -> bool:
    """Ensure that an `as_hours` flag is valid."""
    if isinstance(val, GetDefault):
        return defaults.as_hours

    return bool(val)


def validate_format(val) -> str:
    """Ensure that a format string is valid."""
    if isinstance(val, GetDefault):
        return defaults.format

    if not isinstance(val, str):
        raise TypeError(f"`format` must be a string, not {val}")

    return val


def validate_base(val) -> int:
    """Ensure that an integer base is valid for string conversions."""
    if isinstance(val, GetDefault):
        return defaults.base

    if val != 0 and not 2 <= val <= 36:
        raise ValueError(f"`base` must be 0 or >= 2 and <= 36, not {val}")

    return val


def validate_call(val) -> Callable:
    """Ensure that a callable argument is, in fact, callable."""
    if isinstance(val, GetDefault):
        return defaults.call

    if not callable(val):
        raise TypeError(f"`call` must be callable, not {val}")

    return val


def validate_downcast(val) -> types.CompositeType:
    """Ensure that a downcast specifier is valid."""
    if isinstance(val, GetDefault):
        return defaults.downcast

    # convert booleans into CompositeTypes
    if isinstance(val, bool):
        # empty set is truthy, `None` is falsy
        return types.CompositeType() if val else None
    return resolve.resolve_type([val])


def validate_errors(val) -> str:
    """Ensure that an error-handling rule is valid."""
    if isinstance(val, GetDefault):
        return defaults.errors

    valid = ("raise", "coerce", "ignore")
    if val not in valid:
        raise ValueError(
            f"`errors` must be one of {valid}, not {repr(val)}"
        )
    return val
