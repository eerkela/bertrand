from cpython cimport datetime

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport (
    compute_hash, ElementType, resolve_dtype, shared_registry,
    datetime64_registry
)


##########################
####    SUPERTYPES    ####
##########################


cdef class DatetimeType(ElementType):
    """Datetime supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse,
        self.categorical = categorical
        self.nullable = True
        self.supertype = None
        self.subtypes = frozenset(
            t.instance(sparse=sparse, categorical=categorical)
            for t in (PandasTimestampType, PyDatetimeType)
        )
        self.atomic_type = None
        self.numpy_type = None
        self.pandas_type = None
        self.slug = "datetime"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable values in ns
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000

    def __contains__(self, other) -> bool:
        other = resolve_dtype(other)
        if issubclass(other, NumpyDatetime64Type):  # disregard unit/step_size
            return (
                self.sparse == other.sparse and
                self.categorical == other.categorical
            )
        return self.__eq__(other) or other in self.subtypes


########################
####    SUBTYPES    ####
########################


cdef class PandasTimestampType(DatetimeType):
    """`pandas.Timestamp` datetime subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse,
        self.categorical = categorical
        self.nullable = True
        self.supertype = DatetimeType
        self.subtypes = frozenset()
        self.atomic_type = pd.Timestamp
        self.numpy_type = None
        self.pandas_type = None
        self.slug = "datetime[pandas]"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable values in ns
        self.min = -2**63 + 1
        self.max = 2**63 - 1

    def __contains__(self, other) -> bool:
        # use default ElementType __contains__()
        return super(DatetimeType, self).__contains__(other)


cdef class PyDatetimeType(DatetimeType):
    """`datetime.datetime` datetime subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse,
        self.categorical = categorical
        self.nullable = True
        self.supertype = DatetimeType
        self.subtypes = frozenset()
        self.atomic_type = datetime.datetime
        self.numpy_type = None
        self.pandas_type = None
        self.slug = "datetime[python]"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable values in ns
        self.min = -62135596800000000000
        self.max = 253402300799999999000

    def __contains__(self, other) -> bool:
        # use default ElementType __contains__()
        return super(DatetimeType, self).__contains__(other)


cdef class NumpyDatetime64Type(DatetimeType):
    """`numpy.datetime64` datetime subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        str unit = None,
        unsigned long long step_size = 1
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = DatetimeType
        self.subtypes = frozenset()
        self.atomic_type = np.datetime64
        self.pandas_type = None

        # add unit, step size
        self.unit = None if unit == "generic" else unit
        if step_size < 1:
            raise ValueError(f"`step_size` must be >= 1, not {step_size}")
        self.step_size = step_size

        # get appropriate slug based on unit, step size
        if self.unit is None:
            self.slug = "M8"
        else:
            if self.step_size == 1:
                self.slug = f"M8[{unit}]"
            else:
                self.slug = f"M8[{step_size}{unit}]"

        # get associated numpy type
        self.numpy_type = np.dtype(self.slug)

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
    ) -> NumpyDatetime64Type:
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
        cdef NumpyDatetime64Type result = datetime64_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                sparse=sparse,
                categorical=categorical,
                unit=unit,
                step_size=step_size
            )
    
            # add flyweight to registry
            datetime64_registry[_hash] = result

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
            return self.__eq__(other) or other in self.subtypes
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
