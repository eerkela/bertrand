#ifndef BERTRAND_PYTHON_COMMON_ITER_H
#define BERTRAND_PYTHON_COMMON_ITER_H

#include "declarations.h"
#include "except.h"
#include "object.h"
#include "ops.h"


namespace py {
namespace impl {


// TODO: Iterator should be templated on the container type?  Perhaps the iterator
// policies are implemented in the __iter__ control struct directly?  The default
// behavior would be to either use PyObject_GetIter() for generic Python objects or
// unwrap the contained C++ object and use its begin() and end() iterators directly.
// This can be detected by checking the type's __python__::__origin__ field, and should
// not be placed in the __iter__ control struct itself, so that users don't need to
// insert any custom logic.  Custom __iter__ logic would only be applied for tuples,
// lists, key/value/item views, etc, and would have a 1:1 map with the iterator type.
// -> I can probably also deduce the random_access and bidirectional properties by
// analyzing the __iter__ specialization.  By default, the iterator will be
// forward-only, but if the __iter__ specialization has --, then it will be considered
// bidirectional.  If it has +/-, then it will be considered random access.


// TODO: The generic py::Iterator class has to store both a start and end iterator.
// It might be able to standardize these if all iterators store the current element as
// a PyObject*.  The end iterator would always just hold a null pointer, and would
// terminate iteration.  This might standardize the __iter__ protocol as well, since
// all iterators would store a `PyObject* curr` member that gets default-initialized to
// nullptr.  It would have a constructor that takes the container type and initializes
// the iterator.

// I actually might not need to define the dereference or comparison operators this
// way.  I would just have the __iter__ struct define a constructor to initialize any
// internal values, and then define the increment/decrement/seek operators

// TODO: maybe the __iter__ control struct just *is* the iterator itself?  If nothing
// is filled in, then it uses a generic implementation.
// -> every iterator would contain an __iter__<Container> field.  What actually gets
// stored is a subclass of __iter__ that fills in any missing fields/methods.
// -> `__iter__` would only have to define the `operator++` method, and would have to
// use an explicit this parameter to access the container and current value, which
// can be stored with the appropriate type so that the iterator holds strong references
// to both the container and each element.  The dereference operator would then return
// a reference to the current value, which avoids additional reference counting.

// `__iter__` could define additional fields if necessary, which would be initialized
// by its constructor, which takes the object to iterate over.  If `__iter__` is
// not constructible with a container, but is trivially constructible, then I just
// don't call the parent constructor at all.

// the iterator would be marked as bidirectional if `__iter__` implements an operator--
// using an explicit this parameter.  It would be marked as random access if it also
// implements +, -, +=, -=, and [].  Otherwise, the iterator is forward-only.



// TODO: maybe const iterators are modeled by supplying a const container type to this
// class?

template <typename Container> requires (__iter__<Container>::enable)
class Iterator_ : public Object, public impl::IterTag {

    template <typename T, typename = void>
    static constexpr bool has_increment = false;
    template <typename T>
    static constexpr bool has_increment<T, std::void_t<decltype(&T::operator++)>> = true;


    // TODO: maybe every __python__ type defines a corresponding nested iterator type?
    // Those that originate in Python would use PyObject_GetIter() and PyIter_Next(),
    // while those that originate in C++ would use the container's begin() and end()
    // iterators directly.  I might not need this class at all in that case.
    // -> When a Python object is iterated over in C++, we would just unwrap the object
    // and use its begin() and end() iterators directly.


    /* A C++ iterator type to use as the begin and end iterators in the range.  A pair
    of these are stored in the iterator's Python representation, and back its
    `__next__` method.  They can also be directly referenced from C++ in order to back
    iteration in either language. */
    template <typename T>
    class Iter;

    /* A custom iterator type that uses logic found in the `__iter__` control struct to
    implement the interface. */
    template <typename T>
        requires (!std::is_const_v<T> && has_increment<__iter__<T>>)
    class Iter<T> : public __iter__<T> {
        using Base = __iter__<T>;

    public:
        using value_type = Base::type;

        T container;
        value_type curr;

        template <typename = void>
            requires (std::is_constructible_v<Base, const T&>)
        Iter(const T& container) :
            Base(container),
            container(container),
            curr(reinterpret_steal<value_type>(nullptr))
        {}

