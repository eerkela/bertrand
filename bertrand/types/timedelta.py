"""This module contains all the prepackaged timedelta types for the ``pdcast``
type system.
"""
import datetime
import re

import numpy as np
import pandas as pd

# from bertrand.util import time

from .base import Type, TypeMeta


# TODO:
# >>> Timedelta["numpy", "5ns"].params
# mappingproxy({'unit': None, 'step_size': None})
# -> should be: mappingproxy({'unit': 'ns', 'step_size': 5})


class Timedelta(Type):
    """Abstract timedelta type."""

    aliases = {"timedelta"}


@Timedelta.default
class PandasTimedelta(Timedelta, backend="pandas"):
    """Pandas timedelta type."""

    aliases = {pd.Timedelta, "pandas.Timedelta", "pd.Timedelta"}
    scalar = pd.Timedelta
    dtype = np.dtype("m8[ns]")
    itemsize = 8
    max = pd.Timedelta.max.value
    min = pd.Timedelta.min.value
    missing = pd.NaT



class PythonTimedelta(Timedelta, backend="python"):
    """Python timedelta type."""

    aliases = {datetime.timedelta, "pytimedelta", "datetime.timedelta"}
    scalar = datetime.timedelta
    # max = time.pytimedelta_to_ns(datetime.timedelta.max)
    # min = time.pytimedelta_to_ns(datetime.timedelta.min)
    missing = pd.NaT


class NumpyTimedelta64(Timedelta, backend="numpy", cache_size=64):
    """Numpy timedelta64 type."""

    # NOTE: dtype is set to object due to pandas and its penchant for
    # automatically converting datetimes to pd.Timestamp.  Otherwise, we'd use
    # an ObjectDtype or the raw numpy dtypes here.

    aliases = {
        np.timedelta64, np.dtype("m8"), "m8", "timedelta64", "numpy.timedelta64",
        "np.timedelta64",
    }
    scalar = np.timedelta64
    dtype = np.dtype(object)  # workaround for above
    itemsize = 8
    na_value = np.timedelta64("NaT")

    def __class_getitem__(
        cls,
        unit: str | None = None,
        step_size: int | None = None
    ) -> TypeMeta:
        if unit is None:
            # inverted max, min always triggers upcast mechanism during conversions
            return cls.flyweight(unit, None, max=0, min=1)

        # parse numpy-style m8 string
        m8 = m8_pattern.match(unit)
        if m8 is None:
            raise ValueError(f"invalid unit: '{unit}'")

        p_unit = m8.group("unit")
        if step_size is None:
            p_step = int(m8.group("step_size") or 1)
        elif m8.group("step_size"):
            raise ValueError(
                f"conflicting units: '{unit}' != '{p_unit}, {step_size}'"
            )
        else:
            if step_size is None:
                p_step = 1
            elif step_size < 1:
                raise ValueError(f"step size must be positive: {step_size}")
            else:
                p_step = step_size

        # # NOTE: these epochs are chosen to minimize range in the event of irregular
        # # units ('Y'/'M'), so that conversions work regardless of leap days and
        # # irregular month lengths
        # _max = time.convert_unit(
        #     2**63 - 1,
        #     p_unit,
        #     "ns",
        #     since=time.Epoch(np.datetime64("2001-02-01"))
        # )
        # _min = time.convert_unit(
        #     -2**63 + 1,  # NOTE: -2**63 reserved for NaT
        #     p_unit,
        #     "ns",
        #     since=time.Epoch(np.datetime64("2000-02-01"))
        # )

        # return cls.flyweight(unit, step_size, max=_max, min=_min)
        return cls.flyweight(p_unit, p_step)

    @classmethod
    def from_scalar(cls, scalar: np.timedelta64) -> TypeMeta:
        """Parse a scalar m8 value according to unit, step size."""
        unit, step_size = np.datetime_data(scalar)
        return cls.flyweight(unit, step_size)

    @classmethod
    def from_dtype(cls, dtype: np.dtype[np.timedelta64]) -> TypeMeta:
        """Translate a numpy m8 dtype into the pdcast type system."""
        unit, step_size = np.datetime_data(dtype)

        # # NOTE: pandas uses numpy m8 dtypes for its own Timedelta type, so we
        # # need to check the type of the array to detect this case.
        # if isinstance(array, (pd.Index, pd.Series)):
        #     # TODO: include non-ns units?
        #     return PandasTimedeltaType

        return cls.flyweight(None if unit == "generic" else unit, step_size)

    @classmethod
    def from_string(
        cls,
        unit: str | None = None,
        step_size: str | None = None
    ) -> TypeMeta:
        """Parse an m8 string in the type specification mini-language.

        Numpy timedeltas support two different parametrized syntaxes:
        
            1.  Numpy format, which concatenates step size and unit into a
                single field (e.g. 'm8[5ns]').
            2.  pdcast format, which lists each field individually (e.g.
                'timedelta[numpy, ns, 5]').  This matches the output of the
                str() function for these types.
        """
        if step_size is None:
            return cls[unit, step_size]

        return cls[unit, int(step_size)]

    # @property
    # def larger(self) -> Iterator:
    #     """If no original unit is given, iterate through each one in order."""
    #     if self.unit is None:
    #         yield from (self(unit=unit) for unit in time.valid_units)
    #     else:
    #         yield from ()


#######################
####    PRIVATE    ####
#######################


# REGISTRY.edges.add(PythonTimedelta, NumpyTimedelta64)


# regex to parse numpy-style "m8[{step_size}{unit}]" strings
m8_pattern: re.Pattern[str] = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)
