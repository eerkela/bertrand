cimport numpy as np
cimport pdcast.types as types


# functions
cdef types.AtomicType detect_scalar_type(object example)
cdef types.CompositeType detect_vector_type(np.ndarray[object] arr)