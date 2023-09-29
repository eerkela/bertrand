"""Cython headers for pdcast/util/structs/base.h"""
from cpython.ref cimport PyObject
from libcpp.optional cimport optional
from libcpp.utility cimport pair


# NOTE: since the objects in this subpackage deal with direct memory allocation
# and reference counting, only a subset of the C++ API is exposed here.  These
# are mostly for manual testing and diagnostics from the Python side, and not
# for general use or anything that involves heavy lifting.  The C++ API is
# easier to interact with directly from C++, due to the heavy use of templates
# and static polymorphism.

cdef extern from "core/util.h":
    cdef cppclass Slot[T](optional[T]):  # facilitates stack allocation in Cython
        void move(T* ptr) except +


cdef extern from "core/node.h":
    cdef cppclass BaseNode[T]:
        ctypedef T Value
        Value value()
        bint lt(Value other) except +
        bint le(Value other) except +
        bint eq(Value other) except +
        bint ne(Value other) except +
        bint ge(Value other) except +
        bint gt(Value other) except +
        bint typecheck(PyObject* specialization) except +

    cdef cppclass SingleNode[T](BaseNode[T]):
        ctypedef BaseNode[T].Value Value
        SingleNode(Value value)
        SingleNode(const SingleNode& other)
        SingleNode& operator=(const SingleNode& other)
        SingleNode* next()
        void next(SingleNode* next)
        @staticmethod
        void link(SingleNode* prev, SingleNode* curr, SingleNode* next)
        @staticmethod
        void unlink(SingleNode* prev, SingleNode* curr, SingleNode* next)
        @staticmethod
        void split(SingleNode* prev, SingleNode* curr)
        @staticmethod
        void join(SingleNode* prev, SingleNode* curr)

    cdef cppclass DoubleNode[T](BaseNode[T]):
        ctypedef BaseNode[T].Value Value
        DoubleNode(Value value)
        DoubleNode(const DoubleNode& other)
        DoubleNode& operator=(const DoubleNode& other)
        DoubleNode* next()
        void next(DoubleNode* next)
        DoubleNode* prev()
        void prev(DoubleNode* prev)
        @staticmethod
        void link(DoubleNode* prev, DoubleNode* curr, DoubleNode* next)
        @staticmethod
        void unlink(DoubleNode* prev, DoubleNode* curr, DoubleNode* next)
        @staticmethod
        void split(DoubleNode* prev, DoubleNode* curr)
        @staticmethod
        void join(DoubleNode* prev, DoubleNode* curr)


cdef extern from "core/view.h":
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
            Py_ssize_t max_size
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
            Py_ssize_t max_size
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
