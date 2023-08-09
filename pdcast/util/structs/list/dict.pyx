from timeit import timeit


# cdef dict d = {"a": 1, "b": 2, "c": 3, "d": 4, "e": 5}
# cdef object items = d.items()
# cdef DictView[DoubleNode]* view = new DictView[DoubleNode](<PyObject*>items, False)

# cdef object lookup(object val):
#     cdef Mapped[DoubleNode]* node = view.search(<PyObject*>val)
#     return <object>node.mapped


# print(timeit(lambda: lookup("c"), number=10**7))
# print(timeit(lambda: d["c"], number=10**7))



cdef object iterable = [3, 5, 2, 6, 1, 4]
cdef ListView[DoubleNode]* view = new ListView[DoubleNode](<PyObject*>iterable, False)


cdef object item = 3
print(count(view, <PyObject*>item, 0, 6))



del view
