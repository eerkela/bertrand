

cdef class InstanceFactory:
    cdef:
        type base_class


cdef class FlyweightFactory(InstanceFactory):
    cdef readonly:
        dict instances

cdef class NoInstanceFactory(InstanceFactory):
    pass
