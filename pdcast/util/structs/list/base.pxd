"""Cython headers for pdcast/util/structs/base.h"""
from cpython.ref cimport PyObject
from libcpp.utility cimport pair


# NOTE: since the objects in this subpackage deal with direct memory allocation
# and reference counting, only a subset of the C++ API is exposed here.  These
# are mostly for manual testing and diagnostics from the Python side, and not
# for general use or anything that involves heavy lifting.  The C++ API is
# easier to interact with directly from C++, due to the heavy use of templates
# and static polymorphism.


cdef extern from "Python.h":
    void Py_INCREF(PyObject* obj)
    void Py_DECREF(PyObject* obj)


cdef extern from "core/allocate.h":
    cdef cppclass BaseAllocator:
        size_t allocated()
        size_t nbytes()

    cdef cppclass DirectAllocator(BaseAllocator):
        pass

    cdef cppclass FreeListAllocator(BaseAllocator):
        size_t reserved()

    cdef cppclass PreAllocator(BaseAllocator):
        size_t reserved()


cdef extern from "core/bounds.h":
    const size_t MAX_SIZE_T
    const pair[size_t, size_t] MAX_SIZE_T_PAIR

    size_t normalize_index[T](T index, size_t size, bint truncate) except? MAX_SIZE_T
    pair[size_t, size_t] normalize_bounds[T](
        T start, T stop, size_t size, bint truncate
    ) except? MAX_SIZE_T_PAIR


cdef extern from "core/node.h":
    struct SingleNode:
        PyObject* value
        SingleNode* next

    struct DoubleNode:
        PyObject* value
        DoubleNode* next
        DoubleNode* prev


cdef extern from "core/view.h":
    cdef cppclass ListView[T, U]:
        size_t size
        T* head
        T* tail
        ListView(ssize_t max_size) except +
        ListView(
            PyObject* iterable,
            bint reverse,
            PyObject* spec,
            ssize_t max_size
        ) except +
        T* node(PyObject* value) except NULL
        void recycle(T* node)
        void link(T* prev, T* curr, T* next)
        void unlink(T* prev, T* curr, T* next)
        void clear()
        void specialize(PyObject* spec) except *
        PyObject* get_specialization()
        T* copy(T* curr) except NULL
        ListView[T, U]* copy() except NULL
        size_t nbytes()

    cdef cppclass SetView[T, U]:
        cppclass Node:
            Py_hash_t hash
            PyObject* value
            Node* next
            Node* prev
        size_t size
        Node* head
        Node* tail
        SetView(ssize_t max_size) except +
        SetView(
            PyObject* iterable,
            bint reverse,
            PyObject* spec,
            ssize_t max_size
        ) except +
        Node* node(PyObject* value, PyObject* mapped) except NULL
        void recycle(Node* node)
        void link(Node* prev, Node* curr, Node* next) except *
        void unlink(Node* prev, Node* curr, Node* next) except *
        void clear() except *
        void specialize(PyObject* spec) except *
        PyObject* get_specialization()
        SetView[T, U]* copy() except NULL
        Node* copy(Node* curr) except NULL
        Node* search(PyObject* value) except? NULL
        Node* search(Node* value) except? NULL
        void clear_tombstones() except *
        size_t nbytes()

    cdef cppclass DictView[T, U]:
        cppclass Node:
            Py_hash_t hash
            PyObject* mapped
            PyObject* value
            Node* next
            Node* prev
        size_t size
        Node* head
        Node* tail
        DictView(ssize_t max_size) except +
        DictView(
            PyObject* iterable,
            bint reverse,
            PyObject* spec,
            ssize_t max_size
        ) except +
        Node* node(PyObject* value) except NULL
        Node* node(PyObject* value, PyObject* mapped) except NULL
        void recycle(Node* node)
        void link(Node* prev, Node* curr, Node* next) except *
        void unlink(Node* prev, Node* curr, Node* next) except *
        void clear() except *
        void specialize(PyObject* spec) except *
        PyObject* get_specialization()
        Node* copy(Node* curr) except NULL
        DictView[T, U]* copy() except NULL
        Node* search(PyObject* value) except? NULL
        Node* search(Node* value) except? NULL
        Node* lru_search(PyObject* value) except? NULL
        void clear_tombstones() except *
        size_t nbytes()
