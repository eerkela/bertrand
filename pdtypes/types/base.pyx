from collections import OrderedDict
from functools import cache, lru_cache

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.check import resolve_dtype
from pdtypes.util.structs cimport LRUDict


cdef class ElementType:
    """Base class for type definitions.

    Attributes
    ----------
    is_categorical : bool
        `True` if ElementType represents categorical data.
    is_sparse : bool
        `True` if ElementType represents sparse data.
    is_nullable : bool
        `True` if ElementType can take missing/null values.
    supertype : type
        The supertype to which this ElementType is attached.  If the
        ElementType is itself a top-level supertype, this will be `None`.
    subtypes : tuple
        A tuple of subtypes associated with this ElementType
    atomic_type : type
        The atomic type associated with each of this type's indices in a numpy
        array or pandas series.  If the ElementType describes a scalar value,
        this will always be equivalent to `type(scalar)`.
    extension_type : pd.api.extensions.ExtensionDType
        A pandas extension type for the given ElementType, if it has one.
        These allow non-nullable types to accept missing values in the form of
        `pd.NA`, as well as exposing custom string storage backends, etc.
    hash : int
        A unique hash value based on the unique settings of this ElementType.
        This is used for caching operations and equality checks.
    slug : str
        A shortened string identifier (e.g. 'int64', 'bool', etc.) for this
        ElementType.
    """

    def __eq__(self, other) -> bool:
        # TODO: run resolve_dtype on `other`?
        return isinstance(other, ElementType) and hash(self) == hash(other)

    def __contains__(self, other) -> bool:
        # TODO: run resolve_dtype on `other`?
        if isinstance(other, type):
            return issubclass(other, self.__class__)
        return isinstance(other, self.__class__)

    def __hash__(self) -> int:
        return self.hash
