cimport numpy as np

# classes
cdef class NullValue:
    pass


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
    cdef int validate_replace(self, type subclass) except -1
    cdef int validate_slugify(self, type subclass) except -1
    cdef void update_hash(self)


cdef class AtomicType(BaseType):
    cdef:
        object _subtypes_cache
        object _supertype_cache

    cdef readonly:
        str backend
        type object_type
        object dtype
        object na_value
        object itemsize
        str slug
        long long hash


cdef class ElementType:
    cdef:
        long long hash

    cdef readonly:
        AtomicType atomic_type
        bint sparse
        bint categorical
        object index
        str slug


cdef class CompositeType(BaseType):
    cdef readonly:
        set element_types


# constants
cdef NullValue null
cdef type AliasInfo  # namedtuple
cdef type CacheValue  # namedtuple
