
cdef class CacheValue:
    cdef readonly:
        object value
        long long hash


cdef class TypeRegistry:
    cdef:
        set base_types
        long long _hash
        CacheValue _aliases
        CacheValue _regex
        CacheValue _resolvable

    cdef readonly:
        dict promises

    cdef void update_hash(self)


cdef class AliasManager:
    cdef:
        set aliases

    cdef object _normalize_specifier(self, alias)


cdef class BaseType:
    pass
