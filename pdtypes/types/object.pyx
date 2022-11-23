import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport CompositeType, compute_hash, ElementType


# TODO: confirm support for custom atomic types
# -> ensure no hash collisions and implement a new __contains__() method that
# disregards atomic_type if atomic_type is object


cdef class ObjectType(ElementType):
    """Object supertype"""

    def __init__(
        self,
        bint sparse = False,
        bint categorical = False,
        type atomic_type = object
    ):
        super(ObjectType, self).__init__(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            atomic_type=atomic_type,
            numpy_type=np.dtype("O"),
            pandas_type=None,
            slug="object",
            supertype=None,
            subtypes=CompositeType({self}, immutable=True)
        )

        # hash
        self.hash = compute_hash(
            sparse=sparse,
            categorical=categorical,
            nullable=True,
            base=self.__class__
        )
