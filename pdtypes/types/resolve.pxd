cimport numpy as np
cimport pdtypes.types.atomic as atomic


# constants
cdef dict na_strings
cdef str parens
cdef str brackets
cdef str curlies
cdef str call
cdef object tokenize_regex


# helpers
cdef str nested(str opener, str closer, str name)
cdef list tokenize(str input_str)
cdef atomic.BaseType resolve_typespec_string(str input_str)
cdef atomic.ScalarType resolve_typespec_dtype(object input_dtype)
cdef atomic.AtomicType resolve_typespec_type(type input_type)
