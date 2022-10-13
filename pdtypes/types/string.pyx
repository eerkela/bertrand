import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE, PYARROW_INSTALLED

from .base cimport compute_hash, ElementType, resolve_dtype, shared_registry


##########################
####    SUPERTYPES    ####
##########################


cdef class StringType(ElementType):
    """string supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        str storage = None
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = None
        self.subtypes = frozenset()
        self.atomic_type = str
        self.numpy_type = np.dtype(str)

        # get appropriate slug/extension type based on storage argument
        self.is_default_storage = storage is None
        if self.is_default_storage:
            self.pandas_type = DEFAULT_STRING_DTYPE
            self.slug = "string"
            self.storage = DEFAULT_STRING_DTYPE.storage
        else:
            self.pandas_type = pd.StringDtype(storage)
            self.slug = f"string[{storage}]"
            self.storage = storage

        # compute hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__,
            unit=None,
            step_size=1,
            storage=self.storage
        )

    @classmethod
    def instance(
        cls,
        bint sparse = False,
        bint categorical = False,
        str storage = None
    ) -> StringType:
        """Flyweight constructor."""
        # hash arguments
        cdef long long _hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=cls,
            unit=None,
            step_size=1,
            storage=storage
        )

        # get previous flyweight, if one exists
        cdef StringType result = shared_registry.get(_hash, None)

        if result is None:
            # construct new flyweight
            result = cls(
                sparse=sparse,
                categorical=categorical,
                storage=storage
            )
    
            # add flyweight to registry
            shared_registry[_hash] = result

        # return flyweight
        return result

    def __contains__(self, other) -> bool:
        other = resolve_dtype(other)
        if self.is_default_storage and type(other) == self.__class__:
            return (
                self.sparse == other.sparse and
                self.categorical == other.categorical
            )
        return self.__eq__(other) or other in self.subtypes

    def __repr__(self) -> str:
        cdef str storage
        
        if self.is_default_storage:
            storage = None
        else:
            storage = repr(self.storage)

        return (
            f"{self.__class__.__name__}("
            f"sparse={self.sparse}, "
            f"categorical={self.categorical}, "
            f"storage={storage}"
            f")"
        )
