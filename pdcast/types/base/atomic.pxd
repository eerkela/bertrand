from .registry cimport CacheValue


##########################
####    PRIMITIVES    ####
##########################


cdef class BaseType:
    pass


cdef class ScalarType(BaseType):
    cdef readonly:
        object kwargs  # MappingProxyType
        str slug
        long long hash


######################
####    ATOMIC    ####
######################


cdef class AtomicType(ScalarType):
    cdef public:
        object _dtype

    cdef:
        CacheValue _generic_cache
        CacheValue _backend_cache
        CacheValue _subtype_cache
        CacheValue _supertype_cache
        bint _is_frozen
