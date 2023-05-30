
cdef class TypeRegistry:
    cdef:
        set base_types
        list pinned_aliases
        long long _hash
        CacheValue _aliases
        CacheValue _regex
        CacheValue _resolvable

    cdef readonly:
        dict promises

    cdef void update_hash(self)
    cdef void pin(self, Type instance, AliasManager aliases)
    cdef void unpin(self, Type instance)


cdef class AliasManager:
    cdef:
        set aliases

    cdef readonly:
        Type instance

    cdef object normalize_specifier(self, alias)
    cdef void pin(self)
    cdef void unpin(self)


cdef class Type:
    cdef:
        AliasManager _aliases


cdef class CacheValue:
    cdef readonly:
        object value
        long long hash


cdef class TypeMap:
    cdef:
        list map
