

cdef class SlugFactory:
    cdef:
        str name
        tuple parameters


cdef class BackendSlugFactory(SlugFactory):
    cdef:
        str backend


cdef class InstanceFactory:
    cdef:
        type base_class


cdef class FlyweightFactory(InstanceFactory):
    cdef:
        dict instances
        SlugFactory slugify
