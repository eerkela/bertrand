import datetime
import decimal
from types import MappingProxyType
from typing import Any, Union, Sequence

import numpy as np
cimport numpy as np
import pandas as pd
import regex as re  # using alternate python regex engine

from .base cimport AtomicType, CompositeType
from .base import dispatch, generic, lru_cache

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: import datetime helpers from pdtypes.util.time
# TODO: update type hints from Any to datetime_like, timezone_like, etc.

# TODO: parse() should account for self.tz/unit/step_size
# TODO: allow for tz="local"
# -> handled in Timezone factory, which needs to be introduced wherever `tz` is
# referenced (instance, __init__)

# TODO: add min/max?
# -> these could potentially replace the constants in util/time/


######################
####    MIXINS    ####
######################


class DatetimeMixin:

    pass


#######################
####    GENERIC    ####
#######################


@generic
class DatetimeType(DatetimeMixin, AtomicType):

    conversion_func = cast.to_datetime  # all subtypes/backends inherit this
    name = "datetime"
    aliases = {"datetime"}

    def __init__(self):
        super().__init__(
            type_def=None,
            dtype=np.dtype(np.object_),
            na_value=pd.NaT,
            itemsize=None
        )


#####################
####    NUMPY    ####
#####################


@lru_cache(64)
@DatetimeType.register_backend("numpy")
class NumpyDatetime64Type(DatetimeMixin, AtomicType):

    aliases = {
        np.datetime64,
        # np.dtype("M8") handled in resolve_typespec_dtype special case
        "M8",
        "datetime64",
        "numpy.datetime64",
        "np.datetime64",
    }

    def __init__(self, unit: str = None, step_size: int = 1):
        if unit is None:
            dtype = np.dtype("M8")
        else:
            valid_units = {"ns", "us", "ms", "s", "m", "h", "D", "W", "M", "Y"}
            if unit not in valid_units:
                raise ValueError(f"unit not understood: {repr(unit)}")
            dtype = np.dtype(f"M8[{step_size}{unit}]")

        super().__init__(
            type_def=np.datetime64,
            dtype=dtype,
            na_value=pd.NaT,
            itemsize=8,
            unit=unit,
            step_size=step_size
        )

    @classmethod
    def slugify(cls, unit: str = None, step_size: int = 1) -> str:
        slug = cls.name
        if unit is not None:
            slug += f"[{cls.backend}, {step_size}{unit}]"
        else:
            slug += f"[{cls.backend}]"
        return slug

    def contains(self, other: Any) -> bool:
        other = resolve.resolve_type(other)

        # treat unit=None as wildcard
        if self.unit is None:
            if isinstance(other, CompositeType):
                return all(isinstance(o, type(self)) for o in other)
            return isinstance(other, type(self))

        return super().contains(other)

    @classmethod
    def detect(cls, example: np.datetime64, **defaults) -> AtomicType:
        unit, step_size = np.datetime_data(example)
        return cls.instance(unit=unit, step_size=step_size, **defaults)

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


@lru_cache(64)
@DatetimeType.register_backend("pandas")
class PandasTimestampType(DatetimeMixin, AtomicType):

    aliases = {
        pd.Timestamp,
        # pd.DatetimeTZDtype() handled in resolve_typespec_dtype special case
        "Timestamp",
        "pandas.Timestamp",
        "pd.Timestamp",
    }

    def __init__(self, tz: datetime.tzinfo = None):
        if tz is None:
            dtype = np.dtype("M8[ns]")
        else:
            dtype = pd.DatetimeTZDtype(tz=tz)
        super().__init__(
            type_def=pd.Timestamp,
            dtype=dtype,
            na_value=pd.NaT,
            itemsize=8,
            tz=tz
        )

    @classmethod
    def slugify(cls, tz: datetime.tzinfo = None):
        slug = cls.name
        if tz is not None:
            slug += f"[{cls.backend}, {tz}]"
        else:
            slug += f"[{cls.backend}]"
        return slug

    def contains(self, other: Any) -> bool:
        other = resolve.resolve_type(other)

        # treat tz=None as wildcard
        if self.tz is None:
            if isinstance(other, CompositeType):
                return all(isinstance(o, type(self)) for o in other)
            return isinstance(other, type(self))

        return super().contains(other)

    @classmethod
    def detect(cls, example: pd.Timestamp, **defaults) -> AtomicType:
        return cls.instance(tz=example.tz, **defaults)


######################
####    PYTHON    ####
######################


@lru_cache(64)
@DatetimeType.register_backend("python")
class PythonDatetimeType(DatetimeMixin, AtomicType):

    aliases = {datetime.datetime, "pydatetime", "datetime.datetime"}

    def __init__(self, tz: datetime.tzinfo = None):
        super().__init__(
            type_def=datetime.datetime,
            dtype=np.dtype("O"),
            na_value=pd.NaT,
            itemsize=None,
            tz=tz
        )

    @classmethod
    def slugify(cls, tz: datetime.tzinfo = None):
        slug = cls.name
        if tz is not None:
            slug += f"[{cls.backend}, {tz}]"
        else:
            slug += f"[{cls.backend}]"
        return slug

    def contains(self, other: Any) -> bool:
        other = resolve.resolve_type(other)

        # treat tz=None as wildcard
        if self.tz is None:
            if isinstance(other, CompositeType):
                return all(isinstance(o, type(self)) for o in other)
            return isinstance(other, type(self))

        return super().contains(other)

    @classmethod
    def detect(cls, example: datetime.datetime, **defaults) -> AtomicType:
        return cls.instance(tz=example.tzinfo, **defaults)


#######################
####    PRIVATE    ####
#######################


cdef object M8_pattern = re.compile(
    r"(?P<step_size>[0-9]+)?(?P<unit>ns|us|ms|s|m|h|D|W|M|Y)"
)
