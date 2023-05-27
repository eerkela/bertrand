from .registry cimport BaseType, AliasManager


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


cdef class SlugFactory:
    cdef:
        str name
        tuple parameters


cdef class BackendSlugFactory(SlugFactory):
    cdef:
        str backend


cdef class InstanceFactory:
    cdef:
        type base_class


cdef class FlyweightFactory(InstanceFactory):
    cdef:
        dict instances
        SlugFactory slugify
