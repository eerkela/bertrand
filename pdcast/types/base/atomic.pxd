from .registry cimport CacheValue
from .scalar cimport ScalarType


cdef class AtomicType(ScalarType):
    cdef:
        object _dtype
        CacheValue _generic_cache
        CacheValue _backend_cache
        CacheValue _subtype_cache
        CacheValue _supertype_cache
        bint _is_frozen


cdef class GenericType(AtomicType):
    cdef public:
        type __wrapped__
        AtomicType _default
