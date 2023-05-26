from .registry cimport BaseType, AliasManager
from .instance cimport SlugFactory, InstanceFactory


cdef class ScalarType(BaseType):
    cdef:
        dict _kwargs
        str _slug
        long long _hash
        bint _read_only

    cdef readonly:
        AliasManager _aliases
        SlugFactory slugify
        InstanceFactory instances

    cdef void init_base(self)
    cdef void init_parametrized(self)
