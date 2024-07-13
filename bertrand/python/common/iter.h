#ifndef BERTRAND_PYTHON_MODULE_GUARD
#error "Internal headers should not be included directly.  Import 'bertrand.python' instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_ITER_H
#define BERTRAND_PYTHON_COMMON_ITER_H

#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"
#include "func.h"
#include "item.h"


namespace bertrand {
namespace py {
namespace impl {


template <typename Policy>
concept random_access = 
    std::same_as<typename Policy::iterator_category, std::random_access_iterator_tag>;


template <typename Policy>
concept bidirectional = random_access<Policy> || 
    std::same_as<typename Policy::iterator_category, std::bidirectional_iterator_tag>;


/* An optimized iterator that directly accesses tuple or list elements through the
CPython API. */
template <typename Policy>
class Iterator : public BertrandTag {
    static_assert(
        std::derived_from<typename Policy::value_type, Object>,
        "Iterator must dereference to a subclass of py::Object.  Check your "
        "specialization of __iter__ for this type and ensure the Return type is "
        "derived from py::Object."
    );

protected:
    Policy policy;

public:
    using iterator_category        = Policy::iterator_category;
    using difference_type          = Policy::difference_type;
    using value_type               = Policy::value_type;
    using pointer                  = value_type*;
    using reference                = value_type&;

    /* Default constructor.  Initializes to a sentinel iterator. */
    template <typename... Args>
    explicit Iterator(Args&&... args) : policy(std::forward<Args>(args)...) {}

    /* Copy constructor. */
    Iterator(const Iterator& other) : policy(other.policy) {}

    /* Move constructor. */
    Iterator(Iterator&& other) : policy(std::move(other.policy)) {}

    /* Copy assignment operator. */
    Iterator& operator=(const Iterator& other) {
        policy = other.policy;
        return *this;
    }

    /* Move assignment operator. */
    Iterator& operator=(Iterator&& other) {
        policy = std::move(other.policy);
        return *this;
    }

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* Dereference the iterator. */
    [[nodiscard]] inline value_type operator*() const {
        return policy.deref();
    }

    /* Dereference the iterator. */
    [[nodiscard]] inline pointer operator->() const {
        return &(**this);
    }

    /* Advance the iterator. */
    inline Iterator& operator++() {
        policy.advance();
        return *this;
    }

    /* Advance the iterator. */
    inline Iterator operator++(int) {
        Iterator copy = *this;
        policy.advance();
        return copy;
    }

    /* Compare two iterators for equality. */
    [[nodiscard]] inline bool operator==(const Iterator& other) const {
        return policy.compare(other.policy);
    }

    /* Compare two iterators for inequality. */
    [[nodiscard]] inline bool operator!=(const Iterator& other) const {
        return !policy.compare(other.policy);
    }

    ///////////////////////////////////////
    ////    BIDIRECTIONAL ITERATORS    ////
    ///////////////////////////////////////

    /* Retreat the iterator. */
    template <typename T = Iterator> requires (bidirectional<Policy>)
    inline Iterator& operator--() {
        policy.retreat();
        return *this;
    }

    /* Retreat the iterator. */
    template <typename T = Iterator> requires (bidirectional<Policy>)
    inline Iterator operator--(int) {
        Iterator copy = *this;
        policy.retreat();
        return copy;
    }

    ///////////////////////////////////////
    ////    RANDOM ACCESS ITERATORS    ////
    ///////////////////////////////////////

    /* Advance the iterator by n steps. */
    template <typename T = Iterator> requires (random_access<Policy>)
    [[nodiscard]] inline Iterator operator+(difference_type n) const {
        Iterator copy = *this;
        copy += n;
        return copy;
    }

    /* Advance the iterator by n steps. */
    template <typename T = Iterator> requires (random_access<Policy>)
    inline Iterator& operator+=(difference_type n) {
        policy.advance(n);
        return *this;
    }

    /* Retreat the iterator by n steps. */
    template <typename T = Iterator> requires (random_access<Policy>)
    [[nodiscard]] inline Iterator operator-(difference_type n) const {
        Iterator copy = *this;
        copy -= n;
        return copy;
    }

    /* Retreat the iterator by n steps. */
    template <typename T = Iterator> requires (random_access<Policy>)
    inline Iterator& operator-=(difference_type n) {
        policy.retreat(n);
        return *this;
    }

    /* Calculate the distance between two iterators. */
    template <typename T = Iterator> requires (random_access<Policy>)
    [[nodiscard]] inline difference_type operator-(const Iterator& other) const {
        return policy.distance(other.policy);
    }

