from contextlib import contextmanager
from datetime import tzinfo
import decimal
from functools import partial, wraps
import inspect
from typing import Any, Callable, Iterable, Iterator

cimport numpy as np
import numpy as np
import pandas as pd
import pytz
import tzlocal

cimport pdtypes.types.atomic as atomic
cimport pdtypes.types.detect as detect
cimport pdtypes.types.resolve as resolve
import pdtypes.types.atomic as atomic
import pdtypes.types.detect as detect
import pdtypes.types.resolve as resolve

from pdtypes.error import shorten_list
from pdtypes.type_hints import array_like, datetime_like, numeric
from pdtypes.util.round cimport Tolerance
from pdtypes.util.time cimport Epoch


# TODO: sparse types currently broken.
# -> maybe account for these outside of the conversions themselves, and just
# unwrap() it to do the conversion.  It's possible they'll just work out of
# the box though.


# TODO: have to account for empty series in each conversion.


# TODO: @dispatch should match the signatures of whatever method it is attached
# to.


#######################
####   DEFAULTS    ####
#######################


cdef class CastDefaults:

    cdef:
        bint _as_hours
        unsigned char _base
        bint _categorical
        bint _day_first
        atomic.CompositeType _downcast
        object _epoch
        str _errors
        set _false
        str _format
        bint _ignore_case
        str _rounding
        bint _sparse
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
        self._categorical = False
        self._downcast = None
        self._epoch = Epoch("utc")
        self._errors = "raise"
        self._false = {"false", "f", "no", "n", "off", "0"}
        self._ignore_case = True
        self._rounding = None
        self._sparse = False
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
    def categorical(self) -> bool:
        return self._categorical

    @categorical.setter
    def categorical(self, val: bool) -> None:
        if val is None:
            raise ValueError(f"default `categorical` cannot be None")
        self._categorical = validate_categorical(val)

    @property
    def day_first(self) -> bool:
        return self._day_first

    @day_first.setter
    def day_first(self, val: bool) -> None:
        if val is None:
            raise ValueError(f"default `day_first` cannot be None")
        self._day_first = validate_day_first(val)

    @property
    def downcast(self) -> atomic.CompositeType:
        return self._downcast

    @downcast.setter
    def downcast(self, val: bool | resolve.resolvable) -> None:
        if val is None:
            raise ValueError(f"default `downcast` cannot be None")
        self._downcast = validate_downcast(val)

    @property
    def epoch(self) -> np.datetime64:
        return self._epoch

    @epoch.setter
    def epoch(self, val: str | datetime_like) -> None:
        if val is None:
            raise ValueError(f"default `epoch` cannot be None")
        self._epoch = validate_epoch(val)

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
    def sparse(self) -> bool:
        return self._sparse

    @sparse.setter
    def sparse(self, val: bool) -> None:
        if val is None:
            raise ValueError(f"default `sparse` cannot be None")
        self._sparse = validate_sparse(val)

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


def validate_categorical(val: bool) -> bool:
    if val is None:
        return defaults.categorical
    return val


def validate_day_first(val: bool) -> bool:
    if val is None:
        return defaults.day_first
    return val


def validate_downcast(
    val: bool | resolve.resolvable
) -> atomic.CompositeType:
    if val is None:
        return defaults.downcast

    # convert booleans into CompositeTypes: empty set is truthy, None is false
    if isinstance(val, bool):
        return atomic.CompositeType() if val else None

    return resolve.resolve_type({val})


def validate_dtype(
    dtype: resolve.resolvable,
    supertype: resolve.resolvable = None
) -> atomic.AtomicType:
    """Resolve a type specifier and reject it if it is composite or not a
    subtype of the given supertype.
    """
    dtype = resolve.resolve_type(dtype)
    if not isinstance(dtype, atomic.AtomicType):
        raise ValueError(f"`dtype` cannot be composite (received: {dtype})")

    if supertype is not None:
        supertype = resolve.resolve_type(supertype)
        if not dtype.unwrap().is_subtype(supertype):
            raise ValueError(f"`dtype` must be {supertype}-like, not {dtype}")

    return dtype


