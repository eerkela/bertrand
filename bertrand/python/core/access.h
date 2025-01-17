#ifndef BERTRAND_CORE_ACCESS_H
#define BERTRAND_CORE_ACCESS_H

#include "declarations.h"
#include "object.h"
#include "except.h"


namespace bertrand {


////////////////////////////////
////    ATTRIBUTES/ITEMS    ////
////////////////////////////////


template <typename Self, static_str Name>
    requires (
        __delattr__<Self, Name>::enable &&
        std::is_void_v<typename __delattr__<Self, Name>::type> && (
            std::is_invocable_r_v<void, __delattr__<Self, Name>, Self> ||
            !std::is_invocable_v<__delattr__<Self, Name>, Self>
        )
    )
void del(impl::Attr<Self, Name>&& attr);


template <typename Self, typename... Key>
    requires (
        __delitem__<Self, Key...>::enable &&
        std::is_void_v<typename __delitem__<Self, Key...>::type> && (
            std::is_invocable_r_v<void, __delitem__<Self, Key...>, Self, Key...> ||
            !std::is_invocable_v<__delitem__<Self, Key...>, Self, Key...>
        )
    )
void del(impl::Item<Self, Key...>&& item);


namespace impl {

    /* A proxy for the result of an attribute lookup that is controlled by the
    `__getattr__`, `__setattr__`, and `__delattr__` control structs.

    This is a simple extension of an Object type that intercepts `operator=` and
    assigns the new value back to the attribute using the appropriate API.  Mutating
    the object in any other way will also modify it in-place on the parent. */
    template <typename Self, static_str Name>
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

