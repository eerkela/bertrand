#ifndef BERTRAND_CORE_ACCESS_H
#define BERTRAND_CORE_ACCESS_H

#include "declarations.h"
#include "object.h"
#include "except.h"


namespace py {


////////////////////////////////
////    ATTRIBUTES/ITEMS    ////
////////////////////////////////


template <typename Self, StaticStr Name>
    requires (
        __delattr__<Self, Name>::enable &&
        std::is_void_v<typename __delattr__<Self, Name>::type> && (
            std::is_invocable_r_v<void, __delattr__<Self, Name>, Self> ||
            !impl::has_call_operator<__delattr__<Self, Name>>
        )
    )
void del(impl::Attr<Self, Name>&& attr);


template <typename Self, typename... Key>
    requires (
        __delitem__<Self, Key...>::enable &&
        std::is_void_v<typename __delitem__<Self, Key...>::type> && (
            std::is_invocable_r_v<void, __delitem__<Self, Key...>, Self, Key...> ||
            !impl::has_call_operator<__delitem__<Self, Key...>>
        )
    )
void del(impl::Item<Self, Key...>&& item);


namespace impl {

    template <typename T>
    concept lazily_evaluated = is_attr<T> || is_item<T>;

    template <typename T>
    struct lazy_type_helper {};
    template <is_attr T>
    struct lazy_type_helper<T> { using type = attr_type<T>; };
    template <is_item T>
    struct lazy_type_helper<T> { using type = item_type<T>; };
    template <lazily_evaluated T>
    using lazy_type = lazy_type_helper<std::remove_cvref_t<T>>::type;

    /* A proxy for the result of an attribute lookup that is controlled by the
    `__getattr__`, `__setattr__`, and `__delattr__` control structs.

    This is a simple extension of an Object type that intercepts `operator=` and
    assigns the new value back to the attribute using the appropriate API.  Mutating
    the object in any other way will also modify it in-place on the parent. */
    template <typename Self, StaticStr Name>
        requires (
            __getattr__<Self, Name>::enable &&
            std::derived_from<typename __getattr__<Self, Name>::type, Object> && (
                !std::is_invocable_v<__getattr__<Self, Name>, Self> ||
                std::is_invocable_r_v<
                    typename __getattr__<Self, Name>::type,
                    __getattr__<Self, Name>,
                    Self
                >
            )
        )
    struct Attr : std::remove_cv_t<typename __getattr__<Self, Name>::type> {
    private:
        using Base = std::remove_cv_t<typename __getattr__<Self, Name>::type>;

        template <typename S, StaticStr N>
            requires (
                __delattr__<S, N>::enable &&
                std::is_void_v<typename __delattr__<S, N>::type> && (
                    std::is_invocable_r_v<void, __delattr__<S, N>, S> ||
                    !impl::has_call_operator<__delattr__<S, N>>
                )
            )
        friend void py::del(Attr<S, N>&& item);
        template <impl::inherits<Object> T>
        friend PyObject* ptr(T&&);
        template <impl::inherits<Object> T>
            requires (!std::is_const_v<std::remove_reference_t<T>>)
        friend PyObject* release(T&&);
        template <std::derived_from<Object> T>
        friend T reinterpret_borrow(PyObject*);
        template <std::derived_from<Object> T>
        friend T reinterpret_steal(PyObject*);
        template <impl::python T> requires (impl::has_cpp<T>)
        friend auto& impl::unwrap(T& obj);
        template <impl::python T> requires (impl::has_cpp<T>)
        friend const auto& impl::unwrap(const T& obj);

        /* m_self inherits the same const/volatile/reference qualifiers as the original
        object. */
        Self m_self;

