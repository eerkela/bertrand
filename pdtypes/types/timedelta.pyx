from cpython cimport datetime
from functools import lru_cache

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport (
    base_slugs, ElementType, generate_slug, resolve_dtype, shared_registry,
    timedelta64_registry
)


cdef str generate_m8_slug(
    type base_type,
    str unit,
    unsigned int step_size,
    bint sparse,
    bint categorical
):
    """Return a unique slug string associated with the given `base_type`,
    accounting for `unit`, `step_size`, `sparse`, and `categorical` flags.
    """
    cdef str slug = base_slugs[base_type]

    if unit:
        if step_size == 1:
            slug = f"{slug}[{unit}]"
        else:
            slug = f"{slug}[{step_size}{unit}]"
    if categorical:
        slug = f"categorical[{slug}]"
    if sparse:
        slug = f"sparse[{slug}]"

    return slug


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
        super(TimedeltaType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=None,
            numpy_type=None,
            pandas_type=None,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            ),
            supertype=None,
            subtypes=None  # lazy-loaded
        )

        # min/max representable values in ns
        self.min = -291061508645168391112156800000000000
        self.max = 291061508645168391112243200000000000

    @property
    def subtypes(self) -> frozenset:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self} | {
            t.instance(sparse=self.sparse, categorical=self.categorical)
            for t in (
                PandasTimedeltaType, PyTimedeltaType, NumpyTimedelta64Type
            )
        }
        self._subtypes = frozenset(subtypes)
        return self._subtypes

    def __contains__(self, other) -> bool:
        """Test whether the given type specifier is a subtype of this
        ElementType.
        """
        other = resolve_dtype(other)
        if isinstance(other, NumpyTimedelta64Type):  # disregard unit/step_size
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
        super(TimedeltaType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=pd.Timedelta,
            numpy_type=None,
            pandas_type=None,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            ),
            supertype=None,  # lazy-loaded
            subtypes=frozenset({self})
        )

        # min/max representable values in ns
        self.min = -2**63 + 1
        self.max = 2**63 - 1

    @property
    def supertype(self) -> TimedeltaType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = TimedeltaType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._supertype

    def __contains__(self, other) -> bool:
        """Test whether the given type specifier is a subtype of this
        ElementType.
        """
        return super(TimedeltaType, self).__contains__(other)


cdef class PyTimedeltaType(TimedeltaType):
    """`datetime.timedelta` timedelta subtype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        super(TimedeltaType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=datetime.timedelta,
            numpy_type=None,
            pandas_type=None,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            ),
            supertype=None,  # lazy-loaded
            subtypes=frozenset({self})
        )

        # min/max representable values in ns
        self.min = -86399999913600000000000
        self.max = 86399999999999999999000

    @property
    def supertype(self) -> TimedeltaType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = TimedeltaType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._supertype

    def __contains__(self, other) -> bool:
        """Test whether the given type specifier is a subtype of this
        ElementType.
        """
        return super(TimedeltaType, self).__contains__(other)


cdef class NumpyTimedelta64Type(TimedeltaType):
    """`numpy.timedelta64` timedelta subtype"""

    def __init__(
        self,
        str unit = None,
        unsigned long long step_size = 1,
        bint sparse = False,
        bint categorical = False
    ):
        # ensure unit, step size are valid
        self.unit = None if unit == "generic" else unit
        if step_size < 1:
            raise ValueError(f"`step_size` must be >= 1, not {step_size}")
        self.step_size = step_size

        # find appropriate numpy dtype
        if self.unit is None:
            numpy_type = np.dtype("m8")
        else:
            if self.step_size == 1:
                numpy_type = np.dtype(f"m8[{self.unit}]")
            else:
                numpy_type = np.dtype(f"m8[{self.step_size}{self.unit}]")

        # feed to ElementType constructor
        super(TimedeltaType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=np.timedelta64,
            numpy_type=numpy_type,
            pandas_type=None,
            slug=generate_m8_slug(
                base_type=type(self),
                unit=self.unit,
                step_size=self.step_size,
                sparse=sparse,
                categorical=categorical
            ),
            supertype=None,  # lazy-loaded
            subtypes=frozenset({self})
        )

        # min/max representable values in ns
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000

    @property
    def supertype(self) -> TimedeltaType:
        # cached
        if self._supertype is not None:
            return self._supertype

        # uncached
        self._supertype = TimedeltaType.instance(
            sparse=self.sparse,
            categorical=self.categorical
        )
        return self._supertype

    @classmethod
    def instance(
        cls,
        str unit = None,
        unsigned long long step_size = 1,
        bint sparse = False,
        bint categorical = False
    ) -> NumpyTimedelta64Type:
        """Flyweight constructor."""
        # consolidate numpy 'generic' and None units to prevent cache misses
        if unit == "generic":
            unit = None

        # generate slug
        cdef str slug = generate_m8_slug(
            base_type=cls,
            unit=unit,
            step_size=step_size,
            sparse=sparse,
            categorical=categorical
        )

        # compute hash
        cdef long long _hash = hash(slug)

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

