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
        self.subtypes = frozenset()
        self.atomic_type = None
        self.numpy_dtype = np.dtype("O")
        self.pandas_type = None
        self.slug = "object"
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )
