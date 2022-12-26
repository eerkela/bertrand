cimport numpy as np

from pdtypes.util.structs cimport LRUDict


# constants
cdef type AliasInfo  # namedtuple
cdef type remember  # namedtuple


cdef class AtomicType:
    cdef:
        object _subtypes_cache
        object _supertype_cache
        

    cdef readonly:
        str backend
        type object_type
        object na_value
        object itemsize
        str slug
        object dtype
        long long hash


cdef class ElementType:
    cdef:
        long long hash

    cdef readonly:
        AtomicType atomic_type
        bint sparse
        bint categorical
        object index
        str slug


cdef class CompositeType:
    cdef readonly:
        set element_types