    /* Access the iterator at an offset. */
    template <typename T = Iterator> requires (random_access<Policy>)
    [[nodiscard]] inline value_type operator[](difference_type n) const {
        return *(*this + n);
    }

    /* Compare two iterators for ordering. */
    template <typename T = Iterator> requires (random_access<Policy>)
    [[nodiscard]] inline bool operator<(const Iterator& other) const {
        return !!policy && (*this - other) < 0;
    }

    /* Compare two iterators for ordering. */
    template <typename T = Iterator> requires (random_access<Policy>)
    [[nodiscard]] inline bool operator<=(const Iterator& other) const {
        return !!policy && (*this - other) <= 0;
    }

    /* Compare two iterators for ordering. */
    template <typename T = Iterator> requires (random_access<Policy>)
    [[nodiscard]] inline bool operator>=(const Iterator& other) const {
        return !policy || (*this - other) >= 0;
    }

    /* Compare two iterators for ordering. */
    template <typename T = Iterator> requires (random_access<Policy>)
    [[nodiscard]] inline bool operator>(const Iterator& other) const {
        return !policy || (*this - other) > 0;
    }

};


/* An adapter for an Iterator class that swaps the meanings of the increment and
decrement operators, effectively converting a forward iterator into a reverse
iterator. */
template <typename Policy>
class ReverseIterator : public Iterator<Policy> {
    using Base = Iterator<Policy>;
    static_assert(
        Base::bidirectional,
        "ReverseIterator can only be used with bidirectional iterators."
    );

public:
    using Base::Base;

    /* Advance the iterator. */
    inline ReverseIterator& operator++() {
        Base::operator--();
        return *this;
    }

    /* Advance the iterator. */
    inline ReverseIterator operator++(int) {
        ReverseIterator copy = *this;
        Base::operator--();
        return copy;
    }

    /* Retreat the iterator. */
    inline ReverseIterator& operator--() {
        Base::operator++();
        return *this;
    }

    /* Retreat the iterator. */
    inline ReverseIterator operator--(int) {
        ReverseIterator copy = *this;
        Base::operator++();
        return copy;
    }

    ////////////////////////////////////////
    ////    RANDOM ACCESS ITERATORS     ////
    ////////////////////////////////////////

    /* Advance the iterator by n steps. */
    template <typename T = ReverseIterator> requires (random_access<Policy>)
    inline ReverseIterator operator+(typename Base::difference_type n) const {
        ReverseIterator copy = *this;
        copy -= n;
        return copy;
    }

    /* Advance the iterator by n steps. */
    template <typename T = ReverseIterator> requires (random_access<Policy>)
    inline ReverseIterator& operator+=(typename Base::difference_type n) {
        Base::operator-=(n);
        return *this;
    }

    /* Retreat the iterator by n steps. */
    template <typename T = ReverseIterator> requires (random_access<Policy>)
    inline ReverseIterator operator-(typename Base::difference_type n) const {
        ReverseIterator copy = *this;
        copy += n;
        return copy;
    }

    /* Retreat the iterator by n steps. */
    template <typename T = ReverseIterator> requires (random_access<Policy>)
    inline ReverseIterator& operator-=(typename Base::difference_type n) {
        Base::operator+=(n);
        return *this;
    }

};


/* A generic iterator policy that uses Python's existing iterator protocol. */
template <typename Deref>
class GenericIter : public BertrandTag {
    Object iter;
    PyObject* curr;

public:
    using iterator_category         = std::input_iterator_tag;
    using difference_type           = std::ptrdiff_t;
    using value_type                = Deref;

    /* Default constructor.  Initializes to a sentinel iterator. */
    GenericIter() :
        iter(reinterpret_steal<Object>(nullptr)), curr(nullptr)
    {}

    /* Wrap a raw Python iterator. */
    GenericIter(Object&& iterator) : iter(std::move(iterator)) {
        curr = PyIter_Next(iter.ptr());
        if (curr == nullptr && PyErr_Occurred()) {
            Exception::from_python();
        }
    }

    /* Copy constructor. */
    GenericIter(const GenericIter& other) : iter(other.iter), curr(other.curr) {
        Py_XINCREF(curr);
    }

    /* Move constructor. */
    GenericIter(GenericIter&& other) : iter(std::move(other.iter)), curr(other.curr) {
        other.curr = nullptr;
    }

