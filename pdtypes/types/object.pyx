import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport compute_hash, ElementType


cdef class ObjectType(ElementType):
    """Object supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False
    ):
        self.sparse = sparse
        self.categorical = categorical
        self.nullable = True
        self.supertype = None
        self.atomic_type = None
        self.numpy_type = np.dtype("O")
        self.pandas_type = None
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )

        # generate slug
        self.slug = "object"
        if self.categorical:
            self.slug = f"categorical[{self.slug}]"
        if self.sparse:
            self.slug = f"sparse[{self.slug}]"

        # generate subtypes
        self.subtypes = frozenset((self,))