        /* The wrapper's `m_ptr` member is lazily evaluated to avoid repeated lookups.
        Replacing it with a computed property will trigger a __getattr__ lookup the
        first time it is accessed. */
        __declspec(property(get = _get_ptr, put = _set_ptr)) PyObject* m_ptr;
        void _set_ptr(PyObject* value) { Base::m_ptr = value; }
        PyObject* _get_ptr() {
            if (Base::m_ptr == nullptr) {
                if constexpr (has_call_operator<__getattr__<Self, Name>>) {
                    Base::m_ptr = release(__getattr__<Self, Name>{}(
                        std::forward<Self>(m_self))
                    );
                } else {
                    PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
                    if (name == nullptr) {
                        Exception::from_python();
                    }
                    PyObject* result = PyObject_GetAttr(ptr(m_self), name);
                    Py_DECREF(name);
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    Base::m_ptr = result;
                }
            }
            return Base::m_ptr;
        }

    public:

        Attr(Self&& self) :
            Base(nullptr, Object::stolen_t{}), m_self(std::forward<Self>(self))
        {}
        Attr(const Attr& other) = delete;
        Attr(Attr&& other) = delete;

        template <typename Value> requires (!__setattr__<Self, Name, Value>::enable)
        Attr& operator=(Value&& value) = delete;
        template <typename Value>
            requires (
                __setattr__<Self, Name, Value>::enable &&
                std::is_void_v<typename __setattr__<Self, Name, Value>::type> && (
                    std::is_invocable_r_v<void, __setattr__<Self, Name, Value>, Self, Value> || (
                        !impl::has_call_operator<__setattr__<Self, Name, Value>> &&
                        impl::has_cpp<Base> &&
                        std::is_assignable_v<cpp_type<Base>&, Value>
                    ) || (
                        !impl::has_call_operator<__setattr__<Self, Name, Value>> &&
                        !impl::has_cpp<Base>
                    )
                )
            )
        Attr& operator=(Value&& value) && {
            if constexpr (has_call_operator<__setattr__<Self, Name, Value>>) {
                __setattr__<Self, Name, Value>{}(
                    std::forward<Self>(m_self),
                    std::forward<Value>(value)
                );

            } else if constexpr (has_cpp<Base>) {
                from_python(*this) = std::forward<Value>(value);

            } else {
                Base::operator=(std::forward<Value>(value));
                PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
                if (name == nullptr) {
                    Exception::from_python();
                }
                int rc = PyObject_SetAttr(ptr(m_self), name, ptr(*this));
                Py_DECREF(name);
                if (rc) {
                    Exception::from_python();
                }
            }
            return *this;
        }
    };

    /* A proxy for an item in a Python container that is controlled by the
    `__getitem__`, `__setitem__`, and `__delitem__` control structs.

    This is a simple extension of an Object type that intercepts `operator=` and
    assigns the new value back to the container using the appropriate API.  Mutating
    the object in any other way will also modify it in-place within the container. */
    template <typename Self, typename... Key>
        requires (
            __getitem__<Self, Key...>::enable &&
            std::convertible_to<typename __getitem__<Self, Key...>::type, Object> && (
                std::is_invocable_r_v<
                    typename __getitem__<Self, Key...>::type,
                    __getitem__<Self, Key...>,
                    Self,
                    Key...
                > || (
                    !has_call_operator<__getitem__<Self, Key...>> &&
                    has_cpp<Self> &&
                    lookup_yields<
                        cpp_type<Self>&,
                        typename __getitem__<Self, Key...>::type,
                        Key...
                    >
                ) || (
                    !has_call_operator<__getitem__<Self, Key...>> &&
                    !has_cpp<Self> &&
                    std::derived_from<typename __getitem__<Self, Key...>::type, Object>
                )
            )
        )
    struct Item : std::remove_cv_t<typename __getitem__<Self, Key...>::type> {
    private:
        using Base = std::remove_cv_t<typename __getitem__<Self, Key...>::type>;

