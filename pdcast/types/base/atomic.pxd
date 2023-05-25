from .registry cimport CacheValue
from .scalar cimport ScalarType
from .composite cimport CompositeType


cdef class AtomicType(ScalarType):
    cdef readonly:
        object _dtype
        CacheValue _generic_cache
        CacheValue _backend_cache
        CacheValue _subtype_cache
        CacheValue _supertype_cache
        bint _is_frozen


cdef class HierarchicalType(AtomicType):
    cdef:
        AtomicType _default

    cdef readonly:
        AtomicType __wrapped__


cdef class GenericType(HierarchicalType):
    cdef:
        CompositeType _subtypes
        dict _backends
