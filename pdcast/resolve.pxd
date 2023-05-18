cimport numpy as np
cimport pdcast.types as types


# constants
cdef dict na_strings
cdef object call
cdef object sequence
cdef object literal
cdef object token


# helpers
cdef str nested(str opener, str closer, str name)
cdef list tokenize(str input_str)
cdef types.BaseType resolve_string(str input_str)
cdef types.ScalarType resolve_class(type input_type)
cdef types.ScalarType resolve_numpy_dtype(object input_dtype)
cdef types.ScalarType resolve_pandas_dtype(object input_dtype)
