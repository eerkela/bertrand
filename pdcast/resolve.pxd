
# constants
cdef dict na_strings
cdef object call
cdef object sequence
cdef object literal
cdef object token


# helpers
cdef str nested(str opener, str closer, str name)
cdef list tokenize(str input_str)


# factories
cdef class TypeFactory:
    cdef:
        dict aliases


cdef class ClassFactory(TypeFactory):
    cdef:
        type specifier


cdef class DtypeFactory(TypeFactory):
    cdef:
        object specifier


cdef class StringFactory(TypeFactory):
    cdef:
        str specifier
        object regex
