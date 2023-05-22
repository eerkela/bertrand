from .registry cimport BaseType


cdef class ScalarType(BaseType):
    cdef:
        dict _kwargs
        str _slug
        long long _hash


cdef class AliasManager:
    cdef readonly:
        set _aliases
