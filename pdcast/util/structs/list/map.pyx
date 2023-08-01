


cdef object iterable = [("a", 1), ("b", 2), ("c", 3), ("d", 4), ("e", 5)]
cdef DictView[DoubleNode]* view = new DictView[DoubleNode]()
cdef DictView[DoubleNode]* view2 = view.stage(<PyObject*>iterable)


cdef object lookup = "c"
cdef Mapped[DoubleNode]* node = view2.search(<PyObject*>lookup)
print(<object>node.mapped)


del view
del view2
