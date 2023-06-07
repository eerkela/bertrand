"""This module contains all the prepackaged datetime types for the ``pdcast``
type system.
"""
import datetime
import re

import numpy as np
import pandas as pd

from pdcast.resolve import resolve_type
from pdcast.util import time
from pdcast.util.type_hints import dtype_like, type_specifier

from .base cimport ScalarType, AbstractType, CompositeType
from .base import register


#######################
####    GENERIC    ####
#######################


@register
class DatetimeType(AbstractType):

    name = "datetime"
    aliases = {"datetime"}


######################
####    PANDAS    ####
######################


@register
@DatetimeType.default
@DatetimeType.implementation("pandas")
class PandasTimestampType(ScalarType):

    _cache_size = 64
    aliases = {
        pd.Timestamp,
        pd.DatetimeTZDtype,
        "Timestamp",
        "pandas.Timestamp",
        "pd.Timestamp",
    }
    type_def = pd.Timestamp
    itemsize = 8
    na_value = pd.NaT

    def __init__(self, tz: datetime.tzinfo | str = None):
        self.min = pd.Timestamp.min.value
        self.max = pd.Timestamp.max.value

        # NOTE: timezone localization can cause timestamps to overflow.  To
        # compensate for this, we adjust the min/max values artificially reduce
        # the available range.

        tz = time.tz(tz, {})
        if tz:
            min_offset = time.pytimedelta_to_ns(tz.utcoffset(pd.Timestamp.min))
            max_offset = time.pytimedelta_to_ns(tz.utcoffset(pd.Timestamp.max))
            self.min = max(self.min + min_offset, self.min)
            self.max = min(self.max + max_offset, self.max)

        super(type(self), self).__init__(tz=tz)

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(self, context: str = None) -> ScalarType:
        """Parse a timestamp string in the type specification mini-language."""
        if context is None:
            return self

        return self(tz=time.tz(context, {}))

    def from_dtype(self, dtype: dtype_like) -> ScalarType:
        """Translate a pandas DatetimeTZDtype into the pdcast type system."""
        return self(tz=getattr(dtype, "tz", None))

    def from_scalar(self, example: pd.Timestamp) -> ScalarType:
        """Parse a scalar pandas timestamp according to timezone."""
        return self(tz=example.tzinfo)

    #############################
    ####    CONFIGURATION    ####
    #############################

    @property
    def dtype(self) -> dtype_like:
        """Use a numpy dtype if no timezone is given, otherwise use the
        associated pandas extension type.
        """
        if self.tz is None:
            return np.dtype("M8[ns]")
        return pd.DatetimeTZDtype(tz=self.tz)


######################
####    PYTHON    ####
######################


@register
@DatetimeType.implementation("python")
class PythonDatetimeType(ScalarType):

    _cache_size = 64
    aliases = {datetime.datetime, "pydatetime", "datetime.datetime"}
    na_value = pd.NaT
    type_def = datetime.datetime
    max = time.pydatetime_to_ns(datetime.datetime.max)
    min = time.pydatetime_to_ns(datetime.datetime.min)

    def __init__(self, tz: datetime.tzinfo = None):
        tz = time.tz(tz, {})
        super(type(self), self).__init__(tz=tz)

    ###########################
    ####   CONSTRUCTORS    ####
    ###########################

    def from_string(self, tz: str = None) -> ScalarType:
        """Parse a type string in the type specification mini-language."""
        if tz is None:
            return self

        return self(tz=time.tz(tz, {}))

    def from_scalar(self, example: datetime.datetime) -> ScalarType:
        """Parse a scalar datetime according to timezone."""
        return self(tz=example.tzinfo)

    #############################
    ####    CONFIGURATION    ####
    #############################

    def __lt__(self, other: ScalarType) -> bool:
        """Prioritize python datetimes over numpy."""
        if isinstance(other, NumpyDatetime64Type):
            return True

        return super(type(self), self).__lt__(other)


#####################
####    NUMPY    ####
#####################


@register
@DatetimeType.implementation("numpy")
class NumpyDatetime64Type(ScalarType):

    # NOTE: dtype is set to object due to pandas and its penchant for
    # automatically converting datetimes to pd.Timestamp.  Otherwise, we'd use
    # a custom ExtensionDtype/ObjectDtype or the raw numpy dtypes here.

    _cache_size = 64
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
    na_value = np.datetime64("nat")

    def __init__(self, unit: str = None, step_size: int = 1):
        if unit is None:
            self.min = 1  # NOTE: these values always trigger upcast mechanism
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
                min_M8 = np.datetime64(-2**63 + 1 + 10956, "D")
                max_M8 = np.datetime64(2**63 - 1, "D")
            else:
                min_M8 = np.datetime64(-2**63 + 1, unit)
                max_M8 = np.datetime64(2**63 - 1, unit)

            self.min = time.numpy_datetime64_to_ns(min_M8)
            self.max = time.numpy_datetime64_to_ns(max_M8)

        super(type(self), self).__init__(unit=unit, step_size=step_size)

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(
        self,
        unit: str = None,
        step_size: str = None
    ) -> ScalarType:
        """Parse an M8 string in the type specification mini-language.

        Numpy datetimes support two different parametrized syntaxes:

            1.  Numpy format, which concatenates step size and unit into a
                single field (e.g. 'M8[5ns]').
            2.  pdcast format, which lists each field individually (e.g.
                'datetime[numpy, ns, 5]').  This matches the output of the
                str() function for these types.
        """
        if unit is None:
            return self

        M8 = M8_pattern.match(unit)
        parsed_unit = M8.group("unit")
        if step_size is not None:
            if M8.group("step_size"):
                raise ValueError(
                    f"conflicting units: '{unit}' vs '{parsed_unit}, "
                    f"{step_size}'"
                )
            parsed_step_size = int(step_size)
        else:
            parsed_step_size = int(M8.group("step_size") or 1)

        return self(unit=parsed_unit, step_size=parsed_step_size)

    def from_dtype(self, dtype: dtype_like) -> ScalarType:
        """Translate a numpy M8 dtype into the pdcast type system."""
        unit, step_size = np.datetime_data(dtype)

        return self(
            unit=None if unit == "generic" else unit,
            step_size=step_size
        )

    def from_scalar(self, example: np.datetime64, **defaults) -> ScalarType:
        """Parse a scalar M8 value according to unit, step size."""
        unit, step_size = np.datetime_data(example)
        return self(unit=unit, step_size=step_size, **defaults)

    #############################
    ####    CONFIGURATION    ####
    #############################

    @property
    def larger(self) -> Iterator[ScalarType]:
        """If no original unit is given, iterate through each one in order."""
        if self.unit is None:
            yield from (self(unit=unit) for unit in time.valid_units)
        else:
            yield from ()

    def __lt__(self, other: ScalarType) -> bool:
        """Prioritize numpy datetimes last."""
        if not isinstance(other, type(self)):
            return False

        return super(type(self), self).__lt__(other)

#######################
####    PRIVATE    ####
#######################


cdef object M8_pattern = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)
