import decimal

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport compute_hash, ElementType, shared_registry


##########################
####    SUPERTYPES    ####
##########################


cdef class DecimalType(ElementType):
    """Decimal supertype."""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        super(DecimalType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=decimal.Decimal,
            numpy_type=None,
            pandas_type=None,
            slug="decimal",
            supertype=None,
            subtypes=frozenset({self})
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # min/max representable values
        self.min = -np.inf
        self.max = np.inf
