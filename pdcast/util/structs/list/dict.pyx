from collections import OrderedDict
from timeit import timeit


cdef dict d = {"a": 1, "b": 2, "c": 3, "d": 4, "e": 5}
cdef object od = OrderedDict(d)
cdef object items = d.items()
cdef DictView[DoubleNode]* view = new DictView[DoubleNode](<PyObject*>items, False, NULL)

cdef object lookup(object val):
    cdef DictView[DoubleNode].Node* node = view.lru_search(<PyObject*>val)
    return <object>node.mapped

cdef object od_lru(object val):
    cdef object value = od[val]
    od.move_to_end(val, last=False)


print(timeit(lambda: lookup("c"), number=10**7))
print(timeit(lambda: d["c"], number=10**7))
print(timeit(lambda: od_lru("c"), number=10**7))  # ~4.5x slower than lru_search()



# cdef object iterable = [3, 5, 2, 6, 1, 4]
# cdef SetView[DoubleNode]* view = new SetView[DoubleNode](<PyObject*>iterable, False, NULL)


del view
