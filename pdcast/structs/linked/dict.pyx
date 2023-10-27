from collections import OrderedDict
from timeit import timeit


# cdef dict d = {"a": 1, "b": 2, "c": 3, "d": 4, "e": 5}
# cdef object od = OrderedDict(d)
# cdef object items = d.items()
# cdef DictView[DoubleNode]* view = new DictView[DoubleNode](<PyObject*>items, False, NULL)


# cdef object lookup(object val):
#     cdef DictView[DoubleNode].Node* node = view.lru_search(<PyObject*>val)
#     return <object>node.mapped


# cdef object od_lru(object val):
#     cdef object value = od[val]
#     od.move_to_end(val, last=False)


# print(timeit(lambda: lookup("c"), number=10**7))
# print(timeit(lambda: d["c"], number=10**7))
# print(timeit(lambda: od_lru("c"), number=10**7))  # ~4.5x slower than lru_search()


cdef extern from "Python.h":
    void Py_INCREF(PyObject* obj)
    void Py_DECREF(PyObject* obj)



cdef object iterable = [1, 6, 3, 2, 5, 4]

# cdef ListView[DoubleNode, PreAllocator]* prealloc
# prealloc = new ListView[DoubleNode, PreAllocator](
#     <PyObject*>iterable, False, NULL, 31
# )

# cdef ListView[DoubleNode, DirectAllocator]* dynamic
# dynamic = new ListView[DoubleNode, DirectAllocator](
#     <PyObject*>iterable, False, NULL, -1
# )


cdef object item = 100
cdef bint left = False
# append(dynamic, <PyObject*>item, left)
# contains(dynamic, <PyObject*>item)
# count(dynamic, <PyObject*>item, 0, len(iterable))
# delete_index(dynamic, len(iterable))
# extend(dynamic, <PyObject*>iterable, left=left)


# del prealloc
# del dynamic



cdef VariantList* var = new VariantList(
    True,
    "freelist",
    <PyObject*>iterable,
    False,
    NULL,
    -1
)


var.append(<PyObject*>item, left)
print(bool(var.contains(<PyObject*>item)))
print(var.count(<PyObject*>item, 0, len(iterable)))


del var



# cdef void construct_prealloc(object iterable):
#     cdef ListView[DoubleNode, PreAllocator]* view
#     view = new ListView[DoubleNode, PreAllocator](
#         <PyObject*>iterable, False, NULL, len(iterable)
#     )
#     del view


# cdef void construct_dynamic(object iterable):
#     cdef ListView[DoubleNode, FreeListAllocator]* view
#     view = new ListView[DoubleNode, FreeListAllocator](
#         <PyObject*>iterable, False, NULL, -1
#     )
#     del view


# cdef void iterate_prealloc(ListView[DoubleNode, PreAllocator]* view):
#     cdef DoubleNode* curr = view.head
#     while curr is not NULL:
#         Py_INCREF(curr.value)
#         Py_DECREF(curr.value)
#         curr = curr.next


# cdef void iterate_dynamic(ListView[DoubleNode, FreeListAllocator]* view):
#     cdef DoubleNode* curr = view.head
#     while curr is not NULL:
#         Py_INCREF(curr.value)
#         Py_DECREF(curr.value)
#         curr = curr.next


# print(timeit(lambda: construct_prealloc(iterable), number=10))
# print(timeit(lambda: construct_dynamic(iterable), number=10))
# print(timeit(lambda: iterate_prealloc(prealloc), number=100))
# print(timeit(lambda: iterate_dynamic(dynamic), number=100))
