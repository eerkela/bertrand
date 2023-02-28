from datetime import tzinfo
import decimal
from functools import wraps
import inspect
from typing import Any, Callable, Iterable, Iterator

cimport cython
cimport numpy as np
import numpy as np
import pandas as pd
import pytz
import tzlocal

cimport pdcast.detect as detect
import pdcast.detect as detect
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve
cimport pdcast.types as types
import pdcast.types as types

from pdcast.util.error import shorten_list
from pdcast.util.round cimport Tolerance
from pdcast.util.round import valid_rules
from pdcast.util.structs import as_series
from pdcast.util.time cimport Epoch, epoch_aliases, valid_units
from pdcast.util.type_hints import (
    array_like, datetime_like, numeric, type_specifier
)


# TODO: SparseType works, but not in all cases.
# -> pd.NA disallows non-missing fill values
# -> Timestamps must be sparsified manually by converting to object and then
# to sparse
# -> Timedeltas just don't work at all.  astype() rejects pd.SparseDtype("m8")
# entirely.
# -> SeriesWrappers should unwrap sparse/categorical series during dispatch.


# TODO: have to account for empty series in each conversion.


#######################
####   DEFAULTS    ####
#######################


cdef class CastDefaults:

    cdef:
        bint _as_hours
        unsigned char _base
        bint _day_first
        types.CompositeType _downcast
        str _errors
        set _false
        str _format
        bint _ignore_case
        str _rounding
        object _since
        unsigned int _step_size
        Tolerance _tol
        object _tz
        set _true
        str _unit
        bint _utc
        bint _year_first

    def __init__(self):
        self._as_hours = False
        self._base = 0
        self._downcast = None
        self._errors = "raise"
        self._false = {"false", "f", "no", "n", "off", "0"}
        self._ignore_case = True
        self._rounding = None
        self._since = Epoch("utc")
        self._step_size = 1
        self._tol = Tolerance(1e-6)
        self._tz = None
        self._true = {"true", "t", "yes", "y", "on", "1"}
        self._unit = "ns"

    @property
    def as_hours(self) -> bool:
        return self._as_hours

    @as_hours.setter
    def as_hours(self, val: bool) -> None:
        if val is None:
            raise ValueError(f"default `as_hours` cannot be None")
        self._as_hours = validate_as_hours(val)

    @property
    def base(self) -> int:
        return self._base

    @base.setter
    def base(self, val: int) -> None:
        if val is None:
            raise ValueError(f"default `base` cannot be None")
        self._base = validate_base(val)

    @property
    def day_first(self) -> bool:
        return self._day_first

    @day_first.setter
    def day_first(self, val: bool) -> None:
        if val is None:
            raise ValueError(f"default `day_first` cannot be None")
        self._day_first = validate_day_first(val)

    @property
    def downcast(self) -> types.CompositeType:
        return self._downcast

    @downcast.setter
    def downcast(self, val: bool | type_specifier) -> None:
        if val is None:
            raise ValueError(f"default `downcast` cannot be None")
        self._downcast = validate_downcast(val)

    @property
    def errors(self) -> str:
        return self._errors

    @errors.setter
    def errors(self, val: str) -> None:
        if val is None:
            raise ValueError(f"default `errors` cannot be None")
        self._errors = validate_errors(val)

    @property
    def false(self) -> set:
        return self._false

    @false.setter
    def false(self, val: str | set[str]) -> None:
        if val is None:
            raise ValueError(f"default `false` cannot be None")
        self._false = validate_false(val)

    @property
    def format(self) -> str:
        return self._format

    @format.setter
    def format(self, val: str) -> None:
        if val is None:
            raise ValueError(f"default `format` cannot be None")
        self._format = validate_format(val)

    @property
    def ignore_case(self) -> bool:
        return self._ignore_case

    @ignore_case.setter
    def ignore_case(self, val: bool) -> None:
        if val is None:
            raise ValueError(f"default `ignore_case` cannot be None")
        self._ignore_case = validate_ignore_case(val)

    @property
    def rounding(self) -> str:
        return self._rounding

    @rounding.setter
    def rounding(self, val: str) -> None:
        self._rounding = validate_rounding(val)

    @property
    def since(self) -> np.datetime64:
        return self._since

    @since.setter
    def since(self, val: str | datetime_like) -> None:
        if val is None:
            raise ValueError(f"default `since` cannot be None")
        self._since = validate_since(val)

    @property
    def step_size(self) -> int:
        return self._step_size

    @step_size.setter
    def step_size(self, val: int) -> None:
        if val is None:
            raise ValueError(f"default `step_size` cannot be None")
        self._step_size = validate_step_size(val)

    @property
    def tol(self) -> Tolerance:
        return self._tol

    @tol.setter
    def tol(self, val: numeric) -> None:
        if val is None:
            raise ValueError(f"default `tol` cannot be None")
        self._tol = validate_tol(val)

    @property
    def true(self) -> set:
        return self._true

    @true.setter
    def true(self, val: str | set[str]) -> None:
        if val is None:
            raise ValueError(f"default `true` cannot be None")
        self._true = validate_true(val)

    @property
    def tz(self) -> pytz.BaseTzInfo:
        return self._tz

    @tz.setter
    def tz(self, val: str | tzinfo) -> None:
        self._tz = validate_timezone(val)

    @property
    def unit(self) -> str:
        return self._unit

    @unit.setter
    def unit(self, val: str) -> None:
        if val is None:
            raise ValueError(f"default `unit` cannot be None")
        self._unit = validate_unit(val)

    @property
    def utc(self) -> bool:
        return self._utc

    @utc.setter
    def utc(self, val: bool) -> None:
        if val is None:
            raise ValueError(f"default `utc` cannot be None")
        self._utc = validate_utc(val)

    @property
    def year_first(self) -> bool:
        return self._year_first

    @year_first.setter
    def year_first(self, val: bool) -> None:
        if val is None:
            raise ValueError(f"default `year_first` cannot be None")
        self._year_first = validate_year_first(val)


