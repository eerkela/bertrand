"""Cython headers for pdcast/util/structs/list/double.pyx"""
from cpython.ref cimport PyObject
from libcpp.optional cimport optional, nullopt
from libcpp.stack cimport stack

from .base cimport Slot


# NOTE: LinkedList is a compound class with both a C++ and Cython implementation. Both
# versions are virtually identical, but the C++ implementation can store non-Python
# values and has slightly higher performance due to reduced indirection.  Otherwise,
# they are exactly the same, and can be easily ported from one to another.  In fact,
# the Cython class is actually just a thin wrapper around a C++ LinkedList that casts
# its inputs to and from a Python runtime.  Different template configurations can be
# selected via constructor arguments in the Cython class.


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
        void defragment() except +
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
        void defragment()
        void specialize(PyObject* spec) except +
        PyObject* specialization()
        size_t nbytes()
        IteratorFactory iter
        IteratorFactory.Iterator begin()
        IteratorFactory.Iterator end()
        IteratorFactory.Iterator rbegin()
        IteratorFactory.Iterator rend()


cdef extern from "list.h" namespace "bertrand::structs":
    # NOTE: C++ LinkedLists are renamed to avoid conflict with the Cython class of the
    # same name.
    cdef cppclass CppLinkedList "LinkedList" [T, Node = *, Sort = *, Lock = *]:
        # NOTE: Cython isn't smart enough to allow access to T through Node.Value, so
        # we have to explicitly pass it as a template parameter.  The underlying C++
        # class does not have this limitation, and can automatically infer T from Node.
        ctypedef T Value

        ListView[Node, ListAllocator[Node]] view  # low-level list/memory management

        # constructors
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

        # list interface
        void append(Value item, bint left = False) except +
        void extend[U](U items, bint left = False) except +
        void insert[U](U index, Value item) except +
        size_t index[U](Value item, U start = 0, U stop = -1) except +
        size_t count[U](Value item, U start = 0, U stop = -1) except +
        bint contains(Value item) except +
        void remove(Value item) except +
        Value pop[U](U index) except +
        void clear()
        CppLinkedList copy() except +
        void sort[Func](Func key = NULL, bint reverse = False) except +
        void reverse()
        void rotate(long long steps = 1) except +

        # utility methods
        bint empty()
        size_t size()
        size_t capacity
        optional[size_t] max_size()
        void reserve() except +
        void defragment() except +
        PyObject* specialization()
        void specialize(PyObject* spec) except +
        size_t nbytes()

        # thread locks
        Lock lock  # callable functor that produces lock guards for an internal mutex

        # iterators
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
        IteratorFactory iter
        IteratorFactory.Iterator begin()
        IteratorFactory.Iterator end()
        IteratorFactory.Iterator rbegin()
        IteratorFactory.Iterator rend()

        # indexing
        cppclass ElementProxy:
            ElementProxy& operator=(const Value& value) except +
            Value get()
            void set(Value value) except +
            void insert(Value value) except +
            void delete "del" ()
            Value pop()
        ElementProxy operator[][U](U index) except +

        # slicing
        cppclass SliceProxy:
            cppclass Iterator:
                Iterator(const Iterator& other)
                Value operator*()
                Iterator& operator++()
                bint operator!=(const Iterator& other)
                size_t index()
                Node* drop()
            CppLinkedList get() except +
            void set[U](U items) except +
            void delete "del" ()
            long long start()
            long long stop()
            long long step()
            size_t abs_step()
            size_t first()
            size_t last()
            size_t length()
            bint empty()
            bint backward()
            bint inverted()
            Iterator iter(optional[size_t] length = nullopt)
            Iterator begin()
            Iterator end()
        SliceProxy slice(PyObject* py_slice) except +
        SliceProxy slice(
            optional[long long] start = nullopt,
            optional[long long] stop = nullopt,
            optional[long long] step = nullopt
        ) except +

    # NOTE: overloading in-place operators (+=, *=, etc.) is not fully supported in
    # Cython, but are supported on the C++ implementation.

    # concatenation operator (+)
    CppLinkedList[T, Node, Sort, Lock] operator+[T, Node, Sort, Lock, Rhs](
        CppLinkedList[T, Node, Sort, Lock],
        Rhs rhs
    ) except +
    Lhs operator+[T, Node, Sort, Lock, Lhs](
        Lhs lhs,
        CppLinkedList[T, Node, Sort, Lock],
    ) except +
    # CppLinkedList[T, Node, Sort, Lock]& operator+=[T, Node, Sort, Lock, Rhs](
    #     CppLinkedList[T, Node, Sort, Lock],
    #     Rhs rhs
    # ) except +

    # repetition operator (*)
    CppLinkedList[T, Node, Sort, Lock] operator*[T, Node, Sort, Lock](
        CppLinkedList[T, Node, Sort, Lock],
        ssize_t rhs
    ) except +
    CppLinkedList[T, Node, Sort, Lock] operator*[T, Node, Sort, Lock](
        CppLinkedList[T, Node, Sort, Lock],
        PyObject* rhs
    ) except +
    CppLinkedList[T, Node, Sort, Lock] operator*[T, Node, Sort, Lock](
        ssize_t lhs,
        CppLinkedList[T, Node, Sort, Lock],
    ) except +
    CppLinkedList[T, Node, Sort, Lock] operator*[T, Node, Sort, Lock](
        PyObject* lhs,
        CppLinkedList[T, Node, Sort, Lock],
    ) except +
    # CppLinkedList[T, Node, Sort, Lock]& operator*=[T, Node, Sort, Lock](
    #     CppLinkedList[T, Node, Sort, Lock],
    #     ssize_t rhs
    # ) except +
    # CppLinkedList[T, Node, Sort, Lock]& operator*=[T, Node, Sort, Lock](
    #     CppLinkedList[T, Node, Sort, Lock],
    #     PyObject* rhs
    # ) except +

    # lexical comparisons
    bint operator<[T, Node, Sort, Lock, Rhs](
        CppLinkedList[T, Node, Sort, Lock],
        Rhs rhs
    ) except +
    bint operator<[T, Node, Sort, Lock, Lhs](
        Lhs lhs,
        CppLinkedList[T, Node, Sort, Lock]
    ) except +
    bint operator<=[T, Node, Sort, Lock, Rhs](
        CppLinkedList[T, Node, Sort, Lock],
        Rhs rhs
    ) except +
    bint operator<=[T, Node, Sort, Lock, Lhs](
        Lhs lhs,
        CppLinkedList[T, Node, Sort, Lock]
    ) except +
    bint operator==[T, Node, Sort, Lock, Rhs](
        CppLinkedList[T, Node, Sort, Lock],
        Rhs rhs
    ) except +
    bint operator!=[T, Node, Sort, Lock, Lhs](
        Lhs lhs,
        CppLinkedList[T, Node, Sort, Lock]
    ) except +
    bint operator>=[T, Node, Sort, Lock, Rhs](
        CppLinkedList[T, Node, Sort, Lock],
        Rhs rhs
    ) except +
    bint operator>=[T, Node, Sort, Lock, Lhs](
        Lhs lhs,
        CppLinkedList[T, Node, Sort, Lock]
    ) except +
    bint operator>[T, Node, Sort, Lock, Rhs](
        CppLinkedList[T, Node, Sort, Lock],
        Rhs rhs
    ) except +
    bint operator>[T, Node, Sort, Lock, Lhs](
        Lhs lhs,
        CppLinkedList[T, Node, Sort, Lock]
    ) except +


