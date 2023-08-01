# distutils: language = c++
from cpython.ref cimport PyObject
from libcpp.stack cimport stack
from libcpp.unordered_set cimport unordered_set
from libcpp.utility cimport pair


cdef extern from "node.h":
    struct SingleNode:
        PyObject* value
        SingleNode* next
        SingleNode* prev

    struct DoubleNode:
        PyObject* value
        DoubleNode* next
        DoubleNode* prev

    cdef cppclass Hashed[T]:
        PyObject* value
        Py_hash_t hash
        T* next
        T* prev

    cdef cppclass Mapped[T]:
        PyObject* value
        PyObject* mapped
        Py_hash_t hash
        T* next
        T* prev

    cdef cppclass ListView[T]:
        T* head
        T* tail
        size_t size
        ListView() except +
        T* allocate(PyObject* value) except +
        void deallocate(T* node)
        unsigned char freelist_size()
        void link(T* prev, T* curr, T* next)
        void unlink(T* prev, T* curr, T* next)
        void clear()
        ListView[T]* copy() except +
        ListView[T]* stage(PyObject* iterable, bint reverse = False) except NULL
        size_t nbytes()

    cdef cppclass SetView[T]:
        Hashed[T]* head
        Hashed[T]* tail
        size_t size
        SetView() except +
        Hashed[T]* allocate(PyObject* value, PyObject* mapped) except +
        void deallocate(Hashed[T]* node)
        unsigned char freelist_size()
        void link(Hashed[T]* prev, Hashed[T]* curr, Hashed[T]* next) except +*
        void unlink(Hashed[T]* prev, Hashed[T]* curr, Hashed[T]* next) except *
        void clear() except +
        SetView[T]* copy() except +
        SetView[T]* stage(PyObject* iterable, bint reverse = False) except NULL
        Hashed[T]* search(PyObject* value) except? NULL
        Hashed[T]* search(Hashed[T]* value) except? NULL
        void clear_tombstones() except +
        size_t nbytes()

    cdef cppclass DictView[T]:
        Mapped[T]* head
        Mapped[T]* tail
        size_t size
        DictView() except +
        Mapped[T]* allocate(PyObject* value, PyObject* mapped) except +
        void deallocate(Mapped[T]* node)
        unsigned char freelist_size()
        void link(Mapped[T]* prev, Mapped[T]* curr, Mapped[T]* next) except +*
        void unlink(Mapped[T]* prev, Mapped[T]* curr, Mapped[T]* next) except *
        void clear() except +
        DictView[T]* copy() except +
        DictView[T]* stage(PyObject* iterable, bint reverse = False) except NULL
        Mapped[T]* search(PyObject* value) except? NULL
        Mapped[T]* search(Mapped[T]* value) except? NULL
        void clear_tombstones() except +
        size_t nbytes()
