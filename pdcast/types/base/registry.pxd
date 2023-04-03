
cdef class CacheValue:
    cdef readonly:
        object value
        long long hash


cdef class TypeRegistry:
    cdef:
        CacheValue _aliases
        CacheValue _dispatch_map
        CacheValue _regex
        CacheValue _resolvable
        list atomic_types
        long long hash

    cdef int validate_aliases(self, type subclass) except -1
    cdef int validate_name(self, type subclass) except -1
    cdef int validate_slugify(self, type subclass) except -1
    cdef void update_hash(self)
