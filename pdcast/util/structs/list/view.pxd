"""Cython headers for pdcast/util/structs/list/view.h"""
from cpython.ref cimport PyObject

from libcpp.queue cimport queue
from libcpp.utility cimport pair

from .node cimport Hashed, Mapped

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
        size_t size
        Hashed[T]* head
        Hashed[T]* tail
        SetView() except +
        SetView(PyObject* iterable, bint reverse, PyObject* spec) except +
        Hashed[T]* node(PyObject* value, PyObject* mapped) except NULL
        void recycle(Hashed[T]* node)
        void link(Hashed[T]* prev, Hashed[T]* curr, Hashed[T]* next) except *
        void unlink(Hashed[T]* prev, Hashed[T]* curr, Hashed[T]* next) except *
        void clear() except *
        void specialize(PyObject* spec) except *
        PyObject* get_specialization()
        SetView[T]* copy() except NULL
        Hashed[T]* copy(Hashed[T]* curr) except NULL
        Hashed[T]* search(PyObject* value) except? NULL
        Hashed[T]* search(Hashed[T]* value) except? NULL
        void clear_tombstones() except *
        size_t nbytes()

    cdef cppclass DictView[T]:
        size_t size
        Mapped[T]* head
        Mapped[T]* tail
        DictView() except +
        DictView(PyObject* iterable, bint reverse, PyObject* spec) except +
        Mapped[T]* node(PyObject* value) except NULL
        Mapped[T]* node(PyObject* value, PyObject* mapped) except NULL
        void recycle(Mapped[T]* node)
        void link(Mapped[T]* prev, Mapped[T]* curr, Mapped[T]* next) except *
        void unlink(Mapped[T]* prev, Mapped[T]* curr, Mapped[T]* next) except *
        void clear() except *
        void specialize(PyObject* spec) except *
        PyObject* get_specialization()
        Mapped[T]* copy(Mapped[T]* curr) except NULL
        DictView[T]* copy() except NULL
        Mapped[T]* search(PyObject* value) except? NULL
        Mapped[T]* search(Mapped[T]* value) except? NULL
        Mapped[T]* lru_search(PyObject* value) except? NULL
        void clear_tombstones() except *
        size_t nbytes()