defaults = CastDefaults()


def validate_as_hours(val: bool) -> bool:
    if val is None:
        return defaults.as_hours
    return val


def validate_base(val: int) -> int:
    if val is None:
        return defaults.base

    if val != 0 and not 2 <= val <= 36:
        raise ValueError(f"`base` must be >= 2 and <= 36, or 0")
    return val


def validate_call(val: Callable) -> Callable:
    if val is not None and not callable(val):
        raise ValueError(f"`call` must be callable, not {val}")


def validate_day_first(val: bool) -> bool:
    if val is None:
        return defaults.day_first
    return val


def validate_downcast(
    val: bool | type_specifier
) -> types.CompositeType:
    if val is None:
        return defaults.downcast

    # convert booleans into CompositeTypes: empty set is truthy, None is false
    if isinstance(val, bool):
        return types.CompositeType() if val else None

    return resolve.resolve_type({val})


def validate_dtype(
    dtype: type_specifier,
    supertype: type_specifier = None
) -> types.ScalarType:
    """Resolve a type specifier and reject it if it is composite or not a
    subtype of the given supertype.
    """
    dtype = resolve.resolve_type(dtype)
    if isinstance(dtype, types.CompositeType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")

    if supertype is not None:
        supertype = resolve.resolve_type(supertype)
        if not dtype.unwrap().is_subtype(supertype):
            raise ValueError(f"`dtype` must be {supertype}-like, not {dtype}")

    return dtype


def validate_since(val: str | datetime_like) -> Epoch:
    if val is None:
        return defaults.since

    if isinstance(val, str) and val not in epoch_aliases:
        val = cast(val, "datetime")
        if len(val) != 1:
            raise ValueError(f"`since` must be scalar")
        val = val[0]

    return Epoch(val)


def validate_errors(val: str) -> str:
    if val is None:
        return defaults.errors

    valid = ("raise", "coerce", "ignore")
    if val not in valid:
        raise ValueError(f"`errors` must be one of {valid}, not {repr(val)}")
    return val


def validate_false(val: str | set[str]) -> set[str]:
    if val is None:
        return defaults.false

    if isinstance(val, str):
        return {val}
    return set(val)


def validate_format(val: str) -> str:
    if val is None:
        return defaults.format
    return val


def validate_ignore_case(val: bool) -> bool:
    if val is None:
        return defaults.ignore_case
    return val


def validate_rounding(val: str) -> str:
    if val is None:
        return defaults.rounding
    if val not in valid_rules:
        raise ValueError(
            f"`rounding` must be one of {valid_rules}, not {repr(val)}"
        )
    return val


def validate_step_size(val: int) -> int:
    if val is None:
        return defaults.step_size

    if val < 1:
        raise ValueError(f"`step_size` cannot be negative")
    return val


def validate_timezone(val: str | tzinfo) -> pytz.BaseTzInfo:
    if val == "local":
        return pytz.timezone(tzlocal.get_localzone_name())
    return None if val is None else pytz.timezone(val)


def validate_tol(val: numeric) -> Tolerance:
    if val is None:
        return defaults.tol
    return Tolerance(val)


def validate_true(val: str | set[str]) -> set[str]:
    if val is None:
        return defaults.true

    if isinstance(val, str):
        return {val}
    return set(val)


def validate_unit(val: str) -> str:
    if val is None:
        return defaults.unit
    if val not in valid_units:
        raise ValueError(
            f"`unit` must be one of {valid_units}, not {repr(val)}"
        )
    return val


def validate_utc(val: bool) -> bool:
    if val is None:
        return defaults.utc
    return val


def validate_year_first(val: bool) -> bool:
    if val is None:
        return defaults.year_first
    return val


######################
####    PUBLIC    ####
######################


def cast(
    series: Iterable,
    dtype: type_specifier = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to the given data type."""
    # if no target is given, default to series type
    if dtype is None:
        series = as_series(series)
        dtype = detect.detect_type(series)

    # validate dtype
    dtype = validate_dtype(dtype)

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
    """Convert arbitrary data to boolean representation."""
    # validate args
    dtype = validate_dtype(dtype, types.BooleanType)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    since = validate_since(since)
    true = validate_true(true)
    false = validate_false(false)
    ignore_case = validate_ignore_case(ignore_case)
    call = validate_call(call)
    errors = validate_errors(errors)

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
    dtype = validate_dtype(dtype, types.IntegerType)
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
    dtype = validate_dtype(dtype, types.FloatType)
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
    dtype = validate_dtype(dtype, types.ComplexType)
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
    dtype = validate_dtype(dtype, types.DecimalType)
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
    dtype = validate_dtype(dtype, types.DatetimeType)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    since = validate_since(since)
    tz = validate_timezone(tz)
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
    dtype = validate_dtype(dtype, types.TimedeltaType)
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
    dtype = validate_dtype(dtype, types.StringType)
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
    dtype = validate_dtype(dtype, types.ObjectType)
    call = validate_call(call)
    errors = validate_errors(errors)

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


######################
####    PRIVATE   ####
######################


cdef class SeriesWrapper:
    """Wrapper for type-aware pd.Series objects.

    Implements a dynamic wrapper according to the Gang of Four's Decorator
    Pattern (not to be confused with python decorators).
    """

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        element_type: types.BaseType = None
    ):
        self.series = series
        self.hasnans = hasnans
        self.element_type = element_type

    ##########################
    ####    PROPERTIES    ####
    ##########################

    @property
    def element_type(self) -> types.BaseType:
        if self._element_type is None:
            self._element_type = detect.detect_type(self.series)
        return self._element_type

    @element_type.setter
    def element_type(self, val: type_specifier) -> None:
        if val is not None:
            val = resolve.resolve_type(val)
            if (
                isinstance(val, types.CompositeType) and
                getattr(val.index, "shape", None) != self.shape
            ):
                raise ValueError(
                    f"`element_type.index` must have the same shape as the "
                    f"series it describes"
                )
        self._element_type = val

    @property
    def hasnans(self) -> bool:
        """Check whether a wrapped series contains missing values."""
        if self._hasnans is None:
            self._hasnans = self.isna().any()
        return self._hasnans

    @hasnans.setter
    def hasnans(self, val: bool) -> None:
        self._hasnans = val

    @property
    def imag(self) -> SeriesWrapper:
        """Get the imaginary component of a wrapped series."""
        # NOTE: np.imag() fails when applied over object arrays that may
        # contain complex values.  In this case, we reduce it to a loop.
        if pd.api.types.is_object_dtype(self.series):
            result = np.frompyfunc(np.imag, 1, 1)(self.series)
        else:
            result = pd.Series(np.imag(self.series), index=self.index)

        target = getattr(
            self.element_type,
            "equiv_float",
            self.element_type
        )
        return SeriesWrapper(
            result,
            hasnans=self._hasnans,
            element_type=target
        )

    @property
    def real(self) -> SeriesWrapper:
        """Get the real component of a wrapped series."""
        # NOTE: np.real() fails when applied over object arrays that may
        # contain complex values.  In this case, we reduce it to a loop.
        if pd.api.types.is_object_dtype(self.series):
            result = np.frompyfunc(np.real, 1, 1)(self.series)
        else:
            result = pd.Series(np.real(self.series), index=self.index)

        target = getattr(
            self.element_type,
            "equiv_float",
            self.element_type
        )
        return SeriesWrapper(
            result,
            hasnans=self._hasnans,
            element_type=target
        )

    @property
    def series(self) -> pd.Series:
        return self._series

    @series.setter
    def series(self, val: pd.Series) -> None:
        if not isinstance(val, pd.Series):
            raise TypeError(
                f"`series` must be a pandas Series object, not {type(val)}"
            )
        self._series = val
        self._max = None
        self._min = None

    ###############################
    ####    WRAPPED METHODS    ####
    ###############################

    def argmax(self, *args, **kwargs):
        """Alias for IntegerSeries.max()."""
        return self.max(*args, **kwargs)

    def argmin(self, *args, **kwargs):
        """Alias for IntegerSeries.min()."""
        return self.min(*args, **kwargs)

    def astype(
        self,
        dtype: type_specifier,
        errors: str = "raise"
    ) -> SeriesWrapper:
        """`astype()` equivalent for SeriesWrapper instances that works for
        object-based type specifiers.
        """
        dtype = resolve.resolve_type(dtype)
        if isinstance(dtype, types.CompositeType):
            raise ValueError(f"`dtype` must be atomic, not {repr(dtype)}")

        # apply dtype.type_def elementwise if not astype-compliant
        if dtype.unwrap().dtype == np.dtype("O"):
            result = self.apply_with_errors(
                call=dtype.type_def,
                errors=errors
            )
            result.element_type=dtype
            return result

        # default to pd.Series.astype()
        target = dtype.dtype
        if (
            pd.api.types.is_object_dtype(self.series) and
            hasattr(target, "numpy_dtype")
        ):
            # NOTE: pandas doesn't like converting arbitrary objects to
            # nullable extension types.  Luckily, numpy has no such problem,
            # and SeriesWrapper automatically filters out NAs.
            result = self.series.astype(target.numpy_dtype).astype(target)
        else:
            result = self.series.astype(target)

        return SeriesWrapper(
            result,
            hasnans=self.hasnans,
            element_type=dtype
        )

    def copy(self, *args, **kwargs) -> SeriesWrapper:
        """Duplicate a SeriesWrapper."""
        return SeriesWrapper(
            self.series.copy(*args, **kwargs),
            hasnans=self._hasnans,
            element_type=self._element_type
        )

    def max(self, *args, **kwargs):
        """A cached version of pd.Series.max()."""
        if self._max is None:
            self._max = self.series.max(*args, **kwargs)
        return self._max

    def min(self, *args, **kwargs):
        """A cached version of pd.Series.min()."""
        if self._min is None:
            self._min = self.series.min(*args, **kwargs)
        return self._min

    ###########################
    ####    NEW METHODS    ####
    ###########################

    def __enter__(self) -> SeriesWrapper:
        # record shape
        self._orig_shape = self.series.shape

        # normalize index
        if not isinstance(self.series.index, pd.RangeIndex):
            self._orig_index = self.series.index
            self.series.index = pd.RangeIndex(0, self._orig_shape[0])

        # drop missing values
        is_na = self.isna()
        self.hasnans = is_na.any()
        if self._hasnans:
            self.series = self.series[~is_na]

        # detect element type if not set manually
        if self._element_type is None:
            self.element_type = detect.detect_type(self.series, skip_na=False)

        # unwrap sparse/categorical series
        if isinstance(self.element_type, types.AdapterType):
            self._orig_type = self.element_type
            self.element_type = self.element_type.unwrap()

            # NOTE: this is a pending deprecation shim.  In a future version
            # of pandas, astype() from a sparse to non-sparse dtype will return
            # a non-sparse series.  Currently, it returns a sparse equivalent.
            # When this behavior changes, delete this block.
            if isinstance(self._orig_type, types.SparseType):
                self.series = self.series.sparse.to_dense()

            self.series = self.rectify().series

        # rectify and enter context block
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        # replace missing values, aligning on index
        if self.hasnans:
            result = pd.Series(
                np.full(
                    self._orig_shape,
                    getattr(self.element_type, "na_value", pd.NA),
                    dtype="O"
                ),
                dtype=self.dtype
            )
            result.update(self.series)
            self.series = result

        # replace original index
        if self._orig_index is not None:
            self.series.index = self._orig_index
            self._orig_index = None

        # replace adapters if element_type is unchanged
        if (
            self._orig_type is not None and
            self._orig_type.unwrap() == self.element_type
        ):
            if hasattr(self._orig_type, "levels"):  # update levels
                self._orig_type = self._orig_type.replace(levels=None)
            result = self._orig_type.apply_adapters(self)
            self.series = result.series
            self.element_type = result.element_type

    def __getattr__(self, name: str) -> Any:
        # dynamically re-wrap series outputs
        attr = getattr(self.series, name)

        # method - return a decorator
        if callable(attr):

            @wraps(attr)
            def wrapper(*args, **kwargs):
                result = attr(*args, **kwargs)
                if isinstance(result, pd.Series):
                    return SeriesWrapper(result, hasnans=self._hasnans)
                return result

            return wrapper

        # attribute
        if isinstance(attr, pd.Series):
            return SeriesWrapper(attr, hasnans=self._hasnans)
        return attr

    def apply_with_errors(
        self,
        call: Callable,
        errors: str = "raise"
    ) -> SeriesWrapper:
        """Apply `call` over the series, applying the specified error handling
        rule at each index.
        """
        result, has_errors, index = _apply_with_errors(
            self.series.to_numpy(dtype="O"),
            call=call,
            errors=errors
        )
        result = pd.Series(result, index=self.index, dtype="O")
        if has_errors:
            result = result[~index]
        return SeriesWrapper(result, hasnans=has_errors or self._hasnans)

    def boundscheck(
        self,
        dtype: types.AtomicType,
        errors: str
    ) -> tuple[SeriesWrapper, types.AtomicType]:
        """Ensure that a series does not overflow past the allowable range of the
        given AtomicType.  If overflow is detected, attempt to upcast the
        AtomicType to fit or coerce the series if directed.
        """
        series = self
        min_val = series.min()
        max_val = series.max()
        if min_val < dtype.min or max_val > dtype.max:
            # attempt to upcast dtype to fit series
            try:
                return series, dtype.upcast(series)
            except OverflowError:
                pass

            # continue with OverflowError
            index = (series < dtype.min) | (series > dtype.max)
            if errors == "coerce":
                series = series[~index]
                series.hasnans = True
            else:
                raise OverflowError(
                    f"values exceed {dtype} range at index "
                    f"{shorten_list(series[index].index.values)}"
                )

        return series, dtype

    def dispatch(
        self,
        endpoint: str,
        submap: dict,
        original: Callable,
        *args,
        **kwargs
    ) -> SeriesWrapper:
        """For every type that is present in the series, invoke the named
        endpoint.
        """
        # series is homogenous
        if isinstance(self.element_type, types.AtomicType):
            # check for corresponding AtomicType method
            call = submap.get(type(self.element_type.unwrap()), None)
            if call is not None:
                return call(self.element_type, self, *args, **kwargs)

            # fall back to pandas implementation
            pars = inspect.signature(original).parameters
            kwargs = {k: v for k, v in kwargs.items() if k in pars}
            result = original(*args, **kwargs)
            if isinstance(result, pd.Series):
                return SeriesWrapper(result, hasnans=self._hasnans)
            return result

        # series is composite
        groups = self.series.groupby(self.element_type.index, sort=False)
        if original is not None:  # introspect before grouping
            pars = inspect.signature(original).parameters

        def transform(grp):
            # check for corresponding AtomicType method
            call = submap.get(type(grp.name.unwrap()), None)
            if call is not None:
                result = call(
                    grp.name,
                    SeriesWrapper(grp, hasnans=self._hasnans),
                    *args,
                    **kwargs
                )
                self.hasnans = self._hasnans or result._hasnans
                result = result.series

            # fall back to pandas implementation
            else:
                kw = {k: v for k, v in kwargs.items() if k in pars}
                result = original(*args, **kw)

            # ensure final index is a subset of original index
            if not result.index.difference(grp.index).empty:
                raise RuntimeError(
                    f"index mismatch: output index must be a subset of input "
                    f"index for group {repr(str(grp.name))}"
                )
            return result

        result = groups.transform(transform)
        return SeriesWrapper(result, hasnans=self._hasnans)

    def isinf(self) -> SeriesWrapper:
        """Return a boolean mask indicating the position of infinities in the
        series.
        """
        return self.isin([np.inf, -np.inf])

    def rectify(self) -> SeriesWrapper:
        """Convert an improperly-formatted object series to a standardized
        numpy/pandas data type.
        """
        if self.series.dtype != self.element_type.dtype:
            return self.astype(self.element_type)
        return self

    def within_tol(self, other, tol: numeric) -> array_like:
        """Check if every element of a series is within tolerance of another
        series.
        """
        if not tol:  # fastpath if tolerance=0
            return self == other
        return ~((self - other).abs() > tol)

    ##########################
    ####    ARITHMETIC    ####
    ##########################

    # NOTE: math operators can change the element_type of a SeriesWrapper in
    # unexpected ways.  If you know the final element_type ahead of time, set
    # it manually after the operation by assigning to the result's
    # .element_type field.  Otherwise it will automatically be forgotten and
    # regenerated when you next request it, which can be expensive if the final
    # series has dtype="O".

    def __abs__(self) -> SeriesWrapper:
        return SeriesWrapper(abs(self.series), hasnans=self._hasnans)

    def __add__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series + other, hasnans=self._hasnans)

    def __and__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series & other, hasnans=self._hasnans)

    def __divmod__(self, other) -> SeriesWrapper:
        return SeriesWrapper(divmod(self.series, other), hasnans=self._hasnans)

    def __eq__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series == other, hasnans=self._hasnans)

    def __floordiv__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series // other, hasnans=self._hasnans)

    def __ge__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series >= other, hasnans=self._hasnans)

    def __gt__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series > other, hasnans=self._hasnans)

    def __iadd__(self, other) -> SeriesWrapper:
        self.series += other
        self._element_type = None
        return self

    def __iand__(self, other) -> SeriesWrapper:
        self.series &= other
        self._element_type = None
        return self

    def __idiv__(self, other) -> SeriesWrapper:
        self.series /= other
        self._element_type = None
        return self

    def __ifloordiv__(self, other) -> SeriesWrapper:
        self.series //= other
        self._element_type = None
        return self

    def __ilshift__(self, other) -> SeriesWrapper:
        self.series <<= other
        self._element_type = None
        return self

    def __imod__(self, other) -> SeriesWrapper:
        self.series %= other
        self._element_type = None
        return self

    def __imul__(self, other) -> SeriesWrapper:
        self.series *= other
        self._element_type = None
        return self

    def __invert__(self) -> SeriesWrapper:
        return SeriesWrapper(~self.series, hasnans=self._hasnans)

    def __ior__(self, other) -> SeriesWrapper:
        self.series |= other
        self._element_type = None
        return self

    def __ipow__(self, other) -> SeriesWrapper:
        self.series **= other
        self._element_type = None
        return self

    def __irshift__(self, other) -> SeriesWrapper:
        self.series >>= other
        self._element_type = None
        return self

    def __isub__(self, other) -> SeriesWrapper:
        self.series -= other
        self._element_type = None
        return self

    def __ixor__(self, other) -> SeriesWrapper:
        self.series ^= other
        self._element_type = None
        return self

    def __le__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series <= other, hasnans=self._hasnans)

    def __lshift__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series << other, hasnans=self._hasnans)

    def __lt__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series < other, hasnans=self._hasnans)

    def __mod__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series % other, hasnans=self._hasnans)

    def __mul__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series * other, hasnans=self._hasnans)

    def __ne__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series != other, hasnans=self._hasnans)

    def __neg__(self) -> SeriesWrapper:
        return SeriesWrapper(-self.series, hasnans=self._hasnans)

    def __or__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series | other, hasnans=self._hasnans)

    def __pos__(self) -> SeriesWrapper:
        return SeriesWrapper(+self.series, hasnans=self._hasnans)

    def __pow__(self, other, mod) -> SeriesWrapper:
        return SeriesWrapper(
            self.series.__pow__(other, mod),
            hasnans=self._hasnans
        )

    def __radd__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other + self.series, hasnans=self._hasnans)

    def __rand__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other & self.series, hasnans=self._hasnans)

    def __rdivmod__(self, other) -> SeriesWrapper:
        return SeriesWrapper(divmod(other, self.series), hasnans=self._hasnans)

    def __rfloordiv__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other // self.series, hasnans=self._hasnans)

    def __rlshift__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other << self.series, hasnans=self._hasnans)

    def __rmod__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other % self.series, hasnans=self._hasnans)

    def __rmul__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other * self.series, hasnans=self._hasnans)

    def __ror__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other | self.series, hasnans=self._hasnans)

    def __rpow__(self, other, mod) -> SeriesWrapper:
        return SeriesWrapper(
            self.series.__rpow__(other, mod),
            hasnans=self._hasnans
        )

    def __rrshift__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other >> self.series, hasnans=self._hasnans)

    def __rshift__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other >> self.series, hasnans=self._hasnans)

    def __rsub__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other - self.series, hasnans=self._hasnans)

    def __rtruediv__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other / self.series, hasnans=self._hasnans)

    def __rxor__(self, other) -> SeriesWrapper:
        return SeriesWrapper(other ^ self.series, hasnans=self._hasnans)

    def __sub__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series - other, hasnans=self._hasnans)

    def __truediv__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series / other, hasnans=self._hasnans)

    def __xor__(self, other) -> SeriesWrapper:
        return SeriesWrapper(self.series ^ other, hasnans=self._hasnans)

    #############################
    ####    MAGIC METHODS    ####
    #############################

    def __contains__(self, val) -> bool:
        return val in self.series

    def __delitem__(self, key) -> None:
        del self.series[key]
        self._element_type = None

    def __dir__(self) -> list[str]:
        # direct SeriesWrapper attributes
        result = dir(type(self))
        result += list(self.__dict__.keys())

        # pd.Series attributes
        result += [x for x in dir(self.series) if x not in result]

        # dispatched attributes
        result += [x for x in types.AtomicType.registry.dispatch_map]

        return result

    def __float__(self) -> float:
        return float(self.series)

    def __getitem__(self, key) -> Any:
        result = self.series[key]

        # slicing: re-wrap result
        if isinstance(result, pd.Series):
            # slicing can change element_type of result, but only if composite
            if isinstance(self._element_type, types.CompositeType):
                element_type = None
            else:
                element_type = self._element_type

            # wrap
            result = SeriesWrapper(
                result,
                hasnans=self._hasnans,
                element_type=element_type
            )

        return result

    def __hex__(self) -> hex:
        return hex(self.series)

    def __int__(self) -> int:
        return int(self.series)

    def __iter__(self) -> Iterator:
        return self.series.__iter__()

    def __len__(self) -> int:
        return len(self.series)

    def __next__(self) -> Any:
        return self.series.__next__()

    def __oct__(self) -> oct:
        return oct(self.series)

    def __repr__(self) -> str:
        return repr(self.series)

    def __setitem__(self, key, val) -> None:
        self.series[key] = val
        self._element_type = None
        if self._hasnans == False:
            self._hasnans = None
        self._max = None
        self._min = None

    def __str__(self) -> str:
        return str(self.series)


@cython.boundscheck(False)
@cython.wraparound(False)
cdef tuple _apply_with_errors(np.ndarray[object] arr, object call, str errors):
    """Apply a function over an object array using the given error-handling
    rule.
    """
    cdef unsigned int arr_length = arr.shape[0]
    cdef unsigned int i
    cdef np.ndarray[object] result = np.full(arr_length, None, dtype="O")
    cdef bint has_errors = False
    cdef np.ndarray[np.uint8_t, cast=True] index

    # index is only necessary if errors="coerce"
    if errors == "coerce":
        index = np.full(arr_length, False)
    else:
        index = None

    # apply `call` at every index of array and record errors
    for i in range(arr_length):
        try:
            result[i] = call(arr[i])
        except (KeyboardInterrupt, MemoryError, SystemError, SystemExit):
            raise  # never coerce on these error types
        except Exception as err:
            if errors == "coerce":
                has_errors = True
                index[i] = True
                continue
            raise err

    return result, has_errors, index


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
        with SeriesWrapper(as_series(data)) as series:
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
