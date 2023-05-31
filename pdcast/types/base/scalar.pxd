from .registry cimport Type, AliasManager


cdef class ScalarType(Type):
    cdef:
        dict _kwargs
        str _slug
        long long _hash
        bint _read_only

    cdef readonly:
        ArgumentEncoder encoder
        InstanceFactory instances
        ScalarType base_instance

    cdef void init_base(self)
    cdef void init_parametrized(self)


cdef class ArgumentEncoder:
    cdef:
        str name
        tuple parameters


cdef class BackendEncoder(ArgumentEncoder):
    cdef:
        str backend


cdef class InstanceFactory:
    cdef:
        type base_class


cdef class FlyweightFactory(InstanceFactory):
    cdef:
        dict instances
        ArgumentEncoder encoder
