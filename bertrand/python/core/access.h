#ifndef BERTRAND_CORE_ACCESS_H
#define BERTRAND_CORE_ACCESS_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"


namespace py {


////////////////////////////////
////    ATTRIBUTES/ITEMS    ////
////////////////////////////////


namespace impl {

    /* A proxy for the result of an attribute lookup that is controlled by the
    `__getattr__`, `__setattr__`, and `__delattr__` control structs.

    This is a simple extension of an Object type that intercepts `operator=` and
    assigns the new value back to the attribute using the appropriate API.  Mutating
    the object in any other way will also modify it in-place on the parent. */
    template <typename Self, StaticStr Name>
        requires (__getattr__<Self, Name>::enable)
    struct Attr : std::remove_cvref_t<typename __getattr__<Self, Name>::type> {
    private:
        using Base = std::remove_cvref_t<typename __getattr__<Self, Name>::type>;
        static_assert(
            std::derived_from<Base, Object>,
            "Default attribute access operator must return a subclass of py::Object.  "
            "Check your specialization of __getattr__ for this type and ensure the "
            "Return type derives from py::Object, or define a custom call operator "
            "to override this behavior."
        );

        template <typename S, StaticStr N> requires (__delattr__<S, N>::enable)
        friend void del(Attr<S, N>&& item);
        template <inherits<Object> T>
        friend PyObject* ptr(T&);
        template <inherits<Object> T>
            requires (!std::is_const_v<std::remove_reference_t<T>>)
        friend PyObject* release(T&&);
        template <std::derived_from<Object> T>
        friend T reinterpret_borrow(PyObject*);
        template <std::derived_from<Object> T>
        friend T reinterpret_steal(PyObject*);
        template <typename T>
        friend auto& unwrap(T& obj);
        template <typename T>
        friend const auto& unwrap(const T& obj);

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

        template <typename S, typename Value>
            requires (
                std::is_lvalue_reference_v<S> ||
                !__setattr__<Self, Name, Value>::enable
            )
        Attr& operator=(this S&& self, Value&& value) = delete;
        template <typename S, typename Value>
            requires (
                !std::is_lvalue_reference_v<S> &&
                __setattr__<Self, Name, Value>::enable
            )
        Attr& operator=(this S&& self, Value&& value) {
            using setattr = __setattr__<Self, Name, Value>;
            using Return = typename setattr::type;
            static_assert(
                std::is_void_v<Return>,
                "attribute assignment operator must return void.  Check your "
                "specialization of __setattr__ for these types and ensure the Return "
                "type is set to void."
            );
            if constexpr (has_call_operator<setattr>) {
                setattr{}(std::forward<Self>(self.m_self), std::forward<Value>(value));

            } else if constexpr (has_cpp<Base>) {
                unwrap(self) = unwrap(std::forward<Value>(value));

            } else {
                Base::operator=(std::forward<Value>(value));
                PyObject* name = PyUnicode_FromStringAndSize(Name, Name.size());
                if (name == nullptr) {
                    Exception::from_python();
                }
                int rc = PyObject_SetAttr(ptr(self.m_self), name, ptr(self));
                Py_DECREF(name);
                if (rc) {
                    Exception::from_python();
                }
            }
            return self;
        }

    };

    /* A proxy for an item in a Python container that is controlled by the
    `__getitem__`, `__setitem__`, and `__delitem__` control structs.

    This is a simple extension of an Object type that intercepts `operator=` and
    assigns the new value back to the container using the appropriate API.  Mutating
    the object in any other way will also modify it in-place within the container. */
    template <typename Self, typename... Key>
        requires (__getitem__<Self, Key...>::enable)
    struct Item : __getitem__<Self, Key...>::type {
    private:
        using Base = __getitem__<Self, Key...>::type;
        static_assert(sizeof...(Key) > 0, "Item must have at least one key.");
        static_assert(
            std::derived_from<Base, Object>,
            "Default index operator must return a subclass of py::Object.  Check your "
            "specialization of __getitem__ for this type and ensure the Return type "
            "derives from py::Object, or define a custom call operator to override "
            "this behavior."
        );

        template <typename S, typename... K> requires (__delitem__<S, K...>::enable)
        friend void del(Item<S, K...>&& item);
        template <inherits<Object> T>
        friend PyObject* ptr(T&);
        template <inherits<Object> T>
            requires (!std::is_const_v<std::remove_reference_t<T>>)
        friend PyObject* release(T&&);
        template <std::derived_from<Object> T>
        friend T reinterpret_borrow(PyObject*);
        template <std::derived_from<Object> T>
        friend T reinterpret_steal(PyObject*);
        template <typename T>
        friend auto& unwrap(T& obj);
        template <typename T>
        friend const auto& unwrap(const T& obj);

