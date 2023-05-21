
cdef class CacheValue:
    cdef readonly:
        object value
        long long hash


cdef class TypeRegistry:
    cdef:
        CacheValue _aliases
        CacheValue _regex
        CacheValue _resolvable
        set base_types
        long long _hash

    cdef void update_hash(self)


cdef class BaseType:
    pass
