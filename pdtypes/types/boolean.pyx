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
        bint categorical = False,
        bint sparse = False,
        bint nullable = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = nullable
        self.supertype = None
        self.subtypes = ()
        self.atomic_type = bool
        self.extension_type = pd.BooleanDtype()
        self.slug = "bool"

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"categorical={self.categorical}, "
            f"sparse={self.sparse}, "
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
