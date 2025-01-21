#ifndef BERTRAND_CORE_ITER_H
#define BERTRAND_CORE_ITER_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"


namespace bertrand {


template <typename Begin, typename End = void, typename Container = void>
struct Iterator;


template <typename Begin, typename End, typename Container>
struct interface<Iterator<Begin, End, Container>> : impl::IterTag {
private:

    template <typename B, typename E, typename C>
    struct traits {
        using begin_type = B;
        using end_type = E;
        using container_type = C;
        using value_type = decltype(*std::declval<B>());
    };
    template <typename R>
    struct traits<R, void, void> {
        using begin_type = __iter__<Iterator<R>>;
        using end_type = sentinel;
        using container_type = void;
        using value_type = R;
    };

public:
    using begin_type = traits<Begin, End, Container>::begin_type;
    using end_type = traits<Begin, End, Container>::end_type;
    using container_type = traits<Begin, End, Container>::container_type;
    using value_type = traits<Begin, End, Container>::value_type;

    decltype(auto) __iter__(this auto&& self) {
        return std::forward<decltype(self)>(self);
    }

    decltype(auto) __next__(this auto&& self) {
        if constexpr (std::is_void_v<End>) {
            PyObject* next = PyIter_Next(ptr(self));
            if (next == nullptr) {
                if (PyErr_Occurred()) {
                    Exception::from_python();
                }
                throw StopIteration();
            }
            return steal<Begin>(next);

        } else {
            auto inner = view(self);
            if (inner->begin == inner->end) {
                throw StopIteration();
            }
            ++(inner->begin);
            if (inner->begin == inner->end) {
                throw StopIteration();
            }
            return *(inner->begin);
        }
    }
};


template <typename Begin, typename End, typename Container>
struct interface<Type<Iterator<Begin, End, Container>>> {
    using begin_type        = interface<Iterator<Begin, End, Container>>::begin_type;
    using end_type          = interface<Iterator<Begin, End, Container>>::end_type;
    using container_type    = interface<Iterator<Begin, End, Container>>::container_type;
    using value_type        = interface<Iterator<Begin, End, Container>>::value_type;

    template <meta::inherits<interface<Iterator<Begin, End, Container>>> Self>
    static decltype(auto) __iter__(Self&& self) {
        return std::forward<Self>(self).__iter__();
    }

    template <meta::inherits<interface<Iterator<Begin, End, Container>>> Self>
    static decltype(auto) __next__(Self&& self) {
        return std::forward<Self>(self).__next__();
    }
};


/* A wrapper around a Python iterator that allows it to be used from C++.

This type has no fixed implementation, and can match any kind of Python iterator.  It
roughly corresponds to the `collections.abc.Iterator` abstract base class in Python,
and allows C++ to call the Python-level `__next__()` hook.  Note that the reverse
(exposing C++ iterators to Python) is done via a separate specialization.

In the interest of performance, no explicit checks are done to ensure that the return
type matches expectations.  As such, this class is one of the rare cases where type
safety may be violated, and should therefore be used with caution.  It is mostly meant
for internal use to back the default result of the `begin()` and `end()` operators when
no specialized C++ iterator can be found.  In that case, its value type is set to the
`T` in an `__iter__<Container> : returns<T> {};` specialization.  If you want to use
this class and avoid type safety issues, leave the return type set to `Object` (the
default), which will incur a runtime check on conversion. */
template <meta::python Return>
struct Iterator<Return, void, void> : Object, interface<Iterator<Return, void, void>> {
    struct __python__ : cls<__python__, Iterator>, PyObject {
        static Type<Iterator> __import__() {
            Object collections = steal<Object>(PyImport_Import(
                ptr(impl::template_string<"collections.abc">())
            ));
            if (collections.is(nullptr)) {
                Exception::from_python();
            }
            Type<Iterator> result = steal<Type<Iterator>>(
                PyObject_GetItem(
                    ptr(getattr<"Iterator">(collections)),
                    ptr(Type<Return>())
                )
            );
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return result;
        }
    };

