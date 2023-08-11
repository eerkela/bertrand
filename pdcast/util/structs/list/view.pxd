"""Cython headers for pdcast/util/structs/list/view.h"""
from cpython.ref cimport PyObject

from libcpp.queue cimport queue
from libcpp.utility cimport pair

cdef extern from "view.h":
    const size_t MAX_SIZE_T
    size_t normalize_index[T](T index, size_t size, bint truncate)

    cdef cppclass ListView[T]:
        size_t size
        T* head
        T* tail
        ListView() except +
        ListView(PyObject* iterable, bint reverse, PyObject* spec) except +
        T* node(PyObject* value) except NULL
        void recycle(T* node)
        void link(T* prev, T* curr, T* next)
        void unlink(T* prev, T* curr, T* next)
        void clear()
        void specialize(PyObject* spec) except *
        PyObject* get_specialization()
        T* copy(T* curr) except NULL
        ListView[T]* copy() except NULL
        size_t nbytes()

    cdef cppclass SetView[T]:
        cppclass Node:
            Py_hash_t hash
            PyObject* value
            Node* next
            Node* prev
        size_t size
        Node* head
        Node* tail
        SetView() except +
        SetView(PyObject* iterable, bint reverse, PyObject* spec) except +
        Node* node(PyObject* value, PyObject* mapped) except NULL
        void recycle(Node* node)
        void link(Node* prev, Node* curr, Node* next) except *
        void unlink(Node* prev, Node* curr, Node* next) except *
        void clear() except *
        void specialize(PyObject* spec) except *
        PyObject* get_specialization()
        SetView[T]* copy() except NULL
        Node* copy(Node* curr) except NULL
        Node* search(PyObject* value) except? NULL
        Node* search(Node* value) except? NULL
        void clear_tombstones() except *
        size_t nbytes()

    cdef cppclass DictView[T]:
        cppclass Node:
            Py_hash_t hash
            PyObject* mapped
            PyObject* value
            Node* next
            Node* prev
        size_t size
        Node* head
        Node* tail
        DictView() except +
        DictView(PyObject* iterable, bint reverse, PyObject* spec) except +
        Node* node(PyObject* value) except NULL
        Node* node(PyObject* value, PyObject* mapped) except NULL
        void recycle(Node* node)
        void link(Node* prev, Node* curr, Node* next) except *
        void unlink(Node* prev, Node* curr, Node* next) except *
        void clear() except *
        void specialize(PyObject* spec) except *
        PyObject* get_specialization()
        Node* copy(Node* curr) except NULL
        DictView[T]* copy() except NULL
        Node* search(PyObject* value) except? NULL
        Node* search(Node* value) except? NULL
        Node* lru_search(PyObject* value) except? NULL
        void clear_tombstones() except *
        size_t nbytes()
