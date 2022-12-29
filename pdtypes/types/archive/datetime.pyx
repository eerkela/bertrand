from cpython cimport datetime

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport (
    base_slugs, CompositeType, datetime64_registry, ElementType, generate_slug,
    resolve_dtype, shared_registry
)


# Adopting the callable pattern in resolve_dtype mini-language would force
# only one datetime type with the same universal `backend` arg.



cdef str generate_M8_slug(
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


cdef class DatetimeType(ElementType):
    """Datetime supertype"""

    _base_slug = "datetime"
    aliases = {
        # string
        "datetime": {},
    }

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
            na_value=pd.NaT,
            itemsize=None,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            )
        )

        # min/max representable values in ns
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000

    @property
    def subtypes(self) -> frozenset:
        if self._subtypes is None:
            self._subtypes = frozenset({self})
            self._subtypes |= {
                t.instance(sparse=self.sparse, categorical=self.categorical)
                for t in (
                    PandasTimestampType, PyDatetimeType, NumpyDatetime64Type
                )
            }
        return self._subtypes

    def __contains__(self, other) -> bool:
        """Test whether the given type specifier is a subtype of this
        ElementType.
        """
        valid_M8 = lambda t: (
            isinstance(t, NumpyDatetime64Type) and
            (self.sparse == t.sparse and self.categorical == t.categorical)
        )
        return all(
            valid_M8(t) or t in self.subtypes
            for t in CompositeType(other)
        )


########################
####    SUBTYPES    ####
########################


cdef class PandasTimestampType(DatetimeType):
    """`pandas.Timestamp` datetime subtype"""

    # TODO: fill these out when alias pattern gets more stable
    _base_slug = None
    aliases = {}

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
            na_value=pd.NaT,
            itemsize=None,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            )
        )

        # min/max representable values in ns
        self.min = -2**63 + 1
        self.max = 2**63 - 1

    @property
    def subtypes(self) -> frozenset:
        return super(DatetimeType, self).subtypes

    @property
    def supertype(self) -> DatetimeType:
        if self._supertype is None:
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

    # TODO: fill these out when alias pattern gets more stable
    _base_slug = None
    aliases = {}

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
            na_value=pd.NaT,
            itemsize=None,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            )
        )

        # min/max representable values in ns
        self.min = -62135596800000000000
        self.max = 253402300799999999000

    @property
    def subtypes(self) -> frozenset:
        return super(DatetimeType, self).subtypes

    @property
    def supertype(self) -> DatetimeType:
        if self._supertype is None:
            self._supertype = DatetimeType.instance(
                sparse=self.sparse,
                categorical=self.categorical
            )
        return self._supertype

    def __contains__(self, other) -> bool:
        return super(DatetimeType, self).__contains__(other)


cdef class NumpyDatetime64Type(DatetimeType):
    """`numpy.datetime64` datetime subtype"""

    # TODO: fill these out when alias pattern gets more stable
    _base_slug = None
    aliases = {}

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
            numpy_type = np.dtype("M8")
        else:
            if self.step_size == 1:
                numpy_type = np.dtype(f"M8[{self.unit}]")
            else:
                numpy_type = np.dtype(f"M8[{self.step_size}{self.unit}]")

        # feed to ElementType constructor
        super(DatetimeType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=np.datetime64,
            numpy_type=numpy_type,
            pandas_type=None,
            na_value=pd.NaT,
            itemsize=8,
            slug=generate_M8_slug(
                base_type=type(self),
                unit=self.unit,
                step_size=self.step_size,
                sparse=sparse,
                categorical=categorical
            )
        )

        # min/max representable values in ns
        self.min = -291061508645168391112243200000000000
        self.max = 291061508645168328945024000000000000

    @classmethod
    def instance(
        cls,
        str unit = None,
        unsigned long long step_size = 1,
        bint sparse = False,
        bint categorical = False
    ) -> NumpyDatetime64Type:
        """Flyweight constructor."""
        # consolidate numpy 'generic' and None units to prevent cache misses
        if unit == "generic":
            unit = None

        # generate slug
        cdef str slug = generate_M8_slug(
            base_type=cls,
            unit=unit,
            step_size=step_size,
            sparse=sparse,
            categorical=categorical
        )

        # compute hash
        cdef long long _hash = hash(slug)

        # get previous flyweight, if one exists
        cdef NumpyDatetime64Type result = datetime64_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                unit=unit,
                step_size=step_size,
                sparse=sparse,
                categorical=categorical
            )
    
            # add flyweight to registry
            datetime64_registry[_hash] = result

        # return flyweight
        return result

    @property
    def subtypes(self) -> frozenset:
        return super(DatetimeType, self).subtypes

    @property
    def supertype(self) -> DatetimeType:
        if self._supertype is None:
            self._supertype = DatetimeType.instance(
                sparse=self.sparse,
                categorical=self.categorical
            )
        return self._supertype

    def __contains__(self, other) -> bool:
        """Test whether the given type specifier is a subtype of this
        ElementType.
        """
        valid_M8 = lambda t: (
            self.unit is None and
            isinstance(t, type(self)) and
            (self.sparse == t.sparse and self.categorical == t.categorical)
        )
        return all(
            valid_M8(t) or t in self.subtypes
            for t in CompositeType(other)
        )

    def __repr__(self) -> str:
        return (
            f"{type(self).__name__}("
            f"unit={repr(self.unit)}, "
            f"step_size={self.step_size}, "
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}"
            f")"
        )
