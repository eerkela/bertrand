
cdef class LinkedList:
    cdef readonly:
        ListNode head
        ListNode tail
        dict items

    cdef LinkedList copy(self)
    cdef void append(self, object item)
    cdef void appendleft(self, object item)
    cdef void insert(self, object item, long long index)
    cdef void extend(self, object items)
    cdef void extendleft(self, object items)
    cdef long long count(self, object item)
    cdef long long index(
        self,
        object item,
        long long start = *,
        long long stop = *
    )
    cdef void rotate(self, long long steps = *)
    cdef void reverse(self)
    cdef void remove(self, object item)
    cdef void clear(self)
    cdef object pop(self, long long index = *)
    cdef object popleft(self)
    cdef object popright(self)
    cdef ListNode _node_at_index(self, long long index)
    cdef long long _normalize_index(self, long long index)
    cdef (long long, long long) get_slice_direction(
        self,
        long long start,
        long long stop,
        long long step,
    )
    cdef void _drop_node(self, ListNode node)


cdef class ListNode:
    cdef readonly:
        ListNode next
        ListNode prev
        object item
