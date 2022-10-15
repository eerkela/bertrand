from cpython cimport datetime
from functools import lru_cache

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport (
    compute_hash, ElementType, resolve_dtype, shared_registry,
    timedelta64_registry
)


##########################
####    SUPERTYPES    ####
##########################


cdef class TimedeltaType(ElementType):
    """Timedelta supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = None
        self.atomic_type = None
        self.numpy_type = None
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # generate slug
        self.slug = "timedelta"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        self.subtypes |= {
            t.instance(sparse=sparse, categorical=categorical)
            for t in (PandasTimedeltaType, PyTimedeltaType)
        }

        # min/max representable values in ns
        self.min = -291061508645168391112156800000000000
        self.max = 291061508645168391112243200000000000

    def __contains__(self, other) -> bool:
        other = resolve_dtype(other)
        if issubclass(other, NumpyTimedelta64Type):  # disregard unit/step_size
            return (
                self.sparse == other.sparse and
                self.categorical == other.categorical
            )
        return other in self.subtypes


########################
####    SUBTYPES    ####
########################


cdef class PandasTimedeltaType(TimedeltaType):
    """`pandas.Timedelta` timedelta subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = TimedeltaType
        self.atomic_type = pd.Timedelta
        self.numpy_type = None
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # generate slug
        self.slug = "timedelta[pandas]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))

        # min/max representable values in ns
        self.min = -2**63 + 1
        self.max = 2**63 - 1

    def __contains__(self, other) -> bool:
        # use default ElementType __contains__()
        return super(TimedeltaType, self).__contains__(other)


cdef class PyTimedeltaType(TimedeltaType):
    """`datetime.timedelta` timedelta subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = TimedeltaType
        self.atomic_type = datetime.timedelta
        self.numpy_type = None
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # generate slug
        self.slug = "timedelta[python]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))

        # min/max representable values in ns
        self.min = -86399999913600000000000
        self.max = 86399999999999999999000

    def __contains__(self, other) -> bool:
        # use default ElementType __contains__()
        return super(TimedeltaType, self).__contains__(other)


cdef class NumpyTimedelta64Type(TimedeltaType):
    """`numpy.timedelta64` timedelta subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        str unit = None,
        unsigned long long step_size = 1
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = TimedeltaType
        self.atomic_type = np.timedelta64
        self.pandas_type = None

        # add unit, step size
        self.unit = None if unit == "generic" else unit
        if step_size < 1:
            raise ValueError(f"`step_size` must be >= 1, not {step_size}")
        self.step_size = step_size

        # generate appropriate slug based on unit, step size
        if self.unit is None:
            self.slug = "m8"
        else:
            if self.step_size == 1:
                self.slug = f"m8[{unit}]"
            else:
                self.slug = f"m8[{step_size}{unit}]"

        # get associated numpy type
        self.numpy_type = np.dtype(self.slug)

        # append sparse, categorical flags to slug
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))

        # compute hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__,
            unit=unit,
            step_size=step_size
        )

        # min/max representable values in ns
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000

    @classmethod
    def instance(
        cls,
        bint sparse = False,
        bint categorical = False,
        str unit = None,
        unsigned long long step_size = 1
    ) -> NumpyTimedelta64Type:
        """Flyweight constructor."""
        # hash arguments
        cdef long long _hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=cls,
            unit=unit,
            step_size=step_size
        )

        # get previous flyweight, if one exists
        cdef NumpyTimedelta64Type result = timedelta64_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                sparse=sparse,
                categorical=categorical,
                unit=unit,
                step_size=step_size
            )
    
            # add flyweight to registry
            timedelta64_registry[_hash] = result

        # return flyweight
        return result

    def __contains__(self, other) -> bool:
        other = resolve_dtype(other)
        if issubclass(other, self.__class__):
            if self.unit is None:  # disregard unit/step_size
                return (
                    self.sparse == other.sparse and
                    self.categorical == other.categorical
                )
            return self.__eq__(other)
        return False

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}, "
            f"unit={repr(self.unit)}, "
            f"step_size={self.step_size}"
            f")"
        )

