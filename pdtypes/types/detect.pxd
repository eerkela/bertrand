cimport numpy as np
cimport pdtypes.types.atomic as atomic


# functions
cdef atomic.AtomicType detect_scalar_type(object example)
cdef atomic.CompositeType detect_vector_type(np.ndarray[object] arr)
