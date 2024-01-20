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
        bint drop_na


cdef class ElementWiseDetector(Detector):

    cdef:
        object[:] data
        np.ndarray missing
        bint drop_na
        bint hasnans


# functions
cdef types.CompositeType detect_vector_type(object[:] arr, dict lookup)
