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
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = None
        self.subtypes = frozenset()
        self.atomic_type = bool
        self.numpy_type = np.dtype(bool)
        self.pandas_type = pd.BooleanDtype()
        self.slug = "bool"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

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

    def __str__(self) -> str:
        cdef str result = self.slug

        # append extensions
        if self.nullable:
            result = f"nullable[{result}]"
        if self.categorical:
            result = f"categorical[{result}]"
        if self.sparse:
            result = f"sparse[{result}]"

        return result
