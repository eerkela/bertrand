from functools import cache, lru_cache

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import resolve_dtype


# TODO: from_scalar, from_specifier classmethods.  Both call .instance()


cdef class ElementType:
    """Base class for type definitions."""

    def __cinit__(
        self,
        bint is_categorical = False,
        bint is_sparse = False,
        bint is_extension = False,
        *,
        **_
    ):
        self.is_categorical = is_categorical
        self.is_sparse = is_sparse
        self.is_extension = is_extension

    @cache
    @classmethod
    def instance(cls, **kwargs) -> ElementType:
        """Singleton constructor"""
        return cls(**kwargs)

    def __eq__(self, other) -> bool:
        return self is other

    def __contains__(self, other) -> bool:
        return type(other) in self.subtypes

    def __hash__(self) -> int:
        props = (
            self.__class__,
            self.is_categorical,
            self.is_sparse,
            self.is_extension
        )
        return hash(props)
