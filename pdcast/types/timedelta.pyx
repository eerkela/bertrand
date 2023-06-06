"""This module contains all the prepackaged timedelta types for the ``pdcast``
type system.
"""
import datetime
import re
from typing import Iterator

import numpy as np
cimport numpy as np
import pandas as pd

from pdcast.resolve import resolve_type
from pdcast.util import time
from pdcast.util.type_hints import type_specifier

from .base cimport ScalarType, AbstractType, CompositeType
from .base import register


# TODO: timedelta -> float does not retain longdouble precision.  This is due
# to the / operator in convert_unit() defaulting to float64 precision, which is
# probably unfixable.


#######################
####    GENERIC    ####
#######################


@register
class TimedeltaType(AbstractType):

    name = "timedelta"
    aliases = {"timedelta"}


######################
####    PANDAS    ####
######################


@register
@TimedeltaType.default
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


#####################
####    NUMPY    ####
#####################


@register
@TimedeltaType.implementation("numpy")
class NumpyTimedelta64Type(ScalarType):

    # NOTE: dtype is set to object due to pandas and its penchant for
    # automatically converting datetimes to pd.Timestamp.  Otherwise, we'd use
    # an ObjectDtype or the raw numpy dtypes here.

    _cache_size = 64
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

        super(type(self), self).__init__(unit=unit, step_size=step_size)

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(self, context: str = None) -> ScalarType:
        """Parse an m8 string in the type specification mini-language."""
        if context is None:
            return self

        match = m8_pattern.match(context)
        if not match:
            raise ValueError(f"invalid unit: {repr(context)}")

        unit = match.group("unit")
        step_size = int(match.group("step_size") or 1)
        return self(unit=unit, step_size=step_size)

    def from_dtype(
        self,
        dtype: np.dtype | pd.api.extensions.ExtensionDtype
    ) -> ScalarType:
        """Convert a numpy m8 dtype into the pdcast type system."""
        unit, step_size = np.datetime_data(dtype)

        return self(
            unit=None if unit == "generic" else unit,
            step_size=step_size
        )

    def from_scalar(self, example: np.timedelta64) -> ScalarType:
        """Parse a scalar m8 value into the pdcast type system."""
        unit, step_size = np.datetime_data(example)

        return self(unit=unit, step_size=step_size)

    ############################
    ####    TYPE METHODS    ####
    ############################

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

    @property
    def larger(self) -> Iterator:
        """Get a list of types that this type can be upcasted to."""
        if self.unit is None:
            yield from (self(unit=u) for u in time.valid_units)
        else:
            yield from ()


#######################
####    PRIVATE    ####
#######################


cdef object m8_pattern = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)
