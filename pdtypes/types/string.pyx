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
        bint categorical = False,
        bint sparse = False,
        str storage = None
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
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
            f"categorical={self.categorical}, "
            f"sparse={self.sparse}"
            f")"
        )

    def __str__(self) -> str:
        cdef str result = self.slug

        # append extensions
        if self.categorical:
            result = f"categorical[{result}]"
        if self.sparse:
            result = f"sparse[{result}]"

        return result
