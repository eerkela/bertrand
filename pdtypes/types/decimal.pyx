import decimal

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


cdef class BaseDecimalType(ElementType):
    """Base class for boolean types."""
    pass  # not used, just here to make issubclass() checks easier


##########################
####    SUPERTYPES    ####
##########################


cdef class DecimalType(BaseDecimalType):
    """Boolean supertype."""

    def __cinit__(self):
        if self.is_extension:
            raise ValueError("decimals have no valid extension type")
        self.supertype = None
        self.subtypes = ()
        self.atomic_type = decimal.Decimal
        self.extension_type = None
        self.min = -np.inf
        self.max = np.inf
