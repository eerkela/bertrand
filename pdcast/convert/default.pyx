from datetime import tzinfo
from typing import Callable

cimport numpy as np
import numpy as np
import pytz
import tzlocal

cimport pdcast.resolve as resolve
import pdcast.resolve as resolve
cimport pdcast.types as types
import pdcast.types as types

import pdcast.convert.standalone as standalone

from pdcast.util.round cimport Tolerance
from pdcast.util.round import valid_rules
from pdcast.util.time cimport Epoch, epoch_aliases, valid_units
from pdcast.util.type_hints import datetime_like, numeric, type_specifier


######################
####    PUBLIC    ####
######################


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


#######################
####    PRIVATE    ####
#######################


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

    return resolve.resolve_type([val])


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
        if not dtype.strip().is_subtype(supertype):
            raise ValueError(f"`dtype` must be {supertype}-like, not {dtype}")

    return dtype


def validate_since(val: str | datetime_like) -> Epoch:
    if val is None:
        return defaults.since

    if isinstance(val, str) and val not in epoch_aliases:
        val = standalone.cast(val, "datetime")
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
