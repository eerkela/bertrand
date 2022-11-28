import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport base_slugs, ElementType, shared_registry


cdef str generate_slug(
    type base_type,
    bint sparse,
    bint categorical,
    bint nullable
):
    """Return a unique slug string associated with the given `base_type`,
    accounting for `sparse`, `categorical`, and `nullable` flags.
    """
    cdef str slug = base_slugs[base_type]

    if nullable:
        slug = f"nullable[{slug}]"
    if categorical:
        slug = f"categorical[{slug}]"
    if sparse:
        slug = f"sparse[{slug}]"

    return slug


##########################
####    SUPERTYPES    ####
##########################


cdef class BooleanType(ElementType):
    """Boolean supertype."""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ):
        super(BooleanType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            atomic_type=bool,
            numpy_type=np.dtype(bool),
            pandas_type=pd.BooleanDtype(),
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            ),
            supertype=None,
            subtypes=None  # lazy-loaded
        )

    @property
    def subtypes(self) -> frozenset:
        # cached
        if self._subtypes is not None:
            return self._subtypes

        # uncached
        subtypes = {self}
        if not self.nullable:
            subtypes |= {
                self.__class__.instance(
                    sparse=self.sparse,
                    categorical=self.categorical,
                    nullable=True
                )
            }
        self._subtypes = frozenset(subtypes)
        return self._subtypes

    @classmethod
    def instance(
        cls,
        bint sparse = False,
        bint categorical = False,
        bint nullable = False
    ) -> BooleanType:
        """Flyweight constructor."""
        # generate slug
        cdef str slug = generate_slug(
            base_type=cls,
            sparse=sparse,
            categorical=categorical,
            nullable=nullable
        )

        # compute hash
        cdef long long _hash = hash(slug)

        # get previous flyweight, if one exists
        cdef BooleanType result = shared_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                sparse=sparse,
                categorical=categorical,
                nullable=nullable
            )
    
            # add flyweight to registry
            shared_registry[_hash] = result

        # return flyweight
        return result

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}, "
            f"nullable={self.nullable}"
            f")"
        )
