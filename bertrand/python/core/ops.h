#ifndef BERTRAND_PYTHON_CORE_OPS_H
#define BERTRAND_PYTHON_CORE_OPS_H

#include "declarations.h"
#include "object.h"


namespace bertrand {


/// TODO: replace std::derived_from<> with better concepts from meta:: namespace

/// TODO: add assertions where appropriate to ensure that no object is ever null.


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
void del(impl::Attr<Self, Name>&& attr);


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
void del(impl::Item<Self, Key...>&& item);


namespace impl {

    /* A global map storing cached Python strings for the `template_string<"name">`
    accessor.  This avoids the overhead of repeatedly creating identical string
    instances during attribute lookups, and replaces it with a simple, 2-level hash
    lookup with proper per-interpreter state. */
    inline std::unordered_map<
        PyInterpreterState*,
        std::unordered_map<const char*, Object>
    > template_strings;

    /* Convert a compile-time string into a Python unicode object. */
    template <static_str name>
    const Object& template_string() {
        auto table = template_strings.find(PyInterpreterState_Get());
        if (table == template_strings.end()) {
            throw AssertionError(
                "no template string table found for the current Python interpreter"
            );
        }
        auto it = table->second.find(name);
        if (it != table->second.end()) {
            return it->second;
        }
        PyObject* result = PyUnicode_FromStringAndSize(name, name.size());
        if (result == nullptr) {
            Exception::from_python();
        }
        return table->second.emplace(
            name,
            steal<Object>(result)
        ).first->second;        
    }

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
        template <meta::has_python T> requires (!meta::is_qualified<T>)
        friend obj<T> bertrand::borrow(PyObject*);
        template <meta::has_python T> requires (!meta::is_qualified<T>)
        friend obj<T> bertrand::steal(PyObject*);
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
        template <meta::has_python T> requires (!meta::is_qualified<T>)
        friend obj<T> bertrand::borrow(PyObject*);
        template <meta::has_python T> requires (!meta::is_qualified<T>)
        friend obj<T> bertrand::steal(PyObject*);
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


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <meta::python Self, static_str Name>
[[nodiscard]] constexpr bool hasattr() {
    return __getattr__<Self, Name>::enable;
}


/* Equivalent to Python `hasattr(obj, name)` with a static attribute name. */
template <static_str Name, meta::python Self>
[[nodiscard]] constexpr bool hasattr(Self&& obj) {
    return __getattr__<Self, Name>::enable;
}


/* Equivalent to Python `getattr(obj, name)` with a static attribute name. */
template <static_str Name, meta::python Self>
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
[[nodiscard]] auto getattr(Self&& self) -> __getattr__<Self, Name>::type {
    if constexpr (DEBUG) {
        assert_(
            ptr(self) != nullptr,
            "Cannot get attribute '" + Name + "' from a null object."
        );
    }
    if constexpr (std::is_invocable_v<__getattr__<Self, Name>, Self>) {
        return __getattr__<Self, Name>{}(std::forward<Self>(self));

    } else {
        PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
        if (name == nullptr) {
            Exception::from_python();
        }
        PyObject* result = PyObject_GetAttr(
            ptr(to_python(std::forward<Self>(self))),
            name
        );
        Py_DECREF(name);
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __getattr__<Self, Name>::type>(result);
    }
}


/* Equivalent to Python `getattr(obj, name, default)` with a static attribute name and
default value. */
template <static_str Name, meta::python Self>
    requires (
        __getattr__<Self, Name>::enable &&
        std::derived_from<typename __getattr__<Self, Name>::type, Object> && (
            !std::is_invocable_v<
                __getattr__<Self, Name>,
                Self,
                const typename __getattr__<Self, Name>::type&
            > || std::is_invocable_r_v<
                typename __getattr__<Self, Name>::type,
                __getattr__<Self, Name>,
                Self,
                const typename __getattr__<Self, Name>::type&
            >
        )
    )
[[nodiscard]] auto getattr(
    Self&& self,
    const typename __getattr__<Self, Name>::type& default_value
) -> __getattr__<Self, Name>::type {
    if constexpr (DEBUG) {
        assert_(
            ptr(self) != nullptr,
            "Cannot get attribute '" + Name + "' from a null object."
        );
    }
    using Return = __getattr__<Self, Name>::type;
    if constexpr (std::is_invocable_v<__getattr__<Self, Name>, Self, const Return&>) {
        return __getattr__<Self, Name>{}(std::forward<Self>(self), default_value);

    } else {
        PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
        if (name == nullptr) {
            Exception::from_python();
        }
        PyObject* result = PyObject_GetAttr(
            ptr(to_python(std::forward<Self>(self))),
            name
        );
        Py_DECREF(name);
        if (result == nullptr) {
            PyErr_Clear();
            return default_value;
        }
        return steal<typename __getattr__<Self, Name>::type>(result);
    }
}


/* Equivalent to Python `setattr(obj, name, value)` with a static attribute name. */
template <static_str Name, meta::python Self, typename Value>
    requires (
        __setattr__<Self, Name, Value>::enable &&
        std::is_void_v<typename __setattr__<Self, Name, Value>::type> && (
            std::is_invocable_r_v<void, __setattr__<Self, Name, Value>, Self, Value> || (
                !std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value> &&
                meta::has_cpp<typename std::remove_cvref_t<typename __getattr__<Self, Name>::type>> &&
                std::is_assignable_v<typename std::remove_cvref_t<typename __getattr__<Self, Name>::type>&, Value>
            ) || (
                !std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value> &&
                !meta::has_cpp<typename std::remove_cvref_t<typename __getattr__<Self, Name>::type>>
            )
        )
    )
void setattr(Self&& self, Value&& value) {
    if constexpr (DEBUG) {
        assert_(
            ptr(self) != nullptr,
            "Cannot assign attribute '" + Name + "' on a null object."
        );
    }
    if constexpr (std::is_invocable_v<__setattr__<Self, Name, Value>, Self, Value>) {
        __setattr__<Self, Name, Value>{}(
            std::forward<Self>(self),
            std::forward<Value>(value)
        );

    } else {
        auto obj = to_python(std::forward<Value>(value));
        if constexpr (DEBUG) {
            assert_(
                ptr(obj) != nullptr,
                "Cannot assign attribute '" + Name + "' to a null object."
            );
        }
        PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
        if (name == nullptr) {
            Exception::from_python();
        }
        int rc = PyObject_SetAttr(
            ptr(to_python(std::forward<Self>(self))),
            name,
            ptr(obj)
        );
        Py_DECREF(name);
        if (rc) {
            Exception::from_python();
        }
    }
}


/* Equivalent to Python `delattr(obj, name)` with a static attribute name. */
template <static_str Name, meta::python Self>
    requires (
        __delattr__<Self, Name>::enable && 
        std::is_void_v<typename __delattr__<Self, Name>::type> && (
            std::is_invocable_r_v<void, __delattr__<Self, Name>, Self> ||
            !std::is_invocable_v<__delattr__<Self, Name>, Self>
        )
    )
void delattr(Self&& self) {
    if constexpr (DEBUG) {
        assert_(
            ptr(self) != nullptr,
            "Cannot delete attribute '" + Name + "' on a null object."
        );
    }
    if constexpr (std::is_invocable_v<__delattr__<Self, Name>, Self>) {
        __delattr__<Self, Name>{}(std::forward<Self>(self));

    } else {
        PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
        if (name == nullptr) {
            Exception::from_python();
        }
        int rc = PyObject_DelAttr(
            ptr(to_python(std::forward<Self>(self))),
            name
        );
        Py_DECREF(name);
        if (rc) {
            Exception::from_python();
        }
    }
}


/* Contains operator.  Equivalent to Python's `in` keyword, but as a freestanding
non-member function (i.e. `x in y` -> `bertrand::in(x, y)`).  A member equivalent is
defined for all subclasses of `Object` (i.e. `x.in(y)`), which delegates to this
function. */
template <typename Key, typename Container>
    requires (
        __contains__<Container, Key>::enable &&
        std::same_as<typename __contains__<Container, Key>::type, bool> && (
            std::is_invocable_r_v<bool, __contains__<Container, Key>, Container, Key> || (
                !std::is_invocable_v<__contains__<Container, Key>, Container, Key> &&
                meta::has_cpp<Container> &&
                meta::has_contains<meta::cpp_type<Container>, meta::cpp_type<Key>>
            ) || (
                !std::is_invocable_v<__contains__<Container, Key>, Container, Key> &&
                !meta::has_cpp<Container>
            )
        )
    )
[[nodiscard]] bool in(Key&& key, Container&& container) {
    if constexpr (std::is_invocable_v<__contains__<Container, Key>, Container, Key>) {
        return __contains__<Container, Key>{}(
            std::forward<Container>(container),
            std::forward<Key>(key)
        );

    } else if constexpr (meta::has_cpp<Container>) {
        return from_python(std::forward<Container>(container)).contains(
            from_python(std::forward<Key>(key))
        );

    } else {
        int result = PySequence_Contains(
            ptr(to_python(std::forward<Container>(container))),
            ptr(to_python(std::forward<Key>(key)))
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


/* Member equivalent for `bertrand::in()` function, which simplifies the syntax if the
key is already a Python object */
template <typename Self, typename Key>
    requires (
        __contains__<Self, Key>::enable &&
        std::same_as<typename __contains__<Self, Key>::type, bool> && (
            std::is_invocable_r_v<bool, __contains__<Self, Key>, Self, Key> || (
                !std::is_invocable_v<__contains__<Self, Key>, Self, Key> &&
                meta::has_cpp<Self> &&
                meta::has_contains<meta::cpp_type<Self>, meta::cpp_type<Key>>
            ) || (
                !std::is_invocable_v<__contains__<Self, Key>, Self, Key> &&
                !meta::has_cpp<Self>
            )
        )
    )
[[nodiscard]] inline bool Object::in(this Self&& self, Key&& key) {
    return bertrand::in(std::forward<Key>(key), std::forward<Self>(self));
}


/* Equivalent to Python `repr(obj)`, but returns a std::string and attempts to
represent C++ types using the stream insertion operator (<<) or std::to_string.  If all
else fails, falls back to demangling the result of typeid(obj).name(). */
template <typename Self>
    requires (!__repr__<Self>::enable || (
        std::convertible_to<typename __repr__<Self>::type, std::string> && (
            !std::is_invocable_v<__repr__<Self>> ||
            std::is_invocable_r_v<std::string, __repr__<Self>, Self>
        )
    ))
[[nodiscard]] std::string repr(Self&& obj) {
    if constexpr (std::is_invocable_r_v<std::string, __repr__<Self>, Self>) {
        return __repr__<Self>{}(std::forward<Self>(obj));

    } else if constexpr (meta::has_python<Self>) {
        PyObject* str = PyObject_Repr(
            ptr(to_python(std::forward<Self>(obj)))
        );
        if (str == nullptr) {
            Exception::from_python();
        }
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(str, &size);
        if (data == nullptr) {
            Py_DECREF(str);
            Exception::from_python();
        }
        std::string result(data, size);
        Py_DECREF(str);
        return result;

    } else if constexpr (meta::has_to_string<Self>) {
        return std::to_string(std::forward<Self>(obj));

    } else if constexpr (meta::has_stream_insertion<Self>) {
        std::ostringstream stream;
        stream << std::forward<Self>(obj);
        return stream.str();

    } else {
        return
            "<" + type_name<Self> + " at " + std::to_string(
                reinterpret_cast<size_t>(&obj)
            ) + ">";
    }
}


/* Equivalent to Python `hash(obj)`, but delegates to std::hash, which is overloaded
for the relevant Python types.  This promotes hash-not-implemented exceptions into
compile-time equivalents. */
template <meta::hashable T>
[[nodiscard]] size_t hash(T&& obj) {
    return std::hash<T>{}(std::forward<T>(obj));
}


/* Equivalent to Python `len(obj)`. */
template <typename Self>
    requires (
        __len__<Self>::enable &&
        std::convertible_to<typename __len__<Self>::type, size_t> && (
            std::is_invocable_r_v<size_t, __len__<Self>, Self> || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                meta::has_cpp<Self> &&
                meta::has_size<meta::cpp_type<Self>>
            ) || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                !meta::has_cpp<Self>
            )
        )
    )
[[nodiscard]] size_t len(Self&& obj) {
    if constexpr (std::is_invocable_v<__len__<Self>, Self>) {
        return __len__<Self>{}(std::forward<Self>(obj));

    } else if constexpr (meta::has_cpp<Self>) {
        return std::ranges::size(from_python(std::forward<Self>(obj)));

    } else {
        Py_ssize_t size = PyObject_Length(
            ptr(to_python(std::forward<Self>(obj)))
        );
        if (size < 0) {
            Exception::from_python();
        }
        return size;
    }
}


/* Equivalent to Python `len(obj)`, except that it works on C++ objects implementing a
`size()` method. */
template <typename Self> requires (!__len__<Self>::enable && meta::has_size<Self>)
[[nodiscard]] size_t len(Self&& obj) {
    return std::ranges::size(std::forward<Self>(obj));
}


/* An alias for `bertrand::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename Self>
    requires (
        __len__<Self>::enable &&
        std::convertible_to<typename __len__<Self>::type, size_t> && (
            std::is_invocable_r_v<size_t , __len__<Self>, Self> || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                meta::has_cpp<Self> &&
                meta::has_size<meta::cpp_type<Self>>
            ) || (
                !std::is_invocable_v<__len__<Self>, Self> &&
                !meta::has_cpp<Self>
            )
        )
    )
[[nodiscard]] size_t size(Self&& obj) {
    return len(std::forward<Self>(obj));
}


/* An alias for `bertrand::len(obj)`, but triggers ADL for constructs that expect a
free-floating size() function. */
template <typename Self> requires (!__len__<Self>::enable && meta::has_size<Self>)
[[nodiscard]] size_t size(Self&& obj) {
    return len(std::forward<Self>(obj));
}


/* Equivalent to Python `abs(obj)` for any object that specializes the __abs__ control
struct. */
template <typename Self>
    requires (
        __abs__<Self>::enable &&
        std::convertible_to<typename __abs__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __abs__<Self>::type, __abs__<Self>, Self> || (
                !std::is_invocable_v<__abs__<Self>, Self> &&
                meta::has_cpp<Self> &&
                meta::abs_returns<meta::cpp_type<Self>, typename __abs__<Self>::type>
            ) || (
                !std::is_invocable_v<__abs__<Self>, Self> &&
                !meta::has_cpp<Self> &&
                std::derived_from<typename __abs__<Self>::type, Object>
            )
        )
    )
[[nodiscard]] decltype(auto) abs(Self&& self) {
    if constexpr (std::is_invocable_v<__abs__<Self>, Self>) {
        return __abs__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return std::abs(from_python(std::forward<Self>(self)));

    } else {
        using Return = std::remove_cvref_t<typename __abs__<Self>::type>;
        PyObject* result = PyNumber_Absolute(
            ptr(to_python(std::forward<Self>(self)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<Return>(result);
    }
}


/* Equivalent to Python `abs(obj)`, except that it takes a C++ value and applies
std::abs() for identical semantics. */
template <meta::has_abs Self>
    requires (!__abs__<Self>::enable && meta::abs_returns<Self, Object>)
[[nodiscard]] decltype(auto) abs(Self&& value) {
    return std::abs(std::forward<Self>(value));
}


template <meta::python Self> requires (!__invert__<Self>::enable)
decltype(auto) operator~(Self&& self) = delete;
template <meta::python Self>
    requires (
        __invert__<Self>::enable &&
        std::convertible_to<typename __invert__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __invert__<Self>::type, __invert__<Self>, Self> || (
                !std::is_invocable_v<__invert__<Self>, Self> &&
                meta::has_cpp<Self> &&
                meta::invert_returns<meta::cpp_type<Self>, typename __invert__<Self>::type>
            ) || (
                !std::is_invocable_v<__invert__<Self>, Self> &&
                !meta::has_cpp<Self> &&
                std::derived_from<typename __invert__<Self>::type, Object>
            )
        )
    )
decltype(auto) operator~(Self&& self) {
    if constexpr (std::is_invocable_v<__invert__<Self>, Self>) {
        return __invert__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return ~from_python(std::forward<Self>(self));

    } else {
        PyObject* result = PyNumber_Invert(
            ptr(to_python(std::forward<Self>(self)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __invert__<Self>::type>(result);
    }
}


template <meta::python Self> requires (!__pos__<Self>::enable)
decltype(auto) operator+(Self&& self) = delete;
template <meta::python Self>
    requires (
        __pos__<Self>::enable &&
        std::convertible_to<typename __pos__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __pos__<Self>::type, __pos__<Self>, Self> || (
                !std::is_invocable_v<__pos__<Self>, Self> &&
                meta::has_cpp<Self> &&
                meta::pos_returns<meta::cpp_type<Self>, typename __pos__<Self>::type>
            ) || (
                !std::is_invocable_v<__pos__<Self>, Self> &&
                !meta::has_cpp<Self> &&
                std::derived_from<typename __pos__<Self>::type, Object>
            )
        )
    )
decltype(auto) operator+(Self&& self) {
    if constexpr (std::is_invocable_v<__pos__<Self>, Self>) {
        return __pos__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return +from_python(std::forward<Self>(self));

    } else {
        PyObject* result = PyNumber_Positive(
            ptr(to_python(std::forward<Self>(self)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __pos__<Self>::type>(result);
    }
}


template <meta::python Self> requires (!__neg__<Self>::enable)
decltype(auto) operator-(Self&& self) = delete;
template <meta::python Self>
    requires (
        __neg__<Self>::enable &&
        std::convertible_to<typename __neg__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __neg__<Self>::type, __neg__<Self>, Self> || (
                !std::is_invocable_v<__neg__<Self>, Self> &&
                meta::has_cpp<Self> &&
                meta::neg_returns<meta::cpp_type<Self>, typename __neg__<Self>::type>
            ) || (
                !std::is_invocable_v<__neg__<Self>, Self> &&
                !meta::has_cpp<Self> &&
                std::derived_from<typename __neg__<Self>::type, Object>
            )
        )
    )
decltype(auto) operator-(Self&& self) {
    if constexpr (std::is_invocable_v<__neg__<Self>, Self>) {
        return __neg__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return -from_python(std::forward<Self>(self));

    } else {
        PyObject* result = PyNumber_Negative(
            ptr(to_python(std::forward<Self>(self)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __neg__<Self>::type>(result);
    }
}


template <meta::python Self>
decltype(auto) operator++(Self&& self, int) = delete;  // post-increment is not valid
template <meta::python Self> requires (!__increment__<Self>::enable)
decltype(auto) operator++(Self&& self) = delete;
template <meta::python Self>
    requires (
        __increment__<Self>::enable &&
        std::convertible_to<typename __increment__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __increment__<Self>::type, __increment__<Self>, Self> || (
                !std::is_invocable_v<__increment__<Self>, Self> &&
                meta::has_cpp<Self> &&
                meta::preincrement_returns<meta::cpp_type<Self>, typename __increment__<Self>::type>
            ) || (
                !std::is_invocable_v<__increment__<Self>, Self> &&
                !meta::has_cpp<Self> &&
                std::same_as<typename __increment__<Self>::type, Self>
            )
        )
    )
decltype(auto) operator++(Self&& self) {
    if constexpr (std::is_invocable_v<__increment__<Self>, Self>) {
        return __increment__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return ++from_python(std::forward<Self>(self));

    } else {
        using Return = std::remove_cvref_t<typename __increment__<Self>::type>;
        PyObject* one = PyLong_FromLong(1);
        if (one == nullptr) {
            Exception::from_python();
        }
        PyObject* result = PyNumber_InPlaceAdd(ptr(self), one);
        Py_DECREF(one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = steal<Return>(result);
        }
        return std::forward<Self>(self);
    }
}


template <meta::python Self>
decltype(auto) operator--(Self& self, int) = delete;  // post-decrement is not valid
template <meta::python Self> requires (!__decrement__<Self>::enable)
decltype(auto) operator--(Self& self) = delete;
template <meta::python Self>
    requires (
        __decrement__<Self>::enable &&
        std::convertible_to<typename __decrement__<Self>::type, Object> && (
            std::is_invocable_r_v<typename __decrement__<Self>::type, __decrement__<Self>, Self> || (
                !std::is_invocable_v<__decrement__<Self>, Self> &&
                meta::has_cpp<Self> &&
                meta::predecrement_returns<meta::cpp_type<Self>, typename __decrement__<Self>::type>
            ) || (
                !std::is_invocable_v<__decrement__<Self>, Self> &&
                !meta::has_cpp<Self> &&
                std::same_as<typename __decrement__<Self>::type, Self>
            )
        )
    )
decltype(auto) operator--(Self&& self) {
    if constexpr (std::is_invocable_v<__decrement__<Self>, Self>) {
        return __decrement__<Self>{}(std::forward<Self>(self));

    } else if constexpr (meta::has_cpp<Self>) {
        return --from_python(std::forward<Self>(self));

    } else {
        using Return = std::remove_cvref_t<typename __decrement__<Self>::type>;
        PyObject* one = PyLong_FromLong(1);
        if (one == nullptr) {
            Exception::from_python();
        }
        PyObject* result = PyNumber_InPlaceSubtract(ptr(self), one);
        Py_DECREF(one);
        if (result == nullptr) {
            Exception::from_python();
        } else if (result == ptr(self)) {
            Py_DECREF(result);
        } else {
            self = steal<Return>(result);
        }
        return std::forward<Self>(self);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__lt__<L, R>::enable)
decltype(auto) operator<(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __lt__<L, R>::enable &&
        std::convertible_to<typename __lt__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __lt__<L, R>::type, __lt__<L, R>, L, R> || (
                !std::is_invocable_v<__lt__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::lt_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __lt__<L, R>::type>
            ) || (
                !std::is_invocable_v<__lt__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __lt__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator<(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__lt__<L, R>, L, R>) {
        return __lt__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) < from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_LT
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __lt__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__le__<L, R>::enable)
decltype(auto) operator<=(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __le__<L, R>::enable &&
        std::convertible_to<typename __le__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __le__<L, R>::type, __le__<L, R>, L, R> || (
                !std::is_invocable_v<__le__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::le_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __le__<L, R>::type>
            ) || (
                !std::is_invocable_v<__le__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __le__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator<=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__le__<L, R>, L, R>) {
        return __le__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) <= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_LE
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __le__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__eq__<L, R>::enable)
decltype(auto) operator==(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __eq__<L, R>::enable &&
        std::convertible_to<typename __eq__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __eq__<L, R>::type, __eq__<L, R>, L, R> || (
                !std::is_invocable_v<__eq__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::eq_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __eq__<L, R>::type>
            ) || (
                !std::is_invocable_v<__eq__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __eq__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator==(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__eq__<L, R>, L, R>) {
        return __eq__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) == from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_EQ
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __eq__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__ne__<L, R>::enable)
decltype(auto) operator!=(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __ne__<L, R>::enable &&
        std::convertible_to<typename __ne__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ne__<L, R>::type, __ne__<L, R>, L, R> || (
                !std::is_invocable_v<__ne__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::ne_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __ne__<L, R>::type>
            ) || (
                !std::is_invocable_v<__ne__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __ne__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator!=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ne__<L, R>, L, R>) {
        return __ne__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) != from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_NE
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __ne__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__ge__<L, R>::enable)
decltype(auto) operator>=(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __ge__<L, R>::enable &&
        std::convertible_to<typename __ge__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ge__<L, R>::type, __ge__<L, R>, L, R> || (
                !std::is_invocable_v<__ge__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::ge_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __ge__<L, R>::type>
            ) || (
                !std::is_invocable_v<__ge__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __ge__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator>=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ge__<L, R>, L, R>) {
        return __ge__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) >= from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_GE
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __ge__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__gt__<L, R>::enable)
decltype(auto) operator>(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __gt__<L, R>::enable &&
        std::convertible_to<typename __gt__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __gt__<L, R>::type, __gt__<L, R>, L, R> || (
                !std::is_invocable_v<__gt__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::gt_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __gt__<L, R>::type>
            ) || (
                !std::is_invocable_v<__gt__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __gt__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator>(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__gt__<L, R>, L, R>) {
        return __gt__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) > from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyObject_RichCompare(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs))),
            Py_GT
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __gt__<L, R>::type>(result);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__add__<L, R>::enable)
decltype(auto) operator+(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __add__<L, R>::enable &&
        std::convertible_to<typename __add__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __add__<L, R>::type, __add__<L, R>, L, R> || (
                !std::is_invocable_v<__add__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::add_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __add__<L, R>::type>
            ) || (
                !std::is_invocable_v<__add__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __add__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator+(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__add__<L, R>, L, R>) {
        return __add__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) + from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Add(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __add__<L, R>::type>(result);
    }
}


template <meta::python L, typename R> requires (!__iadd__<L, R>::enable)
decltype(auto) operator+=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (
        __iadd__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __iadd__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __iadd__<L, R>::type, __iadd__<L, R>, L, R> || (
                !std::is_invocable_v<__iadd__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::iadd_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __iadd__<L, R>::type>
            ) || (
                !std::is_invocable_v<__iadd__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::inherits<typename __iadd__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator+=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__iadd__<L, R>, L, R>) {
        return __iadd__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) +=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __iadd__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceAdd(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__sub__<L, R>::enable)
decltype(auto) operator-(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __sub__<L, R>::enable &&
        std::convertible_to<typename __sub__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __sub__<L, R>::type, __sub__<L, R>, L, R> || (
                !std::is_invocable_v<__sub__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::sub_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __sub__<L, R>::type>
            ) || (
                !std::is_invocable_v<__sub__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __sub__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator-(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__sub__<L, R>, L, R>) {
        return __sub__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) - from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Subtract(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __sub__<L, R>::type>(result);
    }
}


template <meta::python L, typename R> requires (!__isub__<L, R>::enable)
decltype(auto) operator-=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (
        __isub__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __isub__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __isub__<L, R>::type, __isub__<L, R>, L, R> || (
                !std::is_invocable_v<__isub__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::isub_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __isub__<L, R>::type>
            ) || (
                !std::is_invocable_v<__isub__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::inherits<typename __isub__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator-=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__isub__<L, R>, L, R>) {
        return __isub__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) -=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __isub__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceSubtract(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__mul__<L, R>::enable)
decltype(auto) operator*(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __mul__<L, R>::enable &&
        std::convertible_to<typename __mul__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __mul__<L, R>::type, __mul__<L, R>, L, R> || (
                !std::is_invocable_v<__mul__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::mul_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __mul__<L, R>::type>
            ) || (
                !std::is_invocable_v<__mul__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __mul__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator*(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__mul__<L, R>, L, R>) {
        return __mul__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) * from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Multiply(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __mul__<L, R>::type>(result);
    }
}


template <meta::python L, typename R> requires (!__imul__<L, R>::enable)
decltype(auto) operator*=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (
        __imul__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __imul__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __imul__<L, R>::type, __imul__<L, R>, L, R> || (
                !std::is_invocable_v<__imul__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::imul_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __imul__<L, R>::type>
            ) || (
                !std::is_invocable_v<__imul__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::inherits<typename __imul__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator*=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__imul__<L, R>, L, R>) {
        return __imul__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) *=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __imul__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceMultiply(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = steal<Return>(result);
        lhs = out;
        return out;
    }
}


/* Equivalent to Python `base ** exp` (exponentiation). */
template <typename Base, typename Exp>
    requires (
        __pow__<Base, Exp>::enable &&
        std::convertible_to<typename __pow__<Base, Exp>::type, Object> && (
            std::is_invocable_r_v<typename __pow__<Base, Exp>::type, __pow__<Base, Exp>, Base, Exp> || (
                !std::is_invocable_v<__pow__<Base, Exp>, Base, Exp> &&
                (meta::has_cpp<Base> && meta::has_cpp<Exp>) &&
                meta::pow_returns<meta::cpp_type<Base>, meta::cpp_type<Exp>, typename __pow__<Base, Exp>::type>
            ) && (
                !std::is_invocable_v<__pow__<Base, Exp>, Base, Exp> &&
                !(meta::has_cpp<Base> && meta::has_cpp<Exp>) &&
                std::derived_from<typename __pow__<Base, Exp>::type, Object>
            )
        )
    )
decltype(auto) pow(Base&& base, Exp&& exp) {
    if constexpr (std::is_invocable_v<__pow__<Base, Exp>, Base, Exp>) {
        return __pow__<Base, Exp>{}(std::forward<Base>(base), std::forward<Exp>(exp));

    } else if constexpr (meta::has_cpp<Base> && meta::has_cpp<Exp>) {
        if constexpr (
            meta::complex_like<meta::cpp_type<Base>> &&
            meta::complex_like<meta::cpp_type<Exp>>
        ) {
            return std::common_type_t<meta::cpp_type<Base>, meta::cpp_type<Exp>>(
                pow(from_python(base).real(), from_python(exp).real()),
                pow(from_python(base).imag(), from_python(exp).imag())
            );
        } else if constexpr (meta::complex_like<meta::cpp_type<Base>>) {
            return Base(
                pow(from_python(base).real(), from_python(exp)),
                pow(from_python(base).real(), from_python(exp))
            );
        } else if constexpr (meta::complex_like<meta::cpp_type<Exp>>) {
            return Exp(
                pow(from_python(base), from_python(exp).real()),
                pow(from_python(base), from_python(exp).imag())
            );
        } else {
            return std::pow(
                from_python(std::forward<Base>(base)),
                from_python(std::forward<Exp>(exp))
            );
        }

    } else {
        PyObject* result = PyNumber_Power(
            ptr(to_python(std::forward<Base>(base))),
            ptr(to_python(std::forward<Exp>(exp))),
            Py_None
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __pow__<Base, Exp>::type>(result);
    }
}


/* Equivalent to Python `pow(base, exp)`, except that it takes a C++ value and applies
std::pow() for identical semantics. */
template <meta::cpp Base, meta::cpp Exp>
    requires (!__pow__<Base, Exp>::enable && (
        meta::complex_like<Base> ||
        meta::complex_like<Exp> ||
        meta::has_pow<Base, Exp>
    ))
decltype(auto) pow(Base&& base, Exp&& exp) {
    if constexpr (meta::complex_like<Base> && meta::complex_like<Exp>) {
        return std::common_type_t<std::remove_cvref_t<Base>, std::remove_cvref_t<Exp>>(
            pow(base.real(), exp.real()),
            pow(base.imag(), exp.imag())
        );
    } else if constexpr (meta::complex_like<Base>) {
        return Base(
            pow(base.real(), exp),
            pow(base.imag(), exp)
        );
    } else if constexpr (meta::complex_like<Exp>) {
        return Exp(
            pow(base, exp.real()),
            pow(base, exp.imag())
        );
    } else {
        return std::pow(base, exp);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`. */
template <typename Base, typename Exp, typename Mod>
    requires (
        __pow__<Base, Exp, Mod>::enable &&
        std::convertible_to<typename __pow__<Base, Exp, Mod>::type, Object> && (
            std::is_invocable_r_v<
                typename __pow__<Base, Exp, Mod>::type,
                __pow__<Base, Exp, Mod>,
                Base,
                Exp,
                Mod
            > || (
                !std::is_invocable_v<__pow__<Base, Exp, Mod>, Base, Exp, Mod> &&
                std::derived_from<typename __pow__<Base, Exp, Mod>::type, Object>
            )
        )
    )
decltype(auto) pow(Base&& base, Exp&& exp, Mod&& mod) {
    if constexpr (std::is_invocable_v<__pow__<Base, Exp, Mod>, Base, Exp, Mod>) {
        return __pow__<Base, Exp, Mod>{}(
            std::forward<Base>(base),
            std::forward<Exp>(exp),
            std::forward<Mod>(mod)
        );

    } else {
        PyObject* result = PyNumber_Power(
            ptr(to_python(std::forward<Base>(base))),
            ptr(to_python(std::forward<Exp>(exp))),
            ptr(to_python(std::forward<Mod>(mod)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __pow__<Base, Exp, Mod>::type>(result);
    }
}


/* Equivalent to Python `pow(base, exp, mod)`, but works on C++ integers with identical
semantics. */
template <std::integral Base, std::integral Exp, std::integral Mod>
auto pow(Base base, Exp exp, Mod mod) {
    std::common_type_t<Base, Exp, Mod> result = 1;
    base = base % mod;
    while (exp > 0) {
        if (exp % 2) {
            result = (result * base) % mod;
        }
        exp >>= 1;
        base = (base * base) % mod;
    }
    return result;
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__truediv__<L, R>::enable)
decltype(auto) operator/(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __truediv__<L, R>::enable &&
        std::convertible_to<typename __truediv__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __truediv__<L, R>::type, __truediv__<L, R>, L, R> || (
                !std::is_invocable_v<__truediv__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::truediv_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __truediv__<L, R>::type>
            ) || (
                !std::is_invocable_v<__truediv__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __truediv__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator/(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__truediv__<L, R>, L, R>) {
        return __truediv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) / from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_TrueDivide(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __truediv__<L, R>::type>(result);
    }
}


template <meta::python L, typename R> requires (!__itruediv__<L, R>::enable)
decltype(auto) operator/=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (
        __itruediv__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __itruediv__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __itruediv__<L, R>::type, __itruediv__<L, R>, L, R> || (
                !std::is_invocable_v<__itruediv__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::itruediv_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __itruediv__<L, R>::type>
            ) || (
                !std::is_invocable_v<__itruediv__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::inherits<typename __itruediv__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator/=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__itruediv__<L, R>, L, R>) {
        return __itruediv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) /=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __itruediv__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceTrueDivide(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires (
        __floordiv__<L, R>::enable &&
        std::convertible_to<typename __floordiv__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __floordiv__<L, R>::type, __floordiv__<L, R>, L, R> || (
                !std::is_invocable_v<__floordiv__<L, R>, L, R> &&
                std::derived_from<typename __floordiv__<L, R>::type, Object>
            )
        )
    )
decltype(auto) floordiv(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__floordiv__<L, R>, L, R>) {
        return __floordiv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));
    } else {
        PyObject* result = PyNumber_FloorDivide(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(lhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __floordiv__<L, R>::type>(result);
    }
}


template <meta::python L, typename R>
    requires (
        __ifloordiv__<L, R>::enable &&
        std::convertible_to<typename __ifloordiv__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ifloordiv__<L, R>::type, __ifloordiv__<L, R>, L, R> || (
                !std::is_invocable_v<__ifloordiv__<L, R>, L, R> &&
                std::derived_from<typename __ifloordiv__<L, R>::type, Object>
            )
        )
    )
decltype(auto) ifloordiv(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ifloordiv__<L, R>, L, R>) {
        return __ifloordiv__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ifloordiv__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceFloorDivide(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<Return>(result);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__mod__<L, R>::enable)
decltype(auto) operator%(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __mod__<L, R>::enable &&
        std::convertible_to<typename __mod__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __mod__<L, R>::type, __mod__<L, R>, L, R> || (
                !std::is_invocable_v<__mod__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::mod_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __mod__<L, R>::type>
            ) || (
                !std::is_invocable_v<__mod__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __mod__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator%(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__mod__<L, R>, L, R>) {
        return __mod__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) % from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Remainder(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __mod__<L, R>::type>(result);
    }
}


template <meta::python L, typename R> requires (!__imod__<L, R>::enable)
decltype(auto) operator%=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (
        __imod__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __imod__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __imod__<L, R>::type, __imod__<L, R>, L, R> || (
                !std::is_invocable_v<__imod__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::imod_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __imod__<L, R>::type>
            ) || (
                !std::is_invocable_v<__imod__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::inherits<typename __imod__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator%=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__imod__<L, R>, L, R>) {
        return __imod__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) %=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __imod__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceRemainder(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__lshift__<L, R>::enable)
decltype(auto) operator<<(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __lshift__<L, R>::enable &&
        std::convertible_to<typename __lshift__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __lshift__<L, R>::type, __lshift__<L, R>, L, R> || (
                !std::is_invocable_v<__lshift__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::lshift_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __lshift__<L, R>::type>
            ) || (
                !std::is_invocable_v<__lshift__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __lshift__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator<<(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__lshift__<L, R>, L, R>) {
        return __lshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) << from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Lshift(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __lshift__<L, R>::type>(result);
    }
}


template <meta::python L, typename R> requires (!__ilshift__<L, R>::enable)
decltype(auto) operator<<=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (
        __ilshift__<L, R>::enable &&
        std::convertible_to<typename __ilshift__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ilshift__<L, R>::type, __ilshift__<L, R>, L, R> || (
                !std::is_invocable_v<__ilshift__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::ilshift_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __ilshift__<L, R>::type>
            ) || (
                !std::is_invocable_v<__ilshift__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __ilshift__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator<<=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ilshift__<L, R>, L, R>) {
        return __ilshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) <<=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ilshift__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceLshift(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<Return>(result);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__rshift__<L, R>::enable)
decltype(auto) operator>>(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __rshift__<L, R>::enable &&
        std::convertible_to<typename __rshift__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __rshift__<L, R>::type, __rshift__<L, R>, L, R> || (
                !std::is_invocable_v<__rshift__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::rshift_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __rshift__<L, R>::type>
            ) || (
                !std::is_invocable_v<__rshift__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __rshift__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator>>(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__rshift__<L, R>, L, R>) {
        return __rshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) >> from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Rshift(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __rshift__<L, R>::type>(result);
    }
}


template <meta::python L, typename R> requires (!__irshift__<L, R>::enable)
decltype(auto) operator>>=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (
        __irshift__<L, R>::enable &&
        std::convertible_to<typename __irshift__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __irshift__<L, R>::type, __irshift__<L, R>, L, R> || (
                !std::is_invocable_v<__irshift__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::irshift_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __irshift__<L, R>::type>
            ) || (
                !std::is_invocable_v<__irshift__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __irshift__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator>>=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__irshift__<L, R>, L, R>) {
        return __irshift__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) >>=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __irshift__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceRshift(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<Return>(result);
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__and__<L, R>::enable)
decltype(auto) operator&(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __and__<L, R>::enable &&
        std::convertible_to<typename __and__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __and__<L, R>::type, __and__<L, R>, L, R> || (
                !std::is_invocable_v<__and__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::and_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __and__<L, R>::type>
            ) || (
                !std::is_invocable_v<__and__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __and__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator&(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__and__<L, R>, L, R>) {
        return __and__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) & from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_And(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __and__<L, R>::type>(result);
    }
}


template <meta::python L, typename R> requires (!__iand__<L, R>::enable)
decltype(auto) operator&=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (
        __iand__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __iand__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __iand__<L, R>::type, __iand__<L, R>, L, R> || (
                !std::is_invocable_v<__iand__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::iand_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __iand__<L, R>::type>
            ) || (
                !std::is_invocable_v<__iand__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::inherits<typename __iand__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator&=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__iand__<L, R>, L, R>) {
        return __iand__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) &=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __iand__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceAnd(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__or__<L, R>::enable)
decltype(auto) operator|(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __or__<L, R>::enable &&
        std::convertible_to<typename __or__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __or__<L, R>::type, __or__<L, R>, L, R> || (
                !std::is_invocable_v<__or__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::or_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __or__<L, R>::type>
            ) || (
                !std::is_invocable_v<__or__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __or__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator|(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__or__<L, R>, L, R>) {
        return __or__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) | from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Or(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __or__<L, R>::type>(result);
    }
}


template <meta::python L, typename R> requires (!__ior__<L, R>::enable)
decltype(auto) operator|=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (
        __ior__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __ior__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ior__<L, R>::type, __ior__<L, R>, L, R> || (
                !std::is_invocable_v<__ior__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::ior_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __ior__<L, R>::type>
            ) || (
                !std::is_invocable_v<__ior__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::inherits<typename __ior__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator|=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ior__<L, R>, L, R>) {
        return __ior__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) |=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ior__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceOr(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = steal<Return>(result);
        lhs = out;
        return out;
    }
}


template <typename L, typename R>
    requires ((meta::python<L> || meta::python<R>) && !__xor__<L, R>::enable)
decltype(auto) operator^(L&& lhs, R&& rhs) = delete;
template <typename L, typename R>
    requires (
        __xor__<L, R>::enable &&
        std::convertible_to<typename __xor__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __xor__<L, R>::type, __xor__<L, R>, L, R> || (
                !std::is_invocable_v<__xor__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::xor_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __xor__<L, R>::type>
            ) || (
                !std::is_invocable_v<__xor__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                std::derived_from<typename __xor__<L, R>::type, Object>
            )
        )
    )
decltype(auto) operator^(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__xor__<L, R>, L, R>) {
        return __xor__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) ^ from_python(std::forward<R>(rhs));

    } else {
        PyObject* result = PyNumber_Xor(
            ptr(to_python(std::forward<L>(lhs))),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return steal<typename __xor__<L, R>::type>(result);
    }
}


template <meta::python L, typename R> requires (!__ixor__<L, R>::enable)
decltype(auto) operator^=(L& lhs, R&& rhs) = delete;
template <meta::python L, typename R>
    requires (
        __ixor__<L, R>::enable &&
        !std::is_const_v<std::remove_reference_t<L>> &&
        std::convertible_to<typename __ixor__<L, R>::type, Object> && (
            std::is_invocable_r_v<typename __ixor__<L, R>::type, __ixor__<L, R>, L, R> || (
                !std::is_invocable_v<__ixor__<L, R>, L, R> &&
                (meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::ixor_returns<meta::cpp_type<L>, meta::cpp_type<R>, typename __ixor__<L, R>::type>
            ) || (
                !std::is_invocable_v<__ixor__<L, R>, L, R> &&
                !(meta::has_cpp<L> && meta::has_cpp<R>) &&
                meta::inherits<typename __ixor__<L, R>::type, L>
            )
        )
    )
decltype(auto) operator^=(L&& lhs, R&& rhs) {
    if constexpr (std::is_invocable_v<__ixor__<L, R>, L, R>) {
        return __ixor__<L, R>{}(std::forward<L>(lhs), std::forward<R>(rhs));

    } else if constexpr (meta::has_cpp<L> && meta::has_cpp<R>) {
        return from_python(std::forward<L>(lhs)) ^=
            from_python(std::forward<R>(rhs));

    } else {
        using Return = std::remove_cvref_t<typename __ixor__<L, R>::type>;
        PyObject* result = PyNumber_InPlaceXor(
            ptr(lhs),
            ptr(to_python(std::forward<R>(rhs)))
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        Return out = steal<Return>(result);
        lhs = out;
        return out;
    }
}


}  // namespace py


namespace std {

    template <bertrand::meta::python T>
        requires (bertrand::__hash__<T>::enable && (
            std::is_invocable_r_v<size_t, bertrand::__hash__<T>, T> ||
            (
                !std::is_invocable_v<bertrand::__hash__<T>, T> &&
                bertrand::meta::has_cpp<T> &&
                bertrand::meta::hashable<bertrand::meta::cpp_type<T>>
            ) || (
                !std::is_invocable_v<bertrand::__hash__<T>, T> &&
                !bertrand::meta::has_cpp<T>
            )
        ))
    struct hash<T> {
        static constexpr size_t operator()(T obj) {
            if constexpr (std::is_invocable_v<bertrand::__hash__<T>, T>) {
                return bertrand::__hash__<T>{}(std::forward<T>(obj));

            } else if constexpr (bertrand::meta::has_cpp<T>) {
                return bertrand::hash(bertrand::from_python(std::forward<T>(obj)));

            } else {
                Py_hash_t result = PyObject_Hash(
                    bertrand::ptr(bertrand::to_python(std::forward<T>(obj)))
                );
                if (result == -1 && PyErr_Occurred()) {
                    bertrand::Exception::from_python();
                }
                return result;
            }
        }
    };

};  // namespace std


#endif