def validate_epoch(val: str | datetime_like) -> Epoch:
    if val is None:
        return defaults.epoch
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

    # TODO: get valid from rounding module itself
    valid = (
        "floor", "ceiling", "down", "up", "half_floor", "half_ceiling",
        "half_down", "half_up", "half_even"
    )
    if val is not None and val not in valid:
        raise ValueError(f"`rounding` must be one of {valid}, not {repr(val)}")
    return val


def validate_sparse(val: bool) -> bool:
    if val is None:
        return defaults.sparse
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

    # TODO: get valid from time module itself
    valid = ("ns", "ms", "us", "s", "m", "h", "D", "W", "M", "Y")
    if val not in valid:
        raise ValueError(f"`unit` must be one of {valid}, not {repr(val)}")
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
    dtype: resolve.resolvable = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to the given data type."""
    # validate args
    dtype = validate_dtype(dtype)

    # delegate to appropriate to_x function below
    return dtype.conversion_func(series, validate_dtype(dtype), **kwargs)


def to_boolean(
    series: Iterable,
    dtype: resolve.resolvable = bool,
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    epoch: str | datetime_like = None,
    tz: str | tzinfo = None,
    true: str | Iterable[str] = None,
    false: str | Iterable[str] = None,
    ignore_case: bool = None,
    call: Callable = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to boolean representation."""
    # validate args
    dtype = validate_dtype(dtype, atomic.BooleanType)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    epoch = validate_epoch(epoch)
    tz = validate_timezone(tz)
    true = validate_true(true)
    false = validate_false(false)
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

    # delegate to SeriesWrapper.to_boolean
    return do_conversion(
        series,
        "to_boolean",
        dtype=dtype,
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        epoch=epoch,
        tz=tz,
        true=true,
        false=false,
        ignore_case=ignore_case,
        errors=errors,
        **kwargs
    )


def to_integer(
    series: Iterable,
    dtype: resolve.resolvable = int,
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    epoch: str | datetime_like = None,
    tz: str | tzinfo = None,
    base: int = None,
    call: Callable = None,
    downcast: bool | resolve.resolvable = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to integer representation."""
    # validate args
    dtype = validate_dtype(dtype, atomic.IntegerType)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    epoch = validate_epoch(epoch)
    tz = validate_timezone(tz)
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
        epoch=epoch,
        base=base,
        call=call,
        downcast=downcast,
        errors=errors,
        **kwargs
    )


def to_float(
    series: Iterable,
    dtype: resolve.resolvable = float,
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    epoch: str | datetime_like = None,
    tz: str | tzinfo = None,
    call: Callable = None,
    downcast: bool | resolve.resolvable = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to float representation."""
    # validate args
    dtype = validate_dtype(dtype, atomic.FloatType)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    epoch = validate_epoch(epoch)
    tz = validate_timezone(tz)
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
        epoch=epoch,
        tz=tz,
        call=call,
        downcast=downcast,
        errors=errors,
        **kwargs
    )


def to_complex(
    series: Iterable,
    dtype: resolve.resolvable = complex,
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    call: Callable = None,
    downcast: bool | resolve.resolvable = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to complex representation."""
    # validate args
    dtype = validate_dtype(dtype, atomic.ComplexType)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
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
        downcast=downcast,
        errors=errors,
        **kwargs
    )


def to_decimal(
    series: Iterable,
    dtype: resolve.resolvable = decimal.Decimal,
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    epoch: str | datetime_like = None,
    tz: str | tzinfo = None,
    call: Callable = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to decimal representation."""
    # validate args
    dtype = validate_dtype(dtype, atomic.DecimalType)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    epoch = validate_epoch(epoch)
    tz = validate_timezone(tz)
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
        epoch=epoch,
        tz=tz,
        call=call,
        errors=errors,
        **kwargs
    )


