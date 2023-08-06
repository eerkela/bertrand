


# cdef object iterable = [("a", 1), ("b", 2), ("c", 3), ("d", 4), ("e", 5)]
# cdef DictView[DoubleNode]* view
# view = view.stage(<PyObject*>iterable)


# from timeit import timeit

# cdef dict d = dict(iterable)
# cdef object lookup(object val):
#     cdef Mapped[DoubleNode]* node = view.search(<PyObject*>val)
#     return <object>node.mapped

# print(timeit(lambda: lookup("c"), number=10**7))
# print(timeit(lambda: d["c"], number=10**7))

# del view




# cdef object iterable = [3, 5, 2, 6, 1, 4]
# cdef ListView[SingleNode]* view
# view = view.stage(<PyObject*>iterable)


# key = lambda x: x
# sort(view, <PyObject*>key, False)


# del view