        template <typename S, typename... K>
            requires (
                __delitem__<S, K...>::enable &&
                std::is_void_v<typename __delitem__<S, K...>::type> && (
                    std::is_invocable_r_v<void, __delitem__<S, K...>, S, K...> ||
                    !impl::has_call_operator<__delitem__<S, K...>>
                )
            )
        friend void py::del(Item<S, K...>&& item);
        template <impl::inherits<Object> T>
        friend PyObject* ptr(T&&);
        template <impl::inherits<Object> T> requires (!std::is_const_v<std::remove_reference_t<T>>)
        friend PyObject* release(T&&);
        template <std::derived_from<Object> T>
        friend T reinterpret_borrow(PyObject*);
        template <std::derived_from<Object> T>
        friend T reinterpret_steal(PyObject*);
        template <impl::python T> requires (impl::has_cpp<T>)
        friend auto& impl::unwrap(T& obj);
        template <impl::python T> requires (impl::has_cpp<T>)
        friend const auto& impl::unwrap(const T& obj);

        /* m_self inherits the same const/volatile/reference qualifiers as the original
        object, and the keys are stored directly as members, retaining their original
        value categories without any extra copies/moves. */
        Self m_self;
        Pack<Key...> m_key;

        /* The wrapper's `m_ptr` member is lazily evaluated to avoid repeated lookups.
        Replacing it with a computed property will trigger a __getitem__ lookup the
        first time it is accessed. */
        __declspec(property(get = _get_ptr, put = _set_ptr)) PyObject* m_ptr;
        void _set_ptr(PyObject* value) { Base::m_ptr = value; }
        PyObject* _get_ptr() {
            if (Base::m_ptr == nullptr) {
                Base::m_ptr = std::move(m_key)([&](Key... key) {
                    if constexpr (has_call_operator<__getitem__<Self, Key...>>) {
                        return release(__getitem__<Self, Key...>{}(
                            std::forward<Self>(m_self),
                            std::forward<Key>(key)...
                        ));

                    } else if constexpr (sizeof...(Key) == 1) {
                        PyObject* result = PyObject_GetItem(
                            ptr(m_self),
                            ptr(to_python(std::forward<Key>(key)))...
                        );
                        if (result == nullptr) {
                            Exception::from_python();
                        }
                        return result;

                    } else {
                        PyObject* tuple = PyTuple_Pack(
                            sizeof...(Key),
                            ptr(to_python(std::forward<Key>(key)))...
                        );
                        if (tuple == nullptr) {
                            Exception::from_python();
                        }
                        PyObject* result = PyObject_GetItem(ptr(m_self), tuple);
                        Py_DECREF(tuple);
                        if (result == nullptr) {
                            Exception::from_python();
                        }
                        return result;
                    }
                });
            }
            return Base::m_ptr;
        }

    public:

        Item(Self&& self, Key&&... key) :
            Base(nullptr, Object::stolen_t{}),
            m_self(std::forward<Self>(self)),
            m_key(std::forward<Key>(key)...)
        {}
        Item(const Item& other) = delete;
        Item(Item&& other) = delete;

