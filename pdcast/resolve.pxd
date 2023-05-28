from pdcast cimport types


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
cdef class Resolver:
    cdef:
        dict aliases


cdef class ClassResolver(Resolver):
    cdef:
        type specifier


cdef class DtypeResolver(Resolver):
    cdef:
        object specifier


cdef class StringResolver(Resolver):
    cdef:
        str specifier

    cdef types.Type process_match(self, object match)
