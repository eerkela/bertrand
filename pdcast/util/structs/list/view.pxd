"""Cython headers for pdcast/util/structs/list/view.h"""
from cpython.ref cimport PyObject

from libcpp.queue cimport queue

from .node cimport Hashed, Mapped

cdef extern from "view.h":
    const size_t MAX_SIZE_T
    size_t normalize_index(
        PyObject* index,
        size_t size,
        bint truncate
    ) except? MAX_SIZE_T

    cdef cppclass ListView[T]:
        T* head
        T* tail
        size_t size
        ListView() except +
        ListView(PyObject* iterable, bint reverse = False) except +
        T* allocate(PyObject* value) except +
        void deallocate(T* node)
        void link(T* prev, T* curr, T* next)
        void unlink(T* prev, T* curr, T* next)
        void clear()
        ListView[T]* copy() except +
        T* copy(T* curr) except +
        size_t nbytes()

    cdef cppclass SetView[T]:
        Hashed[T]* head
        Hashed[T]* tail
        size_t size
        SetView() except +
        SetView(PyObject* iterable, bint reverse = False) except +
        Hashed[T]* allocate(PyObject* value, PyObject* mapped) except +*
        void deallocate(Hashed[T]* node)
        void link(Hashed[T]* prev, Hashed[T]* curr, Hashed[T]* next) except +*
        void unlink(Hashed[T]* prev, Hashed[T]* curr, Hashed[T]* next) except *
        void clear() except +
        SetView[T]* copy() except +
        Hashed[T*] copy(Hashed[T]* curr) except +
        Hashed[T]* search(PyObject* value) except? NULL
        Hashed[T]* search(Hashed[T]* value) except? NULL
        void clear_tombstones() except +
        size_t nbytes()

    cdef cppclass DictView[T]:
        Mapped[T]* head
        Mapped[T]* tail
        size_t size
        DictView() except +
        DictView(PyObject* iterable, bint reverse = False) except +
        Mapped[T]* allocate(PyObject* value) except +*
        Mapped[T]* allocate(PyObject* value, PyObject* mapped) except +*
        void deallocate(Mapped[T]* node)
        void link(Mapped[T]* prev, Mapped[T]* curr, Mapped[T]* next) except +*
        void unlink(Mapped[T]* prev, Mapped[T]* curr, Mapped[T]* next) except *
        void clear() except +
        DictView[T]* copy() except +
        Mapped[T*] copy(Mapped[T]* curr) except +
        Mapped[T]* search(PyObject* value) except? NULL
        Mapped[T]* search(Mapped[T]* value) except? NULL
        void clear_tombstones() except +
        size_t nbytes()
