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
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = None
        self.atomic_type = decimal.Decimal
        self.numpy_type = None
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # generate slug
        self.slug = "decimal"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))

        # min/max representable values
        self.min = -np.inf
        self.max = np.inf
