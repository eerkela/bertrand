"""This module contains all the prepackaged datetime types for the ``pdcast``
type system.
"""
import datetime
import re

import numpy as np
cimport numpy as np
import pandas as pd

from pdcast cimport resolve
from pdcast import resolve
from pdcast.util import time
from pdcast.util.type_hints import type_specifier

from .base cimport AtomicType, CompositeType
from .base import generic, register


# TODO: naive_tz has inconsistent behavior.
# >>> pdcast.cast("2022-03-07", "datetime[pandas, us/pacific]", tz=None, naive_tz=None)
# 0   2022-03-07 00:00:00-08:00
# dtype: datetime64[ns, US/Pacific]
# >>> pdcast.cast(pd.Timestamp("2022-03-07"), "datetime[pandas, us/pacific]", tz=None, naive_tz=None)
# 0   2022-03-07 08:00:00
# dtype: datetime64[ns]


# TODO: PandasTimestampType.from_string cannot convert quarterly dates


# TODO: from_ns, from_string go into util/time



#######################
####    GENERIC    ####
#######################


@register
@generic
class DatetimeType(AtomicType):

    name = "datetime"
    aliases = {"datetime"}
    dtype = None
    na_value = pd.NaT
    max = 0
    min = 1  # NOTE: these values always trip overflow/upcast check

    ############################
    ####    TYPE METHODS    ####
    ############################

    @property
    def larger(self) -> list:
        """Get a list of types that this type can be upcasted to."""
        # get candidates
        candidates = {
            x for y in self.backends.values() for x in y.subtypes if x != self
        }

        # filter off any that are upcast-only or larger than self
        result = [
            x for x in candidates if (
                x.min <= x.max and (x.min < self.min or x.max > self.max)
            )
        ]

        # sort by range
        result.sort(key=lambda x: x.max - x.min)

        # add subtypes that are themselves upcast-only
        others = [x for x in candidates if x.min > x.max]
        result.extend(sorted(others, key=lambda x: x.min - x.max))
        return result


#####################
####    NUMPY    ####
#####################


