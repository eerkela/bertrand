import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes import DEFAULT_STRING_DTYPE

from .base cimport ElementType


cdef class BaseStringType(ElementType):
    """Base class for string types."""
    pass


##########################
####    SUPERTYPES    ####
##########################


cdef class StringType(BaseStringType):
    """string supertype"""

    def __cinit__(
        self,
        str storage = None
    ):
        self.supertype = None
        self.subtypes = ()
        self.atomic_type = str
        if storage is None:
            self.extension_type = DEFAULT_STRING_DTYPE
        else:
            self.extension_type = pd.StringDtype(storage)

    def __hash__(self) -> int:
        props = (
            self.__class__,
            self.is_categorical,
            self.is_sparse,
            self.is_extension,
            self.extension_type
        )
        return hash(props)
