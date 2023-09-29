"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject
from libcpp.optional cimport optional, nullopt
from libcpp.stack cimport stack

from .base cimport SingleNode, DoubleNode


###################
####    C++    ####
###################


cdef extern from "core/allocate.h":
    cdef cppclass ListAllocator[Node]:
        ListAllocator(optional[size_t] capacity, PyObject* specialization) except +
        ListAllocator(const ListAllocator& other) except +
        ListAllocator& operator=(const ListAllocator& other) except +
        Node* create(...) except +
        void recycle(Node* node) except +
        void clear()
        void reserve(size_t new_capacity) except +
        void consolidate() except +
        bint owns(Node* node)
        void specialize(PyObject* spec) except +


cdef extern from "core/view.h":
    cdef cppclass ListView[Node = *, Allocator = *]:
        cppclass IteratorFactory:
            cppclass Iterator:
                Node* prev
                Node* curr
                Node* next
                Node* operator*()
                Iterator& operator++()
                bint operator!=(const Iterator& other)
                void insert(Node* node) except +
                Node* drop() except +
                void replace(Node* node) except +
            Iterator operator()()
            Iterator reverse()
            Iterator begin()
            Iterator end()
            Iterator rbegin()
            Iterator rend()

        ListView(optional[size_t] max_size = nullopt, PyObject* spec = NULL) except +
        ListView(
            PyObject* iterable,
            bint reverse = False,
            optional[size_t] max_size = nullopt,
            PyObject* spec = NULL
        ) except +
        Node* head()
        void head(Node* node) except +
        Node* tail()
        void tail(Node* node) except +
        Node* node(...) except +
        void recycle(Node* node) except +
        ListView copy() except +
        void clear()
        void link(Node* prev, Node* curr, Node* next)
        void unlink(Node* prev, Node* curr, Node* next)
        size_t size()
        size_t capacity()
        optional[size_t] max_size()
        void reserve(size_t capacity)
        void consolidate()
        void specialize(PyObject* spec) except +
        PyObject* specialization()
        size_t nbytes()
        IteratorFactory iter
        IteratorFactory.Iterator begin()
        IteratorFactory.Iterator end()
        IteratorFactory.Iterator rbegin()
        IteratorFactory.Iterator rend()


cdef extern from "list.h":
    cdef cppclass CppLinkedList "LinkedList"[Node = *, Sort = *, Lock = *]:
        # NOTE: renamed to avoid conflict with Cython equivalent.  The two are almost
        # identical, but the C++ version can store non-Python values and has slightly
        # higher performance.  Otherwise, they are exactly the same, and can be easily
        # ported from one to another.  In fact, the Cython class is just a thin wrapper
        # around a CppLinkedList that just casts its inputs to and from Python.
        cppclass IteratorFactory:
            cppclass Iterator:
                Node* operator*()
                Iterator& operator++()
                bint operator!=(const Iterator& other)
            Iterator operator()()
            Iterator reverse()
            Iterator begin()
            Iterator end()
            Iterator rbegin()
            Iterator rend()
            PyObject* python()
            PyObject* rpython()

        CppLinkedList(
            optional[size_t] max_size = nullopt,
            PyObject* spec = NULL
        ) except +
        CppLinkedList(
            PyObject* iterable,
            bint reverse = False,
            optional[size_t] max_size = nullopt,
            PyObject* spec = NULL
        ) except +
        CppLinkedList(const CppLinkedList& other) except +
        CppLinkedList& operator=(const CppLinkedList& other) except +

        bint empty()
        size_t size()
        size_t capacity
        optional[size_t] max_size()
        void reserve() except +
        void consolidate() except +
        PyObject* specialization()
        void specialize(PyObject* spec) except +
        size_t nbytes()
        IteratorFactory iter
        IteratorFactory.Iterator begin()
        IteratorFactory.Iterator end()
        IteratorFactory.Iterator rbegin()
        IteratorFactory.Iterator rend()



######################
####    CYTHON    ####
######################


cdef extern from "list_cython.h":
    cdef cppclass VariantList:
        # nested types/classes
        cppclass Lock:
            cppclass Guard:
                pass
            Guard operator()()
            Guard* context()
            size_t count()
            size_t duration()
            double contention()
            void reset_diagnostics()

        cppclass Index:
            PyObject* get() except +*
            void set(PyObject* item) except +*
            void delete "del" () except +*  # del() shadows Cython `delete` keyword

        cppclass Slice:
            VariantList* get() except +*
            void set(PyObject* items) except +*
            void delete "del" () except +*  # del() shadows Cython `delete` keyword

        # constructors
        VariantList(
            bint doubly_linked,
            optional[size_t] max_size,
            PyObject* spec
        ) except +
        VariantList(
            PyObject* iterable,
            bint doubly_linked,
            bint reverse,
            optional[size_t] max_size,
            PyObject* spec
        ) except +

        # functors
        const Lock lock  # lock() functor

        # list interface
        void append(PyObject* item, bint left) except +*
        void insert[T](T index, PyObject* item) except +*
        void extend(PyObject* items, bint left) except +*
        size_t index[T](PyObject* item, T start, T stop) except +*
        size_t count[T](PyObject* item, T start, T stop) except +*
        int contains(PyObject* item) except +*
        void remove(PyObject*) except *
        PyObject* pop[T](T index) except NULL
        VariantList* copy() except NULL
        void clear()
        void sort(PyObject* key, bint reverse) except +*
        void reverse()
        void rotate(ssize_t steps)
        size_t size()
        Index operator[](PyObject* index)
        Slice slice(PyObject* py_slice)

        # operator overloads
        # VariantList* concat(PyObject* rhs) except +
        # VariantList* rconcat(PyObject* lhs) except +
        # void iconcat(PyObject* rhs) except +
        # VariantList* repeat(PyObject* steps) except +
        # void irepeat(PyObject* steps) except +
        # bint lexicographic_lt(PyObject* other) except +
        # bint lexicographic_le(PyObject* other) except +
        # bint lexicographic_eq(PyObject* other) except +
        # bint lexicographic_ne(PyObject* other) except +
        # bint lexicographic_ge(PyObject* other) except +
        # bint lexicographic_gt(PyObject* other) except +

        # extra methods
        PyObject* specialization()
        void specialize(PyObject* spec) except +*
        size_t nbytes()
        PyObject* iter() except +*
        PyObject* riter() except +*


cdef class LinkedList:
    cdef:
        VariantList* variant
        object __weakref__  # allows LinkedList to be weak-referenced from Python

    @staticmethod
    cdef LinkedList from_variant(VariantList* variant)


cdef class ThreadGuard:
    cdef LinkedList parent
    cdef VariantList.Lock.Guard* context  # context manager threading lock