        template <typename = void>
            requires (
                !std::is_constructible_v<Base, const T&> &&
                std::is_default_constructible_v<Base>
            )
        Iter(const T& container) :
            container(container),
            curr(reinterpret_steal<value_type>(nullptr))
        {}

        template <typename = void>
            requires (std::is_constructible_v<Base, const T&, int>)
        Iter(const T& container, int) :
            Base(container),
            container(container),
            curr(reinterpret_steal<value_type>(nullptr))
        {
            ++(*this);  // obtain the first value
        }

        template <typename = void>
            requires (
                !std::is_constructible_v<Base, const T&, int> &&
                std::is_default_constructible_v<Base>
            )
        Iter(const T& container, int) :
            container(container),
            curr(reinterpret_steal<value_type>(nullptr))
        {
            ++(*this);
        }

        Iter(const Iter& other) :
            Base(other),
            container(other.container),
            curr(other.curr)
        {}

        Iter(Iter&& other) :
            Base(std::move(other)),
            container(std::move(other.container)),
            curr(std::move(other.curr))
        {}

        Iter& operator=(const Iter& other) {
            if (&other != this) {
                container = other.container;
                curr = other.curr;
                Base::operator=(other);
            }
            return *this;
        }

        Iter& operator=(Iter&& other) {
            if (&other != this) {
                container = std::move(other.container);
                curr = std::move(other.curr);
                Base::operator=(std::move(other));
            }
            return *this;
        }

        const value_type& operator*() const {
            return curr;
        }

        value_type& operator*() {
            return curr;
        }

        Iter& operator++(this auto& self) {
            Base::operator++();
            return self;
        }

        /// NOTE: if an iterator should support post-increment or other operators, like
        /// --, +, -, +=, -=, [], etc. then the __iter__ control struct must define
        /// them manually.  Only ++ is guaranteed to be supported at all times.

        bool operator==(const Iter& other) const {
            return ptr(curr) == ptr(other.curr);
        }

    };

    // TODO: This class is only returned by the wrapper's begin() method if it did not
    // originate in C++.  This is determined by doing a PyType_IsSubclass() check on
    // the iterator's interface type.  If so, then we follow up with a PyType_IsSubtype
    // check on the iterator's specific template type.  If true, then we cast to the
    // __python__ type and extract the ->begin and ->end fields and return them
    // directly.  Otherwise, we have a template mismatch and raise a TypeError.
    // If the iterator is not a subclass of the interface type, then we return one of
    // these types.

    // -> This overload is only exposed to C++.

    // How do I deal with different return types in this case?  The origin check has
    // to be a constexpr branch, in which case 

    /* A generic Python iterator type that uses `PyObject_GetIter()` and `PyIter_Next()`
    to implement the interface. */
    template <typename T>
        requires (
            !std::is_const_v<T> &&
            !has_increment<__iter__<T>> &&
            std::derived_from<T, Object>
        )
    class Iter<T> : public __iter__<T> {
        using Base = __iter__<T>;

    public:
        using value_type = Base::type;

        PyObject* iter;
        value_type curr;

        Iter(const T& container) :
            iter(nullptr),
            curr(reinterpret_steal<value_type>(nullptr))
        {}

        Iter(const T& container, int) :
            iter(PyObject_GetIter(ptr(container))),
            curr(reinterpret_steal<value_type>(nullptr))
        {
            if (iter == nullptr) {
                Exception::from_python();
            }
            ++(*this);
        }

        Iter(const Iter& other) :
            Base(other),
            iter(Py_XNewRef(other.iter)),
            curr(other.curr)
        {}

        Iter(Iter&& other) :
            Base(std::move(other)),
            iter(other.iter),
            curr(std::move(other.curr))
        {
            other.iter = nullptr;
        }

        Iter& operator=(const Iter& other) {
            if (&other != this) {
                iter = Py_XNewRef(other.iter);
                curr = other.curr;
                Base::operator=(other);
            }
            return *this;
        }

        Iter& operator=(Iter&& other) {
            if (&other != this) {
                iter = other.iter;
                curr = std::move(other.curr);
                other.iter = nullptr;
                Base::operator=(std::move(other));
            }
            return *this;
        }

        ~Iter() {
            Py_XDECREF(iter);
        }

        const value_type& operator*() const {
            return curr;
        }

