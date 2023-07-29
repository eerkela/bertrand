# distutils: language = c++
from cpython.ref cimport PyObject
from libcpp.stack cimport stack
from libcpp.unordered_set cimport unordered_set


cdef extern from "node.h":
    struct SingleNode:
        PyObject* value
        SingleNode* next
        SingleNode* prev

    struct DoubleNode:
        PyObject* value
        DoubleNode* next
        DoubleNode* prev

    struct HashNode:
        PyObject* value
        Py_hash_t hash
        HashNode* next
        HashNode* prev

    struct DictNode:
        PyObject* value
        PyObject* mapped
        Py_hash_t hash
        DictNode* next
        DictNode* prev

    cdef cppclass ListView[T]:
        T* head
        T* tail
        size_t size
        ListView() except +
        T* allocate(PyObject* value) except +
        void deallocate(T* node)
        ListView[T]* stage(PyObject* iterable, bint reverse = False) except NULL
        void clear()
        unsigned char freelist_size()
        void link(T* prev, T* curr, T* next)
        void unlink(T* prev, T* curr, T* next)
        size_t normalize_index(long long index)

    cdef cppclass HashView[T]:
        T* head
        T* tail
        size_t size
        HashView() except +
        T* allocate(PyObject* value) except +
        void deallocate(T* node)
        ListView[T]* stage(
            PyObject* iterable,
            bint reverse = False,
            unordered_set[T]* override = NULL
        ) except NULL
        void clear()
        unsigned char freelist_size()
        void link(T* prev, T* curr, T* next)
        void unlink(T* prev, T* curr, T* next)
        size_t normalize_index(long long index)

    cdef cppclass DictView[T]:
        T* head
        T* tail
        size_t size
        DictView() except +
        T* allocate(PyObject* value, PyObject* mapped) except +
        void deallocate(T* node)
        ListView[T]* stage(
            PyObject* iterable,
            bint reverse = False,
            unordered_set[T]* override = NULL
        ) except NULL
        void clear()
        unsigned char freelist_size()
        void link(T* prev, T* curr, T* next)
        void unlink(T* prev, T* curr, T* next)
        size_t normalize_index(long long index)
