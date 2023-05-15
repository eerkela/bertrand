cimport numpy as np


# functions
cpdef np.ndarray as_array(object data)
cpdef object as_series(object data)
cpdef object apply_with_errors(object series, object call, str errors)
