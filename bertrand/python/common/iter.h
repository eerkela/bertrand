#ifndef BERTRAND_PYTHON_COMMON_ITER_H
#define BERTRAND_PYTHON_COMMON_ITER_H

#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"
#include "pytypedefs.h"
#include "type.h"


namespace py {


/* A generic Python iterator with a static value type.

This type has no fixed implementation, and can match any kind of iterator.  It roughly
corresponds to the `collections.abc.Iterator` abstract base class in Python, and makes
an arbitrary Python iterator accessible from C++.  Note that the reverse (exposing a
C++ iterator to Python) is done via the `__python__::__iter__(PyObject* self)` API
method, which returns a unique type for each container.  This class will match those
types, but is not restricted to them, and will be universally slower as a result.

In the interest of performance, no explicit checks are done to ensure that the return
type matches expectations.  As such, this class is one of the rare cases where type
safety may be violated, and should therefore be used with caution.  It is mostly meant
for internal use to back the default result of the `begin()` and `end()` operators when
no specialized C++ iterator can be found.  In that case, its value type is set to the
`T` in an `__iter__<Container> : Returns<T> {}` specialization. */
template <std::derived_from<Object> Return = Object>
class Iterator : public Object, public impl::IterTag {
public:

    Iterator(Handle h, borrowed_t t) : Object(h, t) {}
    Iterator(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Iterator>::template enable<Args...>)
    Iterator(Args&&... args) : Object(
        implicit_ctor<Iterator>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Iterator>::template enable<Args...>)
    explicit Iterator(Args&&... args) : Object(
        explicit_ctor<Iterator>{},
        std::forward<Args>(args)...
    ) {}

};


template <std::derived_from<Object> T, typename Return>
struct __isinstance__<T, Iterator<Return>>                  : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        return PyIter_Check(ptr(obj));
    }
};


template <std::derived_from<Object> T, typename Return>
struct __issubclass__<T, Iterator<Return>>                  : Returns<bool> {
    static consteval bool operator()() {
        return
            std::derived_from<T, impl::IterTag> &&
            std::convertible_to<impl::iter_type<T>, Return>;
    }
};


template <typename T>
struct __iter__<Iterator<T>>                               : Returns<T> {
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = T;
    using pointer           = T*;
    using reference         = T&;

    Iterator<T> iter;
    T curr;

    /// NOTE: modifying a copy of a Python iterator will also modify the original due
    /// to the inherent state of the iterator, which is managed by the Python runtime.
    /// This class is only copyable in order to fulfill the requirements of the
    /// iterator protocol, but it is not recommended to use it in this way.

    __iter__(const Iterator<T>& other) :
        iter(iter), curr(reinterpret_steal<T>(nullptr))
    {}

    __iter__(Iterator<T>&& iter) :
        iter(std::move(iter)), curr(reinterpret_steal<T>(nullptr))
    {}

    __iter__(const Iterator<T>& iter, int) : __iter__(iter) {
        ++(*this);
    }

    __iter__(Iterator<T>&& iter, int) : __iter__(std::move(iter)) {
        ++(*this);
    }

    __iter__(const __iter__& other) :
        iter(other.iter), curr(other.curr)
    {}

    __iter__(__iter__&& other) :
        iter(std::move(other.iter)), curr(std::move(other.curr))
    {}

    __iter__& operator=(const __iter__& other) {
        if (&other != this) {
            iter = other.iter;
            curr = other.curr;
        }
        return *this;
    }

    __iter__& operator=(__iter__&& other) {
        if (&other != this) {
            iter = std::move(other.iter);
            curr = std::move(other.curr);
        }
        return *this;
    }

    T& operator*() { return curr; }
    const T& operator*() const { return curr; }

    __iter__& operator++() {
        PyObject* next = PyIter_Next(ptr(iter));
        if (PyErr_Occurred()) {
            Exception::from_python();
        }
        curr = reinterpret_steal<T>(next);
        return *this;
    }

    /// NOTE: post-increment is not supported due to inaccurate copy semantics.

    bool operator==(const __iter__& other) const {
        return ptr(curr) == ptr(other.curr);
    }

    bool operator!=(const __iter__& other) const {
        return ptr(curr) != ptr(other.curr);
    }

};
/// NOTE: no __iter__<const Iterator<T>> specialization since marking the iterator
/// itself as const prevents it from being incremented.


template <typename T, typename Return>
struct __contains__<T, Iterator<Return>>                    : Returns<bool> {};
template <typename Return>
struct __getattr__<Iterator<Return>, "__iter__">            : Returns<Function<Iterator<Return>()>> {};
template <typename Return>
struct __getattr__<Iterator<Return>, "__next__">            : Returns<Function<Return()>> {};


/* The type of a generic Python iterator.  This is identical to the
`collections.abc.Iterator` abstract base class, and will match any Python iterator
regardless of return type. */
template <typename Return>
class Type<Iterator<Return>> : public Object, public impl::TypeTag {
public:
    struct __python__ : public TypeTag::def<__python__, Iterator<Return>> {
        static Type<Iterator<Return>> __import__();
    };

    Type(Handle h, borrowed_t t) : Object(h, t) {}
    Type(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::template enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::template enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    static Iterator<Return> __iter__(Iterator<Return>& iter) {
        return iter;
    }

    static Return __next__(Iterator<Return>& iter) {
        PyObject* next = PyIter_Next(ptr(iter));
        if (next == nullptr) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            throw StopIteration();
        }
        return reinterpret_steal<Return>(next);
    }

};


template <typename Return>
struct __getattr__<Type<Iterator<Return>>, "__iter__">      : Returns<Function<
    Iterator<Return>(Iterator<Return>&)
>> {};
template <typename Return>
struct __getattr__<Type<Iterator<Return>>, "__next__">      : Returns<Function<
    Return(Iterator<Return>&)
>> {};


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
        curr = PyIter_Next(ptr(iter));
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
        curr = PyIter_Next(ptr(iter));
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
        if (!PyDict_Next(ptr(dict), &pos, &curr, nullptr)) {
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
        if (!PyDict_Next(ptr(dict), &pos, &curr, nullptr)) {
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
        if (!PyDict_Next(ptr(dict), &pos, nullptr, &curr)) {
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
        if (!PyDict_Next(ptr(dict), &pos, nullptr, &curr)) {
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
        if (!PyDict_Next(ptr(dict), &pos, &key, &value)) {
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
        if (!PyDict_Next(ptr(dict), &pos, &key, &value)) {
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


#endif