        template <typename Value> requires (!__setitem__<Self, Value, Key...>::enable)
        Item& operator=(Value&& other) = delete;
        template <typename Value>
            requires (
                __setitem__<Self, Value, Key...>::enable &&
                std::is_void_v<typename __setitem__<Self, Value, Key...>::type> && (
                    std::is_invocable_r_v<void, __setitem__<Self, Value, Key...>, Self, Value, Key...> || (
                        !impl::has_call_operator<__setitem__<Self, Value, Key...>> &&
                        impl::has_cpp<Base> &&
                        supports_item_assignment<cpp_type<Self>&, Value, Key...>
                    ) || (
                        !impl::has_call_operator<__setitem__<Self, Value, Key...>> &&
                        !impl::has_cpp<Base>
                    )
                )
            )
        Item& operator=(Value&& value) && {
            std::move(m_key)([&](Key... key) {
                if constexpr (has_call_operator<__setitem__<Self, Value, Key...>>) {
                    __setitem__<Self, Value, Key...>{}(
                        std::forward<Self>(m_self),
                        std::forward<Value>(value),
                        std::forward<Key>(key)...
                    );

                } else if constexpr (has_cpp<Base>) {
                    from_python(std::forward<Self>(m_self))[std::forward<Key>(key)...] =
                        std::forward<Value>(value);

                } else if constexpr (sizeof...(Key) == 1) {
                    Base::operator=(std::forward<Value>(value));
                    if (PyObject_SetItem(
                        ptr(m_self),
                        ptr(to_python(key))...,
                        ptr(*this)
                    )) {
                        Exception::from_python();
                    }

                } else {
                    Base::operator=(std::forward<Value>(value));
                    PyObject* tuple = PyTuple_Pack(
                        sizeof...(Key),
                        ptr(to_python(key))...
                    );
                    if (tuple == nullptr) {
                        Exception::from_python();
                    }
                    int rc = PyObject_SetItem(
                        ptr(m_self),
                        tuple,
                        ptr(*this)
                    );
                    Py_DECREF(tuple);
                    if (rc) {
                        Exception::from_python();
                    }
                }
            });
            return *this;
        }
    };

}


template <typename Self, typename... Key>
    requires (
        __getitem__<Self, Key...>::enable &&
        std::convertible_to<typename __getitem__<Self, Key...>::type, Object> && (
            std::is_invocable_r_v<
                typename __getitem__<Self, Key...>::type,
                __getitem__<Self, Key...>,
                Self,
                Key...
            > || (
                !impl::has_call_operator<__getitem__<Self, Key...>> &&
                impl::has_cpp<Self> &&
                impl::lookup_yields<
                    impl::cpp_type<Self>&,
                    typename __getitem__<Self, Key...>::type,
                    Key...
                >
            ) || (
                !impl::has_call_operator<__getitem__<Self, Key...>> &&
                !impl::has_cpp<Self> &&
                std::derived_from<typename __getitem__<Self, Key...>::type, Object>
            )
        )
    )
decltype(auto) Object::operator[](this Self&& self, Key&&... key) {
    if constexpr (impl::has_cpp<Self> && impl::has_call_operator<__getitem__<Self, Key...>>) {
        return __getitem__<Self, Key...>{}(
            std::forward<Self>(self),
            std::forward<Key>(key)...
        );

    } else {
        return impl::Item<Self, Key...>(
            std::forward<Self>(self),
            std::forward<Key>(key)...
        );
    }
}


/* Replicates Python's `del` keyword for attribute and item deletion.  Note that the
usage of `del` to dereference naked Python objects is not supported - only those uses
which would translate to a `PyObject_DelAttr()` or `PyObject_DelItem()` are considered
valid. */
template <typename Self, StaticStr Name>
    requires (
        __delattr__<Self, Name>::enable &&
        std::is_void_v<typename __delattr__<Self, Name>::type> && (
            std::is_invocable_r_v<void, __delattr__<Self, Name>, Self> ||
            !impl::has_call_operator<__delattr__<Self, Name>>
        )
    )
void del(impl::Attr<Self, Name>&& attr) {
    if constexpr (impl::has_call_operator<__delattr__<Self, Name>>) {
        __delattr__<Self, Name>{}(std::forward<Self>(attr.m_self));

    } else {
        PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
        if (name == nullptr) {
            Exception::from_python();
        }
        int rc = PyObject_DelAttr(ptr(attr.m_self), name);
        Py_DECREF(name);
        if (rc) {
            Exception::from_python();
        }
    }
}


/* Replicates Python's `del` keyword for attribute and item deletion.  Note that the
usage of `del` to dereference naked Python objects is not supported - only those uses
which would translate to a `PyObject_DelAttr()` or `PyObject_DelItem()` are considered
valid. */
template <typename Self, typename... Key>
    requires (
        __delitem__<Self, Key...>::enable &&
        std::is_void_v<typename __delitem__<Self, Key...>::type> && (
            std::is_invocable_r_v<void, __delitem__<Self, Key...>, Self, Key...> ||
            !impl::has_call_operator<__delitem__<Self, Key...>>
        )
    )
void del(impl::Item<Self, Key...>&& item) {
    std::move(item.m_key)([&](Key... key) {
        if constexpr (impl::has_call_operator<__delitem__<Self, Key...>>) {
            __delitem__<Self, Key...>{}(
                std::forward<Self>(item.m_self),
                std::forward<Key>(key)...
            );

        } else if constexpr (sizeof...(Key) == 1) {
            if (PyObject_DelItem(
                ptr(item.m_self),
                ptr(to_python(key))...)
            ) {
                Exception::from_python();
            }

        } else {
            PyObject* tuple = PyTuple_Pack(
                sizeof...(Key),
                ptr(to_python(key))...
            );
            if (tuple == nullptr) {
                Exception::from_python();
            }
            int rc = PyObject_DelItem(ptr(item.m_self), tuple);
            Py_DECREF(tuple);
            if (rc) {
                Exception::from_python();
            }
        }
    });
}


/// TODO: I think these control structures are duplicating work


/* Explicitly convert a lazily-evaluated attribute or item wrapper into a normalized
Object instance. */
template <impl::inherits<Object> Self, impl::lazily_evaluated T>
    requires (std::constructible_from<Self, impl::lazy_type<T>>)
struct __init__<Self, T>                           : Returns<Self> {
    static auto operator()(T&& value) {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return Self(reinterpret_borrow<impl::lazy_type<T>>(ptr(value)));
        } else {
            return Self(reinterpret_steal<impl::lazy_type<T>>(release(value)));
        }
    }
};