        template <typename... Ts>
        struct KeyType {
            template <typename T>
            struct wrap_references {
                using type = T;
            };
            template <typename T> requires (std::is_reference_v<T>)
            struct wrap_references<T> {
                using type = std::reference_wrapper<std::remove_reference_t<T>>;
            };
            using type = std::tuple<wrap_references<Ts>...>;
        };
        template <typename T>
        struct KeyType<T> {
            using type = T;
        };
        using M_Key = KeyType<Key...>::type;

        /* m_self inherits the same const/volatile/reference qualifiers as the original
        object.  The keys are either moved or copied into m_key if it is a tuple, or
        directly references similar to m_self if it is a single value. */
        Self m_self;
        M_Key m_key;

        /* When the key is stored as a tuple, there needs to be an extra coercion step
        to convert the `reference_wrapper`s back into the original references, and to
        move the keys that were originally supplied as raw values or rvalue
        references. */
        template <size_t I>
        struct maybe_move {
            using type = unpack_type<I, Key...>;
            decltype(auto) static operator()(M_Key& keys) {
                if constexpr (std::is_lvalue_reference_v<type>) {
                    return std::get<I>(keys).get();
                } else if constexpr (std::is_rvalue_reference_v<type>) {
                    return std::move(std::get<I>(keys).get());
                } else {
                    return std::move(std::get<I>(keys));
                }
            }
        };

        /* The wrapper's `m_ptr` member is lazily evaluated to avoid repeated lookups.
        Replacing it with a computed property will trigger a __getitem__ lookup the
        first time it is accessed. */
        __declspec(property(get = _get_ptr, put = _set_ptr)) PyObject* m_ptr;
        void _set_ptr(PyObject* value) { Base::m_ptr = value; }
        PyObject* _get_ptr() {
            if (Base::m_ptr == nullptr) {
                using getitem = __getitem__<Self, Key...>;
                PyObject* result;
                if constexpr (sizeof...(Key) == 1) {
                    if constexpr (has_call_operator<getitem>) {
                        result = release(
                            getitem{}(
                                std::forward<Self>(m_self),
                                std::forward<M_Key>(m_key)
                            )
                        );
                    } else {
                        result = PyObject_GetItem(
                            ptr(m_self),
                            ptr(as_object(std::forward<M_Key>(m_key)))
                        );
                        if (result == nullptr) {
                            Exception::from_python();
                        }
                    }

                } else {
                    if constexpr (has_call_operator<getitem>) {
                        [&]<size_t... I>(std::index_sequence<I...>) {
                            result = release(
                                getitem{}(
                                    std::forward<Self>(m_self),
                                    maybe_move<I>{}(m_key)...
                                )
                            );
                        }(std::index_sequence_for<Key...>{});
                    } else {
                        result = PyObject_GetItem(
                            ptr(m_self),
                            ptr(as_object(m_key))
                        );
                        if (result == nullptr) {
                            Exception::from_python();
                        }
                    }
                }
                Base::m_ptr = result;
            }
            return Base::m_ptr;
        }

    public:

        Item(Self&& self, Key&&... key) :
            Base(nullptr, Object::stolen_t{}), m_self(std::forward<Self>(self)),
            m_key(std::forward<Key>(key)...)
        {}
        Item(const Item& other) = delete;
        Item(Item&& other) = delete;

