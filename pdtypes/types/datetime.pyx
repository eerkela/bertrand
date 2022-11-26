from cpython cimport datetime

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport (
    compute_hash, ElementType, resolve_dtype, shared_registry,
    datetime64_registry
)


# TODO: may not need overwritten __contains__() methods with CompositeType
# subtypes implementation.
# -> or maybe you only need it on NumpyDatetime64Type w/ generic units, then
# the default x in subtypes should return True for any choice of unit/step size.


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
        super(DatetimeType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=None,
            numpy_type=None,
            pandas_type=None,
            slug="datetime",
            supertype=None,
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable values in ns
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000

    @property
    def subtypes(self) -> frozenset:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self} | {
            t.instance(sparse=self.sparse, categorical=self.categorical)
            for t in (PandasTimestampType, PyDatetimeType, NumpyDatetime64Type)
        }
        self._subtypes = frozenset(subtypes)
        return self._subtypes

    def __contains__(self, other) -> bool:
        """Test whether the given type specifier is a subtype of this
        ElementType.
        """
        other = resolve_dtype(other)
        if isinstance(other, NumpyDatetime64Type):  # disregard unit/step_size
            return (
                self.sparse == other.sparse and
                self.categorical == other.categorical
            )
        return other in self.subtypes


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
        super(DatetimeType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=pd.Timestamp,
            numpy_type=None,
            pandas_type=None,
            slug="datetime[pandas]",
            supertype=None,  # lazy-loaded
            subtypes=frozenset({self})
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable values in ns
        self.min = -2**63 + 1
        self.max = 2**63 - 1

    @property
    def supertype(self) -> DatetimeType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = DatetimeType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._supertype

    def __contains__(self, other) -> bool:
        """Test whether the given type specifier is a subtype of this
        ElementType.
        """
        return super(DatetimeType, self).__contains__(other)


cdef class PyDatetimeType(DatetimeType):
    """`datetime.datetime` datetime subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        super(DatetimeType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=datetime.datetime,
            numpy_type=None,
            pandas_type=None,
            slug="datetime[python]",
            supertype=None,  # lazy-loaded
            subtypes=frozenset({self})
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable values in ns
        self.min = -62135596800000000000
        self.max = 253402300799999999000

    @property
    def supertype(self) -> DatetimeType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = DatetimeType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._supertype

    def __contains__(self, other) -> bool:
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
        # ensure unit, step size are valid
        self.unit = None if unit == "generic" else unit
        if step_size < 1:
            raise ValueError(f"`step_size` must be >= 1, not {step_size}")
        self.step_size = step_size

        # generate appropriate slug
        if self.unit is None:
            slug = "M8"
        else:
            if self.step_size == 1:
                slug = f"M8[{self.unit}]"
            else:
                slug = f"M8[{self.step_size}{self.unit}]"

        # feed to ElementType constructor
        super(DatetimeType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=np.datetime64,
            numpy_type=np.dtype(slug),
            pandas_type=None,
            slug=slug,
            supertype=None,  # lazy-loaded
            subtypes=frozenset({self})
        )

        # compute hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__,
            unit=self.unit,
            step_size=self.step_size
        )

        # min/max representable values in ns
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000

    @property
    def supertype(self) -> DatetimeType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = DatetimeType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._supertype

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
        """Test whether the given type specifier is a subtype of this
        ElementType.
        """
        other = resolve_dtype(other)
        if isinstance(other, self.__class__):
            if self.unit is None:  # disregard unit/step_size
                return (
                    self.sparse == other.sparse and
                    self.categorical == other.categorical
                )
            return self == other
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
