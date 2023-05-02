from .registry cimport CacheValue


##########################
####    PRIMITIVES    ####
##########################


cdef class BaseType:
    pass


cdef class ScalarType(BaseType):
    cdef:
        dict _kwargs
        str _slug
        long long _hash


######################
####    ATOMIC    ####
######################


cdef class AtomicType(ScalarType):
    cdef:
        object _dtype
        CacheValue _generic_cache
        CacheValue _backend_cache
        CacheValue _subtype_cache
        CacheValue _supertype_cache
        bint _is_frozen
