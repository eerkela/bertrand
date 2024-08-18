#ifndef BERTRAND_PYTHON_CORE_ITER_H
#define BERTRAND_PYTHON_CORE_ITER_H

#include "declarations.h"
#include "except.h"
#include "object.h"
#include "type.h"


namespace py {


////////////////////////
////    ITERATOR    ////
////////////////////////


template <std::derived_from<Object> Return>
struct Type<Iterator<Return>>;


template <std::derived_from<Object> Return>
struct Interface<Iterator<Return>> {};
template <std::derived_from<Object> Return>
struct Interface<Type<Iterator<Return>>> {
    static py::Iterator<Return> __iter__(py::Iterator<Return>& iter);
    static Return __next__(py::Iterator<Return>& iter);
};


/* A generic Python iterator with a static value type.

This type has no fixed implementation, and can match any kind of iterator.  It roughly
corresponds to the `collections.abc.Iterator` abstract base class in Python, and makes
an arbitrary Python iterator accessible from C++.  Note that the reverse (exposing a
C++ iterator to Python) is done via the `__python__::__iter__(PyObject* self)` API
method, which returns a unique type for each container.  This class will match any of
those types, but is not restricted to them, and will be universally slower as a result.

In the interest of performance, no explicit checks are done to ensure that the return
type matches expectations.  As such, this class is one of the rare cases where type
safety may be violated, and should therefore be used with caution.  It is mostly meant
for internal use to back the default result of the `begin()` and `end()` operators when
no specialized C++ iterator can be found.  In that case, its value type is set to the
`T` in an `__iter__<Container> : Returns<T> {};` specialization. */
template <typename Return = Object>
struct Iterator : Object, Interface<Iterator<Return>>, impl::IterTag {

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


/* The type of a generic Python iterator.  This is identical to the
`collections.abc.Iterator` abstract base class, and will match any Python iterator
regardless of return type. */
template <typename Return>
struct Type<Iterator<Return>> : Object, Interface<Type<Iterator<Return>>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, py::Iterator<Return>> {
        static Type __import__() {
            PyObject* collections_abc = PyImport_Import(
                impl::TemplateString<"collections.abc">::ptr
            );
            if (collections_abc == nullptr) {
                Exception::from_python();
            }
            PyObject* iterator = PyObject_GetAttr(
                collections_abc,
                impl::TemplateString<"Iterator">::ptr
            );
            Py_DECREF(collections_abc);
            if (iterator == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Type>(iterator);
        }
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

};


template <std::derived_from<Object> T, typename Return>
struct __isinstance__<T, Iterator<Return>> : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        return PyIter_Check(ptr(obj));
    }
};


template <std::derived_from<Object> T, typename Return>
struct __issubclass__<T, Iterator<Return>> : Returns<bool> {
    static consteval bool operator()() {
        return
            std::derived_from<T, impl::IterTag> &&
            std::convertible_to<impl::iter_type<T>, Return>;
    }
};


template <typename T>
struct __iter__<Iterator<T>> : Returns<T> {
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = T;
    using pointer           = T*;
    using reference         = T&;

    Iterator<T> iter;
    T curr;

    __iter__(const Iterator<T>& self) :
        iter(self), curr(reinterpret_steal<T>(nullptr))
    {}

    __iter__(Iterator<T>&& self) :
        iter(std::move(self)), curr(reinterpret_steal<T>(nullptr))
    {}

    __iter__(const Iterator<T>& self, int) : __iter__(self) {
        ++(*this);
    }

