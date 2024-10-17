#ifndef BERTRAND_CORE_ITER_H
#define BERTRAND_CORE_ITER_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"


namespace py {


namespace impl {
    struct Sentinel {};
}


template <typename Begin, typename End = void, typename Container = void>
struct Iterator;


template <typename Begin, typename End, typename Container>
struct Interface<Iterator<Begin, End, Container>> : impl::IterTag {
    using begin_t = Begin;
    using end_t = End;
    using container_t = Container;

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
            return reinterpret_steal<Begin>(next);

        } else {
            if (self->begin == self->end) {
                throw StopIteration();
            }
            ++(self->begin);
            if (self->begin == self->end) {
                throw StopIteration();
            }
            return *(self->begin);
        }
    }
};


template <typename Begin, typename End, typename Container>
struct Interface<Type<Iterator<Begin, End, Container>>> {
    using begin_t = Begin;
    using end_t = End;
    using container_t = Container;

    template <impl::inherits<Interface<Iterator<Begin, End, Container>>> Self>
    static decltype(auto) __iter__(Self&& self) {
        return std::forward<Self>(self).__iter__();
    }

    template <impl::inherits<Interface<Iterator<Begin, End, Container>>> Self>
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
`T` in an `__iter__<Container> : Returns<T> {};` specialization.  If you want to use
this class and avoid type safety issues, leave the return type set to `Object` (the
default), which will incur a runtime check on conversion. */
template <impl::python Return>
struct Iterator<Return, void, void> : Object, Interface<Iterator<Return, void, void>> {
    struct __python__ : def<__python__, Iterator>, PyObject {
        /// TODO: maybe implement a custom type