        template <typename S, static_str N>
            requires (
                __delattr__<S, N>::enable &&
                std::is_void_v<typename __delattr__<S, N>::type> && (
                    std::is_invocable_r_v<void, __delattr__<S, N>, S> ||
                    !std::is_invocable_v<__delattr__<S, N>, S>
                )
            )
        friend void bertrand::del(Attr<S, N>&& item);
        template <meta::python T>
        friend PyObject* bertrand::ptr(T&&);
        template <meta::python T> requires (!meta::is_const<T>)
        friend PyObject* bertrand::release(T&&);
        template <meta::python T> requires (!meta::is_qualified<T>)
        friend T bertrand::reinterpret_borrow(PyObject*);
        template <meta::python T> requires (!meta::is_qualified<T>)
        friend T bertrand::reinterpret_steal(PyObject*);
        template <meta::python T> requires (meta::has_cpp<T>)
        friend auto& impl::unwrap(T& obj);
        template <meta::python T> requires (meta::has_cpp<T>)
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
                if constexpr (std::is_invocable_v<__getattr__<Self, Name>, Self>) {
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
                        !std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value> &&
                        meta::has_cpp<Base> &&
                        std::is_assignable_v<meta::cpp_type<Base>&, Value>
                    ) || (
                        !std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value> &&
                        !meta::has_cpp<Base>
                    )
                )
            )
        Attr& operator=(Value&& value) && {
            if constexpr (std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value>) {
                __setattr__<Self, Name, Value>{}(
                    std::forward<Self>(m_self),
                    std::forward<Value>(value)
                );

            } else if constexpr (meta::has_cpp<Base>) {
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
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    meta::has_cpp<Self> &&
                    meta::lookup_yields<
                        meta::cpp_type<Self>&,
                        typename __getitem__<Self, Key...>::type,
                        Key...
                    >
                ) || (
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    !meta::has_cpp<Self> &&
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
                    !std::is_invocable_v<__delitem__<S, K...>, S, K...>
                )
            )
        friend void bertrand::del(Item<S, K...>&& item);
        template <meta::python T>
        friend PyObject* bertrand::ptr(T&&);
        template <meta::python T> requires (!meta::is_const<T>)
        friend PyObject* bertrand::release(T&&);
        template <meta::python T> requires (!meta::is_qualified<T>)
        friend T bertrand::reinterpret_borrow(PyObject*);
        template <meta::python T> requires (!meta::is_qualified<T>)
        friend T bertrand::reinterpret_steal(PyObject*);
        template <meta::python T> requires (meta::has_cpp<T>)
        friend auto& impl::unwrap(T& obj);
        template <meta::python T> requires (meta::has_cpp<T>)
        friend const auto& impl::unwrap(const T& obj);

        /* m_self inherits the same const/volatile/reference qualifiers as the original
        object, and the keys are stored directly as members, retaining their original
        value categories without any extra copies/moves. */
        Self m_self;
        args<Key...> m_key;

        /* The wrapper's `m_ptr` member is lazily evaluated to avoid repeated lookups.
        Replacing it with a computed property will trigger a __getitem__ lookup the
        first time it is accessed. */
        __declspec(property(get = _get_ptr, put = _set_ptr)) PyObject* m_ptr;
        void _set_ptr(PyObject* value) { Base::m_ptr = value; }
        PyObject* _get_ptr() {
            if (Base::m_ptr == nullptr) {
                Base::m_ptr = std::move(m_key)([&](Key... key) {
                    if constexpr (std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...>) {
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
                        !std::is_invocable_v<__setitem__<Self, Value, Key...>, Self, Value, Key...> &&
                        meta::has_cpp<Base> &&
                        meta::supports_item_assignment<meta::cpp_type<Self>&, Value, Key...>
                    ) || (
                        !std::is_invocable_v<__setitem__<Self, Value, Key...>, Self, Value, Key...> &&
                        !meta::has_cpp<Base>
                    )
                )
            )
        Item& operator=(Value&& value) && {
            std::move(m_key)([&](Key... key) {
                if constexpr (std::is_invocable_v<__setitem__<Self, Value, Key...>, Self, Value, Key...>) {
                    __setitem__<Self, Value, Key...>{}(
                        std::forward<Self>(m_self),
                        std::forward<Value>(value),
                        std::forward<Key>(key)...
                    );

                } else if constexpr (meta::has_cpp<Base>) {
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
                !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                meta::has_cpp<Self> &&
                meta::lookup_yields<
                    meta::cpp_type<Self>&,
                    typename __getitem__<Self, Key...>::type,
                    Key...
                >
            ) || (
                !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                !meta::has_cpp<Self> &&
                std::derived_from<typename __getitem__<Self, Key...>::type, Object>
            )
        )
    )
decltype(auto) Object::operator[](this Self&& self, Key&&... key) {
    if constexpr (
        meta::has_cpp<Self> &&
        std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...>
    ) {
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
template <typename Self, static_str Name>
    requires (
        __delattr__<Self, Name>::enable &&
        std::is_void_v<typename __delattr__<Self, Name>::type> && (
            std::is_invocable_r_v<void, __delattr__<Self, Name>, Self> ||
            !std::is_invocable_v<__delattr__<Self, Name>, Self>
        )
    )
void del(impl::Attr<Self, Name>&& attr) {
    if constexpr (std::is_invocable_v<__delattr__<Self, Name>, Self>) {
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
            !std::is_invocable_v<__delitem__<Self, Key...>, Self, Key...>
        )
    )
void del(impl::Item<Self, Key...>&& item) {
    std::move(item.m_key)([&](Key... key) {
        if constexpr (std::is_invocable_v<__delitem__<Self, Key...>, Self, Key...>) {
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


/// TODO: perhaps all of these could be avoided by exploiting inheritance for the
/// root control structs, which would help avoid ambiguities.


template <meta::lazily_evaluated Derived, meta::lazily_evaluated Base>
struct __isinstance__<Derived, Base> : __isinstance__<meta::lazy_type<Derived>, meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Derived, typename Base>
struct __isinstance__<Derived, Base> : __isinstance__<meta::lazy_type<Derived>, Base> {};
template <typename Derived, meta::lazily_evaluated Base>
struct __isinstance__<Derived, Base> : __isinstance__<Derived, meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Derived, meta::lazily_evaluated Base>
struct __issubclass__<Derived, Base> : __issubclass__<meta::lazy_type<Derived>, meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Derived, typename Base>
struct __issubclass__<Derived, Base> : __issubclass__<meta::lazy_type<Derived>, Base> {};
template <typename Derived, meta::lazily_evaluated Base>
struct __issubclass__<Derived, Base> : __issubclass__<Derived, meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base, static_str Name>
struct __getattr__<Base, Name> : __getattr__<meta::lazy_type<Base>, Name> {};
template <meta::lazily_evaluated Base, static_str Name, typename Value>
struct __setattr__<Base, Name, Value> : __setattr__<meta::lazy_type<Base>, Name, Value> {};
template <meta::lazily_evaluated Base, static_str Name>
struct __delattr__<Base, Name> : __delattr__<meta::lazy_type<Base>, Name> {};
template <meta::lazily_evaluated Base, typename... Key>
struct __getitem__<Base, Key...> : __getitem__<meta::lazy_type<Base>, Key...> {};
template <meta::lazily_evaluated Base, typename Value, typename... Key>
struct __setitem__<Base, Value, Key...> : __setitem__<meta::lazy_type<Base>, Value, Key...> {};
template <meta::lazily_evaluated Base, typename... Key>
struct __delitem__<Base, Key...> : __delitem__<meta::lazy_type<Base>, Key...> {};
template <meta::lazily_evaluated Base>
struct __len__<Base> : __len__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __iter__<Base> : __iter__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __reversed__<Base> : __reversed__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base, typename Key>
struct __contains__<Base, Key> : __contains__<meta::lazy_type<Base>, Key> {};
template <meta::lazily_evaluated Base>
struct __hash__<Base> : __hash__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __abs__<Base> : __abs__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __invert__<Base> : __invert__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __pos__<Base> : __pos__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __neg__<Base> : __neg__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __increment__<Base> : __increment__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated Base>
struct __decrement__<Base> : __decrement__<meta::lazy_type<Base>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __lt__<L, R> : __lt__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __lt__<L, R> : __lt__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __lt__<L, R> : __lt__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __le__<L, R> : __le__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __le__<L, R> : __le__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __le__<L, R> : __le__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __eq__<L, R> : __eq__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __eq__<L, R> : __eq__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __eq__<L, R> : __eq__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ne__<L, R> : __ne__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ne__<L, R> : __ne__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ne__<L, R> : __ne__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ge__<L, R> : __ge__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ge__<L, R> : __ge__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ge__<L, R> : __ge__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __gt__<L, R> : __gt__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __gt__<L, R> : __gt__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __gt__<L, R> : __gt__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __add__<L, R> : __add__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __add__<L, R> : __add__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __add__<L, R> : __add__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __sub__<L, R> : __sub__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __sub__<L, R> : __sub__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __sub__<L, R> : __sub__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __mul__<L, R> : __mul__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __mul__<L, R> : __mul__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __mul__<L, R> : __mul__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __truediv__<L, R> : __truediv__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __truediv__<L, R> : __truediv__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __truediv__<L, R> : __truediv__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __floordiv__<L, R> : __floordiv__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __floordiv__<L, R> : __floordiv__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __floordiv__<L, R> : __floordiv__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __mod__<L, R> : __mod__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __mod__<L, R> : __mod__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __mod__<L, R> : __mod__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __pow__<L, R> : __pow__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __pow__<L, R> : __pow__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __pow__<L, R> : __pow__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __lshift__<L, R> : __lshift__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __lshift__<L, R> : __lshift__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __lshift__<L, R> : __lshift__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __rshift__<L, R> : __rshift__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __rshift__<L, R> : __rshift__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __rshift__<L, R> : __rshift__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __and__<L, R> : __and__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __and__<L, R> : __and__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __and__<L, R> : __and__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __xor__<L, R> : __xor__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __xor__<L, R> : __xor__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __xor__<L, R> : __xor__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __or__<L, R> : __or__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __or__<L, R> : __or__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __or__<L, R> : __or__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __iadd__<L, R> : __iadd__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __iadd__<L, R> : __iadd__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __iadd__<L, R> : __iadd__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __isub__<L, R> : __isub__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __isub__<L, R> : __isub__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __isub__<L, R> : __isub__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __imul__<L, R> : __imul__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __imul__<L, R> : __imul__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __imul__<L, R> : __imul__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __itruediv__<L, R> : __itruediv__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __itruediv__<L, R> : __itruediv__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __itruediv__<L, R> : __itruediv__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ifloordiv__<L, R> : __ifloordiv__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ifloordiv__<L, R> : __ifloordiv__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ifloordiv__<L, R> : __ifloordiv__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __imod__<L, R> : __imod__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __imod__<L, R> : __imod__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __imod__<L, R> : __imod__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ipow__<L, R> : __ipow__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ipow__<L, R> : __ipow__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ipow__<L, R> : __ipow__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ilshift__<L, R> : __ilshift__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ilshift__<L, R> : __ilshift__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ilshift__<L, R> : __ilshift__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __irshift__<L, R> : __irshift__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __irshift__<L, R> : __irshift__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __irshift__<L, R> : __irshift__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __iand__<L, R> : __iand__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __iand__<L, R> : __iand__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __iand__<L, R> : __iand__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ixor__<L, R> : __ixor__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ixor__<L, R> : __ixor__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ixor__<L, R> : __ixor__<L, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, meta::lazily_evaluated R>
struct __ior__<L, R> : __ior__<meta::lazy_type<L>, meta::lazy_type<R>> {};
template <meta::lazily_evaluated L, typename R>
struct __ior__<L, R> : __ior__<meta::lazy_type<L>, R> {};
template <typename L, meta::lazily_evaluated R>
struct __ior__<L, R> : __ior__<L, meta::lazy_type<R>> {};


/////////////////////
////    SLICE    ////
/////////////////////


struct Slice;


template <>
struct interface<Slice> {

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
struct interface<Type<Slice>> {
    template <meta::inherits<interface<Slice>> Self>
    [[nodiscard]] static Object start(Self&& self) { return self.start; }
    template <meta::inherits<interface<Slice>> Self>
    [[nodiscard]] static Object stop(Self&& self) { return self.stop; }
    template <meta::inherits<interface<Slice>> Self>
    [[nodiscard]] static Object step(Self&& self) { return self.step; }
    template <meta::inherits<interface<Slice>> Self>
    [[nodiscard]] static auto indices(Self&& self, size_t size) { return self.indices(size); }
};


/* Represents a statically-typed Python `slice` object in C++.  Note that the start,
stop, and step values do not strictly need to be integers. */
struct Slice : Object, interface<Slice> {
    struct __python__ : cls<__python__, Slice>, PySliceObject {
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
// struct __getattr__<Self, "indices"> : returns<Function<
//     Tuple<Int>(Arg<"length", const Int&>)
// >> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "start">                           : returns<Object> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "stop">                            : returns<Object> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "step">                            : returns<Object> {};


template <meta::is<Object> Derived, meta::is<Slice> Base>
struct __isinstance__<Derived, Base>                        : returns<bool> {
    static constexpr bool operator()(Derived obj) {
        return PySlice_Check(ptr(obj));
    }
};


template <>
struct __init__<Slice>                                      : returns<Slice> {
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
struct __init__<Slice, Start, Stop, Step>                   : returns<Slice> {
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
struct __init__<Slice, Start, Stop>                         : returns<Slice> {
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
struct __init__<Slice, Stop>                                : returns<Slice> {
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


template <meta::is<Slice> L, meta::is<Slice> R>
struct __lt__<L, R>                                         : returns<Bool> {};
template <meta::is<Slice> L, meta::is<Slice> R>
struct __le__<L, R>                                         : returns<Bool> {};
template <meta::is<Slice> L, meta::is<Slice> R>
struct __eq__<L, R>                                         : returns<Bool> {};
template <meta::is<Slice> L, meta::is<Slice> R>
struct __ne__<L, R>                                         : returns<Bool> {};
template <meta::is<Slice> L, meta::is<Slice> R>
struct __ge__<L, R>                                         : returns<Bool> {};
template <meta::is<Slice> L, meta::is<Slice> R>
struct __gt__<L, R>                                         : returns<Bool> {};


}


#endif
