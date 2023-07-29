


l = [1, 2, 3]
cdef ListView[DoubleNode]* parent = new ListView[DoubleNode]()
cdef ListView[DoubleNode]* child = parent.stage(<PyObject*>l)

print(child.normalize_index(-1))

del child
del parent