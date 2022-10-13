from pdtypes.types.base cimport ElementType

# constants
cdef dict keywords

# parsing functions
cdef ElementType parse_typespec_string(str typespec)
cdef object flatten(object nested)
cdef dict lookup(
    str original_string,
    unsigned int location,
    object tokens
)
