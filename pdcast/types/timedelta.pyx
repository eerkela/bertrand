"""This module contains all the prepackaged timedelta types for the ``pdcast``
type system.
"""
import datetime
import re
from typing import Iterator

import numpy as np
import pandas as pd

from pdcast.resolve import resolve_type
from pdcast.util import time
from pdcast.util.type_hints import type_specifier

from .base cimport ScalarType, AbstractType, CompositeType
from .base import register


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
    type_def = pd.Timedelta
    dtype = np.dtype("m8[ns]")
    itemsize = 8
    na_value = pd.NaT
    max = pd.Timedelta.max.value
    min = pd.Timedelta.min.value


######################
####    PYTHON    ####
######################


@register
@TimedeltaType.implementation("python")
class PythonTimedeltaType(ScalarType):

    aliases = {datetime.timedelta, "pytimedelta", "datetime.timedelta"}
    type_def = datetime.timedelta
    na_value = pd.NaT
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
            self.min = 1  # NOTE: these values always trigger upcast mechanism
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

    def from_string(
        self,
        unit: str = None,
        step_size: str = None
    ) -> ScalarType:
        """Parse an m8 string in the type specification mini-language.

        Numpy timedeltas support two different parametrized syntaxes:
        
            1.  Numpy format, which concatenates step size and unit into a
                single field (e.g. 'm8[5ns]').
            2.  pdcast format, which lists each field individually (e.g.
                'timedelta[numpy, ns, 5]').  This matches the output of the
                str() function for these types.
        """
        if unit is None:
            return self

        m8 = m8_pattern.match(unit)
        parsed_unit = m8.group("unit")
        if step_size is not None:
            if m8.group("step_size"):
                raise ValueError(
                    f"conflicting units: '{unit}' vs '{parsed_unit}, "
                    f"{step_size}'"
                )
            parsed_step_size = int(step_size)
        else:
            parsed_step_size = int(m8.group("step_size") or 1)

        return self(unit=parsed_unit, step_size=parsed_step_size)

    def from_dtype(
        self,
        dtype: np.dtype | pd.api.extensions.ExtensionDtype
    ) -> ScalarType:
        """Translate a numpy m8 dtype into the pdcast type system."""
        unit, step_size = np.datetime_data(dtype)

        return self(
            unit=None if unit == "generic" else unit,
            step_size=step_size
        )

    def from_scalar(self, example: np.timedelta64) -> ScalarType:
        """Parse a scalar m8 value according to unit, step size."""
        unit, step_size = np.datetime_data(example)

        return self(unit=unit, step_size=step_size)

    #############################
    ####    CONFIGURATION    ####
    #############################

    @property
    def larger(self) -> Iterator:
        """If no original unit is given, iterate through each one in order."""
        if self.unit is None:
            yield from (self(unit=unit) for unit in time.valid_units)
        else:
            yield from ()


#######################
####    PRIVATE    ####
#######################


# overrides for ``<`` and ``>`` operators ``(A < B)``
ScalarType.registry.priority.update([
    (PythonTimedeltaType, NumpyTimedelta64Type),
])


# regex to parse numpy-style "m8[{step_size}{unit}]" strings
cdef object m8_pattern = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)
