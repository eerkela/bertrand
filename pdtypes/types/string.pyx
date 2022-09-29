import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE

from .base cimport ElementType


##########################
####    SUPERTYPES    ####
##########################


cdef class StringType(ElementType):
    """string supertype"""

    def __init__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        str storage = None
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_nullable = True
        self.supertype = None
        self.subtypes = ()
        self.atomic_type = str

        if storage is None:
            self.extension_type = DEFAULT_STRING_DTYPE
            self.slug = "string"
        else:
            self.extension_type = pd.StringDtype(storage)
            self.slug = f"string[{storage}]"

    def __repr__(self) -> str:
        return (
            f"{self.__class__.__name__}("
            f"is_categorical={self.is_categorical}, "
            f"is_sparse={self.is_sparse}"
            f")"
        )

    def __str__(self) -> str:
        cdef str result = self.slug

        # append extensions
        if self.is_categorical:
            result = f"categorical[{result}]"
        if self.is_sparse:
            result = f"sparse[{result}]"

        return result
