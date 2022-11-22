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
        # super(BooleanType, self).__init__(
        #     sparse=sparse,
        #     categorical=categorical,
        #     supertype=None,
        #     subtypes=frozenset(),
        #     atomic_type=bool,
        #     numpy_type=np.dtype(bool),
        #     pandas_type=pd.BooleanDtype(),
        #     hash=compute_hash(
        #         sparse=sparse,
        #         categorical=categorical,
        #         nullable=nullable,
        #         base=self.__class__
        #     )
        # )
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = nullable
        self.supertype = None
        self.subtypes = frozenset()
        self.atomic_type = bool
        self.numpy_type = np.dtype(bool)
        self.pandas_type = pd.BooleanDtype()
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=nullable,
            base=self.__class__
        )

        # generate slug
        self.slug = "bool"
        if self.nullable:
            self.slug = f"nullable[{self.slug}]"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
        if not self.nullable:
            self.subtypes |= {
                self.__class__.instance(
                    sparse=sparse,
                    categorical=categorical,
                    nullable=True
                )
            }

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
