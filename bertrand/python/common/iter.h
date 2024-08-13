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

    /// NOTE: modifying a copy of a Python iterator will also modify the original due
    /// to the inherent state of the iterator, which is managed by the Python runtime.
    /// This class is only copyable in order to fulfill the requirements of the
    /// iterator protocol, but it is not recommended to use it in this way.

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
    T* operator->() { return &curr; }
    const T& operator*() const { return curr; }
    const T* operator->() const { return &curr; }

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


}  // namespace py


#endif