/* Implicitly convert a lazily-evaluated attribute or item wrapper into a normalized
Object instance. */
template <impl::lazily_evaluated T, impl::inherits<Object> Self>
    requires (std::convertible_to<impl::lazy_type<T>, Self>)
struct __cast__<T, Self>                                    : Returns<Self> {
    static Self operator()(T&& value) {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return reinterpret_borrow<impl::lazy_type<T>>(ptr(value));
        } else {
            return reinterpret_steal<impl::lazy_type<T>>(release(value));
        }
    }
};


template <impl::lazily_evaluated From, typename To>
    requires (std::convertible_to<impl::lazy_type<From>, To>)
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(From&& item) {
        if constexpr (impl::has_cpp<impl::lazy_type<From>>) {
            return impl::implicit_cast<To>(from_python(std::forward<From>(item)));
        } else {
            return impl::implicit_cast<To>(
                reinterpret_steal<impl::lazy_type<From>>(ptr(item))
            );
        }
    }
};


template <impl::lazily_evaluated From, typename To>
    requires (impl::explicitly_convertible_to<impl::lazy_type<From>, To>)
struct __explicit_cast__<From, To>                          : Returns<To> {
    static To operator()(const From& item) {
        if constexpr (impl::has_cpp<impl::lazy_type<From>>) {
            return static_cast<To>(from_python(item));
        } else {
            return static_cast<To>(
                reinterpret_steal<impl::lazy_type<From>>(ptr(item))
            );
        }
    }
};


