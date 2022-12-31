cimport numpy as np


cdef class BaseType:
    pass


cdef class AtomicTypeRegistry:
    cdef:
        list atomic_types
        long long int hash
        object _aliases
        object _regex
        object _resolvable

    cdef int validate_aliases(self, type subclass) except -1
    cdef int validate_name(self, type subclass) except -1
    cdef int validate_slugify(self, type subclass) except -1
    cdef void update_hash(self)


cdef class AtomicType(BaseType):
    cdef:
        object _subtypes
        object _supertype
        bint _is_frozen

    cdef readonly:
        type type_def
        object dtype
        object na_value
        object itemsize
        str slug
        long long hash


cdef class CompositeType(BaseType):
    cdef readonly:
        set element_types


# constants
cdef type AliasInfo  # namedtuple
cdef type CacheValue  # namedtuple


# functions
cdef set traverse_subtypes(type atomic_type)
