import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport compute_hash, ElementType, shared_registry


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
            slug="nullable[bool]" if nullable else "bool",
            supertype=None,
            subtypes=None  # lazy-loaded
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
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
        # hash arguments
        cdef long long _hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=cls
        )

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