template <impl::lazily_evaluated Derived, impl::lazily_evaluated Base>
struct __isinstance__<Derived, Base> : __isinstance__<impl::lazy_type<Derived>, impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Derived, typename Base> requires (!impl::lazily_evaluated<Base>)
struct __isinstance__<Derived, Base> : __isinstance__<impl::lazy_type<Derived>, Base> {};
template <typename Derived, impl::lazily_evaluated Base> requires (!impl::lazily_evaluated<Derived>)
struct __isinstance__<Derived, Base> : __isinstance__<Derived, impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Derived, impl::lazily_evaluated Base>
struct __issubclass__<Derived, Base> : __issubclass__<impl::lazy_type<Derived>, impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Derived, typename Base> requires (!impl::lazily_evaluated<Base>)
struct __issubclass__<Derived, Base> : __issubclass__<impl::lazy_type<Derived>, Base> {};
template <typename Derived, impl::lazily_evaluated Base> requires (!impl::lazily_evaluated<Derived>)
struct __issubclass__<Derived, Base> : __issubclass__<Derived, impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base, StaticStr Name>
struct __getattr__<Base, Name> : __getattr__<impl::lazy_type<Base>, Name> {};
template <impl::lazily_evaluated Base, StaticStr Name, typename Value>
struct __setattr__<Base, Name, Value> : __setattr__<impl::lazy_type<Base>, Name, Value> {};
template <impl::lazily_evaluated Base, StaticStr Name>
struct __delattr__<Base, Name> : __delattr__<impl::lazy_type<Base>, Name> {};
template <impl::lazily_evaluated Base, typename... Key>
struct __getitem__<Base, Key...> : __getitem__<impl::lazy_type<Base>, Key...> {};
template <impl::lazily_evaluated Base, typename Value, typename... Key>
struct __setitem__<Base, Value, Key...> : __setitem__<impl::lazy_type<Base>, Value, Key...> {};
template <impl::lazily_evaluated Base, typename... Key>
struct __delitem__<Base, Key...> : __delitem__<impl::lazy_type<Base>, Key...> {};
template <impl::lazily_evaluated Base>
struct __len__<Base> : __len__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __iter__<Base> : __iter__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __reversed__<Base> : __reversed__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base, typename Key>
struct __contains__<Base, Key> : __contains__<impl::lazy_type<Base>, Key> {};
template <impl::lazily_evaluated Base>
struct __hash__<Base> : __hash__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __abs__<Base> : __abs__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __invert__<Base> : __invert__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __pos__<Base> : __pos__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __neg__<Base> : __neg__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __increment__<Base> : __increment__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated Base>
struct __decrement__<Base> : __decrement__<impl::lazy_type<Base>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __lt__<L, R> : __lt__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __lt__<L, R> : __lt__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __lt__<L, R> : __lt__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __le__<L, R> : __le__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __le__<L, R> : __le__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __le__<L, R> : __le__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __eq__<L, R> : __eq__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __eq__<L, R> : __eq__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __eq__<L, R> : __eq__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ne__<L, R> : __ne__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ne__<L, R> : __ne__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ne__<L, R> : __ne__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ge__<L, R> : __ge__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ge__<L, R> : __ge__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ge__<L, R> : __ge__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __gt__<L, R> : __gt__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __gt__<L, R> : __gt__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __gt__<L, R> : __gt__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __add__<L, R> : __add__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __add__<L, R> : __add__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __add__<L, R> : __add__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __sub__<L, R> : __sub__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __sub__<L, R> : __sub__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __sub__<L, R> : __sub__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __mul__<L, R> : __mul__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __mul__<L, R> : __mul__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __mul__<L, R> : __mul__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __truediv__<L, R> : __truediv__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __truediv__<L, R> : __truediv__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __truediv__<L, R> : __truediv__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __floordiv__<L, R> : __floordiv__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __floordiv__<L, R> : __floordiv__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __floordiv__<L, R> : __floordiv__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __mod__<L, R> : __mod__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __mod__<L, R> : __mod__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __mod__<L, R> : __mod__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __pow__<L, R> : __pow__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __pow__<L, R> : __pow__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __pow__<L, R> : __pow__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __lshift__<L, R> : __lshift__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __lshift__<L, R> : __lshift__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __lshift__<L, R> : __lshift__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __rshift__<L, R> : __rshift__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __rshift__<L, R> : __rshift__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __rshift__<L, R> : __rshift__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __and__<L, R> : __and__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __and__<L, R> : __and__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __and__<L, R> : __and__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __xor__<L, R> : __xor__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __xor__<L, R> : __xor__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __xor__<L, R> : __xor__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __or__<L, R> : __or__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __or__<L, R> : __or__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __or__<L, R> : __or__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __iadd__<L, R> : __iadd__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __iadd__<L, R> : __iadd__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __iadd__<L, R> : __iadd__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __isub__<L, R> : __isub__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __isub__<L, R> : __isub__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __isub__<L, R> : __isub__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __imul__<L, R> : __imul__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __imul__<L, R> : __imul__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __imul__<L, R> : __imul__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __itruediv__<L, R> : __itruediv__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __itruediv__<L, R> : __itruediv__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __itruediv__<L, R> : __itruediv__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ifloordiv__<L, R> : __ifloordiv__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ifloordiv__<L, R> : __ifloordiv__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ifloordiv__<L, R> : __ifloordiv__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __imod__<L, R> : __imod__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __imod__<L, R> : __imod__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __imod__<L, R> : __imod__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ipow__<L, R> : __ipow__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ipow__<L, R> : __ipow__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ipow__<L, R> : __ipow__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ilshift__<L, R> : __ilshift__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ilshift__<L, R> : __ilshift__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ilshift__<L, R> : __ilshift__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __irshift__<L, R> : __irshift__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __irshift__<L, R> : __irshift__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __irshift__<L, R> : __irshift__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __iand__<L, R> : __iand__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __iand__<L, R> : __iand__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __iand__<L, R> : __iand__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ixor__<L, R> : __ixor__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ixor__<L, R> : __ixor__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ixor__<L, R> : __ixor__<L, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, impl::lazily_evaluated R>
struct __ior__<L, R> : __ior__<impl::lazy_type<L>, impl::lazy_type<R>> {};
template <impl::lazily_evaluated L, typename R> requires (!impl::lazily_evaluated<R>)
struct __ior__<L, R> : __ior__<impl::lazy_type<L>, R> {};
template <typename L, impl::lazily_evaluated R> requires (!impl::lazily_evaluated<L>)
struct __ior__<L, R> : __ior__<L, impl::lazy_type<R>> {};


