
cdef class CacheValue:
    cdef readonly:
        object value
        long long hash


cdef class TypeRegistry:
    cdef:
        set base_types
        list alias_map
        long long _hash
        CacheValue _aliases
        CacheValue _regex
        CacheValue _resolvable

    cdef readonly:
        dict promises

    cdef void update_hash(self)
    cdef void pin(self, BaseType instance, AliasManager aliases)
    cdef void unpin(self, BaseType instance)


cdef class AliasManager:
    cdef:
        BaseType instance
        set aliases

    cdef object normalize_specifier(self, alias)
    cdef void pin(self)
    cdef void unpin(self)


cdef class BaseType:
    cdef readonly:
        AliasManager _aliases