@register
@DatetimeType.register_backend("numpy")
class NumpyDatetime64Type(AtomicType, cache_size=64):

    # NOTE: dtype is set to object due to pandas and its penchant for
    # automatically converting datetimes to pd.Timestamp.  Otherwise, we'd use
    # a custom ExtensionDtype/AbstractDtype or the raw numpy dtypes here.

    aliases = {
        np.datetime64,
        np.dtype("M8"),
        "M8",
        "datetime64",
        "numpy.datetime64",
        "np.datetime64",
    }
    type_def = np.datetime64
    dtype = np.dtype(object)  # workaround for above
    itemsize = 8
    na_value = pd.NaT

    def __init__(self, unit: str = None, step_size: int = 1):
        if unit is None:
            # NOTE: these min/max values always trigger upcast check.
            self.min = 1  # increase this to take precedence when upcasting
            self.max = 0
        else:
            # NOTE: min/max datetime64 depends on unit
            if unit == "Y":  # appears to be biased toward UTC
                min_M8 = np.datetime64(-2**63 + 1, "Y")
                max_M8 = np.datetime64(2**63 - 1 - 1970, "Y")
            elif unit == "W":  # appears almost identical to unit="D"
                min_M8 = np.datetime64((-2**63 + 1 + 10956) // 7 + 1, "W")
                max_M8 = np.datetime64((2**63 - 1 + 10956) // 7 , "W")
            elif unit == "D":
                min_M8 = np.datetime64(-2**63 + 1 + 10956, "D")  # 10956 ??
                max_M8 = np.datetime64(2**63 - 1, "D")  # unbiased ??
            else:
                min_M8 = np.datetime64(-2**63 + 1, unit)
                max_M8 = np.datetime64(2**63 - 1, unit)
            self.min = time.numpy_datetime64_to_ns(min_M8)
            self.max = time.numpy_datetime64_to_ns(max_M8)

        super().__init__(unit=unit, step_size=step_size)

    ###########################
    ####   TYPE METHODS    ####
    ###########################

    @classmethod
    def slugify(cls, unit: str = None, step_size: int = 1) -> str:
        if unit is None:
            return f"{cls.name}[{cls._backend}]"
        if step_size == 1:
            return f"{cls.name}[{cls._backend}, {unit}]"
        return f"{cls.name}[{cls._backend}, {step_size}{unit}]"

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
            )

        # treat unit=None as wildcard
        if self.unit is None:
            return isinstance(other, type(self))
        return super().contains(other, include_subtypes=include_subtypes)

    @classmethod
    def detect(cls, example: np.datetime64, **defaults) -> AtomicType:
        unit, step_size = np.datetime_data(example)
        return cls.instance(unit=unit, step_size=step_size, **defaults)

    @classmethod
    def from_dtype(
        cls,
        dtype: np.dtype | pd.api.extensions.ExtensionDtype
    ) -> AtomicType:
        unit, step_size = np.datetime_data(dtype)
        return cls.instance(
            unit=None if unit == "generic" else unit,
            step_size=step_size
        )

    @property
    def larger(self) -> list:
        """Get a list of types that this type can be upcasted to."""
        if self.unit is None:
            return [self.instance(unit=u) for u in time.valid_units]
        return []

    @classmethod
    def resolve(cls, context: str = None) -> AtomicType:
        if context is not None:
            match = M8_pattern.match(context)
            if not match:
                raise ValueError(f"invalid unit: {repr(context)}")
            unit = match.group("unit")
            step_size = int(match.group("step_size") or 1)
            return cls.instance(unit=unit, step_size=step_size)
        return cls.instance()


######################
####    PANDAS    ####
######################


@register
@DatetimeType.register_backend("pandas")
class PandasTimestampType(AtomicType, cache_size=64):

    aliases = {
        pd.Timestamp,
        pd.DatetimeTZDtype,
        "Timestamp",
        "pandas.Timestamp",
        "pd.Timestamp",
    }
    # NOTE: timezone localization can cause pd.Timestamp objects to overflow.
    # In order to account for this, we artificially reduce the available range
    # to ensure that all timezones, no matter how extreme, are representable.
    itemsize = 8
    na_value = pd.NaT
    type_def = pd.Timestamp
    min = pd.Timestamp.min.value + 14 * time.as_ns["h"]  # UTC-14 most ahead
    max = pd.Timestamp.max.value - 12 * time.as_ns["h"]  # UTC+12 most behind

    def __init__(self, tz: datetime.tzinfo | str = None):
        tz = time.tz(tz)
        super().__init__(tz=tz)

    ########################
    ####    REQUIRED    ####
    ########################

    @property
    def dtype(self) -> np.dtype | pd.api.extensions.ExtensionDtype:
        if self.tz is None:
            return np.dtype("M8[ns]")
        return pd.DatetimeTZDtype(tz=self.tz)

    ############################
    ####    TYPE METHODS    ####
    ############################

    @classmethod
    def slugify(cls, tz: datetime.tzinfo | str = None):
        if tz is None:
            return f"{cls.name}[{cls._backend}]"
        return f"{cls.name}[{cls._backend}, {str(tz))}]"

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
            )

        # treat tz=None as wildcard
        if self.tz is None:
            return isinstance(other, type(self))
        return super().contains(other, include_subtypes=include_subtypes)

    @classmethod
    def detect(cls, example: pd.Timestamp, **defaults) -> AtomicType:
        return cls.instance(tz=example.tzinfo, **defaults)

    @classmethod
    def from_dtype(
        cls,
        dtype: np.dtype | pd.api.extensions.ExtensionDtype
    ) -> AtomicType:
        return cls.instance(tz=getattr(dtype, "tz", None))

    @classmethod
    def resolve(cls, context: str = None) -> AtomicType:
        if context is not None:
            return cls.instance(tz=time.tz(context))
        return cls.instance()


######################
####    PYTHON    ####
######################


@register
@DatetimeType.register_backend("python")
class PythonDatetimeType(AtomicType, cache_size=64):

    aliases = {datetime.datetime, "pydatetime", "datetime.datetime"}
    na_value = pd.NaT
    type_def = datetime.datetime
    max = time.pydatetime_to_ns(datetime.datetime.max)
    min = time.pydatetime_to_ns(datetime.datetime.min)

    def __init__(self, tz: datetime.tzinfo = None):
        tz = time.tz(tz)
        super().__init__(tz=tz)

    ############################
    ####    TYPE METHODS    ####
    ############################

    @classmethod
    def slugify(cls, tz: datetime.tzinfo = None):
        if tz is None:
            return f"{cls.name}[{cls._backend}]"
        return f"{cls.name}[{cls._backend}, {str(tz))}]"

    def contains(
        self,
        other: type_specifier,
        include_subtypes: bool = True
    ) -> bool:
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(
                self.contains(o, include_subtypes=include_subtypes)
                for o in other
            )

        # treat tz=None as wildcard
        if self.tz is None:
            return isinstance(other, type(self))
        return super().contains(other, include_subtypes=include_subtypes)

    @classmethod
    def detect(cls, example: datetime.datetime, **defaults) -> AtomicType:
        return cls.instance(tz=example.tzinfo, **defaults)

    @classmethod
    def resolve(cls, context: str = None) -> AtomicType:
        if context is not None:
            return cls.instance(tz=time.tz(context))
        return cls.instance()


#######################
####    PRIVATE    ####
#######################


cdef object M8_pattern = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)
