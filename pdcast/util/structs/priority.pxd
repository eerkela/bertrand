
cdef class PriorityList:
    cdef:
        PriorityNode head
        PriorityNode tail
        dict items

    cdef void append(self, object item)
    cdef void remove(self, object item)
    cdef int normalize_index(self, int index)


cdef class PriorityNode:
    cdef public:
        object item
        PriorityNode next
        PriorityNode prev