    __iter__(Iterator<T>&& self, int) : __iter__(std::move(self)) {
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

    [[nodiscard]] T& operator*() { return curr; }
    [[nodiscard]] T* operator->() { return &curr; }
    [[nodiscard]] const T& operator*() const { return curr; }
    [[nodiscard]] const T* operator->() const { return &curr; }

    __iter__& operator++() {
        PyObject* next = PyIter_Next(ptr(iter));
        if (PyErr_Occurred()) {
            Exception::from_python();
        }
        curr = reinterpret_steal<T>(next);
        return *this;
    }

    /// NOTE: post-increment is not supported due to inaccurate copy semantics.

    [[nodiscard]] bool operator==(const __iter__& other) const {
        return ptr(curr) == ptr(other.curr);
    }

    [[nodiscard]] bool operator!=(const __iter__& other) const {
        return ptr(curr) != ptr(other.curr);
    }

};
/// NOTE: no __iter__<const Iterator<T>> specialization since marking the iterator
/// itself as const prevents it from being incremented.


template <typename T, typename Return>
struct __contains__<T, Iterator<Return>> : Returns<bool> {};


template <typename Return>
struct __getattr__<Iterator<Return>, "__iter__"> : Returns<Function<Iterator<Return>()>> {};
template <typename Return>
struct __getattr__<Iterator<Return>, "__next__"> : Returns<Function<Return()>> {};
template <typename Return>
struct __getattr__<Type<Iterator<Return>>, "__iter__"> : Returns<Function<
    Iterator<Return>(Iterator<Return>&)
>> {};
template <typename Return>
struct __getattr__<Type<Iterator<Return>>, "__next__"> : Returns<Function<
    Return(Iterator<Return>&)
>> {};


template <std::derived_from<Object> Return>
auto Interface<Type<py::Iterator<Return>>>::__iter__(py::Iterator<Return>& iter)
    -> py::Iterator<Return>
{
    return iter;
}


template <std::derived_from<Object> Return>
auto Interface<Type<py::Iterator<Return>>>::__next__(py::Iterator<Return>& iter)
    -> Return
{
    PyObject* next = PyIter_Next(ptr(iter));
    if (next == nullptr) {
        if (PyErr_Occurred()) {
            Exception::from_python();
        }
        throw StopIteration();
    }
    return reinterpret_steal<Return>(next);
}


/////////////////////////
////    OPERATORS    ////
/////////////////////////


/* Begin iteration operator.  Both this and the end iteration operator are
controlled by the __iter__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__iter__<Self>::enable)
[[nodiscard]] auto begin(Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__iter__<Self>, const Self&>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<Self>(self, 0);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<Self>, const Self&>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::begin(py::unwrap(self));
        } else {
            using Return = typename __iter__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __iter__ for this types and ensure the Return type "
                "is a subclass of py::Object."
            );
            PyObject* iter = PyObject_GetIter(ptr(self));
            if (iter == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Iterator<Return>>(iter);
        }
    }
}


/* Begin iteration operator.  Both this and the end iteration operator are
controlled by the __iter__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__iter__<const Self>::enable)
[[nodiscard]] auto begin(const Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__iter__<Self>, const Self&>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<Self>(self, 0);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<Self>, const Self&>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::begin(py::unwrap(self));
        } else {
            using Return = typename __iter__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __iter__ for this types and ensure the Return type "
                "is a subclass of py::Object."
            );
            PyObject* iter = PyObject_GetIter(ptr(self));
            if (iter == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Iterator<Return>>(iter);
        }
    }
}


/* Const iteration operator.  Python has no distinction between mutable and
immutable iterators, so this is fundamentally the same as the ordinary begin()
method.  Some libraries assume the existence of this method. */
template <typename Self> requires (__iter__<const Self>::enable)
[[nodiscard]] auto cbegin(const Self& self) {
    return begin(self);
}


/* End iteration operator.  This terminates the iteration and is controlled by the
__iter__ control struct. */
template <typename Self> requires (__iter__<Self>::enable)
[[nodiscard]] auto end(Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__iter__<Self>, const Self&, int>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<Self>(self);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<Self>, const Self&, int>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::end(py::unwrap(self));
        } else {
            using Return = typename __iter__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __iter__ for this types and ensure the Return type "
                "is a subclass of py::Object."
            );
            return reinterpret_steal<Iterator<Return>>(nullptr);
        }
    }
}


