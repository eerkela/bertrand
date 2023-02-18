cimport numpy as np


##########################
####    PRIMITIVES    ####
##########################


cdef class CacheValue:
    cdef readonly:
        object value
        long long hash


cdef class BaseType:
    pass


###########################
####    ATOMIC TYPE    ####
###########################


cdef class TypeRegistry:
    cdef:
        CacheValue _aliases
        CacheValue _dispatch_map
        CacheValue _regex
        CacheValue _resolvable
        list atomic_types
        long long hash

    cdef int validate_aliases(self, type subclass) except -1
    cdef int validate_dtype(self, type subclass) except -1
    cdef int validate_itemsize(self, type subclass) except -1
    cdef int validate_name(self, type subclass) except -1
    cdef int validate_na_value(self, type subclass) except -1
    cdef int validate_slugify(self, type subclass) except -1
    cdef int validate_type_def(self, type subclass) except -1
    cdef void update_hash(self)


cdef class AtomicType(BaseType):
    cdef:
        CacheValue _generic_cache
        CacheValue _subtype_cache
        CacheValue _supertype_cache
        bint _is_frozen

    cdef readonly:
        object kwargs
        str slug
        long long hash


cdef class AdapterType(AtomicType):
    cdef readonly:
        AtomicType atomic_type


##############################
####    COMPOSITE TYPE    ####
##############################


cdef class CompositeType(BaseType):
    cdef readonly:
        set atomic_types

    cdef public:
        np.ndarray index

    cdef void forget_index(self)