/////////////////////
////    SLICE    ////
/////////////////////


struct Slice;


template <>
struct Interface<Slice> {

    /* Get the start object of the slice.  Note that this might not be an integer. */
    __declspec(property(get = _get_start)) Object start;
    [[nodiscard]] Object _get_start(this auto&& self) {
        return self->start ?
            reinterpret_borrow<Object>(self->start) :
            reinterpret_borrow<Object>(Py_None);
    }

    /* Get the stop object of the slice.  Note that this might not be an integer. */
    __declspec(property(get = _get_stop)) Object stop;
    [[nodiscard]] Object _get_stop(this auto&& self) {
        return self->stop ?
            reinterpret_borrow<Object>(self->stop) :
            reinterpret_borrow<Object>(Py_None);
    }

    /* Get the step object of the slice.  Note that this might not be an integer. */
    __declspec(property(get = _get_step)) Object step;
    [[nodiscard]] Object _get_step(this auto&& self) {
        return self->step ?
            reinterpret_borrow<Object>(self->step) :
            reinterpret_borrow<Object>(Py_None);
    }

    /* Normalize the indices of this slice against a container of the given length.
    This accounts for negative indices and clips those that are out of bounds.
    Returns a simple data struct with the following fields:

        * (Py_ssize_t) start: the normalized start index
        * (Py_ssize_t) stop: the normalized stop index
        * (Py_ssize_t) step: the normalized step size
        * (Py_ssize_t) length: the number of indices that are included in the slice

    It can be destructured using C++17 structured bindings:

        auto [start, stop, step, length] = slice.indices(size);
    */
    [[nodiscard]] auto indices(this auto&& self, size_t size) {
        struct Indices {
            Py_ssize_t start = 0;
            Py_ssize_t stop = 0;
            Py_ssize_t step = 0;
            Py_ssize_t length = 0;
        } result;
        if (PySlice_GetIndicesEx(
            ptr(self),
            size,
            &result.start,
            &result.stop,
            &result.step,
            &result.length
        )) {
            Exception::from_python();
        }
        return result;
    }

};
template <>
struct Interface<Type<Slice>> {
    template <impl::inherits<Interface<Slice>> Self>
    [[nodiscard]] static Object start(Self&& self) { return self.start; }
    template <impl::inherits<Interface<Slice>> Self>
    [[nodiscard]] static Object stop(Self&& self) { return self.stop; }
    template <impl::inherits<Interface<Slice>> Self>
    [[nodiscard]] static Object step(Self&& self) { return self.step; }
    template <impl::inherits<Interface<Slice>> Self>
    [[nodiscard]] static auto indices(Self&& self, size_t size) { return self.indices(size); }
};


