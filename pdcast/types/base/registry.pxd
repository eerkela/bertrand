
cdef class TypeRegistry:
    cdef:
        dict instances
        list pinned_aliases
        dict names
        dict defaults
        dict supertypes
        dict subtypes
        dict generics
        dict implementations
        long long _hash
        CacheValue _roots
        CacheValue _leaves
        CacheValue _families
        CacheValue _decorators
        CacheValue _abstract
        CacheValue _aliases
        CacheValue _regex
        CacheValue _resolvable
        PrioritySet _priority

    cdef void update_hash(self)
    cdef void validate_instance(self, typ)
    cdef void validate_name(self, typ)
    cdef void validate_type_def(self, typ)
    cdef void validate_dtype(self, typ)
    cdef void validate_itemsize(self, typ)
    cdef void validate_min_max(self, typ)
    cdef void validate_na_value(self, typ)


cdef class AliasManager:
    cdef:
        set aliases
        bint pinned

    cdef readonly:
        Type instance

    cdef object normalize_specifier(self, alias)


cdef class Type:
    cdef:
        AliasManager _aliases


cdef class CacheValue:
    cdef readonly:
        object value
        long long hash


cdef class PrioritySet(set):
    pass
