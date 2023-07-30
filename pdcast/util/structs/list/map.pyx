


cdef ListView[DoubleNode]* view = new ListView[DoubleNode]()


for val in [1, 2, 3, 4, 5]:
    view.link(view.tail, view.allocate(<PyObject*> val), NULL)


del view