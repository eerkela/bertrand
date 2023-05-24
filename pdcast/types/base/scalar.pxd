from .registry cimport BaseType


cdef class ScalarType(BaseType):
    cdef:
        dict _kwargs
        str _slug
        long long _hash

    cdef str slugify(self, tuple args, dict kwargs)