        template <typename S, typename Value>
            requires (
                std::is_lvalue_reference_v<S> ||
                !__setitem__<Self, Value, Key...>::enable
            )
        Item& operator=(this S&& self, Value&& other) = delete;
        template <typename S, typename Value>
            requires (
                !std::is_lvalue_reference_v<S> &&
                __setitem__<Self, Value, Key...>::enable
            )
        Item& operator=(this S&& self, Value&& value) {
            using setitem = __setitem__<Self, Value, Key...>;
            using Return = typename setitem::type;
            static_assert(
                std::is_void_v<Return>,
                "index assignment operator must return void.  Check your "
                "specialization of __setitem__ for these types and ensure the Return "
                "type is set to void."
            );
            /// TODO: all custom __setitem__ operators must reverse the order of the
            /// value and keys.  Also, they will only ever be called with the
            /// value as a python object.
            if constexpr (sizeof...(Key) == 1) {
                if constexpr (has_call_operator<setitem>) {
                    setitem{}(
                        std::forward<Self>(self.m_self),
                        std::forward<Value>(value),
                        std::forward<M_Key>(self.m_key)
                    );
                } else if constexpr (has_cpp<Base>) {
                    static_assert(
                        supports_item_assignment<Base, Value, Key...>,
                        "__setitem__<Self, Value, Key...> is enabled for operands "
                        "whose C++ representations have no viable overload for "
                        "`Self[Key...] = Value`"
                    );
                    unwrap(self) = unwrap(std::forward<Value>(value));

                } else {
                    Base::operator=(std::forward<Value>(value));
                    if (PyObject_SetItem(
                        ptr(self.m_self),
                        ptr(as_object(std::forward<M_Key>(self.m_key))),
                        ptr(self)
                    )) {
                        Exception::from_python();
                    }
                }

            } else {
                if constexpr (has_call_operator<setitem>) {
                    [&]<size_t... I>(std::index_sequence<I...>) {
                        setitem{}(
                            std::forward<Self>(self.m_self),
                            std::forward<Value>(value),
                            maybe_move<I>{}(self.m_key)...
                        );
                    }(std::index_sequence_for<Key...>{});
                } else if constexpr (has_cpp<Base>) {
                    static_assert(
                        supports_item_assignment<Base, Value, Key...>,
                        "__setitem__<Self, Value, Key...> is enabled for operands "
                        "whose C++ representations have no viable overload for "
                        "`Self[Key...] = Value`"
                    );
                    unwrap(self) = unwrap(std::forward<Value>(value));

                } else {
                    Base::operator=(std::forward<Value>(value));
                    if (PyObject_SetItem(
                        ptr(self.m_self),
                        ptr(as_object(self.m_key)),
                        ptr(self)
                    )) {
                        Exception::from_python();
                    }
                }
            }
            return self;
        }

    };

}


template <typename Self, typename... Key> requires (__getitem__<Self, Key...>::enable)
decltype(auto) Object::operator[](this Self&& self, Key&&... key) {
    using getitem = __getitem__<Self, Key...>;
    if constexpr (std::derived_from<typename getitem::type, Object>) {
        return impl::Item<Self, Key...>(
            std::forward<Self>(self),
            std::forward<Key>(key)...
        );
    } else {
        static_assert(
            std::is_invocable_r_v<typename getitem::type, getitem, const Self&, Key...>,
            "__getitem__ is specialized to return a C++ value, but the call operator "
            "does not accept the correct arguments.  Check your specialization of "
            "__getitem__ for these types and ensure a call operator is defined that "
            "accepts these arguments."
        );
        return getitem{}(std::forward<Self>(self), std::forward<Key>(key)...);
    }
}


/* Replicates Python's `del` keyword for attribute and item deletion.  Note that the
usage of `del` to dereference naked Python objects is not supported - only those uses
which would translate to a `PyObject_DelAttr()` or `PyObject_DelItem()` are considered
valid. */
template <typename Self, StaticStr Name> requires (__delattr__<Self, Name>::enable)
void del(impl::Attr<Self, Name>&& attr) {
    using delattr = __delattr__<Self, Name>;
    using Return = delattr::type;
    static_assert(
        std::is_void_v<Return>,
        "index deletion operator must return void.  Check your specialization "
        "of __delitem__ for these types and ensure the Return type is set to void."
    );
    if constexpr (impl::has_call_operator<delattr>) {
        delattr{}(std::forward<Self>(attr.m_self));
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
template <typename Self, typename... Key> requires (__delitem__<Self, Key...>::enable)
void del(impl::Item<Self, Key...>&& item) {
    using delitem = __delitem__<Self, Key...>;
    using Return = delitem::type;
    static_assert(
        std::is_void_v<Return>,
        "index deletion operator must return void.  Check your specialization "
        "of __delitem__ for these types and ensure the Return type is set to void."
    );
    if constexpr (sizeof...(Key) == 1) {
        if constexpr (impl::has_call_operator<delitem>) {
            delitem{}(
                std::forward<Self>(item.m_self),
                std::forward<Key>(item.m_key)...
            );
        } else {
            if (PyObject_DelItem(
                ptr(item.m_self),
                ptr(as_object(std::forward<Key>(item.m_key)))...
            )) {
                Exception::from_python();
            }
        }

    } else {
        if constexpr (impl::has_call_operator<delitem>) {
            [&]<size_t... I>(std::index_sequence<I...>) {
                delitem{}(
                    std::forward<Self>(item.m_self),
                    typename impl::Item<Self, Key...>::template maybe_move<I>{}(
                        item.m_key
                    )...
                );
            }(std::index_sequence_for<Key...>{});
        } else {
            if (PyObject_DelItem(
                ptr(item.m_self),
                ptr(as_object(item.m_key))
            )) {
                Exception::from_python();
            }
        }
    }
}


template <impl::lazily_evaluated From, typename To>
    requires (std::convertible_to<impl::lazy_type<From>, To>)
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(From&& item) {
        if constexpr (impl::has_cpp<impl::lazy_type<From>>) {
            return impl::implicit_cast<To>(unwrap(std::forward<From>(item)));
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
            return static_cast<To>(unwrap(item));
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


template <typename Derived, impl::is<Slice> Base>
struct __isinstance__<Derived, Base>                        : Returns<bool> {
    static constexpr bool operator()(Derived obj) {
        if constexpr (impl::dynamic<Derived>) {
            return PySlice_Check(ptr(obj));
        } else {
            return issubclass<Derived, Slice>();
        }
    }
};


template <typename Derived, impl::is<Slice> Base>
struct __issubclass__<Derived, Base>                        : Returns<bool> {
    static consteval bool operator()() {
        return impl::inherits<Derived, Interface<Slice>>;
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