def to_datetime(
    series: Iterable,
    dtype: resolve.resolvable = "datetime",
    unit: str = None,
    step_size: int = None,
    epoch: str | datetime_like = None,
    tz: str | tzinfo = None,
    format: str = None,
    utc: bool = None,
    day_first: bool = None,
    year_first: bool = None,
    tol: numeric = None,
    rounding: str = None,
    call: Callable = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to datetime representation."""
    # validate args
    dtype = validate_dtype(dtype, atomic.DatetimeType)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    epoch = validate_epoch(epoch)
    tz = validate_timezone(tz)
    format = validate_format(format)
    utc = validate_utc(utc)
    day_first = validate_day_first(day_first)
    year_first = validate_year_first(year_first)
    call = validate_call(call)
    errors = validate_errors(errors)

    # delegate to SeriesWrapper.to_datetime
    return do_conversion(
        series,
        "to_datetime",
        dtype=dtype,
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        tz=tz,
        epoch=epoch,
        format=format,
        utc=utc,
        day_first=day_first,
        year_first=year_first,
        call=call,
        errors=errors,
        **kwargs
    )


def to_timedelta(
    series: Iterable,
    dtype: resolve.resolvable = "timedelta",
    tol: numeric = None,
    rounding: str = None,
    unit: str = None,
    step_size: int = None,
    epoch: str | datetime_like = None,
    tz: str | tzinfo = None,
    as_hours: bool = None,
    call: Callable = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to timedelta representation."""
    # validate args
    dtype = validate_dtype(dtype, atomic.TimedeltaType)
    tol = validate_tol(tol)
    rounding = validate_rounding(rounding)
    unit = validate_unit(unit)
    step_size = validate_step_size(step_size)
    epoch = validate_epoch(epoch)
    tz = validate_timezone(tz)
    as_hours = validate_as_hours(as_hours)
    call = validate_call(call)
    errors = validate_errors(errors)

    # delegate to SeriesWrapper.to_timedelta
    return do_conversion(
        series,
        "to_timedelta",
        dtype=dtype,
        tol=tol,
        rounding=rounding,
        unit=unit,
        step_size=step_size,
        epoch=epoch,
        tz=tz,
        as_hours=as_hours,
        call=call,
        errors=errors,
        **kwargs
    )


def to_string(
    series: Iterable,
    dtype: resolve.resolvable = str,
    base: int = None,
    format: str = None,
    call: Callable = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # validate args
    dtype = validate_dtype(dtype, atomic.StringType)
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
    dtype: resolve.resolvable = object,
    call: Callable = None,
    errors: str = None,
    **kwargs
) -> pd.Series:
    """Convert arbitrary data to string representation."""
    # validate args
    dtype = validate_dtype(dtype, atomic.ObjectType)
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
        element_type: atomic.BaseType = None
    ):
        self.series = series
        self.hasnans = hasnans
        self.element_type = element_type

    ##########################
    ####    PROPERTIES    ####
    ##########################

    @property
    def element_type(self) -> atomic.BaseType:
        if self._element_type is None:
            self._element_type = detect.detect_type(self.series)
        return self._element_type

    @element_type.setter
    def element_type(self, val: resolve.resolvable) -> None:
        if val is not None:
            val = resolve.resolve_type(val)
            if (
                isinstance(val, atomic.CompositeType) and
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
        dtype: resolve.resolvable,
        errors: str = "raise"
    ) -> SeriesWrapper:
        """`astype()` equivalent for SeriesWrapper instances that works for
        object-based type specifiers.
        """
        dtype = resolve.resolve_type(dtype)
        if isinstance(dtype, atomic.CompositeType):
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
        self._original_shape = self.series.shape

        # normalize index
        if not isinstance(self.series.index, pd.RangeIndex):
            self._original_index = self.series.index
            self.series.index = pd.RangeIndex(0, self._original_shape[0])

        # drop missing values
        is_na = self.isna()
        self.hasnans = is_na.any()
        if self._hasnans:
            self.series = self.series[~is_na]

        # detect element type if not set manually
        if self._element_type is None:
            self.element_type = detect.detect_type(self.series)

        # enter context block
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        # replace missing values, aligning on index
        if self.hasnans:
            result = pd.Series(
                np.full(
                    self._original_shape,
                    getattr(self.element_type, "na_value", pd.NA),
                    dtype="O"
                ),
                dtype=self.dtype
            )
            result.update(self.series)
            self.series = result

        # replace original index
        if self._original_index is not None:
            self.series.index = self._original_index

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
        dtype: atomic.AtomicType,
        errors: str
    ) -> tuple[SeriesWrapper, atomic.AtomicType]:
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

    def dispatch(self, endpoint: str, *args, **kwargs) -> SeriesWrapper:
        """For every type that is present in the series, invoke the named
        endpoint.
        """
        # series is homogenous
        if isinstance(self.element_type, atomic.AtomicType):
            # check for corresponding AtomicType method
            call = getattr(self.element_type, endpoint, None)
            if call is not None:
                return call(self, *args, **kwargs)

            # fall back to pandas implementation
            call = getattr(pd.Series, endpoint)
            pars = inspect.signature(call).parameters
            kwargs = {k: v for k, v in kwargs.items() if k in pars}
            result = call(self.series, *args, **kwargs)
            if isinstance(result, pd.Series):
                return SeriesWrapper(result, hasnans=self._hasnans)
            return result

        # series is composite
        groups = self.series.groupby(self.element_type.index, sort=False)
        pars = getattr(pd.Series, endpoint, None)
        if pars is not None:  # introspect before grouping
            pars = inspect.signature(pars).parameters

        def transform(grp):
            # check for corresponding AtomicType method
            call = getattr(grp.name, endpoint, None)
            if call is not None:
                result = call(
                    SeriesWrapper(grp, hasnans=self._hasnans),
                    *args,
                    **kwargs
                )
                self.hasnans = self._hasnans or result._hasnans
                result = result.series

            # fall back to pandas implementation
            else:
                call = getattr(pd.Series, endpoint)
                kw = {k: v for k, v in kwargs.items() if k in pars}
                result = call(grp, *args, **kw)

            return result

        result = groups.transform(transform)
        return SeriesWrapper(result, hasnans=self._hasnans)

    def isinf(self) -> SeriesWrapper:
        """TODO"""
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

    #################################
    ####   DISPATCHED METHODS    ####
    #################################

    # TODO: this is superseded by new @dispatch behavior.  Dispatched methods
    # will not appear here.

    # NOTE: SeriesWrapper dynamically inherits every @dispatch method that is
    # defined by its element_type.  In the case of a composite series, these
    # dispatched methods are applied independently to each type that is present
    # in the series.  If a dispatched method is not defined for a given
    # element_type, then SeriesWrapper automatically defaults to the pandas
    # implementation if one exists.  See __getattr__() for more details on how
    # this is done.

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
        result += [x for x in atomic.AtomicType.registry.dispatch_map]

        return result

    def __float__(self) -> float:
        return float(self.series)

    def __getitem__(self, key) -> Any:
        result = self.series[key]

        # slicing: re-wrap result
        if isinstance(result, pd.Series):
            # slicing can change element_type of result, but only if composite
            if isinstance(self._element_type, atomic.CompositeType):
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


def as_series(data) -> pd.Series:
    """Convert the given data into a corresponding pd.Series object."""
    if isinstance(data, pd.Series):
        return data.copy()

    if isinstance(data, np.ndarray):
        return pd.Series(np.atleast_1d(data))

    return pd.Series(data, dtype="O")


def do_conversion(
    data,
    endpoint: str,
    *args,
    errors: str,
    **kwargs
) -> pd.Series:
    try:
        with SeriesWrapper(as_series(data)) as series:
            result = series.dispatch(endpoint, *args, errors=errors, **kwargs)
            series.series = result.series
            series.hasnans = result.hasnans
            series.element_type = result.element_type
        return series.series
    except (KeyboardInterrupt, MemoryError, SystemError, SystemExit):
        raise  # never ignore these errors
    except Exception as err:
        if errors == "ignore":
            return data
        raise err
