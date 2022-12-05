cimport numpy as np

from pdtypes.util.structs cimport LRUDict


# constants
cdef unsigned short cache_size
cdef dict shared_registry
cdef LRUDict datetime64_registry
cdef LRUDict decimal_registry
cdef LRUDict object_registry
cdef LRUDict timedelta64_registry
cdef dict base_slugs


# helpers
cdef str generate_slug(
    type base_type,
    bint sparse,
    bint categorical
)


# factory functions
cpdef ElementType resolve_dtype(
    object typespec,
    bint sparse = *,
    bint categorical = *,
    bint nullable = *
)


# classes
cdef class CompositeType(set):
    cdef readonly:
        set types
        np.ndarray index


cdef class ElementType:
    cdef:
        ElementType _supertype
        frozenset _subtypes
        long long hash

    cdef readonly:
        bint sparse
        bint categorical
        bint nullable
        type atomic_type
        object numpy_type
        object pandas_type
        object na_value
        object itemsize
        str slug
