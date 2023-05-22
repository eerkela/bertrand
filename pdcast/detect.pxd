cimport numpy as np
cimport pdcast.types as types


# factories
cdef class TypeFactory:
    cdef:
        dict aliases


cdef class ScalarFactory(TypeFactory):
    cdef:
        object example
        type example_type


cdef class ArrayFactory(TypeFactory):

    cdef:
        object data
        object dtype
        bint skip_na


cdef class ElementWiseFactory(TypeFactory):

    cdef:
        object[:] data


# functions
cdef types.CompositeType detect_vector_type(object[:] arr, dict lookup)
