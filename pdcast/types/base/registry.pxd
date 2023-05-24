
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


cdef class AliasManager:
    cdef:
        set aliases

    cdef int _check_specifier(self, alias: type_specifier) except -1
    cdef object _normalize_specifier(self, alias: type_specifier)


cdef class BaseType:
    pass
