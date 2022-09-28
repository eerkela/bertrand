import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


cdef class BaseBooleanType(ElementType):
    """Base class for boolean types."""
    pass  # not used, just here to make issubclass() checks easier


##########################
####    SUPERTYPES    ####
##########################


cdef class BooleanType(BaseBooleanType):
    """Boolean supertype."""

    def __cinit__(self):
        self.supertype = None
        self.subtypes = ()
        self.atomic_type = bool
        self.extension_type = pd.BooleanDtype()
