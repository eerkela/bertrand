"""This module contains all the prepackaged timedelta types for the ``pdcast``
type system.
"""
import datetime
import re

import numpy as np
cimport numpy as np
import pandas as pd

from pdcast.resolve import resolve_type
from pdcast.util import time
from pdcast.util.type_hints import type_specifier

from .base cimport ScalarType, CompositeType
from .base import generic, register


# TODO: timedelta -> float does not retain longdouble precision.  This is due
# to the / operator in convert_unit() defaulting to float64 precision, which is
# probably unfixable.


#######################
####    GENERIC    ####
#######################


@register
@generic
class TimedeltaType(ScalarType):

    name = "timedelta"
    aliases = {"timedelta"}
    dtype = None
    na_value = pd.NaT
    max = 0
    min = 1  # these values always trip overflow/upcast check

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

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    # NOTE: because this type has no associated scalars, it will never be given
    # as the result of a detect_type() operation.  It can only be specified
    # manually, as the target of a resolve_type() call.


#####################
####    NUMPY    ####
#####################


@register
@TimedeltaType.implementation("numpy")
class NumpyTimedelta64Type(ScalarType, cache_size=64):

    # NOTE: dtype is set to object due to pandas and its penchant for
    # automatically converting datetimes to pd.Timestamp.  Otherwise, we'd use
    # an ObjectDtype or the raw numpy dtypes here.

    aliases = {
        np.timedelta64,
        np.dtype("m8"),
        "m8",
        "timedelta64",
        "numpy.timedelta64",
        "np.timedelta64",
    }
    type_def = np.timedelta64
    dtype = np.dtype(object)  # workaround for above
    itemsize = 8
    na_value = np.timedelta64("NaT")

    def __init__(self, unit: str = None, step_size: int = 1):
        if unit is None:
            # NOTE: these min/max values always trigger upcast check.
            self.min = 1  # increase this to take precedence when upcasting
            self.max = 0
        else:
            # NOTE: these epochs are chosen to minimize range in the event of
            # irregular units ('Y'/'M'), so that conversions work regardless of
            # leap days and irregular month lengths.
            self.max = time.convert_unit(
                2**63 - 1,
                unit,
                "ns",
                since=time.Epoch(np.datetime64("2001-02-01"))
            )
            self.min = time.convert_unit(
                -2**63 + 1,  # NOTE: -2**63 reserved for NaT
                unit,
                "ns",
                since=time.Epoch(np.datetime64("2000-02-01"))
            )

        super().__init__(unit=unit, step_size=step_size)

    ############################
    ####    TYPE METHODS    ####
    ############################

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
        other = resolve_type(other)
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
    def detect(cls, example: np.datetime64, **defaults) -> ScalarType:
        unit, step_size = np.datetime_data(example)
        return cls.instance(unit=unit, step_size=step_size, **defaults)

    @classmethod
    def from_dtype(
        cls,
        dtype: np.dtype | pd.api.extensions.ExtensionDtype
    ) -> ScalarType:
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
    def resolve(cls, context: str = None) -> ScalarType:
        if context is not None:
            match = m8_pattern.match(context)
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
@TimedeltaType.implementation("pandas")
class PandasTimedeltaType(ScalarType):

    aliases = {pd.Timedelta, "Timedelta", "pandas.Timedelta", "pd.Timedelta"}
    dtype = np.dtype("m8[ns]")
    itemsize = 8
    na_value = pd.NaT
    type_def = pd.Timedelta
    max = pd.Timedelta.max.value
    min = pd.Timedelta.min.value


######################
####    PYTHON    ####
######################


@register
@TimedeltaType.implementation("python")
class PythonTimedeltaType(ScalarType):

    aliases = {datetime.timedelta, "pytimedelta", "datetime.timedelta"}
    na_value = pd.NaT
    type_def = datetime.timedelta
    max = time.pytimedelta_to_ns(datetime.timedelta.max)
    min = time.pytimedelta_to_ns(datetime.timedelta.min)


#######################
####    PRIVATE    ####
#######################


cdef object m8_pattern = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)
