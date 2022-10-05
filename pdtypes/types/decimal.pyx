import decimal

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType


##########################
####    SUPERTYPES    ####
##########################


cdef class DecimalType(ElementType):
    """Decimal supertype."""

    def __init__(
        self,
        bint categorical = False,
        bint sparse = False
    ):
        self.categorical = categorical
        self.sparse = sparse
        self.nullable = True
        self.supertype = None
        self.subtypes = ()
        self.atomic_type = decimal.Decimal
        self.extension_type = None
        self.min = -np.inf
        self.max = np.inf
