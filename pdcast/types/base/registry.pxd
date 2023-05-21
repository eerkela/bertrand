# from . cimport atomic


cdef class CacheValue:
    cdef readonly:
        object value
        long long hash


cdef class TypeRegistry:
    cdef:
        CacheValue _aliases
        CacheValue _regex
        CacheValue _resolvable
        list atomic_types
        long long _hash

    cdef int validate_aliases(self, object typ) except -1
    cdef int validate_name(self, object typ) except -1
    cdef int validate_slugify(self, object typ) except -1
    cdef void update_hash(self)
