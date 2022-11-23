cimport numpy as np

from pdtypes.util.structs cimport LRUDict


# constants
cdef unsigned short cache_size
cdef dict shared_registry
cdef LRUDict datetime64_registry
cdef LRUDict timedelta64_registry
cdef str default_string_storage


# helpers
cdef long long compute_hash(
    bint sparse = *,
    bint categorical = *,
    bint nullable = *,
    type base = *,
    str unit = *,
    unsigned long long step_size = *,
    str storage = *
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
        np.ndarray index
        bint immutable


cdef class ElementType:
    cdef:
        ElementType _supertype
        CompositeType _subtypes

    cdef readonly:
        bint sparse
        bint categorical
        bint nullable
        type atomic_type
        object numpy_type
        object pandas_type
        long long hash
        str slug
