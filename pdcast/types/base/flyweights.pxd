

cdef class InstanceManager:
    cdef:
        type base_class


cdef class FlyweightManager(InstanceManager):
    cdef readonly:
        dict instances

cdef class NoInstanceManager(InstanceManager):
    pass