    Iterator(PyObject* p, borrowed_t t) : Object(p, t) {}
    Iterator(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Iterator> requires (__initializer__<T>::enable)
    Iterator(const std::initializer_list<typename __initializer__<T>::type>& init) :
        Object(__initializer__<T>{}(init))
    {}

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


/* A wrapper around a non-owning C++ range that allows them to be iterated over from
Python.

This will instantiate a unique Python type with an appropriate `__next__()` method for
every combination of C++ iterators, forwarding to their respective `operator*()`,
`operator++()`, and `operator==()` methods. */
template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
struct Iterator<Begin, End, void> : Object, interface<Iterator<Begin, End, void>> {
    struct __python__ : cls<__python__, Iterator>, PyObject {
        inline static bool initialized = false;
        static PyTypeObject __type__;

        std::remove_reference_t<Begin> begin;
        std::remove_reference_t<End> end;

        __python__(auto& container) :
            begin(std::ranges::begin(container)), end(std::ranges::end(container))
        {
            ready();
        }

        __python__(Begin&& begin, End&& end) :
            begin(std::forward(begin)), end(std::forward(end))
        {
            ready();
        }

        static Type<Iterator> __import__() {
            ready();
            return borrow<Type<Iterator>>(&__type__);
        }

        static int __bool__(__python__* self) {
            try {
                return self->begin != self->end;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __next__(__python__* self) {
            try {
                if (self->begin == self->end) {
                    return nullptr;
                }
                auto result = to_python(*(self->begin));  // owning obj
                ++(self->begin);
                if constexpr (meta::python<decltype(*(self->begin))>) {
                    return Py_NewRef(ptr(result));
                } else {
                    return release(result);
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        inline static PyNumberMethods number = {
            .nb_bool = reinterpret_cast<inquiry>(__bool__)
        };

        static void ready() {
            if (!initialized) {
                __type__ = {
                    .tp_name = typeid(Iterator).name(),
                    .tp_basicsize = sizeof(__python__),
                    .tp_itemsize = 0,
                    .tp_as_number = &number,
                    .tp_flags = 
                        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
                    .tp_iter = PyObject_SelfIter,
                    .tp_iternext = reinterpret_cast<iternextfunc>(__next__)
                };
                if (PyType_Ready(&__type__) < 0) {
                    Exception::from_python();
                }
                initialized = true;
            }
        }

    };

    Iterator(PyObject* p, borrowed_t t) : Object(p, t) {}
    Iterator(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Iterator> requires (__initializer__<T>::enable)
    Iterator(const std::initializer_list<typename __initializer__<T>::type>& init) :
        Object(__initializer__<T>{}(init))
    {}

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


/* A wrapper around an owning C++ range that was generated from a temporary container.
The container is moved into the Python iterator object and will remain valid as long as
the iterator object has a nonzero reference count.

This will instantiate a unique Python type with an appropriate `__next__()` method for
every combination of C++ iterators, forwarding to their respective `operator*()`,
`operator++()`, and `operator==()` methods. */
template <
    std::input_or_output_iterator Begin,
    std::sentinel_for<Begin> End,
    meta::iterable Container
>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
struct Iterator<Begin, End, Container> : Object, interface<Iterator<Begin, End, Container>> {
    struct __python__ : cls<__python__, Iterator>, PyObject {
        inline static bool initialized = false;
        static PyTypeObject __type__;

        Container container;
        Begin begin;
        End end;

        __python__(Container&& container) :
            container(std::move(container)),
            begin(std::ranges::begin(this->container)),
            end(std::ranges::end(this->container))
        {
            ready();
        }

        static Type<Iterator> __import__() {
            ready();
            return borrow<Type<Iterator>>(&__type__);
        }

        static int __bool__(__python__* self) {
            try {
                return self->begin != self->end;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static PyObject* __next__(__python__* self) {
            try {
                if (self->begin == self->end) {
                    return nullptr;
                }
                auto result = to_python(*(self->begin));
                ++(self->begin);
                if constexpr (meta::python<decltype(*(self->begin))>) {
                    return Py_NewRef(ptr(result));
                } else {
                    return release(result);
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        inline static PyNumberMethods number = {
            .nb_bool = reinterpret_cast<inquiry>(__bool__)
        };

        static void ready() {
            if (!initialized) {
                __type__ = {
                    .tp_name = typeid(Iterator).name(),
                    .tp_basicsize = sizeof(__python__),
                    .tp_itemsize = 0,
                    .tp_as_number = &number,
                    .tp_flags = 
                        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
                    .tp_iter = PyObject_SelfIter,
                    .tp_iternext = reinterpret_cast<iternextfunc>(__next__)
                };
                if (PyType_Ready(&__type__) < 0) {
                    Exception::from_python();
                }
                initialized = true;
            }
        }
    };

    Iterator(PyObject* p, borrowed_t t) : Object(p, t) {}
    Iterator(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Iterator> requires (__initializer__<T>::enable)
    Iterator(const std::initializer_list<typename __initializer__<T>::type>& init) :
        Object(__initializer__<T>{}(init))
    {}

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


namespace impl {

    template <meta::iterable Container>
    struct IterTraits {
        using begin = decltype(std::ranges::begin(
            std::declval<std::add_lvalue_reference_t<Container>>()
        ));
        using end = decltype(std::ranges::end(
            std::declval<std::add_lvalue_reference_t<Container>>()
        ));
        using container = std::remove_reference_t<Container>;
    };
    template <meta::iterable Container> requires (std::is_lvalue_reference_v<Container>)
    struct IterTraits<Container> {
        using begin = decltype(std::ranges::begin(std::declval<Container>()));
        using end = decltype(std::ranges::end(std::declval<Container>()));
        using container = void;
    };

}


/* CTAD guide will generate a Python iterator around a pair of raw C++ iterators. */
template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
Iterator(Begin, End) -> Iterator<Begin, End, void>;


/* CTAD guide will generate a Python iterator from an arbitrary C++ container, with
correct ownership semantics. */
template <meta::iterable Container>
    requires (meta::yields<Container, Object>)
Iterator(Container&&) -> Iterator<
    typename impl::IterTraits<Container>::begin,
    typename impl::IterTraits<Container>::end,
    typename impl::IterTraits<Container>::container
>;


/* Implement the CTAD guide for iterable containers.  The container type may be const,
which will be reflected in the deduced iterator types. */
template <meta::iterable Container>
    requires (meta::yields<Container, Object>)
struct __init__<
    Iterator<
        typename impl::IterTraits<Container>::begin,
        typename impl::IterTraits<Container>::end,
        typename impl::IterTraits<Container>::container
    >,
    Container
> : returns<Iterator<
    typename impl::IterTraits<Container>::begin,
    typename impl::IterTraits<Container>::end,
    typename impl::IterTraits<Container>::container
>> {
    static auto operator()(Container&& self) {
        return impl::construct<Iterator<
            typename impl::IterTraits<Container>::begin,
            typename impl::IterTraits<Container>::end,
            typename impl::IterTraits<Container>::container
        >>(std::forward<Container>(self));
    }
};


/* Construct a Python iterator from a pair of C++ iterators. */
template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
struct __init__<Iterator<Begin, End, void>, Begin, End> : returns<Iterator<Begin, End, void>> {
    static auto operator()(auto&& begin, auto&& end) {
        return impl::construct<Iterator<Begin, End, void>>(
            std::forward<decltype(begin)>(begin),
            std::forward<decltype(end)>(end)
        );
    }
};


template <meta::is<Object> Derived, typename Return>
struct __isinstance__<Derived, Iterator<Return, void, void>> : returns<bool> {
    static constexpr bool operator()(Derived obj) {
        return PyIter_Check(ptr(obj));
    }
};


template <typename Derived, typename Return>
struct __issubclass__<Derived, Iterator<Return, void, void>> : returns<bool> {
    static constexpr bool operator()() {
        return
            meta::inherits<Derived, impl::IterTag> &&
            std::convertible_to<meta::iter_type<Derived>, Return>;
    }
    static constexpr bool operator()(Derived obj) {
        if constexpr (meta::is<Derived, Object>) {
            int rc = PyObject_IsSubclass(
                ptr(obj),
                ptr(Type<Iterator<Return, void, void>>())
            );
            if (rc == -1) {
                Exception::from_python();
            }
            return rc;
        } else {
            return operator()();
        }
    }
};


/* Traversing a Python iterator requires a customized C++ iterator type. */
template <typename T>
struct __iter__<Iterator<T, void, void>>                    : returns<T> {
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = T;
    using pointer           = T*;
    using reference         = T&;

    Iterator<T> iter;
    T curr;

    __iter__(const Iterator<T>& self) :
        iter(self), curr(steal<T>(nullptr))
    {
        ++(*this);
    }

    __iter__(Iterator<T>&& self) :
        iter(self), curr(steal<T>(nullptr))
    {
        ++(*this);
    }

    /// NOTE: python iterators cannot be copied due to their stateful nature via the
    /// shared PyObject* pointer.

    __iter__(const __iter__&) = delete;
    __iter__(__iter__&& other) :
        iter(std::move(other.iter)), curr(std::move(other.curr))
    {}

    __iter__& operator=(const __iter__&) = delete;
    __iter__& operator=(__iter__&& other) {
        if (&other != this) {
            iter = std::move(other.iter);
            curr = std::move(other.curr);
        }
        return *this;
    }

    [[nodiscard]] const T& operator*() const { return curr; }
    [[nodiscard]] const T* operator->() const { return &curr; }

    __iter__& operator++() {
        PyObject* next = PyIter_Next(ptr(iter));
        if (PyErr_Occurred()) {
            Exception::from_python();
        }
        curr = steal<T>(next);
        return *this;
    }

    __iter__& operator++(int) {
        return ++(*this);
    }

    [[nodiscard]] friend bool operator==(const __iter__& self, sentinel) {
        return ptr(self.curr) == nullptr;
    }

    [[nodiscard]] friend bool operator==(sentinel, const __iter__& self) {
        return ptr(self.curr) == nullptr;
    }

    [[nodiscard]] friend bool operator!=(const __iter__& self, sentinel) {
        return ptr(self.curr) != nullptr;
    }

    [[nodiscard]] friend bool operator!=(sentinel, const __iter__& self) {
        return ptr(self.curr) != nullptr;
    }

};
/// NOTE: can't iterate over a const Iterator<T> because the iterator itself must be
/// mutable.


/* py::Iterator<Begin, End, ...> is special cased in the begin() and end() operators to
extract the internal C++ iterators rather than creating yet another layer of
indirection. */
template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End, typename Container>
struct __iter__<Iterator<Begin, End, Container>> : returns<decltype(*std::declval<Begin>())> {};


template <typename T, typename Begin, typename End, typename Container>
struct __contains__<T, Iterator<Begin, End, Container>> : returns<bool> {};


/// TODO: these attributes can only be defined after functions are defined


// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Self, "__iter__"> : returns<
//     Function<impl::qualify<Self(std::remove_cvref_t<Self>::*)(), Self>>
// > {};
// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Self, "__next__"> : returns<
//     Function<impl::qualify<
//         std::conditional_t<
//             std::is_void_v<typename std::remove_reference_t<Self>::end_type>,
//             std::remove_reference_t<decltype(
//                 *std::declval<typename std::remove_reference_t<Self>::begin_type>()
//             )>,
//             decltype(
//                 *std::declval<typename std::remove_reference_t<Self>::begin_type>()
//             )
//         >(std::remove_cvref_t<Self>::*)(),
//         Self
//     >>
// > {};
// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Type<Self>, "__iter__"> : returns<Function<
//     Self(*)(Self)
// >> {};
// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Type<Self>, "__next__"> : returns<Function<
//     std::conditional_t<
//         std::is_void_v<typename std::remove_reference_t<Self>::end_type>,
//         std::remove_reference_t<decltype(
//             *std::declval<typename std::remove_reference_t<Self>::begin_type>()
//         )>,
//         decltype(
//             *std::declval<typename std::remove_reference_t<Self>::begin_type>()
//         )
//     >(*)(Self)
// >> {};


/* Begin iteration operator.  Both this and the end iteration operator are
controlled by the __iter__ control struct, whose return type dictates the
iterator's dereference type. */
template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto begin(const Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, Self>) {
        return __iter__<Self>(self);

    } else if constexpr (meta::has_cpp<Self>) {
        return std::ranges::begin(from_python(self));

    } else if constexpr (meta::inherits<Self, impl::IterTag>) {
        if constexpr (!std::is_void_v<typename std::remove_reference_t<Self>::end_type>) {
            return view(self)->begin;
        } else {
            using T = __iter__<Self>::type;
            PyObject* iter = PyObject_GetIter(ptr(self));
            if (iter == nullptr) {
                Exception::from_python();
            }
            return __iter__<Iterator<T>>{steal<Iterator<T>>(iter)};
        }

    } else {
        using T = __iter__<Self>::type;
        PyObject* iter = PyObject_GetIter(ptr(self));
        if (iter == nullptr) {
            Exception::from_python();
        }
        return __iter__<Iterator<T>>{steal<Iterator<T>>(iter)};
    }
}


template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto begin(Self& self) {
    return begin(reinterpret_cast<std::add_const_t<Self>&>(self));
}


template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto cbegin(const Self& self) {
    return begin(self);
}


/* End iteration operator.  This terminates the iteration and is controlled by the
__iter__ control struct. */
template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto end(const Self& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, Self>) {
        return sentinel{};

    } else if constexpr (meta::has_cpp<Self>) {
        return std::ranges::end(from_python(std::forward<Self>(self)));

    } else if constexpr (meta::inherits<Self, impl::IterTag>) {
        if constexpr (!std::is_void_v<typename std::remove_reference_t<Self>::end_type>) {
            return view(self)->end;
        } else {
            return sentinel{};
        }

    } else {
        return sentinel{};
    }
}


template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto end(Self& self) {
    return end(reinterpret_cast<std::add_const_t<Self>&>(self));
}


/* Const end operator.  Similar to `cbegin()`, this is identical to `end()`. */
template <meta::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ) && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] auto cend(const Self& self) {
    return end(std::forward<Self>(self));
}


/* Reverse iteration operator.  Both this and the reverse end operator are
controlled by the __reversed__ control struct, whose return type dictates the
iterator's dereference type. */
template <meta::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::reverse_iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ))
[[nodiscard]] auto rbegin(const Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, Self>) {
        return __reversed__<Self>(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return std::ranges::rbegin(from_python(std::forward<Self>(self)));

    } else {
        using T = typename __reversed__<Self>::type;
        Object builtins = steal<Object>(
            PyFrame_GetBuiltins(PyEval_GetFrame())
        );
        Object func = steal<Object>(PyDict_GetItemWithError(
            ptr(builtins),
            ptr(impl::template_string<"reversed">())
        ));
        if (func.is(nullptr)) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            throw KeyError("'reversed'");
        }
        PyObject* iter = PyObject_CallOneArg(ptr(func), ptr(self));
        if (iter == nullptr) {
            Exception::from_python();
        }
        return __iter__<Iterator<T>>{steal<Iterator<T>>(iter)};
    }
}


template <meta::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::reverse_iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ))
[[nodiscard]] auto rbegin(Self& self) {
    return rbegin(reinterpret_cast<std::add_const_t<Self>&>(self));
}


/* Const reverse iteration operator.  Python has no distinction between mutable
and immutable iterators, so this is fundamentally the same as the ordinary
rbegin() method.  Some libraries assume the existence of this method. */
template <meta::python Self>
    requires (__reversed__<Self>::enable && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] auto crbegin(const Self& self) {
    return rbegin(std::forward<Self>(self));
}


/* Reverse end operator.  This terminates the reverse iteration and is controlled
by the __reversed__ control struct. */
template <meta::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::reverse_iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ))
[[nodiscard]] auto rend(const Self& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, Self>) {
        return sentinel{};

    } else if constexpr (meta::has_cpp<Self>) {
        return std::ranges::rend(from_python(std::forward<Self>(self)));

    } else {
        return sentinel{};
    }
}


template <meta::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::reverse_iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ))
[[nodiscard]] auto rend(Self& self) {
    return rend(reinterpret_cast<std::add_const_t<Self>&>(self));
}


/* Const reverse end operator.  Similar to `crbegin()`, this is identical to
`rend()`. */
template <meta::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (meta::has_cpp<Self> && meta::reverse_iterable<meta::cpp_type<Self>>) ||
        (!meta::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ) && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] auto crend(const Self& self) {
    return rend(std::forward<Self>(self));
}


namespace impl {

