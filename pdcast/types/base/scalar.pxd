from .registry cimport BaseType, AliasManager
from .instance cimport SlugFactory, InstanceFactory


cdef class ScalarType(BaseType):
    cdef:
        dict _kwargs
        str _slug
        long long _hash

    cdef readonly:
        AliasManager _aliases
        SlugFactory _slugify
        InstanceFactory _instances

    cdef void init_base(self)
    cdef void init_parametrized(self)