        value_type& operator*() {
            return curr;
        }

        Iter& operator++(this auto& self) {
            self.curr = reinterpret_steal<value_type>(PyIter_Next(self.iter));
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            return self;
        }

        /// NOTE: Python iterators don't support post-increment because they don't have
        /// accurate copy semantics.  Incrementing the iterator will always modify the
        /// copy.

        bool operator==(const Iter& other) const {
            return ptr(curr) == ptr(other.curr);
        }

    };

    // TODO: the only way to do this properly is to pass the actual iterator type into
    // the Iter<T> class.  It might also not inherit from __iter__ at all.  In fact,
    // it might inherit from the iterator type so that all I need to do is wrap the
    // output.

    // TODO: the simplest way to do this would be to have the Python type store two
    // Iter<Container> objects, and would just expose them directly to Python.  Perhaps
    // py::Iterator<Container, py::Direction::Forward/Reverse>?
    // -> Maybe it's cleaner to use a separate ReverseIterator class?

    /* A generic C++ iterator type that wraps around an existing C++ iterator and
    forwards its interface. */
    template <typename T>
        requires (
            !std::is_const_v<T> &&
            !has_increment<__iter__<T>> &&
            !std::derived_from<T, Object>
        )
    class Iter<T> : public __iter__<T> {
        using Base = __iter__<T>;

        // TODO: what if this represents a reverse or const iterator?
        using begin_type = decltype(std::ranges::begin(std::declval<T>()));
        using end_type = decltype(std::ranges::end(std::declval<T>()));
        static_assert(
            std::same_as<begin_type, end_type>,
            "begin and end iterators must have the same type to be compatible."
        );

    public:
        using value_type = Base::type;

        begin_type iter;

        Iter(const T& container) : iter(std::ranges::end(container)) {}

        Iter(const T& container, int) : iter(std::ranges::begin(container)) {}

        Iter(const Iter& other) : Base(other), iter(other.iter) {}

        Iter(Iter&& other) : Base(std::move(other)), iter(std::move(other.iter)) {}

        Iter& operator=(const Iter& other) {
            if (&other != this) {
                iter = other.iter;
                Base::operator=(other);
            }
            return *this;
        }

        Iter& operator=(Iter&& other) {
            if (&other != this) {
                iter = std::move(other.iter);
                Base::operator=(std::move(other));
            }
            return *this;
        }

        const value_type operator*() const {
            return py::wrap(*iter);
        }

        value_type operator*() {
            return py::wrap(*iter);
        }

        Iter& operator++(this auto& self) {
            ++self.iter;
            return self;
        }

        bool operator==(const Iter& other) const {
            return iter == other.iter;
        }

    };

    /* An extension of the Iter class that allows it to model iterators over const
    containers. */
    template <typename T>
    class Iter<const T> : public Iter<T> {
    public:
        using Iter<T>::Iter;
        const Iter<T>::value_type& operator*() const { return Iter<T>::operator*(); }
        const Iter<T>::value_type& operator*() { return Iter<T>::operator*(); }
    };

public:

    // enclosing class is a py::Object subclass that stores a pair of `it` iterators
    // in its Python representation.  The iterator can then be passed up to Python,
    // in which case it has the normal __iter__ and __next__ methods and works like
    // normal, or it can be iterated over from C++, which will directly reference the
    // C++ iterators in the python object.  That way, you should get native performance
    // in both directions.

    auto begin() {
        if constexpr (
            std::derived_from<Container, Object> &&
            impl::originates_from_cpp<Container>
        ) {
            // check against this template type and issue an error if it doesn't match
            
        }
    }


};


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
        tuple(tuple), index(index), size(PyTuple_GET_SIZE(ptr(tuple)))
    {
        if (index >= 0 && index < size) {
            curr = PyTuple_GET_ITEM(ptr(tuple), index);
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
            curr = PyTuple_GET_ITEM(ptr(tuple), index);
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
            curr = PyTuple_GET_ITEM(ptr(tuple), index);
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
        list(list), index(index), size(PyList_GET_SIZE(ptr(list)))
    {
        if (index >= 0 && index < size) {
            curr = PyList_GET_ITEM(ptr(list), index);
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
            curr = PyList_GET_ITEM(ptr(list), index);
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
            curr = PyList_GET_ITEM(ptr(list), index);
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