/* End iteration operator.  This terminates the iteration and is controlled by the
__iter__ control struct. */
template <typename Self> requires (__iter__<const Self>::enable)
[[nodiscard]] auto end(const Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__iter__<Self>, const Self&, int>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<Self>(self);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<Self>, const Self&, int>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::end(py::unwrap(self));
        } else {
            using Return = typename __iter__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __iter__ for this types and ensure the Return type "
                "is a subclass of py::Object."
            );
            return reinterpret_steal<Iterator<Return>>(nullptr);
        }
    }
}


/* Const end operator.  Similar to `cbegin()`, this is identical to `end()`. */
template <typename Self> requires (__iter__<const Self>::enable)
[[nodiscard]] auto cend(const Self& self) {
    return end(self);
}


/* Reverse iteration operator.  Both this and the reverse end operator are
controlled by the __reversed__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__reversed__<Self>::enable)
[[nodiscard]] auto rbegin(Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__reversed__<Self>, const Self&>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<Self>(self, 0);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<Self>, const Self&>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::rbegin(py::unwrap(self));
        } else {
            using Return = typename __reversed__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __reversed__ for this types and ensure the Return "
                "type is a subclass of py::Object."
            );
            PyObject* iter = PyObject_CallMethodNoArgs(
                ptr(self),
                impl::TemplateString<"__reversed__">::ptr
            );
            if (iter == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Iterator<Return>>(iter);
        }
    }
}


/* Reverse iteration operator.  Both this and the reverse end operator are
controlled by the __reversed__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__reversed__<const Self>::enable)
[[nodiscard]] auto rbegin(const Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__reversed__<Self>, const Self&>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<Self>(self, 0);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<Self>, const Self&>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::rbegin(py::unwrap(self));
        } else {
            using Return = typename __reversed__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __reversed__ for this types and ensure the Return "
                "type is a subclass of py::Object."
            );
            PyObject* iter = PyObject_CallMethodNoArgs(
                ptr(self),
                impl::TemplateString<"__reversed__">::ptr
            );
            if (iter == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Iterator<Return>>(iter);
        }
    }
}


/* Const reverse iteration operator.  Python has no distinction between mutable
and immutable iterators, so this is fundamentally the same as the ordinary
rbegin() method.  Some libraries assume the existence of this method. */
template <typename Self> requires (__reversed__<const Self>::enable)
[[nodiscard]] auto crbegin(const Self& self) {
    return rbegin(self);
}


/* Reverse end operator.  This terminates the reverse iteration and is controlled
by the __reversed__ control struct. */
template <typename Self> requires (__reversed__<Self>::enable)
[[nodiscard]] auto rend(Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__reversed__<Self>, const Self&, int>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<Self>(self);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<Self>, const Self&, int>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::rend(py::unwrap(self));
        } else {
            using Return = typename __reversed__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __reversed__ for this types and ensure the Return "
                "type is a subclass of py::Object."
            );
            return reinterpret_steal<Iterator<Return>>(nullptr);
        }
    }
}


/* Reverse end operator.  This terminates the reverse iteration and is controlled
by the __reversed__ control struct. */
template <typename Self> requires (__reversed__<const Self>::enable)
[[nodiscard]] auto rend(const Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__reversed__<Self>, const Self&, int>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<Self>(self);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<Self>, const Self&, int>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::originates_from_cpp<Self>) {
            return std::ranges::rend(py::unwrap(self));
        } else {
            using Return = typename __reversed__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __reversed__ for this types and ensure the Return "
                "type is a subclass of py::Object."
            );
            return reinterpret_steal<Iterator<Return>>(nullptr);
        }
    }
}


/* Const reverse end operator.  Similar to `crbegin()`, this is identical to
`rend()`. */
template <typename Self> requires (__reversed__<const Self>::enable)
[[nodiscard]] auto crend(const Self& self) {
    return rend(self);
}


}  // namespace py


#endif
