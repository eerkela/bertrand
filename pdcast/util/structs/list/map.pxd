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
        void clear()
        unsigned char freelist_size()
        void link(T* prev, T* curr, T* next)
        void unlink(T* prev, T* curr, T* next)

    cdef cppclass DictView[T]:
        T* head
        T* tail
        size_t size
        DictView() except +
        T* allocate(PyObject* value, PyObject* mapped) except +
        void deallocate(T* node)
        void clear()
        unsigned char freelist_size()
        void link(T* prev, T* curr, T* next)
        void unlink(T* prev, T* curr, T* next)





# TODO: Cython can't handle nested templates like what we're trying to do here.
# Instead, we can define a typedef in the C++ code itself and then use that
# directly.


# template <typename T>
# using ListViewOps = ListOps<ListView, T>;


# cdef extern from "index.h":
#     cdef cppclass ListOps[ViewType, NodeType]:
#         size_t normalize_index(long long index)
#         pair[size_t, size_t] get_slice_direction(
#             size_t start,
#             size_t stop,
#             ssize_t step
#         )
#         NodeType* node_at_index(size_t index)
#         ViewType[NodeType]* stage(
#             PyObject* iterable,
#             bint reverse = False,
#             unordered_set[NodeType]* override = NULL
#         ) except NULL
#         ViewType[NodeType]* get_slice(
#             size_t start,
#             size_t stop,
#             ssize_t step
#         ) except +
#         int set_slice(
#             size_t start,
#             size_t stop,
#             ssize_t step,
#             PyObject* iterator
#         ) except -1
#         void delete_slice(size_t start, size_t stop, ssize_t step)
