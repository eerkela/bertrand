
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
        CacheValue _aliases
        CacheValue _regex
        CacheValue _resolvable

    cdef readonly:
        PriorityList decorator_priority

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


cdef class PriorityList:
    cdef:
        PriorityNode head
        PriorityNode tail
        dict items

    cdef void append(self, object item)
    cdef void remove(self, object item)
    cdef int normalize_index(self, int index)


cdef class PriorityNode:
    cdef public:
        object item
        PriorityNode next
        PriorityNode prev