    /* A range adaptor that concatenates a sequence of subranges into a single view.
    Every element in the input range must yield another range, which will be flattened
    into a single output range. */
    template <std::ranges::view View>
        requires (std::ranges::view<std::ranges::range_value_t<View>>)
    struct Comprehension : BertrandTag, std::ranges::view_base {
    private:
        using InnerView = std::ranges::range_value_t<View>;

        View m_view;

        struct Iterator {
        private:

            void skip_empty_views() {
                while (inner_begin == inner_end) {
                    if (++outer_begin == outer_end) {
                        break;
                    }
                    curr = *outer_begin;
                    inner_begin = std::ranges::begin(curr);
                    inner_end = std::ranges::end(curr);
                }
            }

        public:
            using iterator_category = std::input_iterator_tag;
            using value_type = std::ranges::range_value_t<InnerView>;
            using difference_type = std::ranges::range_difference_t<InnerView>;
            using pointer = value_type*;
            using reference = value_type&;

            std::ranges::iterator_t<View> outer_begin;
            std::ranges::sentinel_t<View> outer_end;
            InnerView curr;
            std::ranges::iterator_t<InnerView> inner_begin;
            std::ranges::sentinel_t<InnerView> inner_end;

            Iterator() = default;
            Iterator(
                std::ranges::iterator_t<View>&& outer_begin,
                std::ranges::sentinel_t<View>&& outer_end
            ) : outer_begin(std::move(outer_begin)), outer_end(std::move(outer_end))
            {
                if (this->outer_begin != this->outer_end) {
                    curr = *(this->outer_begin);
                    inner_begin = std::ranges::begin(curr);
                    inner_end = std::ranges::end(curr);
                    skip_empty_views();
                }
            }

