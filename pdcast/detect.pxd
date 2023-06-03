cimport numpy as np
from pdcast cimport types


# factories
cdef class Detector:
    cdef:
        dict aliases


cdef class ScalarDetector(Detector):
    cdef:
        object example
        type example_type


cdef class ArrayDetector(Detector):

    cdef:
        object data
        object dtype
        bint skip_na


cdef class ElementWiseDetector(Detector):

    cdef:
        object[:] data


# functions
cdef types.CompositeType detect_vector_type(object[:] arr, dict lookup)
