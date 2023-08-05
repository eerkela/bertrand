"""Cython headers for pdcast/util/structs/list/view.h"""
from cpython.ref cimport PyObject

from libcpp.queue cimport queue

from .node cimport Hashed, Mapped

cdef extern from "view.h":
    size_t MAX_SIZE_T
    size_t normalize_index(
        PyObject* index,
        size_t size,
        bint truncate
    ) except? MAX_SIZE_T

    cdef cppclass ListView[T]:
        queue[T*] freelist
        T* head
        T* tail
        size_t size
        ListView() except NULL
        ListView(PyObject* iterable, bint reverse = False) except NULL
        T* allocate(PyObject* value) except NULL
        void deallocate(T* node)
        void link(T* prev, T* curr, T* next)
        void unlink(T* prev, T* curr, T* next)
        void clear()
        ListView[T]* copy() except +
        size_t nbytes()

    cdef cppclass SetView[T]:
        queue[Hashed[T]*] freelist
        Hashed[T]* head
        Hashed[T]* tail
        size_t size
        SetView() except NULL
        SetView(PyObject* iterable, bint reverse = False) except NULL
        Hashed[T]* allocate(PyObject* value, PyObject* mapped) except NULL
        void deallocate(Hashed[T]* node)
        void link(Hashed[T]* prev, Hashed[T]* curr, Hashed[T]* next) except +*
        void unlink(Hashed[T]* prev, Hashed[T]* curr, Hashed[T]* next) except *
        void clear() except +
        SetView[T]* copy() except +
        Hashed[T]* search(PyObject* value) except? NULL
        Hashed[T]* search(Hashed[T]* value) except? NULL
        void clear_tombstones() except +
        size_t nbytes()

    cdef cppclass DictView[T]:
        queue[Mapped[T]*] freelist
        Mapped[T]* head
        Mapped[T]* tail
        size_t size
        DictView() except NULL
        DictView(PyObject* iterable, bint reverse = False) except NULL
        Mapped[T]* allocate(PyObject* value) except NULL
        Mapped[T]* allocate(PyObject* value, PyObject* mapped) except NULL
        void deallocate(Mapped[T]* node)
        void link(Mapped[T]* prev, Mapped[T]* curr, Mapped[T]* next) except +*
        void unlink(Mapped[T]* prev, Mapped[T]* curr, Mapped[T]* next) except *
        void clear() except +
        DictView[T]* copy() except +
        Mapped[T]* search(PyObject* value) except? NULL
        Mapped[T]* search(Mapped[T]* value) except? NULL
        void clear_tombstones() except +
        size_t nbytes()