            Iterator& operator++() {
                if (++inner_begin == inner_end) {
                    if (++outer_begin != outer_end) {
                        curr = *outer_begin;
                        inner_begin = std::ranges::begin(curr);
                        inner_end = std::ranges::end(curr);
                        skip_empty_views();
                    }
                }
                return *this;
            }

            decltype(auto) operator*() const {
                return *inner_begin;
            }

            friend bool operator==(const Iterator& self, const sentinel&) {
                return self.outer_begin == self.outer_end;
            }

            friend bool operator==(const sentinel&, const Iterator& self) {
                return self.outer_begin == self.outer_end;
            }

            friend bool operator!=(const Iterator& self, const sentinel&) {
                return self.outer_begin != self.outer_end;
            }

            friend bool operator!=(const sentinel&, const Iterator& self) {
                return self.outer_begin != self.outer_end;
            }

        };

    public:

        Comprehension() = default;
        Comprehension(const Comprehension&) = default;
        Comprehension(Comprehension&&) = default;
        Comprehension(View&& view) : m_view(std::move(view)) {}

        Iterator begin() {
            return Iterator(std::ranges::begin(m_view), std::ranges::end(m_view));
        }

        sentinel end() {
            return {};
        };

    };

    template <std::ranges::view View>
        requires (std::ranges::view<std::ranges::range_value_t<View>>)
    Comprehension(View&&) -> Comprehension<std::remove_cvref_t<View>>;

}


/* Apply a C++ range adaptor to a Python object.  This is similar to the C++-style `|`
operator for chaining range adaptors, but uses the `->*` operator to avoid conflicts
with Python and apply higher precedence than typical binary operators. */
template <meta::python Self, std::ranges::view View> requires (meta::iterable<Self>)
[[nodiscard]] auto operator->*(Self&& self, View&& view) {
    return std::views::all(std::forward<Self>(self)) | std::forward<View>(view);
}


/* Generate a C++ range adaptor that approximates a Python-style list comprehension.
This is done by piping a raw function pointer or lambda in place of a C++ range
adaptor, which will be applied to each element in the sequence.  The function must be
callable with the container's value type, and may return any type.

If the function returns a range adaptor, then the adaptor's output will be flattened
into the parent range, similar to a nested `for` loop within a comprehension.
Returning a range with no elements will effectively filter out the current element,
similar to a Python `if` clause within a comprehension.

Here's an example:

    py::List list = {1, 2, 3, 4, 5};
    py::List new_list = list->*[](const py::Int& x) {
        return std::views::repeat(x, x % 2 ? 0 : x);
    };
    py::print(new_list);  // [2, 2, 4, 4, 4, 4]
*/
template <meta::python Self, typename Func>
    requires (
        meta::iterable<Self> &&
        !std::ranges::view<Func> &&
        std::is_invocable_v<Func, meta::iter_type<Self>>
    )
[[nodiscard]] auto operator->*(Self&& self, Func&& func) {
    using Return = std::invoke_result_t<Func, meta::iter_type<Self>>;
    if constexpr (std::ranges::view<Return>) {
        return impl::Comprehension(
            std::views::all(std::forward<Self>(self)) |
            std::views::transform(std::forward<Func>(func))
        );
    } else {
        return
            std::views::all(std::forward<Self>(self)) |
            std::views::transform(std::forward<Func>(func));
    }
}


}


#endif
