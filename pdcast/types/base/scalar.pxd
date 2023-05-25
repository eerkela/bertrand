from .registry cimport BaseType


cdef class ScalarType(BaseType):
    cdef:
        dict _kwargs
        str _slug
        long long _hash


cdef class InstanceFactory:
    cdef:
        type base_class
        str name

    cdef readonly:
        tuple parameters

    # cdef str slugify(self, tuple args, dict kwargs)


cdef class NullFactory(InstanceFactory):
    pass


cdef class FlyweightFactory(InstanceFactory):
    cdef:
        dict instances

    cdef readonly:
        unsigned int cache_size


cdef class ImplementationFactory(FlyweightFactory):
    cdef:
        str backend
