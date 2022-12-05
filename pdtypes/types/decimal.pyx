import decimal

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport ElementType, generate_slug, shared_registry


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
            na_value=pd.NA,
            itemsize=None,
            slug=generate_slug(
                base_type=type(self),
                sparse=sparse,
                categorical=categorical
            )
        )

        # min/max representable values
        self.min = -np.inf
        self.max = np.inf