        static Type<Iterator> __import__() {
            constexpr StaticStr str = "collections.abc";
            PyObject* name = PyUnicode_FromStringAndSize(str, str.size());
            if (name == nullptr) {
                Exception::from_python();
            }
            Object collections_abc = reinterpret_steal<Object>(
                PyImport_Import(name)
            );
            Py_DECREF(name);
            if (collections_abc.is(nullptr)) {
                Exception::from_python();
            }
            return reinterpret_steal<Type<Iterator>>(
                release(getattr<"iterator">( collections_abc))
            );
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


/* A wrapper around a non-ownding C++ range that allows them to be iterated over from
Python.

This will instantiate a unique Python type with an appropriate `__next__()` method for
every combination of C++ iterators, forwarding to their respective `operator*()`,
`operator++()`, and `operator==()` methods. */
template <std::input_iterator Begin, std::sentinel_for<Begin> End>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
struct Iterator<Begin, End, void> : Object, Interface<Iterator<Begin, End, void>> {
    struct __python__ : def<__python__, Iterator>, PyObject {
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
            return reinterpret_borrow<Type<Iterator>>(&__type__);
        }

        static PyObject* __next__(__python__* self) {
            try {
                if (self->begin == self->end) {
                    return nullptr;
                }
                if constexpr (std::is_lvalue_reference_v<decltype(*(self->begin))>) {
                    auto result = wrap(*(self->begin));  // non-owning obj
                    ++(self->begin);
                    return reinterpret_cast<PyObject*>(release(result));
                } else {
                    auto result = as_object(*(self->begin));  // owning obj
                    ++(self->begin);
                    return reinterpret_cast<PyObject*>(release(result));
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        static void ready() {
            if (!initialized) {
                __type__ = {
                    .tp_name = typeid(Iterator).name(),
                    .tp_basicsize = sizeof(__python__),
                    .tp_itemsize = 0,
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
template <std::input_iterator Begin, std::sentinel_for<Begin> End, impl::iterable Container>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
struct Iterator<Begin, End, Container> : Object, Interface<Iterator<Begin, End, Container>> {
    struct __python__ : def<__python__, Iterator>, PyObject {
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
            return reinterpret_borrow<Type<Iterator>>(&__type__);
        }

        /// TODO: what if the container yields Python objects?  What about references
        /// to Python objects?

        static PyObject* __next__(__python__* self) {
            try {
                if (self->begin == self->end) {
                    return nullptr;
                }
                if constexpr (std::is_lvalue_reference_v<decltype(*(self->begin))>) {
                    auto result = wrap(*(self->begin));  // non-owning obj
                    ++(self->begin);
                    return reinterpret_cast<PyObject*>(release(result));
                } else {
                    auto result = as_object(*(self->begin));  // owning obj
                    ++(self->begin);
                    return reinterpret_cast<PyObject*>(release(result));
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        static void ready() {
            if (!initialized) {
                __type__ = {
                    .tp_name = typeid(Iterator).name(),
                    .tp_basicsize = sizeof(__python__),
                    .tp_itemsize = 0,
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

    template <iterable Container>
    struct IterTraits {
        using begin = decltype(std::ranges::begin(
            std::declval<std::add_lvalue_reference_t<Container>>()
        ));
        using end = decltype(std::ranges::end(
            std::declval<std::add_lvalue_reference_t<Container>>()
        ));
        using container = std::remove_reference_t<Container>;
    };
    template <iterable Container> requires (std::is_lvalue_reference_v<Container>)
    struct IterTraits<Container> {
        using begin = decltype(std::ranges::begin(std::declval<Container>()));
        using end = decltype(std::ranges::end(std::declval<Container>()));
        using container = void;
    };

}


/* CTAD guide will generate a Python iterator around a pair of raw C++ iterators. */
template <std::input_iterator Begin, std::sentinel_for<Begin> End>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
Iterator(Begin, End) -> Iterator<Begin, End, void>;


/* CTAD guide will generate a Python iterator from an arbitrary C++ container, with
correct ownership semantics. */
template <impl::iterable Container>
    requires (impl::yields<Container, Object>)
Iterator(Container&&) -> Iterator<
    typename impl::IterTraits<Container>::begin,
    typename impl::IterTraits<Container>::end,
    typename impl::IterTraits<Container>::container
>;


/* Implement the CTAD guide for iterable containers.  The container type may be const,
which will be reflected in the deduced iterator types. */
template <impl::iterable Container>
    requires (impl::yields<Container, Object>)
struct __init__<
    Iterator<
        typename impl::IterTraits<Container>::begin,
        typename impl::IterTraits<Container>::end,
        typename impl::IterTraits<Container>::container
    >,
    Container
> : Returns<Iterator<
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
template <std::input_iterator Begin, std::sentinel_for<Begin> End>
    requires (std::convertible_to<decltype(*std::declval<Begin>()), Object>)
struct __init__<Iterator<Begin, End, void>, Begin, End> : Returns<Iterator<Begin, End, void>> {
    static auto operator()(auto&& begin, auto&& end) {
        return impl::construct<Iterator<Begin, End, void>>(
            std::forward<decltype(begin)>(begin),
            std::forward<decltype(end)>(end)
        );
    }
};


template <impl::python T, impl::python Return>
struct __isinstance__<T, Iterator<Return, void, void>>      : Returns<bool> {
    static constexpr bool operator()(T&& obj) {
        if constexpr (impl::dynamic<T>) {
            return PyIter_Check(ptr(obj));
        } else {
            return issubclass<T, Iterator<Return, void, void>>();
        }
    }
};


template <impl::python T, impl::python Return>
struct __issubclass__<T, Iterator<Return, void, void>>      : Returns<bool> {
    static constexpr bool operator()() {
        return
            impl::inherits<T, impl::IterTag> &&
            std::convertible_to<impl::iter_type<T>, Return>;
    }
};


template <
    impl::python T,
    std::input_iterator Begin,
    std::sentinel_for<Begin> End,
    typename Container
>
struct __isinstance__<T, Iterator<Begin, End, Container>>   : Returns<bool> {};


template <
    impl::python T,
    std::input_iterator Begin,
    std::sentinel_for<Begin> End,
    typename Container
>
struct __issubclass__<T, Iterator<Begin, End, Container>>   : Returns<bool> {};


/* Traversing a Python iterator requires a customized C++ iterator type. */
template <typename T>
struct __iter__<Iterator<T, void, void>>                    : Returns<T> {
    using iterator_category = std::input_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = T;
    using pointer           = T*;
    using reference         = T&;

    Iterator<T> iter;
    T curr;

    __iter__(const Iterator<T>& self) :
        iter(self), curr(reinterpret_steal<T>(nullptr))
    {
        ++(*this);
    }

    __iter__(Iterator<T>&& self) :
        iter(self), curr(reinterpret_steal<T>(nullptr))
    {
        ++(*this);
    }

    /// NOTE: python iterators cannot be copied due to their stateful nature through
    /// the shared PyObject* pointer.

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

    __iter__& operator++(int) {
        return ++(*this);
    }

    friend bool operator==(const __iter__& self, py::impl::Sentinel) {
        return ptr(self.curr) == nullptr;
    }

    friend bool operator==(py::impl::Sentinel, const __iter__& self) {
        return ptr(self.curr) == nullptr;
    }

    friend bool operator!=(const __iter__& self, py::impl::Sentinel) {
        return ptr(self.curr) != nullptr;
    }

    friend bool operator!=(py::impl::Sentinel, const __iter__& self) {
        return ptr(self.curr) != nullptr;
    }

};
/// NOTE: can't iterate over a const Iterator<T> because the iterator itself must be
/// mutable.


/* py::Iterator<Begin, End, ...> is special cased in the begin() and end() operators to
extract the internal C++ iterators rather than creating yet another layer of
indirection. */
template <std::input_iterator Begin, std::sentinel_for<Begin> End, typename Container>
struct __iter__<Iterator<Begin, End, Container>> : Returns<decltype(*std::declval<Begin>())> {};


template <typename T, typename Begin, typename End, typename Container>
struct __contains__<T, Iterator<Begin, End, Container>> : Returns<bool> {};


/// TODO: these attributes can only be defined after functions are defined


// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Self, "__iter__"> : Returns<
//     Function<impl::qualify<Self(std::remove_cvref_t<Self>::*)(), Self>>
// > {};
// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Self, "__next__"> : Returns<
//     Function<impl::qualify<
//         std::conditional_t<
//             std::is_void_v<typename std::remove_reference_t<Self>::end_t>,
//             std::remove_reference_t<decltype(
//                 *std::declval<typename std::remove_reference_t<Self>::begin_t>()
//             )>,
//             decltype(
//                 *std::declval<typename std::remove_reference_t<Self>::begin_t>()
//             )
//         >(std::remove_cvref_t<Self>::*)(),
//         Self
//     >>
// > {};
// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Type<Self>, "__iter__"> : Returns<Function<
//     Self(*)(Self)
// >> {};
// template <impl::inherits<impl::IterTag> Self>
// struct __getattr__<Type<Self>, "__next__"> : Returns<Function<
//     std::conditional_t<
//         std::is_void_v<typename std::remove_reference_t<Self>::end_t>,
//         std::remove_reference_t<decltype(
//             *std::declval<typename std::remove_reference_t<Self>::begin_t>()
//         )>,
//         decltype(
//             *std::declval<typename std::remove_reference_t<Self>::begin_t>()
//         )
//     >(*)(Self)
// >> {};


/* Begin iteration operator.  Both this and the end iteration operator are
controlled by the __iter__ control struct, whose return type dictates the
iterator's dereference type. */
template <impl::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (impl::has_cpp<Self> && impl::iterable<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto begin(Self&& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, Self>) {
        return __iter__<Self>(std::forward<Self>(self));

    } else if constexpr (impl::has_cpp<Self>) {
        return std::ranges::begin(from_python(std::forward<Self>(self)));

    } else if constexpr (impl::inherits<Self, impl::IterTag>) {
        if constexpr (!std::is_void_v<typename std::remove_reference_t<Self>::end_t>) {
            return self->begin;
        } else {
            using T = __iter__<Self>::type;
            PyObject* iter = PyObject_GetIter(ptr(self));
            if (iter == nullptr) {
                Exception::from_python();
            }
            return __iter__<Iterator<T>>{reinterpret_steal<Iterator<T>>(iter)};
        }

    } else {
        using T = __iter__<Self>::type;
        PyObject* iter = PyObject_GetIter(ptr(self));
        if (iter == nullptr) {
            Exception::from_python();
        }
        return __iter__<Iterator<T>>{reinterpret_steal<Iterator<T>>(iter)};
    }
}


/* Const iteration operator.  Python has no distinction between mutable and
immutable iterators, so this is fundamentally the same as the ordinary begin()
method.  Some libraries assume the existence of this method. */
template <impl::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (impl::has_cpp<Self> && impl::iterable<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ) && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] auto cbegin(Self&& self) {
    return begin(std::forward<Self>(self));
}


/* End iteration operator.  This terminates the iteration and is controlled by the
__iter__ control struct. */
template <impl::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (impl::has_cpp<Self> && impl::iterable<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ))
[[nodiscard]] auto end(Self&& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, Self>) {
        return py::impl::Sentinel{};

    } else if constexpr (impl::has_cpp<Self>) {
        return std::ranges::end(from_python(std::forward<Self>(self)));

    } else if constexpr (impl::inherits<Self, impl::IterTag>) {
        if constexpr (!std::is_void_v<typename std::remove_reference_t<Self>::end_t>) {
            return self->end;
        } else {
            return py::impl::Sentinel{};
        }

    } else {
        return py::impl::Sentinel{};
    }
}


/* Const end operator.  Similar to `cbegin()`, this is identical to `end()`. */
template <impl::python Self>
    requires (__iter__<Self>::enable && (
        std::is_constructible_v<__iter__<Self>, Self> ||
        (impl::has_cpp<Self> && impl::iterable<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __iter__<Self>::type, Object>)
    ) && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] auto cend(Self&& self) {
    return end(std::forward<Self>(self));
}


/* Reverse iteration operator.  Both this and the reverse end operator are
controlled by the __reversed__ control struct, whose return type dictates the
iterator's dereference type. */
template <impl::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (impl::has_cpp<Self> && impl::reverse_iterable<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ))
[[nodiscard]] auto rbegin(Self&& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, Self>) {
        return __reversed__<Self>(std::forward<Self>(self));

    } else if constexpr (impl::has_cpp<Self>) {
        return std::ranges::rbegin(from_python(std::forward<Self>(self)));

    } else {
        using T = typename __reversed__<Self>::type;
        Object builtins = reinterpret_steal<Object>(
            PyFrame_GetBuiltins(PyEval_GetFrame())
        );
        constexpr StaticStr str = "reversed";
        Object name = reinterpret_steal<Object>(
            PyUnicode_FromStringAndSize(str, str.size())
        );
        if (name.is(nullptr)) {
            Exception::from_python();
        }
        Object func = reinterpret_steal<Object>(PyDict_GetItemWithError(
            ptr(builtins),
            ptr(name)
        ));
        if (func.is(nullptr)) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            throw KeyError(str);
        }
        PyObject* iter = PyObject_CallFunctionOneArg(
            ptr(func),
            ptr(self)
        );
        if (iter == nullptr) {
            Exception::from_python();
        }
        return __iter__<Iterator<T>>{reinterpret_steal<Iterator<T>>(iter)};
    }
}


/* Const reverse iteration operator.  Python has no distinction between mutable
and immutable iterators, so this is fundamentally the same as the ordinary
rbegin() method.  Some libraries assume the existence of this method. */
template <impl::python Self>
    requires (__reversed__<Self>::enable && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] auto crbegin(Self&& self) {
    return rbegin(std::forward<Self>(self));
}


/* Reverse end operator.  This terminates the reverse iteration and is controlled
by the __reversed__ control struct. */
template <impl::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (impl::has_cpp<Self> && impl::reverse_iterable<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ))
[[nodiscard]] auto rend(Self&& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, Self>) {
        return py::impl::Sentinel{};

    } else if constexpr (impl::has_cpp<Self>) {
        return std::ranges::rend(unwrap(std::forward<Self>(self)));

    } else {
        return py::impl::Sentinel{};
    }
}


/* Const reverse end operator.  Similar to `crbegin()`, this is identical to
`rend()`. */
template <impl::python Self>
    requires (__reversed__<Self>::enable && (
        std::is_constructible_v<__reversed__<Self>, Self> ||
        (impl::has_cpp<Self> && impl::reverse_iterable<impl::cpp_type<Self>>) ||
        (!impl::has_cpp<Self> && std::derived_from<typename __reversed__<Self>::type, Object>)
    ) && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] auto crend(Self&& self) {
    return rend(std::forward<Self>(self));
}


namespace impl {

    /* A range adaptor that concatenates a sequence of subranges into a single view.
    Every element in the input range must yield another range, which will be flattened
    into a single output range. */
    template <std::ranges::input_range View>
        requires (
            std::ranges::view<View> &&
            std::ranges::input_range<std::ranges::range_value_t<View>>
        )
    struct Comprehension : BertrandTag, std::ranges::view_base {
    private:
        using InnerView = std::ranges::range_value_t<View>;

        View m_view;

        struct Sentinel;

        struct Iterator {
        private:

            void skip_empty_views() {
                while (inner_begin == inner_end) {
                    if (++outer_begin == outer_end) {
                        break;
                    }
                    inner_begin = std::ranges::begin(*outer_begin);
                    inner_end = std::ranges::end(*outer_begin);
                }
            }

        public:
            using iterator_category = std::input_iterator_tag;
            using value_type = std::ranges::range_value_t<InnerView>;
            using difference_type = std::ranges::range_difference_t<InnerView>;
            using pointer = value_type*;
            using reference = value_type&;

            std::ranges::iterator_t<View> outer_begin;
            std::ranges::iterator_t<View> outer_end;
            std::ranges::iterator_t<InnerView> inner_begin;
            std::ranges::iterator_t<InnerView> inner_end;

            Iterator() = default;
            Iterator(
                std::ranges::iterator_t<View>&& outer_begin,
                std::ranges::iterator_t<View>&& outer_end
            ) : outer_begin(std::move(outer_begin)), outer_end(std::move(outer_end))
            {
                if (outer_begin != outer_end) {
                    inner_begin = std::ranges::begin(*outer_begin);
                    inner_end = std::ranges::end(*outer_begin);
                    skip_empty_views();
                }
            }

            Iterator& operator++() {
                if (++inner_begin == inner_end) {
                    if (++outer_begin != outer_end) {
                        inner_begin = std::ranges::begin(*outer_begin);
                        inner_end = std::ranges::end(*outer_begin);
                        skip_empty_views();
                    }
                }
                return *this;
            }

            decltype(auto) operator*() const {
                return *inner_begin;
            }

            bool operator==(const Sentinel&) const {
                return outer_begin == outer_end;
            }

            bool operator!=(const Sentinel&) const {
                return outer_begin != outer_end;
            }

        };

        struct Sentinel {
            bool operator==(const Iterator& iter) const {
                return iter.outer_begin == iter.outer_end;
            }
            bool operator!=(const Iterator& iter) const {
                return iter.outer_begin != iter.outer_end;
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

        Sentinel end() {
            return {};
        };

    };

    template <typename View>
    Comprehension(View&&) -> Comprehension<std::remove_cvref_t<View>>;

}


/* Apply a C++ range adaptor to a Python object.  This is similar to the C++-style `|`
operator for chaining range adaptors, but uses the `->*` operator to avoid conflicts
with Python and apply higher precedence than typical binary operators. */
template <impl::python Self, std::ranges::view View> requires (impl::iterable<Self>)
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
template <impl::python Self, typename Func>
    requires (
        impl::iterable<Self> &&
        !std::ranges::view<Func> &&
        std::is_invocable_v<Func, impl::iter_type<Self>>
    )
[[nodiscard]] auto operator->*(Self&& self, Func&& func) {
    using Return = std::invoke_result_t<Func, impl::iter_type<Self>>;
    if constexpr (std::ranges::view<Return>) {
        return impl::Comprehension(
            std::views::all(std::forward<Self>(self))) |
            std::views::transform(std::forward<Func>(func)
        );
    } else {
        return
            std::views::all(std::forward<Self>(self)) |
            std::views::transform(std::forward<Func>(func));
    }
}


}


#endif