/* Represents a statically-typed Python `slice` object in C++.  Note that the start,
stop, and step values do not strictly need to be integers. */
struct Slice : Object, Interface<Slice> {
    struct __python__ : def<__python__, Slice>, PySliceObject {
        static Type<Slice> __import__();
    };

    Slice(PyObject* p, borrowed_t t) : Object(p, t) {}
    Slice(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = Slice> requires (__initializer__<Self>::enable)
    Slice(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args> requires (implicit_ctor<Slice>::enable<Args...>)
    Slice(Args&&... args) : Object(
        implicit_ctor<Slice>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Slice>::enable<Args...>)
    explicit Slice(Args&&... args) : Object(
        explicit_ctor<Slice>{},
        std::forward<Args>(args)...
    ) {}

};


/// TODO: this must be declared in core.h
// template <std::derived_from<Slice> Self>
// struct __getattr__<Self, "indices"> : Returns<Function<
//     Tuple<Int>(Arg<"length", const Int&>)
// >> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "start">                           : Returns<Object> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "stop">                            : Returns<Object> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "step">                            : Returns<Object> {};


template <impl::is<Object> Derived, impl::is<Slice> Base>
struct __isinstance__<Derived, Base>                        : Returns<bool> {
    static constexpr bool operator()(Derived obj) {
        return PySlice_Check(ptr(obj));
    }
};


template <>
struct __init__<Slice>                                      : Returns<Slice> {
    static auto operator()() {
        PyObject* result = PySlice_New(nullptr, nullptr, nullptr);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


template <
    std::convertible_to<Object> Start,
    std::convertible_to<Object> Stop,
    std::convertible_to<Object> Step
>
struct __init__<Slice, Start, Stop, Step>                   : Returns<Slice> {
    static auto operator()(const Object& start, const Object& stop, const Object& step) {
        PyObject* result = PySlice_New(
            ptr(start),
            ptr(stop),
            ptr(step)
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


template <
    std::convertible_to<Object> Start,
    std::convertible_to<Object> Stop
>
struct __init__<Slice, Start, Stop>                         : Returns<Slice> {
    static auto operator()(const Object& start, const Object& stop) {
        PyObject* result = PySlice_New(
            ptr(start),
            ptr(stop),
            nullptr
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


template <std::convertible_to<Object> Stop>
struct __init__<Slice, Stop>                                : Returns<Slice> {
    static auto operator()(const Object& stop) {
        PyObject* result = PySlice_New(
            nullptr,
            ptr(stop),
            nullptr
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Slice>(result);
    }
};


template <impl::is<Slice> L, impl::is<Slice> R>
struct __lt__<L, R>                                         : Returns<Bool> {};
template <impl::is<Slice> L, impl::is<Slice> R>
struct __le__<L, R>                                         : Returns<Bool> {};
template <impl::is<Slice> L, impl::is<Slice> R>
struct __eq__<L, R>                                         : Returns<Bool> {};
template <impl::is<Slice> L, impl::is<Slice> R>
struct __ne__<L, R>                                         : Returns<Bool> {};
template <impl::is<Slice> L, impl::is<Slice> R>
struct __ge__<L, R>                                         : Returns<Bool> {};
template <impl::is<Slice> L, impl::is<Slice> R>
struct __gt__<L, R>                                         : Returns<Bool> {};


}


#endif
