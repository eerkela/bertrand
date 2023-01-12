import datetime
import decimal
from types import MappingProxyType
from typing import Any, Union, Sequence

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd
import regex as re  # using alternate python regex engine

from .base cimport AtomicType, CompositeType

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: import timedelta helpers from pdtypes.util.time
# TODO: update type hints from Any to datetime_like, timezone_like, etc.

# TODO: parse() should account for self.unit/step_size

# TODO: add min/max?
# -> these could potentially replace the constants in util/time/


class TimedeltaType(AtomicType):

    name = "timedelta"
    aliases = {
        "timedelta": {}
    }

    def __init__(self):
        super().__init__(
            type_def=None,
            dtype=None,
            na_value=pd.NaT,
            itemsize=None,
            slug=self.slugify()
        )

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(cls):
        return cls.name

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({})

    ###############################
    #####    CUSTOMIZATIONS    ####
    ###############################

    def contains(self, other: Any) -> bool:
        other = resolve.resolve_type(other)

        # respect wildcard rules in subtypes
        subtypes = self.subtypes.atomic_types - {self}
        if isinstance(other, CompositeType):
            return all(
                o == self or any(o in a for a in subtypes) for o in other
            )
        return other == self or any(other in a for a in subtypes)

    @classmethod
    def resolve(cls, backend: str = None, *args) -> AtomicType:
        if backend is None:
            call = cls.instance
        elif backend == "pandas":
            call = PandasTimedeltaType.resolve
        elif backend == "python":
            call = PyTimedeltaType.resolve 
        elif backend == "numpy":
            call = NumpyTimedelta64Type.resolve
        return call(*args)


class PandasTimedeltaType(AtomicType, supertype=TimedeltaType):

    name = "Timedelta"
    aliases = {
        pd.Timedelta: {},
        "Timedelta": {},
        "pandas.Timedelta": {},
        "pd.Timedelta": {},
    }

    def __init__(self):
        super().__init__(
            type_def=pd.Timedelta,
            dtype=np.dtype("m8[ns]"),
            na_value=pd.NaT,
            itemsize=8,
            slug=self.slugify()
        )

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(cls):
        return cls.name

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({})


class PyTimedeltaType(AtomicType, supertype=TimedeltaType):

    name = "pytimedelta"
    aliases = {
        datetime.timedelta: {},
        "pytimedelta": {},
        "datetime.timedelta": {},
    }

    def __init__(self):
        super().__init__(
            type_def=datetime.timedelta,
            dtype=np.dtype("O"),
            na_value=pd.NaT,
            itemsize=None,
            slug=self.slugify()
        )

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(cls):
        return cls.name

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({})


class NumpyTimedelta64Type(AtomicType, cache_size=64, supertype=TimedeltaType):

    name = "m8"
    aliases = {
        np.timedelta64: {},
        # np.dtype("m8") handled in resolve_typespec_dtype special case
        "m8": {},
        "timedelta64": {},
        "numpy.timedelta64": {},
        "np.timedelta64": {},
    }

    def __init__(self, unit: str = None, step_size: int = 1):
        if unit is None:
            dtype = np.dtype("m8")
        else:
            valid_units = {"ns", "us", "ms", "s", "m", "h", "D", "W", "M", "Y"}
            if unit not in valid_units:
                raise ValueError(f"unit not understood: {repr(unit)}")
            dtype = np.dtype(f"m8[{step_size}{unit}]")

        self.unit = unit
        self.step_size = step_size

        super().__init__(
            type_def=np.timedelta64,
            dtype=dtype,
            na_value=pd.NaT,
            itemsize=8,
            slug=self.slugify(unit=unit, step_size=step_size)
        )

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(cls, unit: str = None, step_size: int = 1) -> str:
        slug = cls.name
        if unit is not None:
            slug += f"[{step_size}{unit}]"
        return slug

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({
            "unit": self.unit,
            "step_size": self.step_size
        })

    ##############################
    ####    CUSTOMIZATIONS    ####
    ##############################

    def _generate_supertype(self, type_def: type) -> AtomicType:
        if type_def is None:
            return None
        return type_def.instance()

    def contains(self, other: Any) -> bool:
        other = resolve.resolve_type(other)

        # treat unit=None as wildcard
        if self.unit is None:
            if isinstance(other, CompositeType):
                return all(isinstance(o, type(self)) for o in other)
            return isinstance(other, type(self))

        subtypes = self.subtypes.atomic_types
        if isinstance(other, CompositeType):
            return all(o in subtypes for o in other)
        return other in subtypes

    @classmethod
    def detect(cls, example: np.datetime64, **defaults) -> AtomicType:
        unit, step_size = np.datetime_data(example)
        return cls.instance(unit=unit, step_size=step_size, **defaults)

    @classmethod
    def resolve(cls, context: str = None) -> AtomicType:
        if context is not None:
            match = m8_pattern.match(context)
            if not match:
                raise ValueError(f"invalid unit: {repr(context)}")
            unit = match.group("unit")
            step_size = int(match.group("step_size") or 1)
            return cls.instance(unit=unit, step_size=step_size)
        return cls.instance()


#######################
####    PRIVATE    ####
#######################


cdef object m8_pattern = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)