    /* Copy assignment operator. */
    GenericIter& operator=(const GenericIter& other) {
        if (&other != this) {
            iter = other.iter;
            PyObject* temp = curr;
            Py_XINCREF(curr);
            curr = other.curr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    GenericIter& operator=(GenericIter&& other) {
        if (&other != this) {
            iter = std::move(other.iter);
            PyObject* temp = curr;
            curr = other.curr;
            other.curr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    ~GenericIter() {
        Py_XDECREF(curr);
    }

    /* Dereference the iterator. */
    inline Deref deref() const {
        if (curr == nullptr) {
            throw ValueError("attempt to dereference a null iterator.");
        }
        return reinterpret_borrow<Deref>(curr);
    }

    /* Advance the iterator. */
    inline void advance() {
        PyObject* temp = curr;
        curr = PyIter_Next(iter.ptr());
        Py_XDECREF(temp);
        if (curr == nullptr && PyErr_Occurred()) {
            Exception::from_python();
        }
    }

    /* Compare two iterators for equality. */
    inline bool compare(const GenericIter& other) const {
        return curr == other.curr;
    }

    inline explicit operator bool() const {
        return curr != nullptr;
    }

};


/* A random access iterator policy that directly addresses tuple elements using the
CPython API. */
template <typename Deref>
class TupleIter : public BertrandTag {
    Object tuple;
    PyObject* curr;
    Py_ssize_t index;
    Py_ssize_t size;

public:
    using iterator_category         = std::random_access_iterator_tag;
    using difference_type           = std::ptrdiff_t;
    using value_type                = Deref;

    /* Sentinel constructor. */
    TupleIter(Py_ssize_t index = -1) :
        tuple(reinterpret_steal<Object>(nullptr)), curr(nullptr), index(index),
        size(0)
    {}

    /* Construct an iterator from a tuple and a starting index. */
    TupleIter(const Object& tuple, Py_ssize_t index) :
        tuple(tuple), index(index), size(PyTuple_GET_SIZE(tuple.ptr()))
    {
        if (index >= 0 && index < size) {
            curr = PyTuple_GET_ITEM(tuple.ptr(), index);
        } else {
            curr = nullptr;
        }
    }

    /* Copy constructor. */
    TupleIter(const TupleIter& other) :
        tuple(other.tuple), curr(other.curr), index(other.index), size(other.size)
    {}

    /* Move constructor. */
    TupleIter(TupleIter&& other) :
        tuple(std::move(other.tuple)), curr(other.curr), index(other.index),
        size(other.size)
    {
        other.curr = nullptr;
    }

    /* Copy assignment operator. */
    TupleIter& operator=(const TupleIter& other) {
        if (&other != this) {
            tuple = other.tuple;
            curr = other.curr;
            index = other.index;
            size = other.size;
        }
        return *this;
    }

    /* Move assignment operator. */
    TupleIter& operator=(TupleIter&& other) {
        if (&other != this) {
            tuple = other.tuple;
            curr = other.curr;
            index = other.index;
            size = other.size;
            other.curr = nullptr;
        }
        return *this;
    }

    /* Dereference the iterator. */
    inline Deref deref() const {
        if (curr == nullptr) {
            throw ValueError("attempt to dereference a null iterator.");
        }
        return reinterpret_borrow<Deref>(curr);
    }

    /* Advance the iterator. */
    inline void advance(Py_ssize_t n = 1) {
        index += n;
        if (index >= 0 && index < size) {
            curr = PyTuple_GET_ITEM(tuple.ptr(), index);
        } else {
            curr = nullptr;
        }
    }

    /* Compare two iterators for equality. */
    inline bool compare(const TupleIter& other) const {
        return curr == other.curr;
    }

    /* Retreat the iterator. */
    inline void retreat(Py_ssize_t n = 1) {
        index -= n;
        if (index >= 0 && index < size) {
            curr = PyTuple_GET_ITEM(tuple.ptr(), index);
        } else {
            curr = nullptr;
        }
    }

    /* Calculate the distance between two iterators. */
    inline difference_type distance(const TupleIter& other) const {
        return index - other.index;
    }

    inline explicit operator bool() const {
        return curr != nullptr;
    }

};


/* A random access iterator policy that directly addresses list elements using the
CPython API. */
template <typename Deref>
class ListIter : public BertrandTag {
    Object list;
    PyObject* curr;
    Py_ssize_t index;
    Py_ssize_t size;

public:
    using iterator_category         = std::random_access_iterator_tag;
    using difference_type           = std::ptrdiff_t;
    using value_type                = Deref;

    /* Sentinel constructor. */
    ListIter(Py_ssize_t index = -1) :
        list(reinterpret_steal<Object>(nullptr)), curr(nullptr), index(index),
        size(0)
    {}

    /* Construct an iterator from a list and a starting index. */
    ListIter(const Object& list, Py_ssize_t index) :
        list(list), index(index), size(PyList_GET_SIZE(list.ptr()))
    {
        if (index >= 0 && index < size) {
            curr = PyList_GET_ITEM(list.ptr(), index);
        } else {
            curr = nullptr;
        }
    }

    /* Copy constructor. */
    ListIter(const ListIter& other) :
        list(other.list), curr(other.curr), index(other.index), size(other.size)
    {}

    /* Move constructor. */
    ListIter(ListIter&& other) :
        list(std::move(other.list)), curr(other.curr), index(other.index),
        size(other.size)
    {
        other.curr = nullptr;
    }

    /* Copy assignment operator. */
    ListIter& operator=(const ListIter& other) {
        if (&other != this) {
            list = other.list;
            curr = other.curr;
            index = other.index;
            size = other.size;
        }
        return *this;
    }

    /* Move assignment operator. */
    ListIter& operator=(ListIter&& other) {
        if (&other != this) {
            list = other.list;
            curr = other.curr;
            index = other.index;
            size = other.size;
            other.curr = nullptr;
        }
        return *this;
    }

    /* Dereference the iterator. */
    inline Deref deref() const {
        if (curr == nullptr) {
            throw IndexError("list index out of range");
        }
        return reinterpret_borrow<Deref>(curr);
    }

    /* Advance the iterator. */
    inline void advance(Py_ssize_t n = 1) {
        index += n;
        if (index >= 0 && index < size) {
            curr = PyList_GET_ITEM(list.ptr(), index);
        } else {
            curr = nullptr;
        }
    }

    /* Compare two iterators for equality. */
    inline bool compare(const ListIter& other) const {
        return curr == other.curr;
    }

    /* Retreat the iterator. */
    inline void retreat(Py_ssize_t n = 1) {
        index -= n;
        if (index >= 0 && index < size) {
            curr = PyList_GET_ITEM(list.ptr(), index);
        } else {
            curr = nullptr;
        }
    }

    /* Calculate the distance between two iterators. */
    inline difference_type distance(const ListIter& other) const {
        return index - other.index;
    }

    inline explicit operator bool() const {
        return curr != nullptr;
    }

};


/* An iterator policy that extracts keys from a dictionary using PyDict_Next(). */
template <typename Deref>
class KeyIter : public BertrandTag {
    Object dict;
    PyObject* curr = nullptr;
    Py_ssize_t pos = 0;

public:
    using iterator_category         = std::input_iterator_tag;
    using difference_type           = std::ptrdiff_t;
    using value_type                = Deref;

    /* Default constructor.  Initializes to a sentinel iterator. */
    KeyIter() : dict(reinterpret_steal<Object>(nullptr)) {}

    /* Construct an iterator from a dictionary. */
    KeyIter(const Object& dict) : dict(dict) {
        if (!PyDict_Next(dict.ptr(), &pos, &curr, nullptr)) {
            curr = nullptr;
        }
    }

    /* Copy constructor. */
    KeyIter(const KeyIter& other) :
        dict(other.dict), curr(other.curr), pos(other.pos)
    {}

    /* Move constructor. */
    KeyIter(KeyIter&& other) :
        dict(std::move(other.dict)), curr(other.curr), pos(other.pos)
    {
        other.curr = nullptr;
    }

    /* Copy assignment operator. */
    KeyIter& operator=(const KeyIter& other) {
        if (&other != this) {
            dict = other.dict;
            curr = other.curr;
            pos = other.pos;
        }
        return *this;
    }

    /* Move assignment operator. */
    KeyIter& operator=(KeyIter&& other) {
        if (&other != this) {
            dict = other.dict;
            curr = other.curr;
            pos = other.pos;
            other.curr = nullptr;
        }
        return *this;
    }

    /* Dereference the iterator. */
    inline Deref deref() const {
        if (curr == nullptr) {
            throw StopIteration();
        }
        return reinterpret_borrow<Deref>(curr);
    }

    /* Advance the iterator. */
    inline void advance() {
        if (!PyDict_Next(dict.ptr(), &pos, &curr, nullptr)) {
            curr = nullptr;
        }
    }

    /* Compare two iterators for equality. */
    inline bool compare(const KeyIter& other) const {
        return curr == other.curr;
    }

    inline explicit operator bool() const {
        return curr != nullptr;
    }

};


/* An iterator policy that extracts values from a dictionary using PyDict_Next(). */
template <typename Deref>
class ValueIter : public BertrandTag {
    Object dict;
    PyObject* curr = nullptr;
    Py_ssize_t pos = 0;

public:
    using iterator_category         = std::input_iterator_tag;
    using difference_type           = std::ptrdiff_t;
    using value_type                = Deref;

    /* Default constructor.  Initializes to a sentinel iterator. */
    ValueIter() : dict(reinterpret_steal<Object>(nullptr)) {}

    /* Construct an iterator from a dictionary. */
    ValueIter(const Object& dict) : dict(dict) {
        if (!PyDict_Next(dict.ptr(), &pos, nullptr, &curr)) {
            curr = nullptr;
        }
    }

    /* Copy constructor. */
    ValueIter(const ValueIter& other) :
        dict(other.dict), curr(other.curr), pos(other.pos)
    {}

    /* Move constructor. */
    ValueIter(ValueIter&& other) :
        dict(std::move(other.dict)), curr(other.curr), pos(other.pos)
    {
        other.curr = nullptr;
    }

    /* Copy assignment operator. */
    ValueIter& operator=(const ValueIter& other) {
        if (&other != this) {
            dict = other.dict;
            curr = other.curr;
            pos = other.pos;
        }
        return *this;
    }

    /* Move assignment operator. */
    ValueIter& operator=(ValueIter&& other) {
        if (&other != this) {
            dict = other.dict;
            curr = other.curr;
            pos = other.pos;
            other.curr = nullptr;
        }
        return *this;
    }

    /* Dereference the iterator. */
    inline Deref deref() const {
        if (curr == nullptr) {
            throw StopIteration();
        }
        return reinterpret_borrow<Deref>(curr);
    }

    /* Advance the iterator. */
    inline void advance() {
        if (!PyDict_Next(dict.ptr(), &pos, nullptr, &curr)) {
            curr = nullptr;
        }
    }

    /* Compare two iterators for equality. */
    inline bool compare(const ValueIter& other) const {
        return curr == other.curr;
    }

    inline explicit operator bool() const {
        return curr != nullptr;
    }

};


/* An iterator policy that extracts key-value pairs from a dictionary using
PyDict_Next(). */
template <typename Deref>
class ItemIter : public BertrandTag {
    Object dict;
    PyObject* key = nullptr;
    PyObject* value = nullptr;
    Py_ssize_t pos = 0;

public:
    using iterator_category         = std::input_iterator_tag;
    using difference_type           = std::ptrdiff_t;
    using value_type                = Deref;

    /* Default constructor.  Initializes to a sentinel iterator. */
    ItemIter() : dict(reinterpret_steal<Object>(nullptr)) {}

    /* Construct an iterator from a dictionary. */
    ItemIter(const Object& dict) : dict(dict) {
        if (!PyDict_Next(dict.ptr(), &pos, &key, &value)) {
            key = nullptr;
            value = nullptr;
        }
    }

    /* Copy constructor. */
    ItemIter(const ItemIter& other) :
        dict(other.dict), key(other.key), value(other.value), pos(other.pos)
    {}

    /* Move constructor. */
    ItemIter(ItemIter&& other) :
        dict(std::move(other.dict)), key(other.key), value(other.value), pos(other.pos)
    {
        other.key = nullptr;
        other.value = nullptr;
    }

    /* Copy assignment operator. */
    ItemIter& operator=(const ItemIter& other) {
        if (&other != this) {
            dict = other.dict;
            key = other.key;
            value = other.value;
            pos = other.pos;
        }
        return *this;
    }

    /* Move assignment operator. */
    ItemIter& operator=(ItemIter&& other) {
        if (&other != this) {
            dict = other.dict;
            key = other.key;
            value = other.value;
            pos = other.pos;
            other.key = nullptr;
            other.value = nullptr;
        }
        return *this;
    }

    /* Dereference the iterator. */
    inline Deref deref() const {
        if (key == nullptr || value == nullptr) {
            throw StopIteration();
        }
        return Deref(key, value);
    }

    /* Advance the iterator. */
    inline void advance() {
        if (!PyDict_Next(dict.ptr(), &pos, &key, &value)) {
            key = nullptr;
            value = nullptr;
        }
    }

    /* Compare two iterators for equality. */
    inline bool compare(const ItemIter& other) const {
        return key == other.key && value == other.value;
    }

    inline explicit operator bool() const {
        return key != nullptr && value != nullptr;
    }

};


}  // namespace impl
}  // namespace py
}  // namespace bertrand


#endif
