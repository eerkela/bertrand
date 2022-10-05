# constants
cdef dict keywords

# parsing functions
cdef dict parse_string(str typespec)
cdef object flatten(object nested)
cdef dict lookup(object tokens, str category)
