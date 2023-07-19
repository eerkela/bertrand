from cpython.ref cimport PyObject


cdef packed struct ListStruct:
    PyObject* value
    ListStruct* next
    ListStruct* prev
    size_t ref_count  # we need a manual refcounter to avoid dangling pointers


cdef ListStruct* allocate_struct(PyObject* value)
cdef void incref(ListStruct* c_struct)
cdef void decref(ListStruct* c_struct)
cdef ListNode node_from_struct(ListStruct* c_struct)


cdef class ListNode:
    cdef:
        ListStruct* c_struct


cdef class LinkedList:
    cdef:
        long long size

    cdef public:
        ListNode head
        ListNode tail

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
    cdef void sort(self)
    cdef void rotate(self, long long steps = *)
    cdef void reverse(self)
    cdef void remove(self, object item)
    cdef void clear(self)
    cdef object pop(self, long long index = *)
    cdef object popleft(self)
    cdef object popright(self)
    cdef void _add_node(self, ListNode node, ListNode prev, ListNode next)
    cdef void _remove_node(self, ListNode node)
    cdef ListNode _node_at_index(self, long long index)
    cdef long long _normalize_index(self, long long index)
    cdef (long long, long long) _get_slice_direction(
        self,
        long long start,
        long long stop,
        long long step,
    )
    cdef ListNode _split(self, ListNode head, long long length)
    cdef tuple _merge(self, ListNode left, ListNode right, ListNode temp)


cdef class HashedList(LinkedList):
    cdef readonly:
        dict nodes