######################
####    CYTHON    ####
######################


cdef extern from "list_cython.h" namespace "bertrand::structs::linked::cython":
    cdef cppclass CyLinkedList:
        # constructors
        CyLinkedList(
            optional[size_t] max_size,
            PyObject* spec,
            bint doubly_linked
        ) except +
        CyLinkedList(
            PyObject* iterable,
            optional[size_t] max_size,
            PyObject* spec,
            bint reverse,
            bint doubly_linked
        ) except +

        # low level methods
        bint empty()
        size_t size()
        size_t capacity()
        optional[size_t] max_size()
        void reserve(size_t capacity) except +
        void defragment() except +
        PyObject* specialization()
        void specialize(PyObject* spec) except +*
        size_t nbytes()
        PyObject* iter() except +*
        PyObject* riter() except +*

        # thread locks
        cppclass Lock:
            PyObject* operator()() except +
            PyObject* shared() except +
            size_t count()
            size_t duration()
            double contention()
            void reset_diagnostics()
        const Lock lock

        # list interface
        void append(PyObject* item, bint left) except +*
        void insert[T](T index, PyObject* item) except +*
        void extend(PyObject* items, bint left) except +*
        size_t index[T](PyObject* item, T start, T stop) except +*
        size_t count[T](PyObject* item, T start, T stop) except +*
        int contains(PyObject* item) except +*
        void remove(PyObject*) except *
        PyObject* pop[T](T index) except NULL
        Slot[CyLinkedList] copy() except +
        void clear()
        void sort(PyObject* key, bint reverse) except +*
        void reverse()
        void rotate(ssize_t steps)

        # indexing
        cppclass Index:
            PyObject* get() except +*
            void set(PyObject* item) except +*
            void delete "del" () except +*  # del() shadows Cython `delete` keyword
        Index operator[](PyObject* index)

        # slicing
        cppclass Slice:
            Slot[CyLinkedList] get() except +
            void set(PyObject* items) except +*
            void delete "del" () except +*  # del() shadows Cython `delete` keyword
        Slice slice(PyObject* py_slice)

        # operator overloads
        Slot[CyLinkedList] concat[T](const T& other) except +
        Slot[CyLinkedList] rconcat[T](const T& other) except +
        void iconcat[T](const T& other) except +
        Slot[CyLinkedList] repeat[T](T steps) except +
        void irepeat[T](T steps) except +
        bint lt[T](const T& other) except +
        bint le[T](const T& other) except +
        bint eq[T](const T& other) except +
        bint ne[T](const T& other) except +
        bint ge[T](const T& other) except +
        bint gt[T](const T& other) except +


cdef class LinkedList:
    cdef:
        Slot[CyLinkedList] variant  # stack-allocated
        # NOTE: uncommenting this line causes a compiler warning about memory alignment
        # object __weakref__  # allows LinkedList to be weak-referenced from Python

    @staticmethod
    cdef LinkedList from_variant(CyLinkedList* variant)
