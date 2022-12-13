cimport numpy as np

from pdtypes.util.structs cimport LRUDict


# constants
cdef unsigned short cache_size
cdef dict shared_registry
cdef LRUDict datetime64_registry
cdef LRUDict decimal_registry
cdef LRUDict object_registry
cdef LRUDict timedelta64_registry


cdef class AtomicType:
    cdef:
        frozenset _subtypes_cache
        long long _subtypes_hash
        type _supertype_cache
        long long _supertype_hash
        long long hash

    cdef readonly:
        str backend
        type object_type
        object na_value
        object itemsize
        str slug
        object dtype
