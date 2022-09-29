import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


##########################
####    SUPERTYPES    ####
##########################


cdef class BooleanType(ElementType):
    """Boolean supertype."""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_nullable = False
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = is_nullable
        self.supertype = None
        self.subtypes = ()
        self.atomic_type = bool
        self.extension_type = pd.BooleanDtype()
        self.slug = "bool"

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"is_categorical={self.is_categorical}, "
            f"is_sparse={self.is_sparse}, "
            f"is_nullable={self.is_nullable}"
            f")"
        )

    def __str__(self) -> str:
        cdef str result = self.slug

        # append extensions
        if self.is_nullable:
            result = f"nullable[{result}]"
        if self.is_categorical:
            result = f"categorical[{result}]"
        if self.is_sparse:
            result = f"sparse[{result}]"

        return result
