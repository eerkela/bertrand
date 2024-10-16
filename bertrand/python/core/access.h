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


/////////////////////////
////    ITERATORS    ////
/////////////////////////


template <typename Begin, typename End = void, typename Container = void>
struct Iterator;


template <typename Begin, typename End, typename Container>
struct Interface<Iterator<Begin, End, Container>> : impl::IterTag {
    using begin_t = Begin;
    using end_t = End;
    using container_t = Container;

    decltype(auto) __iter__(this auto&& self);
    decltype(auto) __next__(this auto&& self);
};


template <typename Begin, typename End, typename Container>
struct Interface<Type<Iterator<Begin, End, Container>>> {
    using begin_t = Begin;
    using end_t = End;
    using container_t = Container;

    static decltype(auto) __iter__(auto&& self) { return self.__iter__(); }
    static decltype(auto) __next__(auto&& self) { return self.__next__(); }
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

    template <typename Container>
    struct IterTraits {
        using begin = decltype(std::ranges::begin(std::declval<std::remove_reference_t<Container>&>()));
        using end = decltype(std::ranges::end(std::declval<std::remove_reference_t<Container&>&>()));
        using container = std::remove_reference_t<Container>;
    };

    template <typename Container> requires (std::is_lvalue_reference_v<Container>)
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
template <impl::iterable Container> requires (impl::yields<Container, Object>)
Iterator(Container&&) -> Iterator<
    typename impl::IterTraits<Container>::begin,
    typename impl::IterTraits<Container>::end,
    typename impl::IterTraits<Container>::container
>;


/* Implement the CTAD guide for iterable containers.  The container type may be const,
which will be reflected in the deduced iterator types. */
template <impl::iterable Container> requires (impl::yields<Container, Object>)
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
    {}
    __iter__(Iterator<T>&& self) :
        iter(std::move(self)), curr(reinterpret_steal<T>(nullptr))
    {}

    __iter__(const Iterator<T>& self, bool) : __iter__(self) { ++(*this); }
    __iter__(Iterator<T>&& self, bool) : __iter__(std::move(self)) { ++(*this); }

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

    [[nodiscard]] bool operator==(const __iter__& other) const {
        return ptr(curr) == ptr(other.curr);
    }

    [[nodiscard]] bool operator!=(const __iter__& other) const {
        return ptr(curr) != ptr(other.curr);
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


template <typename Begin, typename End, typename Container>
decltype(auto) Interface<Iterator<Begin, End, Container>>::__iter__(this auto&& self) {
    return std::forward<decltype(self)>(self);
}


template <typename Begin, typename End, typename Container>
decltype(auto) Interface<Iterator<Begin, End, Container>>::__next__(this auto&& self) {
    using Iter = std::remove_reference_t<decltype(self)>;

    if constexpr (std::is_void_v<typename Iter::end_t>) {
        PyObject* next = PyIter_Next(ptr(self));
        if (next == nullptr) {
            if (PyErr_Occurred()) {
                Exception::from_python();
            }
            throw StopIteration();
        }
        return reinterpret_steal<typename Iter::begin_t>(next);

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


/* Begin iteration operator.  Both this and the end iteration operator are
controlled by the __iter__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__iter__<Self>::enable)
[[nodiscard]] decltype(auto) begin(Self&& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, Self, int>) {
        static_assert(
            std::is_constructible_v<__iter__<Self>, Self>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(T&&)` constructor?"
        );
        return __iter__<Self>(std::forward<Self>(self), 0);
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<Self>, Self>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(T&&, int)` constructor?"
        );
        if constexpr (impl::has_cpp<Self>) {
            return std::ranges::begin(unwrap(std::forward<Self>(self)));

        } else {
            auto result = [](Self&& self) {
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
                return __iter__<Iterator<Return>>{
                    reinterpret_steal<Iterator<Return>>(iter),
                    0
                };
            };
            if constexpr (impl::inherits<Self, impl::IterTag>) {
                if constexpr (!std::is_void_v<typename std::remove_reference_t<Self>::end_t>) {
                    return self->begin;
                } else {
                    return result(std::forward<Self>(self));
                }
            } else {
                return result(std::forward<Self>(self));
            }
        }
    }
}


/* Const iteration operator.  Python has no distinction between mutable and
immutable iterators, so this is fundamentally the same as the ordinary begin()
method.  Some libraries assume the existence of this method. */
template <typename Self>
    requires (__iter__<Self>::enable && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] decltype(auto) cbegin(Self&& self) {
    return begin(std::forward<Self>(self));
}


/* End iteration operator.  This terminates the iteration and is controlled by the
__iter__ control struct. */
template <typename Self> requires (__iter__<Self>::enable)
[[nodiscard]] decltype(auto) end(Self&& self) {
    if constexpr (std::is_constructible_v<__iter__<Self>, const Self&>) {
        static_assert(
            std::is_constructible_v<__iter__<Self>, Self, int>,
            "__iter__<T> specializes the begin iterator, but not the end iterator.  "
            "Did you forget to define an `__iter__(const T&)` constructor?"
        );
        return __iter__<Self>(std::forward<Self>(self));
    } else {
        static_assert(
            !std::is_constructible_v<__iter__<Self>, Self, int>,
            "__iter__<T> specializes the end iterator, but not the begin iterator.  "
            "Did you forget to define an `__iter__(const T&, int)` constructor?"
        );
        if constexpr (impl::has_cpp<Self>) {
            return std::ranges::end(unwrap(std::forward<Self>(self)));

        } else {
            auto result = [](Self&& self) {
                using Return = typename __iter__<Self>::type;
                static_assert(
                    std::derived_from<Return, Object>,
                    "iterator must dereference to a subclass of Object.  Check your "
                    "specialization of __iter__ for this types and ensure the Return type "
                    "is a subclass of py::Object."
                );
                return __iter__<Iterator<Return>>{
                    reinterpret_steal<Iterator<Return>>(nullptr)
                };
            };
            if constexpr (impl::inherits<Self, impl::IterTag>) {
                if constexpr (!std::is_void_v<typename std::remove_reference_t<Self>::end_t>) {
                    return self->end;
                } else {
                    return result(std::forward<Self>(self));
                }
            } else {
                return result(std::forward<Self>(self));
            }
        }
    }
}


/* Const end operator.  Similar to `cbegin()`, this is identical to `end()`. */
template <typename Self>
    requires (__iter__<Self>::enable && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] decltype(auto) cend(Self&& self) {
    return end(std::forward<Self>(self));
}


/* Reverse iteration operator.  Both this and the reverse end operator are
controlled by the __reversed__ control struct, whose return type dictates the
iterator's dereference type. */
template <typename Self> requires (__reversed__<Self>::enable)
[[nodiscard]] decltype(auto) rbegin(Self&& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, const Self&, int>) {
        static_assert(
            std::is_constructible_v<__reversed__<Self>, Self>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<Self>(std::forward<Self>(self), 0);
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<Self>, Self>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::has_cpp<Self>) {
            return std::ranges::rbegin(unwrap(std::forward<Self>(self)));

        } else {
            using Return = typename __reversed__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __reversed__ for this types and ensure the Return "
                "type is a subclass of py::Object."
            );
            constexpr StaticStr str = "__reversed__";
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(str, str.size())
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* iter = PyObject_CallMethodNoArgs(
                ptr(self),
                ptr(name)
            );
            if (iter == nullptr) {
                Exception::from_python();
            }
            return __iter__<Iterator<Return>>{
                reinterpret_steal<Iterator<Return>>(iter),
                0
            };
        }
    }
}


/* Const reverse iteration operator.  Python has no distinction between mutable
and immutable iterators, so this is fundamentally the same as the ordinary
rbegin() method.  Some libraries assume the existence of this method. */
template <typename Self>
    requires (__reversed__<Self>::enable && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] decltype(auto) crbegin(Self&& self) {
    return rbegin(std::forward<Self>(self));
}


/* Reverse end operator.  This terminates the reverse iteration and is controlled
by the __reversed__ control struct. */
template <typename Self> requires (__reversed__<Self>::enable)
[[nodiscard]] decltype(auto) rend(Self&& self) {
    if constexpr (std::is_constructible_v<__reversed__<Self>, Self>) {
        static_assert(
            std::is_constructible_v<__reversed__<Self>, Self, int>,
            "__reversed__<T> specializes the begin iterator, but not the end "
            "iterator.  Did you forget to define a `__reversed__(const T&)` "
            "constructor?"
        );
        return __reversed__<Self>(std::forward<Self>(self));
    } else {
        static_assert(
            !std::is_constructible_v<__reversed__<Self>, Self, int>,
            "__reversed__<T> specializes the end iterator, but not the begin "
            "iterator.  Did you forget to define a `__reversed__(const T&, int)` "
            "constructor?"
        );
        if constexpr (impl::has_cpp<Self>) {
            return std::ranges::rend(unwrap(std::forward<Self>(self)));

        } else {
            using Return = typename __reversed__<Self>::type;
            static_assert(
                std::derived_from<Return, Object>,
                "iterator must dereference to a subclass of Object.  Check your "
                "specialization of __reversed__ for this types and ensure the Return "
                "type is a subclass of py::Object."
            );
            return __iter__<Iterator<Return>>{
                reinterpret_steal<Iterator<Return>>(nullptr)
            };
        }
    }
}


/* Const reverse end operator.  Similar to `crbegin()`, this is identical to
`rend()`. */
template <typename Self>
    requires (__reversed__<const Self>::enable && std::is_const_v<std::remove_reference_t<Self>>)
[[nodiscard]] decltype(auto) crend(Self&& self) {
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


/// TODO: maybe argument types are placed here, so they can be reused in structural
/// types?


////////////////////////
////    OPTIONAL    ////
////////////////////////


/// TODO: not sure if type checks are being handled correctly, and it might be a good use
/// case for specialization of `isinstance<>`, etc. based on the derived class rather
/// than (or perhaps in addition to) the base class.  This will require a fair bit of
/// thinking to get right, and it has to be applied to Object and other dynamic types
/// as well.


// template <impl::inherits<impl::OptionalTag> Derived, typename Base>
//     requires (__isinstance__<impl::wrapped_type<Derived>, Base>::enable)
// struct __isinstance__<Derived, Base>                        : Returns<bool> {
//     static constexpr bool operator()(Derived obj) {
//         if (obj->m_value.is(None)) {
//             return impl::is<Base, NoneType>;
//         } else {
//             return isinstance<Base>(
//                 reinterpret_cast<impl::wrapped_type<Derived>>(
//                     std::forward<Derived>(obj)->m_value
//                 )
//             );
//         }
//     }
//     template <typename T = impl::wrapped_type<Derived>>
//         requires (std::is_invocable_v<__isinstance__<T, Base>, T, Base>)
//     static constexpr bool operator()(Derived obj, Base&& base) {
//         if (obj->m_value.is(None)) {
//             return false;  /// TODO: ???
//         } else {
//             return isinstance(
//                 reinterpret_cast<impl::wrapped_type<Derived>>(
//                     std::forward<Derived>(obj)->m_value
//                 ),
//                 std::forward<Base>(base)
//             );
//         }
//     }
// };


// template <typename Derived, impl::inherits<impl::OptionalTag> Base>
//     requires (
//         __issubclass__<Derived, impl::wrapped_type<Base>>::enable &&
//         !impl::inherits<Derived, impl::OptionalTag>
//     )
// struct __isinstance__<Derived, Base>                         : Returns<bool> {
//     static constexpr bool operator()(Derived&& obj) {
//         if constexpr (impl::dynamic<Derived>) {
//             return
//                 obj.is(None) ||
//                 isinstance<impl::wrapped_type<Base>>(std::forward<Derived>(obj));
//         } else {
//             return
//                 impl::none_like<Derived> ||
//                 isinstance<impl::wrapped_type<Base>>(std::forward<Derived>(obj));
//         }
//     }
//     template <typename T = impl::wrapped_type<Base>>
//         requires (std::is_invocable_v<__isinstance__<Derived, T>, Derived, T>)
//     static constexpr bool operator()(Derived&& obj, Base base) {
//         if (base->m_value.is(None)) {
//             return false;  /// TODO: ???
//         } else {
//             return isinstance(
//                 std::forward<Derived>(obj),
//                 reinterpret_cast<impl::wrapped_type<Derived>>(
//                     std::forward<Base>(base)->m_value
//                 )
//             );
//         }
//     }
// };





// template <typename Derived, impl::inherits<impl::OptionalTag> Base>
// struct __issubclass__<Derived, Base>                         : Returns<bool> {
//     using Wrapped = std::remove_reference_t<Base>::__wrapped__;
//     static constexpr bool operator()() {
//         return impl::none_like<Derived> || issubclass<Derived, Wrapped>();
//     }
//     template <typename T = Wrapped>
//         requires (std::is_invocable_v<__issubclass__<Derived, T>, Derived>)
//     static constexpr bool operator()(Derived&& obj) {
//         if constexpr (impl::dynamic<Derived>) {
//             return
//                 obj.is(None) ||
//                 issubclass<Wrapped>(std::forward<Derived>(obj));
//         } else {
//             return
//                 impl::none_like<Derived> ||
//                 issubclass<Wrapped>(std::forward<Derived>(obj));
//         }
//     }
//     template <typename T = Wrapped>
//         requires (std::is_invocable_v<__issubclass__<Derived, T>, Derived, T>)
//     static constexpr bool operator()(Derived&& obj, Base base) {
//         if (base.is(None)) {
//             return false;
//         } else {
//             return issubclass(std::forward<Derived>(obj), base.value());
//         }
//     }
// };



// template <impl::inherits<impl::OptionalTag> L, impl::inherits<impl::OptionalTag> R>
//     requires (__floordiv__<impl::wrapped_type<L>, impl::wrapped_type<R>>::enable)
// struct __floordiv__<L, R> : Returns<Optional<
//     typename __floordiv__<impl::wrapped_type<L>, impl::wrapped_type<R>>::type
// >> {
//     using Return = __floordiv__<impl::wrapped_type<L>, impl::wrapped_type<R>>::type;
//     static Optional<Return> operator()(L lhs, R rhs) {
//         if (lhs->m_value.is(None) || rhs->m_value.is(None)) {
//             return None;
//         } else {
//             return floordiv(
//                 reinterpret_cast<impl::wrapped_type<L>>(
//                     std::forward<L>(lhs)->m_value
//                 ),
//                 reinterpret_cast<impl::wrapped_type<R>>(
//                     std::forward<R>(rhs)->m_value
//                 )
//             );
//         }
//     }
// };


// template <impl::inherits<impl::OptionalTag> L, typename R>
//     requires (
//         !impl::inherits<impl::OptionalTag, R> &&
//         __floordiv__<impl::wrapped_type<L>, R>::enable
//     )
// struct __floordiv__<L, R> : Returns<Optional<
//     typename __floordiv__<impl::wrapped_type<L>, R>::type
// >> {
//     using Return = __floordiv__<impl::wrapped_type<L>, R>::type;
//     static Optional<Return> operator()(L lhs, R rhs) {
//         if (lhs->m_value.is(None)) {
//             return None;
//         } else {
//             return floordiv(
//                 reinterpret_cast<impl::wrapped_type<L>>(
//                     std::forward<L>(lhs)->m_value
//                 ),
//                 std::forward<R>(rhs)
//             );
//         }
//     }
// };


// template <typename L, impl::inherits<impl::OptionalTag> R>
//     requires (
//         !impl::inherits<impl::OptionalTag, L> &&
//         __floordiv__<L, impl::wrapped_type<R>>::enable
//     )
// struct __floordiv__<L, R> : Returns<Optional<
//     typename __floordiv__<L, impl::wrapped_type<R>>::type
// >> {
//     using Return = __floordiv__<L, impl::wrapped_type<R>>::type;
//     static Optional<Return> operator()(L lhs, R rhs) {
//         if (rhs->m_value.is(None)) {
//             return None;
//         } else {
//             return floordiv(
//                 std::forward<L>(lhs),
//                 reinterpret_cast<impl::wrapped_type<R>>(
//                     std::forward<R>(rhs)->m_value
//                 )
//             );
//         }
//     }
// };


// template <impl::inherits<impl::OptionalTag> L, impl::inherits<impl::OptionalTag> R>
//     requires (__ifloordiv__<impl::wrapped_type<L>, impl::wrapped_type<R>>::enable)
// struct __ifloordiv__<L, R> : Returns<L> {
//     static L operator()(L lhs, R rhs) {
//         if (!lhs->m_value.is(None) && !rhs->m_value.is(None)) {
//             ifloordiv(
//                 reinterpret_cast<impl::wrapped_type<L>>(
//                     std::forward<L>(lhs)->m_value
//                 ),
//                 reinterpret_cast<impl::wrapped_type<R>>(
//                     std::forward<R>(rhs)->m_value
//                 )
//             );
//         }
//         return std::forward<L>(lhs);
//     }
// };


// template <impl::inherits<impl::OptionalTag> L, typename R>
//     requires (
//         !impl::inherits<impl::OptionalTag, R> &&
//         __ifloordiv__<impl::wrapped_type<L>, R>::enable
//     )
// struct __ifloordiv__<L, R> : Returns<L> {
//     static L operator()(L lhs, R rhs) {
//         if (!lhs->m_value.is(None)) {
//             ifloordiv(
//                 reinterpret_cast<impl::wrapped_type<L>>(
//                     std::forward<L>(lhs)->m_value
//                 ),
//                 std::forward<R>(rhs)
//             );
//         }
//         return std::forward<L>(lhs);
//     }
// };


/////////////////////
////    UNION    ////
/////////////////////


template <std::derived_from<Object>... Types>
    requires (
        sizeof...(Types) > 0 &&
        impl::types_are_unique<Types...> &&
        !(std::is_const_v<Types> || ...) &&
        !(std::is_volatile_v<Types> || ...)
    )
struct Union;

template <std::derived_from<Object> T = Object>
using Optional = Union<T, NoneType>;

template <impl::has_python T> requires (!std::same_as<T, NoneType>)
Union(T) -> Union<obj<T>, NoneType>;


namespace impl {

    template <typename T>
    constexpr bool _py_union = false;
    template <typename... Types>
    constexpr bool _py_union<Union<Types...>> = true;
    template <typename T>
    concept py_union = _py_union<std::remove_cvref_t<T>>;

    template <typename T, typename>
    struct holds_alternative {
        static constexpr bool enable = false;
        template <typename Self>
        static bool operator()(Self&&) { return false; }
    };
    template <typename T, typename... Types>
    struct holds_alternative<T, Union<Types...>> {
        static constexpr bool enable = (std::same_as<T, Types> || ...);
        template <typename Self>
        static bool operator()(Self&& self) {
            return index_of<T, Types...> == self->m_index;
        }
    };

    template <typename... Ts>
    struct Placeholder {};

    template <typename>
    struct to_union;
    template <typename... Matches>
    struct to_union<Placeholder<Matches...>> {
        // remove exact duplicates
        template <typename value, typename... Ts>
        struct extract { using type = value; };
        template <typename... Ms, typename T, typename... Ts>
        struct extract<Placeholder<Ms...>, T, Ts...> {
            template <typename>
            struct helper { using type = Placeholder<Ms...>; };
            template <typename T2> requires (!(std::same_as<T2, Ts> || ...))
            struct helper<T2> { using type = Placeholder<Ms..., T2>; };
            using type = extract<typename helper<T>::type, Ts...>::type;
        };

        // remove duplicates that differ only in qualifiers, replacing them with a
        // stripped version that forces a copy/move
        template <typename value>
        struct filter;
        template <typename... Ms>
        struct filter<Placeholder<Ms...>> {
            template <typename filtered, typename... Ts>
            struct do_filter { using type = filtered; };
            template <typename... filtered, typename T, typename... Ts>
            struct do_filter<Placeholder<filtered...>, T, Ts...> {
                template <typename>
                struct helper {
                    using type = Placeholder<filtered...>;
                };
                template <typename T2>
                    requires (!(std::same_as<std::remove_cvref_t<T2>, filtered> || ...))
                struct helper<T2> {
                    using type = Placeholder<filtered..., std::conditional_t<
                        (std::same_as<
                            std::remove_cvref_t<T2>,
                            std::remove_cvref_t<Ts>
                        > || ...),
                        std::remove_cvref_t<T2>,
                        T2
                    >>;
                };
                using type = do_filter<typename helper<T>::type, Ts...>::type;
            };
            using type = do_filter<Placeholder<>, Ms...>::type;
        };

        // unwrap singletons with correct reference semantics or return a new union
        template <typename>
        struct convert;
        template <typename M>
        struct convert<Placeholder<M>> { using type = M; };
        template <typename M, typename... Ms>
        struct convert<Placeholder<M, Ms...>> {
            using type = Union<std::remove_cvref_t<M>, std::remove_cvref_t<Ms>...>;
        };

        // apply the above transformations to get the final return type
        using type = convert<
            typename filter<
                typename extract<Placeholder<>, Matches...>::type
            >::type
        >::type;
    };

    /// TODO: converting std::variant to py::Union may need to account for duplicate
    /// types and cv qualifiers and convert everything accordingly.  Perhaps this can
    /// also demote variants to singular Python types if they all resolve to the
    /// same type
    template <typename T>
    struct VariantToUnion { static constexpr bool enable = false; };
    template <std::convertible_to<Object>... Ts>
    struct VariantToUnion<std::variant<Ts...>> {
        static constexpr bool enable = true;
        using type = Union<std::remove_cv_t<python_type<Ts>>...>;

        template <size_t I, typename... Us>
        static constexpr bool _convertible_to = true;
        template <size_t I, typename... Us> requires (I < sizeof...(Ts))
        static constexpr bool _convertible_to<I, Us...> =
            (std::convertible_to<impl::unpack_type<I, Ts...>, Us> || ...) &&
            _convertible_to<I + 1, Us...>;

        template <typename... Us>
        static constexpr bool convertible_to = _convertible_to<0, Us...>;
    };

    template <typename T>
    struct UnionToType { static constexpr bool enable = false; };
    template <typename... Types>
    struct UnionToType<Union<Types...>> {
        static constexpr bool enable = true;

        template <typename Out, typename... Ts>
        static constexpr bool _convertible_to = true;
        template <typename Out, typename T, typename... Ts>
        static constexpr bool _convertible_to<Out, T, Ts...> =
            std::convertible_to<T, Out> &&
            _convertible_to<Out, Ts...>;

        template <typename Out>
        static constexpr bool convertible_to = _convertible_to<Out, Types...>;
    };

    /// TODO: all operators must return python objects except for:
    ///     - __isinstance__ (bool)
    ///     - __issubclass__ (bool)
    ///     - __repr__ (std::string)
    ///     - __setattr__ (void)
    ///     - __delattr__ (void)
    ///     - __setitem__ (void)
    ///     - __delitem__ (void)
    ///     - __len__ (size_t)
    ///     - __contains__ (bool)
    ///     - __hash__ (size_t)

    template <typename, StaticStr>
    struct UnionGetAttr { static constexpr bool enable = false; };
    template <py_union Self, StaticStr Name>
    struct UnionGetAttr<Self, Name> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename... Types>
            requires (__getattr__<qualify_lvalue<Types, Self>, Name>::enable || ...)
        struct traits<Union<Types...>> {
            template <typename result, typename... Ts>
            struct unary { using type = result; };
            template <typename... Matches, typename T, typename... Ts>
            struct unary<Placeholder<Matches...>, T, Ts...> {
                template <typename>
                struct conditional { using type = Placeholder<Matches...>; };
                template <typename T2>
                    requires (__getattr__<qualify_lvalue<T2, Self>, Name>::enable)
                struct conditional<T2> {
                    using type = Placeholder<
                        Matches...,
                        typename __getattr__<qualify_lvalue<T2, Self>, Name>::type
                    >;
                };
                using type = unary<typename conditional<T>::type, Ts...>::type;
            };
            static constexpr bool enable = true;
            using type = to_union<typename unary<Placeholder<>, Types...>::type>::type;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = traits<std::remove_cvref_t<Self>>::type;
    };

    template <typename, StaticStr, typename>
    struct UnionSetAttr { static constexpr bool enable = false; };
    template <py_union Self, StaticStr Name, typename Value>
    struct UnionSetAttr<Self, Name, Value> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
        };
        template <typename... Types>
            requires (__setattr__<qualify_lvalue<Types, Self>, Name, Value>::enable || ...)
        struct traits<Union<Types...>> {
            static constexpr bool enable = true;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = void;
    };

    template <typename, StaticStr>
    struct UnionDelAttr { static constexpr bool enable = false; };
    template <py_union Self, StaticStr Name>
    struct UnionDelAttr<Self, Name> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
        };
        template <typename... Types>
            requires (__delattr__<qualify_lvalue<Types, Self>, Name>::enable || ...)
        struct traits<Union<Types...>> {
            static constexpr bool enable = true;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = void;
    };

    template <typename, typename... Args>
    struct UnionCall { static constexpr bool enable = false; };
    template <py_union Self, typename... Args>
    struct UnionCall<Self, Args...> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename... Types>
            requires (__call__<qualify_lvalue<Types, Self>, Args...>::enable || ...)
        struct traits<Union<Types...>> {
            template <typename result, typename... Ts>
            struct unary { using type = result; };
            template <typename... Matches, typename T, typename... Ts>
            struct unary<Placeholder<Matches...>, T, Ts...> {
                template <typename>
                struct conditional { using type = Placeholder<Matches...>; };
                template <typename T2>
                    requires (__call__<qualify_lvalue<T2, Self>, Args...>::enable)
                struct conditional<T2> {
                    using type = Placeholder<
                        Matches...,
                        typename __call__<qualify_lvalue<T2, Self>, Args...>::type
                    >;
                };
                using type = unary<typename conditional<T>::type, Ts...>::type;
            };
            static constexpr bool enable = true;
            using type = to_union<typename unary<Placeholder<>, Types...>::type>::type;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = traits<std::remove_cvref_t<Self>>::type;
    };

    template <typename, typename... Key>
    struct UnionGetItem { static constexpr bool enable = false; };
    template <py_union Self, typename... Key>
    struct UnionGetItem<Self, Key...> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
            using type = void;
        };
        template <typename... Types>
            requires (__getitem__<qualify_lvalue<Types, Self>, Key...>::enable || ...)
        struct traits<Union<Types...>> {
            template <typename result, typename... Ts>
            struct unary { using type = result; };
            template <typename... Matches, typename T, typename... Ts>
            struct unary<Placeholder<Matches...>, T, Ts...> {
                template <typename>
                struct conditional { using type = Placeholder<Matches...>; };
                template <typename T2>
                    requires (__getitem__<qualify_lvalue<T2, Self>, Key...>::enable)
                struct conditional<T2> {
                    using type = Placeholder<
                        Matches...,
                        typename __getitem__<qualify_lvalue<T2, Self>, Key...>::type
                    >;
                };
                using type = unary<typename conditional<T>::type, Ts...>::type;
            };
            static constexpr bool enable = true;
            using type = to_union<typename unary<Placeholder<>, Types...>::type>::type;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = traits<std::remove_cvref_t<Self>>::type;
    };

    template <typename, typename, typename... Key>
    struct UnionSetItem { static constexpr bool enable = false; };
    template <py_union Self, typename Value, typename... Key>
    struct UnionSetItem<Self, Value, Key...> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
        };
        template <typename... Types>
            requires (__setitem__<qualify_lvalue<Types, Self>, Value, Key...>::enable || ...)
        struct traits<Union<Types...>> {
            static constexpr bool enable = true;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = void;
    };

    template <typename, typename... Key>
    struct UnionDelItem { static constexpr bool enable = false; };
    template <py_union Self, typename... Key>
    struct UnionDelItem<Self, Key...> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
        };
        template <typename... Types>
            requires (__delitem__<qualify_lvalue<Types, Self>, Key...>::enable || ...)
        struct traits<Union<Types...>> {
            static constexpr bool enable = true;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = void;
    };

    template <typename>
    struct UnionLen { static constexpr bool enable = false; };
    template <py_union Self>
    struct UnionLen<Self> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
        };
        template <typename... Types>
            requires (__len__<qualify_lvalue<Types, Self>>::enable || ...)
        struct traits<Union<Types...>> {
            static constexpr bool enable = true;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = size_t;
    };

    template <typename, typename>
    struct UnionContains { static constexpr bool enable = false; };
    template <py_union Self, typename Key>
    struct UnionContains<Self, Key> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
        };
        template <typename... Types>
            requires (__contains__<qualify_lvalue<Types, Self>, Key>::enable || ...)
        struct traits<Union<Types...>> {
            static constexpr bool enable = true;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = bool;
    };

    template <typename>
    struct UnionHash { static constexpr bool enable = false; };
    template <py_union Self>
    struct UnionHash<Self> {
        template <typename>
        struct traits {
            static constexpr bool enable = false;
        };
        template <typename... Types>
            requires (__hash__<qualify_lvalue<Types, Self>>::enable || ...)
        struct traits<Union<Types...>> {
            static constexpr bool enable = true;
        };
        static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
        using type = size_t;
    };

    template <template <typename> typename control>
    struct union_unary_operator {
        template <typename>
        struct op { static constexpr bool enable = false; };
        template <py_union Self>
        struct op<Self> {
            template <typename>
            struct traits {
                static constexpr bool enable = false;
                using type = void;
            };
            template <typename... Types>
                requires (control<qualify_lvalue<Types, Self>>::enable || ...)
            struct traits<Union<Types...>> {
                template <typename result, typename... Ts>
                struct unary { using type = result; };
                template <typename... Matches, typename T, typename... Ts>
                struct unary<Placeholder<Matches...>, T, Ts...> {
                    template <typename>
                    struct conditional { using type = Placeholder<Matches...>; };
                    template <typename T2>
                        requires (control<qualify_lvalue<T2, Self>>::enable)
                    struct conditional<T2> {
                        using type = Placeholder<
                            Matches...,
                            typename control<qualify_lvalue<T2, Self>>::type
                        >;
                    };
                    using type = unary<typename conditional<T>::type, Ts...>::type;
                };
                static constexpr bool enable = true;
                using type = to_union<typename unary<Placeholder<>, Types...>::type>::type;
            };
            static constexpr bool enable = traits<std::remove_cvref_t<Self>>::enable;
            using type = traits<std::remove_cvref_t<Self>>::type;

            template <typename>
            struct call;
            template <typename... Types>
            struct call<Union<Types...>> {
                template <size_t I>
                static constexpr bool in_range = I < sizeof...(Types);

                template <size_t I, typename OnSuccess, typename OnFailure>
                static type exec(
                    Self self,
                    OnSuccess&& success,
                    OnFailure&& failure
                ) {
                    using S = qualify_lvalue<unpack_type<I, Types...>, Self>;
                    if constexpr (control<S>::enable) {
                        return success(reinterpret_cast<S>(
                            std::forward<Self>(self)->m_value
                        ));
                    } else {
                        return failure(reinterpret_cast<S>(
                            std::forward<Self>(self)->m_value
                        ));
                    }
                }
            };

            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                Self self,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                if (I == self->m_index) {
                    return call<std::remove_cvref_t<Self>>::template exec<I>(
                        std::forward<Self>(self),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                }
                if constexpr (call<std::remove_cvref_t<Self>>::template in_range<I + 1>) {
                    return exec<I + 1>(
                        std::forward<Self>(self),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                } else {
                    return failure(std::forward<Self>(self));
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                Self self,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                return exec<0>(
                    std::forward<Self>(self),
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure)
                );
            }
        };
    };

    template <typename T>
    using UnionIter = union_unary_operator<__iter__>::template op<T>;
    template <typename T>
    using UnionReversed = union_unary_operator<__reversed__>::template op<T>;
    template <typename T>
    using UnionAbs = union_unary_operator<__abs__>::template op<T>;
    template <typename T>
    using UnionInvert = union_unary_operator<__invert__>::template op<T>;
    template <typename T>
    using UnionPos = union_unary_operator<__pos__>::template op<T>;
    template <typename T>
    using UnionNeg = union_unary_operator<__neg__>::template op<T>;

    template <template <typename> typename control>
    struct union_unary_inplace_operator {
        template <typename>
        struct op { static constexpr bool enable = false; };
        template <py_union Self>
        struct op<Self> {
            template <typename>
            static constexpr bool match = false;
            template <typename... Types>
                requires (control<qualify_lvalue<Types, Self>>::enable || ...)
            static constexpr bool match<Union<Types...>> = true;
            static constexpr bool enable = match<std::remove_cvref_t<Self>>;
            using type = Self;

            template <typename>
            struct call;
            template <typename... Types>
            struct call<Union<Types...>> {
                template <size_t I>
                static constexpr bool in_range = I < sizeof...(Types);

                template <size_t I, typename OnSuccess, typename OnFailure>
                static type exec(
                    Self self,
                    OnSuccess&& success,
                    OnFailure&& failure
                ) {
                    using S = qualify_lvalue<unpack_type<I, Types...>, Self>;
                    if constexpr (control<S>::enable) {
                        success(reinterpret_cast<S>(
                            std::forward<Self>(self)->m_value
                        ));
                    } else {
                        failure(reinterpret_cast<S>(
                            std::forward<Self>(self)->m_value
                        ));
                    }
                    return self;
                }
            };

            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                Self self,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                if (I == self->m_index) {
                    return call<std::remove_cvref_t<Self>>::template exec<I>(
                        std::forward<Self>(self),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                }
                if constexpr (call<std::remove_cvref_t<Self>>::template in_range<I + 1>) {
                    return exec<I + 1>(
                        std::forward<Self>(self),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                } else {
                    failure(std::forward<Self>(self));
                    return std::forward<Self>(self);
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                Self self,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                return exec<0>(
                    std::forward<Self>(self),
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure)
                );
            }
        };
    };

    template <typename T>
    using UnionIncrement = union_unary_inplace_operator<__increment__>::template op<T>;
    template <typename T>
    using UnionDecrement = union_unary_inplace_operator<__decrement__>::template op<T>;

    template <template <typename, typename> typename control>
    struct union_binary_operator {
        template <typename, typename>
        struct op { static constexpr bool enable = false; };
        template <py_union L, py_union R>
        struct op<L, R> {
            template <typename L2, typename... R2s>
            struct any_match { static constexpr bool enable = false; };
            template <typename L2, typename R2, typename... R2s>
            struct any_match<L2, R2, R2s...> {
                static constexpr bool enable =
                    control<qualify_lvalue<L2, L>, qualify_lvalue<R2, R>>::enable ||
                    any_match<L2, R2s...>::enable;
            };
            template <typename, typename>
            struct traits {
                static constexpr bool enable = false;
                using type = void;
            };
            template <typename... Ls, typename... Rs>
                requires (any_match<Ls, Rs...>::enable || ...)
            struct traits<Union<Ls...>, Union<Rs...>> {
                template <typename result, typename... L2s>
                struct left { using type = result; };
                template <typename... Matches, typename L2, typename... L2s>
                struct left<Placeholder<Matches...>, L2, L2s...> {
                    template <typename result, typename L3, typename... R2s>
                    struct right { using type = result; };
                    template <typename... Ms, typename L3, typename R2, typename... R2s>
                    struct right<Placeholder<Ms...>, L3, R2, R2s...> {
                        template <typename>
                        struct conditional { using type = Placeholder<Ms...>; };
                        template <typename R3>
                            requires (control<qualify_lvalue<L3, L>, qualify_lvalue<R3, R>>::enable)
                        struct conditional<R3> {
                            using type = Placeholder<
                                Ms...,
                                typename control<qualify_lvalue<L3, L>, qualify_lvalue<R3, R>>::type
                            >;
                        };
                        using type = right<
                            typename conditional<R2>::type,
                            L3,
                            R2s...
                        >::type;
                    };
                    using type = left<
                        typename right<Placeholder<Matches...>, L2, Rs...>::type,
                        L2s...
                    >::type;
                };
                static constexpr bool enable = true;
                using type = to_union<typename left<Placeholder<>, Ls...>::type>::type;
            };
            static constexpr bool enable = traits<
                std::remove_cvref_t<L>,
                std::remove_cvref_t<R>
            >::enable;
            using type = traits<
                std::remove_cvref_t<L>,
                std::remove_cvref_t<R>
            >::type;

            template <typename, typename>
            struct call;
            template <typename... Ls, typename... Rs>
            struct call<Union<Ls...>, Union<Rs...>> {
                template <size_t I, size_t J>
                static constexpr bool in_range =
                    (I < sizeof...(Ls) && J < sizeof...(Rs));

                template <size_t I, size_t J, typename OnSuccess, typename OnFailure>
                static type exec(
                    L lhs,
                    R rhs,
                    OnSuccess&& success,
                    OnFailure&& failure
                ) {
                    using L2 = qualify_lvalue<unpack_type<I, Ls...>, L>;
                    using R2 = qualify_lvalue<unpack_type<J, Rs...>, R>;
                    if constexpr (control<L2, R2>::enable) {
                        return success(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    } else {
                        return failure(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    }
                }
            };

            template <size_t I, size_t J, typename OnSuccess, typename OnFailure>
            static type exec(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                if (I == lhs->m_index) {
                    if (J == rhs->m_index) {
                        return call<
                            std::remove_cvref_t<L>,
                            std::remove_cvref_t<R>
                        >::template exec<I, J>(
                            std::forward<L>(lhs),
                            std::forward<R>(rhs),
                            std::forward<OnSuccess>(success),
                            std::forward<OnFailure>(failure)
                        );
                    }
                    if constexpr (call<
                        std::remove_cvref_t<L>,
                        std::remove_cvref_t<R>
                    >::template in_range<I, J + 1>) {
                        return exec<I, J + 1>(
                            std::forward<L>(lhs),
                            std::forward<R>(rhs),
                            std::forward<OnSuccess>(success),
                            std::forward<OnFailure>(failure)
                        );
                    } else {
                        return failure(std::forward<L>(lhs), std::forward<R>(rhs));
                    }
                }
                if constexpr (call<
                    std::remove_cvref_t<L>,
                    std::remove_cvref_t<R>
                >::template in_range<I + 1, J>) {
                    return exec<I + 1, J>(
                        std::forward<L>(lhs),
                        std::forward<R>(rhs),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                } else {
                    return failure(std::forward<L>(lhs), std::forward<R>(rhs));
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                return exec<0, 0>(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs),
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure)
                );
            }
        };
        template <py_union L, typename R> requires (!py_union<R>)
        struct op<L, R> {
            template <typename>
            struct traits {
                static constexpr bool enable = false;
                using type = void;
            };
            template <typename... Ls>
                requires (control<qualify_lvalue<Ls, L>, R>::enable || ...)
            struct traits<Union<Ls...>> {
                template <typename result, typename... L2s>
                struct left { using type = result; };
                template <typename... Matches, typename L2, typename... L2s>
                struct left<Placeholder<Matches...>, L2, L2s...> {
                    template <typename>
                    struct conditional { using type = Placeholder<Matches...>; };
                    template <typename L3>
                        requires (control<qualify_lvalue<L3, L>, R>::enable)
                    struct conditional<L3> {
                        using type = Placeholder<
                            Matches...,
                            typename control<qualify_lvalue<L3, L>, R>::type
                        >;
                    };
                    using type = left<typename conditional<L2>::type, L2s...>::type;
                };
                static constexpr bool enable = true;
                using type = to_union<typename left<Placeholder<>, Ls...>::type>::type;
            };
            static constexpr bool enable = traits<std::remove_cvref_t<L>>::enable;
            using type = traits<std::remove_cvref_t<L>>::type;

            template <typename>
            struct call;
            template <typename... Ls>
            struct call<Union<Ls...>> {
                template <size_t I>
                static constexpr bool in_range = I < sizeof...(Ls);

                template <size_t I, typename OnSuccess, typename OnFailure>
                static type exec(
                    L lhs,
                    R rhs,
                    OnSuccess&& success,
                    OnFailure&& failure
                ) {
                    using L2 = qualify_lvalue<unpack_type<I, Ls...>, L>;
                    if constexpr (control<L2, R>::enable) {
                        return success(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            std::forward<R>(rhs)
                        );
                    } else {
                        return failure(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            std::forward<R>(rhs)
                        );
                    }
                }
            };

            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                if (I == lhs->m_index) {
                    return call<std::remove_cvref_t<L>>::template exec<I>(
                        std::forward<L>(lhs),
                        std::forward<R>(rhs),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                }
                if constexpr (call<std::remove_cvref_t<L>>::template in_range<I + 1>) {
                    return exec<I + 1>(
                        std::forward<L>(lhs),
                        std::forward<R>(rhs),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                } else {
                    return failure(std::forward<L>(lhs), std::forward<R>(rhs));
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                return exec<0>(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs),
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure)
                );
            }
        };
        template <typename L, py_union R> requires (!py_union<L>)
        struct op<L, R> {
            template <typename>
            struct traits {
                static constexpr bool enable = false;
                using type = void;
            };
            template <typename... Rs>
                requires (control<L, qualify_lvalue<Rs, R>>::enable || ...)
            struct traits<Union<Rs...>> {
                template <typename result, typename... R2s>
                struct right { using type = result; };
                template <typename... Matches, typename R2, typename... R2s>
                struct right<Placeholder<Matches...>, R2, R2s...> {
                    template <typename>
                    struct conditional { using type = Placeholder<Matches...>; };
                    template <typename R3>
                        requires (control<L, qualify_lvalue<R3, R>>::enable)
                    struct conditional<R3> {
                        using type = Placeholder<
                            Matches...,
                            typename control<L, qualify_lvalue<R3, R>>::type
                        >;
                    };
                    using type = right<typename conditional<R2>::type, R2s...>::type;
                };
                static constexpr bool enable = true;
                using type = to_union<typename right<Placeholder<>, Rs...>::type>::type;
            };
            static constexpr bool enable = traits<std::remove_cvref_t<R>>::enable;
            using type = traits<std::remove_cvref_t<R>>::type;

            template <typename>
            struct call;
            template <typename... Rs>
            struct call<Union<Rs...>> {
                template <size_t J>
                static constexpr bool in_range = J < sizeof...(Rs);

                template <size_t J, typename OnSuccess, typename OnFailure>
                static type exec(
                    L lhs,
                    R rhs,
                    OnSuccess&& success,
                    OnFailure&& failure
                ) {
                    using R2 = qualify_lvalue<unpack_type<J, Rs...>, R>;
                    if constexpr (control<L, R2>::enable) {
                        return success(
                            std::forward<L>(lhs),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    } else {
                        return failure(
                            std::forward<L>(lhs),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    }
                }
            };

            template <size_t J, typename OnSuccess, typename OnFailure>
            static type exec(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                if (J == rhs->m_index) {
                    return call<std::remove_cvref_t<L>>::template exec<J>(
                        std::forward<L>(lhs),
                        std::forward<R>(rhs),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                }
                if constexpr (call<std::remove_cvref_t<L>>::template in_range<J + 1>) {
                    return exec<J + 1>(
                        std::forward<L>(lhs),
                        std::forward<R>(rhs),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                } else {
                    return failure(std::forward<L>(lhs), std::forward<R>(rhs));
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                return exec<0>(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs),
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure)
                );
            }
        };
    };

    template <typename L, typename R>
    using UnionLess = union_binary_operator<__lt__>::template op<L, R>;
    template <typename L, typename R>
    using UnionLessEqual = union_binary_operator<__le__>::template op<L, R>;
    template <typename L, typename R>
    using UnionEqual = union_binary_operator<__eq__>::template op<L, R>;
    template <typename L, typename R>
    using UnionNotEqual = union_binary_operator<__ne__>::template op<L, R>;
    template <typename L, typename R>
    using UnionGreaterEqual = union_binary_operator<__ge__>::template op<L, R>;
    template <typename L, typename R>
    using UnionGreater = union_binary_operator<__gt__>::template op<L, R>;
    template <typename L, typename R>
    using UnionAdd = union_binary_operator<__add__>::template op<L, R>;
    template <typename L, typename R>
    using UnionSub = union_binary_operator<__sub__>::template op<L, R>;
    template <typename L, typename R>
    using UnionMul = union_binary_operator<__mul__>::template op<L, R>;
    template <typename L, typename R>
    using UnionTrueDiv = union_binary_operator<__truediv__>::template op<L, R>;
    template <typename L, typename R>
    using UnionFloorDiv = union_binary_operator<__floordiv__>::template op<L, R>;
    template <typename L, typename R>
    using UnionMod = union_binary_operator<__mod__>::template op<L, R>;
    // template <typename L, typename R>
    // using UnionPow = union_binary_operator<__pow__>::template op<L, R>;
    template <typename L, typename R>
    using UnionLShift = union_binary_operator<__lshift__>::template op<L, R>;
    template <typename L, typename R>
    using UnionRShift = union_binary_operator<__rshift__>::template op<L, R>;
    template <typename L, typename R>
    using UnionAnd = union_binary_operator<__and__>::template op<L, R>;
    template <typename L, typename R>
    using UnionXor = union_binary_operator<__xor__>::template op<L, R>;
    template <typename L, typename R>
    using UnionOr = union_binary_operator<__or__>::template op<L, R>;

    template <template <typename, typename> typename control>
    struct union_inplace_binary_operator {
        template <typename, typename>
        struct op { static constexpr bool enable = false; };
        template <py_union L, py_union R>
        struct op<L, R> {
            template <typename, typename...>
            struct any_match { static constexpr bool enable = false; };
            template <typename L2, typename R2, typename... R2s>
            struct any_match<L2, R2, R2s...> {
                static constexpr bool enable =
                    control<qualify_lvalue<L2, L>, qualify_lvalue<R2, R>>::enable ||
                    any_match<L2, R2s...>::enable;
            };
            template <typename, typename>
            static constexpr bool match = false;
            template <typename... Ls, typename... Rs>
                requires (any_match<Ls, Rs...>::enable || ...)
            static constexpr bool match<Union<Ls...>, Union<Rs...>> = true;
            static constexpr bool enable = match<
                std::remove_cvref_t<L>,
                std::remove_cvref_t<R>
            >;
            using type = L;

            template <typename, typename>
            struct call;
            template <typename... Ls, typename... Rs>
            struct call<Union<Ls...>, Union<Rs...>> {
                template <size_t I, size_t J>
                static constexpr bool in_range =
                    (I < sizeof...(Ls) && J < sizeof...(Rs));

                template <size_t I, size_t J, typename OnSuccess, typename OnFailure>
                static type exec(
                    L lhs,
                    R rhs,
                    OnSuccess&& success,
                    OnFailure&& failure
                ) {
                    using L2 = qualify_lvalue<unpack_type<I, Ls...>, L>;
                    using R2 = qualify_lvalue<unpack_type<J, Rs...>, R>;
                    if constexpr (control<L2, R2>::enable) {
                        success(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    } else {
                        failure(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    }
                    return std::forward<L>(lhs);
                }
            };

            template <size_t I, size_t J, typename OnSuccess, typename OnFailure>
            static type exec(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                if (I == lhs->m_index) {
                    if (J == rhs->m_index) {
                        return call<
                            std::remove_cvref_t<L>,
                            std::remove_cvref_t<R>
                        >::template exec<I, J>(
                            std::forward<L>(lhs),
                            std::forward<R>(rhs),
                            std::forward<OnSuccess>(success),
                            std::forward<OnFailure>(failure)
                        );
                    }
                    if constexpr (call<
                        std::remove_cvref_t<L>,
                        std::remove_cvref_t<R>
                    >::template in_range<I, J + 1>) {
                        return exec<I, J + 1>(
                            std::forward<L>(lhs),
                            std::forward<R>(rhs),
                            std::forward<OnSuccess>(success),
                            std::forward<OnFailure>(failure)
                        );
                    } else {
                        failure(std::forward<L>(lhs), std::forward<R>(rhs));
                    }
                }
                if constexpr (call<
                    std::remove_cvref_t<L>,
                    std::remove_cvref_t<R>
                >::template in_range<I + 1, J>) {
                    return exec<I + 1, J>(
                        std::forward<L>(lhs),
                        std::forward<R>(rhs),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                } else {
                    failure(std::forward<L>(lhs), std::forward<R>(rhs));
                    return std::forward<L>(lhs);
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                return exec<0, 0>(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs),
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure)
                );
            }
        };
        template <py_union L, typename R> requires (!py_union<R>)
        struct op<L, R> {
            template <typename>
            static constexpr bool match = false;
            template <typename... Ls>
                requires (control<qualify_lvalue<Ls, L>, R>::enable || ...)
            static constexpr bool match<Union<Ls...>> = true;
            static constexpr bool enable = match<std::remove_cvref_t<L>>;
            using type = L;

            template <typename>
            struct call;
            template <typename... Ls>
            struct call<Union<Ls...>> {
                template <size_t I>
                static constexpr bool in_range = I < sizeof...(Ls);

                template <size_t I, typename OnSuccess, typename OnFailure>
                static type exec(
                    L lhs,
                    R rhs,
                    OnSuccess&& success,
                    OnFailure&& failure
                ) {
                    using L2 = qualify_lvalue<unpack_type<I, Ls...>, L>;
                    if constexpr (control<L2, R>::enable) {
                        success(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            std::forward<R>(rhs)
                        );
                    } else {
                        failure(
                            reinterpret_cast<L2>(std::forward<L>(lhs)->m_value),
                            std::forward<R>(rhs)
                        );
                    }
                    return std::forward<L>(lhs);
                }
            };

            template <size_t I, typename OnSuccess, typename OnFailure>
            static type exec(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                if (I == lhs->m_index) {
                    return call<std::remove_cvref_t<L>>::template exec<I>(
                        std::forward<L>(lhs),
                        std::forward<R>(rhs),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                }
                if constexpr (call<std::remove_cvref_t<L>>::template in_range<I + 1>) {
                    return exec<I + 1>(
                        std::forward<L>(lhs),
                        std::forward<R>(rhs),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                } else {
                    failure(std::forward<L>(lhs), std::forward<R>(rhs));
                    return std::forward<L>(lhs);
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                return exec<0>(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs),
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure)
                );
            }
        };
        template <typename L, py_union R> requires (!py_union<L>)
        struct op<L, R> {
            template <typename>
            static constexpr bool match = false;
            template <typename... Rs>
                requires (control<L, qualify_lvalue<Rs, R>>::enable || ...)
            static constexpr bool match<Union<Rs...>> = true;
            static constexpr bool enable = match<std::remove_cvref_t<R>>;
            using type = L;

            template <typename>
            struct call;
            template <typename... Rs>
            struct call<Union<Rs...>> {
                template <size_t J>
                static constexpr bool in_range = J < sizeof...(Rs);

                template <size_t J, typename OnSuccess, typename OnFailure>
                static type exec(
                    L lhs,
                    R rhs,
                    OnSuccess&& success,
                    OnFailure&& failure
                ) {
                    using R2 = qualify_lvalue<unpack_type<J, Rs...>, R>;
                    if constexpr (control<L, R2>::enable) {
                        success(
                            std::forward<L>(lhs),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    } else {
                        failure(
                            std::forward<L>(lhs),
                            reinterpret_cast<R2>(std::forward<R>(rhs)->m_value)
                        );
                    }
                    return std::forward<L>(lhs);
                }
            };

            template <size_t J, typename OnSuccess, typename OnFailure>
            static type exec(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                if (J == lhs->m_index) {
                    return call<std::remove_cvref_t<L>>::template exec<J>(
                        std::forward<L>(lhs),
                        std::forward<R>(rhs),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                }
                if constexpr (call<std::remove_cvref_t<L>>::template in_range<J + 1>) {
                    return exec<J + 1>(
                        std::forward<L>(lhs),
                        std::forward<R>(rhs),
                        std::forward<OnSuccess>(success),
                        std::forward<OnFailure>(failure)
                    );
                } else {
                    failure(std::forward<L>(lhs), std::forward<R>(rhs));
                    return std::forward<L>(lhs);
                }
            }

            template <typename OnSuccess, typename OnFailure>
            static type operator()(
                L lhs,
                R rhs,
                OnSuccess&& success,
                OnFailure&& failure
            ) {
                return exec<0>(
                    std::forward<L>(lhs),
                    std::forward<R>(rhs),
                    std::forward<OnSuccess>(success),
                    std::forward<OnFailure>(failure)
                );
            }
        };
    };

    template <typename L, typename R>
    using UnionInplaceAdd = union_inplace_binary_operator<__iadd__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceSub = union_inplace_binary_operator<__isub__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceMul = union_inplace_binary_operator<__imul__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceTrueDiv = union_inplace_binary_operator<__itruediv__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceFloorDiv = union_inplace_binary_operator<__ifloordiv__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceMod = union_inplace_binary_operator<__imod__>::template op<L, R>;
    // template <typename L, typename R>
    // using UnionInplacePow = union_inplace_binary_operator<__ipow__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceLShift = union_inplace_binary_operator<__ilshift__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceRShift = union_inplace_binary_operator<__irshift__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceAnd = union_inplace_binary_operator<__iand__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceXor = union_inplace_binary_operator<__ixor__>::template op<L, R>;
    template <typename L, typename R>
    using UnionInplaceOr = union_inplace_binary_operator<__ior__>::template op<L, R>;

    /// TODO: ternary operator support (__pow__, __ipow__)

    /// TODO: I may need a way to generalize to n-ary operators in order to account for
    /// __call__, __getitem__, etc.  That would mean that calling a function with a
    /// union that is not directly convertible to the expected type would return
    /// another union (or a single type if it collapses down to one).

}


template <typename... Types>
struct Interface<Union<Types...>> : impl::UnionTag {};
template <typename... Types>
struct Interface<Type<Union<Types...>>> : impl::UnionTag {};


template <std::derived_from<Object>... Types>
    requires (
        sizeof...(Types) > 0 &&
        impl::types_are_unique<Types...> &&
        !(std::is_const_v<Types> || ...) &&
        !(std::is_volatile_v<Types> || ...)
    )
struct Union : Object, Interface<Union<Types...>> {
    struct __python__ : def<__python__, Union>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A simple union type in Python, similar to `std::variant` in C++.

Notes
-----
Due to its dynamic nature, all Python objects can technically be unions by
default, just not in a type-safe manner, and not in a way that can be easily
translated into C++.  This class is meant to provide a more structured way of
binding multiple types to a single object, allowing Python variables and
functions that are annotated with `Union[T1, T2, ...]` or `T1 | T2 | ...`
to have those same semantics reflected in C++ and enforced at compile time.

Note that due to the presence of the `Optional[]` type, the union will not
normally contain `None` as a valid member, and will instead be modeled as a
nested optional type to represent this case.  So, a Python annotation of
`T1 | T2 | None` will be transformed into `Optional[Union[T1, T2]]` when
exposed to C++.  It is still possible to include `None` as an explicit member
of the union, but doing so requires the user to manually request a matching
template (i.e. `Union[T1, T2, NoneType]`) in either language.  Bertrand will
not generate this form of union in any other case.

Additionally, `Union[...]` types are automatically produced whenever a bertrand
type is used with `|` syntax in Python, and the resulting union type can be
used in Python-level `isinstance()` and `issubclass()` checks as if it were a
tuple of its constituent types.

Examples
--------
>>> from bertrand import Union
>>> x = Union[int, str](42)
>>> x
42
>>> x + 15
57
>>> x = Union[int, str]("hello")
>>> x
'hello'
>>> x.capitalize()
'Hello'
>>> x = Union[int, str](True)  # bool is a subclass of int
>>> x
True
>>> x.capitalize()
Traceback (most recent call last):
    ...
AttributeError: 'bool' object has no attribute 'capitalize'
>>> x = Union[int, str](1+2j)  # complex is not a member of the union
Traceback (most recent call last):
    ...
TypeError: cannot convert complex to Union[int, str]

Just like other Bertrand types, unions are type-safe and usable in both
languages with the same interface and semantics.  This allows for seamless
interoperability across the language barrier, and ensures that the same code
can be used in both contexts.

```
export module example;
export import :bertrand;

export namespace example {

    py::Union<py::Int, py::Str> foo(py::Int x, py::Str y) {
        if (y % 2) {
            return x;
        } else {
            return y;
        }
    }

    py::Str bar(py::Union<py::Int, py::Str> x) {
        if (std::holds_alternative<py::Int>(x)) {
            return "int";
        } else {
            return "str";
        }
    }

}
```

>>> import example
>>> example.foo(0, "hello")
'hello'
>>> example.foo(1, "hello")
1
>>> example.bar(0)
'int'
>>> example.bar("hello")
'str'
>>> example.bar(1.0)
Traceback (most recent call last):
    ...
TypeError: cannot convert float to Union[int, str]

Additionally, Python functions that accept or return unions using Python-style
type hints will automatically be translated into C++ functions using the
corresponding `py::Union<...>` types when bindings are generated.  Note that
due to the interaction between unions and optionals, a `None` type hint as a
member of the union in Python syntax will be transformed into a nested optional
type in C++, as described above.

```
#example.py

def foo(x: int | str) -> str:
    if isinstance(x, int):
        return "int"
    else:
        return "str"

def bar(x: int | None = None) -> str:
    if x is None:
        return "none"
    else:
        return "int"

def baz(x: int | str | None = None) -> str:
    if x is None:
        return "none"
    elif isinstance(x, int):
        return "int"
    else:
        return "str"
```

```
import example;

int main() {
    using namespace py;

    // Str(*example::foo)(Arg<"x", Union<Int, Str>>)
    example::foo(0);        // "int"
    example::foo("hello");  // "str"

    // Str(*example::bar)(Arg<"x", Optional<Int>>::opt)
    example::bar();         // "none"
    example::bar(None);     // "none"
    example::bar(0);        // "int"

    // Str(*example::baz)(Arg<"x", Optional<Union<Int, Str>>>::opt)
    example::baz();         // "none"
    example::baz(None);     // "none"
    example::baz(0);        // "int"
    example::baz("hello");  // "str"
}
```)doc";

    struct TODO {
        static constexpr StaticStr __doc__ =
R"doc(A monadic optional type in Python, similar to `std::optional` in C++.

Notes
-----
Due to its dynamic nature, all Python objects can technically be optional by
default, just not in a type-safe manner, and not in a way that can be easily
translated into C++.  This class fixes that, allowing Python variables and
functions that are annotated with `Optional[T]` or `T | None` types to have
those same semantics reflected in C++ and enforced at compile time.

Additionally, this class allows some C++ types that have no direct Python
equivalent, such as pointers (both smart and dumb) and `std::optional` to be
translated into Python syntax when used as function arguments or return values.
Null pointers and empty optionals are thus translated to `None` when passed
up to Python, and vice versa.

Examples
--------
>>> from bertrand import Optional
>>> x = Optional[int]()  # default-initializes to None
>>> x
None
>>> x.has_value()
False
>>> x.value()
Traceback (most recent call last):
    ...
TypeError: optional is empty
>>> x.value_or(42)
42
>>> x -= 15  # optionals are monads
>>> x
None
>>> x = Optional(42)  # CTAD deduces to Optional[int] in this case
>>> x.has_value()
True
>>> x.value()
42
>>> x.value_or(15)
42
>>> x += 15
>>> x
57
>>> x = Optional[str](42)
Traceback (most recent call last):
    ...
TypeError: cannot convert 'int' to 'str'

Just like other Bertrand types, optionals are type-safe and usable in both
languages with the same interface and semantics.  This allows for seamless
interoperability across the language barrier, and ensures that the same code
can be used in both contexts.

```
export module example;
export import :bertrand;

export namespace example {

    py::Optional<py::Float> divide(py::Int a, py::Int b) {
        if (b) {
            return a / b;
        }
        return None;
    }

    py::List<py::Str> split(py::Optional<py::Str> text = py::None) {
        if (text.has_value()) {
            return text.value().split();
        }
        return {};
    }

}
```

>>> import example
>>> example.divide(10, 4)
2.5
>>> example.divide(10, 0)
None
>>> example.split("hello, world")
['hello,', 'world']
>>> example.split()
[]

Additionally, Python functions that accept or return optionals using
Python-style type hints will automatically be translated into C++ functions
using the corresponding `py::Optional<T>` types when bindings are generated.

```
# example.py

def foo(name: str | None = None) -> None:
    print(f"Hello, {'World' if name is None else name}!")

def bar(x: int) -> int | None:
    if x % 2:
        return None
    return x // 2
```

```
import example;

int main() {
    example::foo();  // Hello, World!
    example::foo("Bertrand");  // Hello, Bertrand!
    py::Int x = example::bar(4).value();  // 2
    py::Optional<py::Int> y = example::bar(5);  // None
}
```

Lastly, optionals can also be translated to and from pointers and
`std::optional` types at the C++ level, allowing such types to be used from
Python without any additional work.

```
export module example;
export import :bertrand;

export namespace example {

    std::optional<int> foo(std::optional<int> x) {
        if (x.has_value()) {
            return x.value() * x.value();
        }
        return std::nullopt;
    }


    std::unique_ptr<int> bar(std::unique_ptr<int> x) {
        if (x == nullptr) {
            return nullptr;
        }
        ++(*x);
        return x;
    }

}
```

>>> import example
>>> example.foo(9)
81
>>> example.foo(None)  # None is converted to/from std::nullopt
None
>>> example.bar(10)
11
>>> example.bar(None)  # None is converted to/from nullptr
None
)doc";
    };

    private:

        template <typename Cls, typename... Ts>
        static constexpr size_t match_idx = 0;
        template <typename Cls, typename T, typename... Ts>
        static constexpr size_t match_idx<Cls, T, Ts...> =
            std::same_as<std::remove_cvref_t<Cls>, T> ? 0 : match_idx<Cls, Ts...> + 1;

        template <typename T>
        static constexpr bool is_match =
            match_idx<T, Types...> < sizeof...(Types);

        template <typename Cls, typename... Ts>
        static constexpr size_t convertible_idx = 0;
        template <typename Cls, typename T, typename... Ts>
        static constexpr size_t convertible_idx<Cls, T, Ts...> =
            std::convertible_to<Cls, T> ? 0 : convertible_idx<Cls, Ts...> + 1;

        template <typename T>
        static constexpr bool is_convertible =
            convertible_idx<T, Types...> < sizeof...(Types);

        template <typename Cls, typename... Ts>
        static constexpr size_t constructible_idx = 0;
        template <typename Cls, typename T, typename... Ts>
        static constexpr size_t constructible_idx<Cls, T, Ts...> =
            std::constructible_from<Cls, T> ? 0 : constructible_idx<Cls, Ts...> + 1;

        template <typename T>
        static constexpr bool is_constructible =
            constructible_idx<T, Types...> < sizeof...(Types);

        template <typename T>
        struct ctor {
            static constexpr bool enable = false;
        };
        template <typename T> requires (is_match<T>)
        struct ctor<T> {
            static constexpr bool enable = true;
            static constexpr size_t idx = match_idx<T, Types...>;
            using type = impl::unpack_type<idx, Types...>;
        };
        template <typename T> requires (!is_match<T> && is_convertible<T>)
        struct ctor<T> {
            static constexpr bool enable = true;
            static constexpr size_t idx = convertible_idx<T, Types...>;
            using type = impl::unpack_type<idx, Types...>;
        };
        template <typename T>
            requires (!is_match<T> && !is_convertible<T> && is_constructible<T>)
        struct ctor<T> {
            static constexpr bool enable = true;
            static constexpr size_t idx = constructible_idx<T, Types...>;
            using type = impl::unpack_type<idx, Types...>;
        };

        template <size_t I>
        static size_t find_matching_type(const Object& obj, size_t& first) {
            Type<impl::unpack_type<I, Types...>> type;
            if (reinterpret_cast<PyObject*>(Py_TYPE(ptr(obj))) == ptr(type)) {
                return I;
            } else if (first == sizeof...(Types)) {
                int rc = PyObject_IsInstance(
                    ptr(obj),
                    ptr(type)
                );
                if (rc == -1) {
                    Exception::to_python();
                } else if (rc) {
                    first = I;
                }
            }
            if constexpr (I + 1 >= sizeof...(Types)) {
                return sizeof...(Types);
            } else {
                return find_matching_type<I + 1>(obj, first);
            }
        }

    public:

        Object m_value;
        size_t m_index;
        vectorcallfunc vectorcall = reinterpret_cast<vectorcallfunc>(__call__);

        /// TODO: I may want to avoid calling the type here, and instead handle it
        /// in the C++ level __cast__/__init__ structs.

        template <typename T> requires (ctor<T>::enable)
        explicit __python__(T&& value) :
            m_value(typename ctor<T>::type(std::forward<T>(value))),
            m_index(ctor<T>::idx)
        {}

        static PyObject* __new__(
            PyTypeObject* type,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            __python__* self = reinterpret_cast<__python__*>(
                type->tp_alloc(type, 0)
            );
            if (self != nullptr) {
                new (&self->m_value) Object(None);
                self->m_index = 0;
            }
            return self;
        }

        static int __init__(
            __python__* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            try {
                if (kwargs) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "Union constructor does not accept keyword arguments"
                    );
                    return -1;
                }
                size_t nargs = PyTuple_GET_SIZE(args);
                if (nargs != 1) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "Union constructor requires exactly one argument"
                    );
                    return -1;
                }
                constexpr StaticStr str = "bertrand";
                PyObject* name = PyUnicode_FromStringAndSize(str, str.size());
                if (name == nullptr) {
                    return -1;
                }
                Object bertrand = reinterpret_steal<Object>(PyImport_Import(name));
                Py_DECREF(name);
                if (bertrand.is(nullptr)) {
                    return -1;
                }
                Object converted = reinterpret_steal<Object>(PyObject_CallOneArg(
                    ptr(bertrand),
                    PyTuple_GET_ITEM(args, 0)
                ));
                if (converted.is(nullptr)) {
                    return -1;
                }
                size_t subclass = sizeof...(Types);
                size_t match = find_matching_type<0>(converted, subclass);
                if (match == sizeof...(Types)) {
                    if (subclass == sizeof...(Types)) {
                        std::string message = "cannot convert object of type '";
                        message += impl::demangle(Py_TYPE(ptr(converted))->tp_name);
                        message += "' to '";
                        message += impl::demangle(ptr(Type<Union<Types...>>())->tp_name);
                        message += "'";
                        PyErr_SetString(PyExc_TypeError, message.c_str());
                        return -1;
                    } else {
                        match = subclass;
                    }
                }
                self->m_index = match;
                self->m_value = std::move(converted);
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        template <StaticStr ModName>
        static Type<Union> __export__(Module<ModName>& mod);
        static Type<Union> __import__();

        static PyObject* __wrapped__(__python__* self) noexcept {
            return Py_NewRef(ptr(self->m_value));
        }

        static PyObject* __repr__(__python__* self) noexcept {
            return PyObject_Repr(ptr(self->m_value));
        }

        /// TODO: these slots should only be enabled if the underlying objects support
        /// them.  Basically, when I'm exposing the heap type, I'll use a static
        /// vector and conditionally append all of these slots.

        static PyObject* __hash__(__python__* self) noexcept {
            return PyObject_Hash(ptr(self->m_value));
        }

        static PyObject* __call__(
            __python__* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            return PyObject_Vectorcall(
                ptr(self->m_value),
                args,
                nargsf,
                kwnames
            );
        }

        static PyObject* __str__(__python__* self) noexcept {
            return PyObject_Str(ptr(self->m_value));
        }

        static PyObject* __getattr__(__python__* self, PyObject* attr) noexcept {
            try {
                Py_ssize_t len;
                const char* data = PyUnicode_AsUTF8AndSize(attr, &len);
                if (data == nullptr) {
                    return nullptr;
                }
                std::string_view name = {data, static_cast<size_t>(len)};
                if (name == "__wrapped__") {
                    return PyObject_GenericGetAttr(ptr(self->m_value), attr);
                }
                return PyObject_GetAttr(ptr(self->m_value), attr);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __setattr__(
            __python__* self,
            PyObject* attr,
            PyObject* value
        ) noexcept {
            try {
                Py_ssize_t len;
                const char* data = PyUnicode_AsUTF8AndSize(attr, &len);
                if (data == nullptr) {
                    return -1;
                }
                std::string_view name = {data, static_cast<size_t>(len)};
                if (name == "__wrapped__") {
                    std::string message = "cannot ";
                    message += value ? "set" : "delete";
                    message += " attribute '" + std::string(name) + "'";
                    PyErr_SetString(
                        PyExc_AttributeError,
                        message.c_str()
                    );
                    return -1;
                }
                return PyObject_SetAttr(ptr(self->m_value), attr, value);

            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        static int __traverse__(
            __python__* self,
            visitproc visit,
            void* arg
        ) noexcept {
            PyTypeObject* type = Py_TYPE(ptr(self->m_value));
            if (type->tp_traverse) {
                return type->tp_traverse(ptr(self->m_value), visit, arg);
            }
            return def<__python__, Union>::__traverse__(self, visit, arg);
        }

        static int __clear__(__python__* self) noexcept {
            PyTypeObject* type = Py_TYPE(ptr(self->m_value));
            if (type->tp_clear) {
                return type->tp_clear(ptr(self->m_value));
            }
            return def<__python__, Union>::__clear__(self);
        }

        static int __richcmp__(
            __python__* self,
            PyObject* other,
            int op
        ) noexcept {
            return PyObject_RichCompareBool(ptr(self->m_value), other, op);
        }

        static PyObject* __iter__(__python__* self) noexcept {
            return PyObject_GetIter(ptr(self->m_value));
        }

        static PyObject* __next__(__python__* self) noexcept {
            return PyIter_Next(ptr(self->m_value));
        }

        static PyObject* __get__(
            __python__* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            PyTypeObject* cls = reinterpret_cast<PyTypeObject*>(type);
            if (cls->tp_descr_get) {
                return cls->tp_descr_get(ptr(self), obj, type);
            }
            PyErr_SetString(
                PyExc_TypeError,
                "object is not a descriptor"
            );
            return nullptr;
        }

        static PyObject* __set__(
            __python__* self,
            PyObject* obj,
            PyObject* value
        ) noexcept {
            PyTypeObject* cls = reinterpret_cast<PyTypeObject*>(Py_TYPE(ptr(self)));
            if (cls->tp_descr_set) {
                return cls->tp_descr_set(ptr(self), obj, value);
            }
            if (value) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "object does not support descriptor assignment"
                );
            } else {
                PyErr_SetString(
                    PyExc_AttributeError,
                    "object does not support descriptor deletion"
                );
            }
            return nullptr;
        }

        static PyObject* __getitem__(__python__* self, PyObject* key) noexcept {
            return PyObject_GetItem(ptr(self->m_value), key);
        }

        static PyObject* __sq_getitem__(__python__* self, Py_ssize_t index) noexcept {
            return PySequence_GetItem(ptr(self->m_value), index);
        }

        static PyObject* __setitem__(
            __python__* self,
            PyObject* key,
            PyObject* value
        ) noexcept {
            return PyObject_SetItem(ptr(self->m_value), key, value);
        }

        static int __sq_setitem__(
            __python__* self,
            Py_ssize_t index,
            PyObject* value
        ) noexcept {
            return PySequence_SetItem(ptr(self->m_value), index, value);
        }

        static Py_ssize_t __len__(__python__* self) noexcept {
            return PyObject_Length(ptr(self->m_value));
        }

        static int __contains__(__python__* self, PyObject* key) noexcept {
            return PySequence_Contains(ptr(self->m_value), key);
        }

        static PyObject* __await__(__python__* self) noexcept {
            PyAsyncMethods* async = Py_TYPE(ptr(self))->tp_as_async;
            if (async && async->am_await) {
                return async->am_await(ptr(self));
            }
            PyErr_SetString(
                PyExc_TypeError,
                "object is not awaitable"
            );
            return nullptr;
        }

        static PyObject* __aiter__(__python__* self) noexcept {
            PyAsyncMethods* async = Py_TYPE(ptr(self))->tp_as_async;
            if (async && async->am_aiter) {
                return async->am_aiter(ptr(self));
            }
            PyErr_SetString(
                PyExc_TypeError,
                "object is not an async iterator"
            );
            return nullptr;
        }

        static PyObject* __anext__(__python__* self) noexcept {
            PyAsyncMethods* async = Py_TYPE(ptr(self))->tp_as_async;
            if (async && async->am_anext) {
                return async->am_anext(ptr(self));
            }
            PyErr_SetString(
                PyExc_TypeError,
                "object is not an async iterator"
            );
            return nullptr;
        }

        static PySendResult __asend__(
            __python__* self,
            PyObject* arg,
            PyObject** prsesult
        ) noexcept {
            return PyIter_Send(ptr(self->m_value), arg, prsesult);
        }

        static PyObject* __add__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Add(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Add(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __iadd__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceAdd(ptr(lhs->m_value), rhs);
        }

        static PyObject* __sub__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Subtract(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Subtract(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __isub__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceSubtract(ptr(lhs->m_value), rhs);
        }

        static PyObject* __mul__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Multiply(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Multiply(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repeat__(__python__* lhs, Py_ssize_t rhs) noexcept {
            return PySequence_Repeat(ptr(lhs->m_value), rhs);
        }

        static PyObject* __imul__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceMultiply(ptr(lhs->m_value), rhs);
        }

        static PyObject* __irepeat__(__python__* lhs, Py_ssize_t rhs) noexcept {
            return PySequence_InPlaceRepeat(ptr(lhs->m_value), rhs);
        }

        static PyObject* __mod__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Remainder(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Remainder(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __imod__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceRemainder(ptr(lhs->m_value), rhs);
        }

        static PyObject* __divmod__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Divmod(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Divmod(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __power__(PyObject* lhs, PyObject* rhs, PyObject* mod) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Power(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs,
                        mod
                    );
                } else if (PyType_IsSubtype(
                    Py_TYPE(rhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Power(
                        lhs,
                        ptr(reinterpret_cast<__python__*>(rhs)->m_value),
                        mod
                    );
                }
                return PyNumber_Power(
                    lhs,
                    rhs,
                    ptr(reinterpret_cast<__python__*>(mod)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __ipower__(__python__* lhs, PyObject* rhs, PyObject* mod) noexcept {
            return PyNumber_InPlacePower(ptr(lhs->m_value), rhs, mod);
        }

        static PyObject* __neg__(__python__* self) noexcept {
            return PyNumber_Negative(ptr(self->m_value));
        }

        static PyObject* __pos__(__python__* self) noexcept {
            return PyNumber_Positive(ptr(self->m_value));
        }

        static PyObject* __abs__(__python__* self) noexcept {
            return PyNumber_Absolute(ptr(self->m_value));
        }

        static int __bool__(__python__* self) noexcept {
            return PyObject_IsTrue(ptr(self->m_value));
        }

        static PyObject* __invert__(__python__* self) noexcept {
            return PyNumber_Invert(ptr(self->m_value));
        }

        static PyObject* __lshift__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Lshift(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Lshift(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __ilshift__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceLshift(ptr(lhs->m_value), rhs);
        }

        static PyObject* __rshift__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Rshift(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Rshift(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __irshift__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceRshift(ptr(lhs->m_value), rhs);
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_And(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_And(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __iand__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceAnd(ptr(lhs->m_value), rhs);
        }

        static PyObject* __xor__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Xor(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Xor(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __ixor__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceXor(ptr(lhs->m_value), rhs);
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_Or(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_Or(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __ior__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceOr(ptr(lhs->m_value), rhs);
        }

        static PyObject* __int__(__python__* self) noexcept {
            return PyNumber_Long(ptr(self->m_value));
        }

        static PyObject* __float__(__python__* self) noexcept {
            return PyNumber_Float(ptr(self->m_value));
        }

        static PyObject* __floordiv__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_FloorDivide(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_FloorDivide(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __ifloordiv__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceFloorDivide(ptr(lhs->m_value), rhs);
        }

        static PyObject* __truediv__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_TrueDivide(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_TrueDivide(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __itruediv__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceTrueDivide(ptr(lhs->m_value), rhs);
        }

        static PyObject* __index__(__python__* self) noexcept {
            return PyNumber_Index(ptr(self->m_value));
        }

        static PyObject* __matmul__(PyObject* lhs, PyObject* rhs) noexcept {
            try {
                Type<Union> cls;
                if (PyType_IsSubtype(
                    Py_TYPE(lhs),
                    reinterpret_cast<PyTypeObject*>(ptr(cls)))
                ) {
                    return PyNumber_MatrixMultiply(
                        ptr(reinterpret_cast<__python__*>(lhs)->m_value),
                        rhs
                    );
                }
                return PyNumber_MatrixMultiply(
                    lhs,
                    ptr(reinterpret_cast<__python__*>(rhs)->m_value)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __imatmul__(__python__* lhs, PyObject* rhs) noexcept {
            return PyNumber_InPlaceMatrixMultiply(ptr(lhs->m_value), rhs);
        }

        static int __buffer__(
            __python__* exported,
            Py_buffer* view,
            int flags
        ) noexcept {
            return PyObject_GetBuffer(ptr(exported->m_value), view, flags);
        }

        static void __release_buffer__(__python__* exported, Py_buffer* view) noexcept {
            PyBuffer_Release(view);
        }

    private:

        inline static PyGetSetDef properties[] = {
            {
                "__wrapped__",
                reinterpret_cast<getter>(__wrapped__),
                nullptr,
                PyDoc_STR(
R"doc(The value stored in the optional.

Returns
-------
object
    The value stored in the optional, or None if it is currently empty.

Notes
-----
The presence of a `__wrapped__` attribute triggers some special behavior in
both the Python and Bertrand APIs.  In Python, it allows the `inspect` module
to unwrap the optional and inspect the internal value, in the same way as
`functools.partial` and `functools.wraps`.  In Bertrand, some operators
(like the `isinstance()` operator) will check for the presence of this
attribute and unwrap the optional if it is present.)doc"
                )
            },
            {nullptr}
        };

        inline static PyAsyncMethods async = {
            .am_await = reinterpret_cast<unaryfunc>(__await__),
            .am_aiter = reinterpret_cast<unaryfunc>(__aiter__),
            .am_anext = reinterpret_cast<unaryfunc>(__anext__),
            .am_send = reinterpret_cast<sendfunc>(__asend__),
        };

        inline static PyNumberMethods number = {
            .nb_add = reinterpret_cast<binaryfunc>(__add__),
            .nb_subtract = reinterpret_cast<binaryfunc>(__sub__),
            .nb_multiply = reinterpret_cast<binaryfunc>(__mul__),
            .nb_remainder = reinterpret_cast<binaryfunc>(__mod__),
            .nb_divmod = reinterpret_cast<binaryfunc>(__divmod__),
            .nb_power = reinterpret_cast<ternaryfunc>(__power__),
            .nb_negative = reinterpret_cast<unaryfunc>(__neg__),
            .nb_positive = reinterpret_cast<unaryfunc>(__pos__),
            .nb_absolute = reinterpret_cast<unaryfunc>(__abs__),
            .nb_bool = reinterpret_cast<inquiry>(__bool__),
            .nb_invert = reinterpret_cast<unaryfunc>(__invert__),
            .nb_lshift = reinterpret_cast<binaryfunc>(__lshift__),
            .nb_rshift = reinterpret_cast<binaryfunc>(__rshift__),
            .nb_and = reinterpret_cast<binaryfunc>(__and__),
            .nb_xor = reinterpret_cast<binaryfunc>(__xor__),
            .nb_or = reinterpret_cast<binaryfunc>(__or__),
            .nb_int = reinterpret_cast<unaryfunc>(__int__),
            .nb_float = reinterpret_cast<unaryfunc>(__float__),
            .nb_inplace_add = reinterpret_cast<binaryfunc>(__iadd__),
            .nb_inplace_subtract = reinterpret_cast<binaryfunc>(__isub__),
            .nb_inplace_multiply = reinterpret_cast<binaryfunc>(__imul__),
            .nb_inplace_remainder = reinterpret_cast<binaryfunc>(__imod__),
            .nb_inplace_power = reinterpret_cast<ternaryfunc>(__ipower__),
            .nb_inplace_lshift = reinterpret_cast<binaryfunc>(__ilshift__),
            .nb_inplace_rshift = reinterpret_cast<binaryfunc>(__irshift__),
            .nb_inplace_and = reinterpret_cast<binaryfunc>(__iand__),
            .nb_inplace_xor = reinterpret_cast<binaryfunc>(__ixor__),
            .nb_inplace_or = reinterpret_cast<binaryfunc>(__ior__),
            .nb_floor_divide = reinterpret_cast<binaryfunc>(__floordiv__),
            .nb_true_divide = reinterpret_cast<binaryfunc>(__truediv__),
            .nb_inplace_floor_divide = reinterpret_cast<binaryfunc>(__ifloordiv__),
            .nb_inplace_true_divide = reinterpret_cast<binaryfunc>(__itruediv__),
            .nb_index = reinterpret_cast<unaryfunc>(__index__),
            .nb_matrix_multiply = reinterpret_cast<binaryfunc>(__matmul__),
            .nb_inplace_matrix_multiply = reinterpret_cast<binaryfunc>(__imatmul__),
        };

        inline static PyMappingMethods mapping = {
            .mp_length = reinterpret_cast<lenfunc>(__len__),
            .mp_subscript = reinterpret_cast<binaryfunc>(__getitem__),
            .mp_ass_subscript = reinterpret_cast<objobjargproc>(__setitem__)
        };

        inline static PySequenceMethods sequence = {
            .sq_length = reinterpret_cast<lenfunc>(__len__),
            .sq_concat = reinterpret_cast<binaryfunc>(__add__),
            .sq_repeat = reinterpret_cast<ssizeargfunc>(__repeat__),
            .sq_item = reinterpret_cast<ssizeargfunc>(__sq_getitem__),
            .sq_ass_item = reinterpret_cast<ssizeobjargproc>(__sq_setitem__),
            .sq_contains = reinterpret_cast<objobjproc>(__contains__),
            .sq_inplace_concat = reinterpret_cast<binaryfunc>(__iadd__),
            .sq_inplace_repeat = reinterpret_cast<ssizeargfunc>(__irepeat__)
        };

        inline static PyBufferProcs buffer = {
            .bf_getbuffer = reinterpret_cast<getbufferproc>(__buffer__),
            .bf_releasebuffer = reinterpret_cast<releasebufferproc>(__release_buffer__)
        };

    };

    Union(PyObject* p, borrowed_t t) : Object(p, t) {}
    Union(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = Union> requires (__initializer__<Self>::enable)
    Union(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args> requires (implicit_ctor<Union>::template enable<Args...>)
    Union(Args&&... args) : Object(
        implicit_ctor<Union>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Union>::template enable<Args...>)
    explicit Union(Args&&... args) : Object(
        explicit_ctor<Union>{},
        std::forward<Args>(args)...
    ) {}

    /// TODO: Maybe I should overload the `.is()` method to forward to the underlying
    /// object, so that optionals can be checked for None by just doing obj.is(None),
    /// just like you would in Python, without needing any specific interface.
    /// -> This would probably require another control struct to properly customize.

};


template <typename... Ts>
struct __template__<Union<Ts...>>                           : Returns<Object> {
    static Object operator()() {
        /// TODO: this would need some special handling for argument annotations
        /// denoting structural fields, as well as a way to instantiate them
        /// accordingly.
        Object result = reinterpret_steal<Object>(PyTuple_Pack(
            sizeof...(Ts),
            ptr(Type<Ts>())...
        ));
        if (result.is(nullptr)) {
            Exception::to_python();
        }
        return result;
    }
};


/// TODO: isinstance() and issubclass() can be optimized in some cases to just call
/// std::holds_alternative<T>() on the underlying object, rather than needing to
/// invoke Python.  This can be done if the type that is checked against is a
/// supertype of any of the members of the union, in which case the type check can be
/// reduced to just a simple comparison against the index.


/// TODO: __explicit_cast__ for union types?


/* Initializer list constructor is only enabled for `Optional<T>`, and not for any
other form of union, where such a call might be ambiguous. */
template <typename T> requires (__initializer__<T>::enable)
struct __initializer__<Optional<T>>                        : Returns<Optional<T>> {
    using Element = __initializer__<T>::type;
    static Optional<T> operator()(const std::initializer_list<Element>& init) {
        return impl::construct<Optional<T>>(T(init));
    }
};


/* Default constructor is enabled as long as at least one of the member types is
default constructible, in which case the first such type is initialized.  If NoneType
is present in the union, it is preferred over any other type. */
template <typename... Ts> requires (std::is_default_constructible_v<Ts> || ...)
struct __init__<Union<Ts...>>                               : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t none_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t none_idx<I, U, Us...> =
        std::same_as<std::remove_cvref_t<U>, NoneType> ?
            0 : none_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t idx<I, U, Us...> =
        std::is_default_constructible_v<U> ? 0 : idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()() {
        if constexpr (none_idx<0, Ts...> < sizeof...(Ts)) {
            return impl::construct<Union<Ts...>>(None);
        } else {
            return impl::construct<Union<Ts...>>(
                impl::unpack_type<idx<0, Ts...>>()
            );
        }
    }
};


/* Explicit constructor calls are only allowed for `Optional<T>`, and not for any
other form of union, where such a call might be ambiguous. */
template <typename T, typename... Args>
    requires (sizeof...(Args) > 0 && std::constructible_from<T, Args...>)
struct __init__<Optional<T>, Args...>                : Returns<Optional<T>> {
    static Optional<T> operator()(Args&&... args) {
        return impl::construct<Optional<T>>(T(std::forward<Args>(args)...));
    }
};


/* Universal conversion from any type that is convertible to one or more types within
the union.  Prefers exact matches (and therefore copy/move semantics) over secondary
conversions, and always converts to the first matching type within the union */
template <typename From, typename... Ts>
    requires (!impl::py_union<From> && (std::convertible_to<From, Ts> || ...))
struct __cast__<From, Union<Ts...>>                            : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cvref_t<From>, U> ? 0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<From, U> ? 0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
            return impl::construct<Union<Ts...>>(
                impl::unpack_type<match_idx<0, Ts...>>(std::forward<From>(from))
            );
        } else {
            return impl::construct<Union<Ts...>>(
                impl::unpack_type<convert_idx<0, Ts...>>(std::forward<From>(from))
            );
        }
    }
};


/* Universal conversion from a union to any type for which each element of the union is
convertible.  This covers conversions to `std::variant` provided all types are
accounted for, as well as to `std::optional` and pointer types whereby `NoneType` is
convertible to `std::nullopt` and `nullptr`, respectively. */
template <impl::py_union From, typename To>
    requires (
        !impl::py_union<To> &&
        impl::UnionToType<std::remove_cvref_t<From>>::template convertible_to<To>
    )
struct __cast__<From, To>                                   : Returns<To> {
    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static To get(From from) {
            return reinterpret_cast<impl::unpack_type<I, Types...>&>(
                std::forward<From>(from)->m_value
            );
        }
    };

    template <size_t I>
    static To get(From from) {
        using F = std::remove_cvref_t<From>;
        if (I == from->m_index) {
            return context<F>::template get<I>(std::forward<From>(from));
        } else {
            return get<I + 1>(std::forward<From>(from));
        }
    }

    static To operator()(From from) {
        return get<0>(std::forward<From>(from));
    }
};


template <impl::is_variant T> requires (impl::VariantToUnion<T>::enable)
struct __cast__<T> : Returns<typename impl::VariantToUnion<T>::type> {};
template <impl::is_optional T> requires (impl::has_python<impl::optional_type<T>>)
struct __cast__<T> : Returns<
    Optional<impl::python_type<std::remove_cv_t<impl::optional_type<T>>>>
> {};
template <impl::has_python T> requires (impl::python<T> || std::same_as<
    std::remove_cv_t<T>,
    impl::cpp_type<impl::python_type<std::remove_cv_t<T>>>
>)
struct __cast__<T*> : Returns<
    Optional<impl::python_type<std::remove_cv_t<T>>>
> {};
template <impl::is_shared_ptr T> requires (impl::has_python<impl::shared_ptr_type<T>>)
struct __cast__<T> : Returns<
    Optional<impl::python_type<std::remove_cv_t<impl::shared_ptr_type<T>>>>
> {};
template <impl::is_unique_ptr T> requires (impl::has_python<impl::unique_ptr_type<T>>)
struct __cast__<T> : Returns<
    Optional<impl::python_type<std::remove_cv_t<impl::unique_ptr_type<T>>>>
> {};


template <impl::is_variant From, typename... Ts>
    requires (impl::VariantToUnion<From>::template convertible_to<Ts...>)
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    template <typename T>
    struct convert {
        static Union<Ts...> operator()(const T& value) {
            return impl::construct<Union<Ts...>>(
                impl::python_type<T>(value)
            );
        }
        static Union<Ts...> operator()(T&& value) {
            return impl::construct<Union<Ts...>>(
                impl::python_type<T>(std::move(value))
            );
        }
    };
    struct Visitor : convert<Ts>... { using convert<Ts>::operator()...; };
    static Union<Ts...> operator()(From value) {
        return std::visit(Visitor{}, std::forward<From>(value));
    }
};


template <impl::is_optional From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<impl::optional_type<From>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    using T = impl::optional_type<From>;

    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cv_t<T>, U> ? 0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<T, U> ? 0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if (from.has_value()) {
            if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<match_idx<0, Ts...>>(
                        std::forward<From>(from).value()
                    )
                );
            } else {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<convert_idx<0, Ts...>>(
                        std::forward<From>(from).value()
                    )
                );
            }
        } else {
            return impl::construct<Union<Ts...>>(None);
        }
    }
};


template <impl::is_ptr From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<impl::ptr_type<From>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cv_t<impl::ptr_type<From>>, U> ?
            0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<impl::ptr_type<From>, U> ?
            0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if (from) {
            if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<match_idx<0, Ts...>>(*from)
                );
            } else {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<convert_idx<0, Ts...>>(*from)
                );
            }
        } else {
            return impl::construct<Union<Ts...>>(None);
        }
    }
};


template <impl::is_shared_ptr From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<impl::shared_ptr_type<From>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cv_t<impl::shared_ptr_type<From>>, U> ?
            0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<impl::shared_ptr_type<From>, U> ?
            0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if (from) {
            if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<match_idx<0, Ts...>>(*from)
                );
            } else {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<convert_idx<0, Ts...>>(*from)
                );
            }
        } else {
            return impl::construct<Union<Ts...>>(None);
        }
    }
};


template <impl::is_unique_ptr From, typename... Ts>
    requires (
        (std::same_as<NoneType, Ts> || ...) &&
        (std::convertible_to<impl::unique_ptr_type<From>, Ts> || ...)
    )
struct __cast__<From, Union<Ts...>>                         : Returns<Union<Ts...>> {
    template <size_t I, typename... Us>
    static constexpr size_t match_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t match_idx<I, U, Us...> =
        std::same_as<std::remove_cv_t<impl::unique_ptr_type<From>>, U> ?
            0 : match_idx<I + 1, Us...> + 1;

    template <size_t I, typename... Us>
    static constexpr size_t convert_idx = 0;
    template <size_t I, typename U, typename... Us>
    static constexpr size_t convert_idx<I, U, Us...> =
        std::convertible_to<impl::unique_ptr_type<From>, U> ?
            0 : convert_idx<I + 1, Us...> + 1;

    static Union<Ts...> operator()(From from) {
        if (from) {
            if constexpr (match_idx<0, Ts...> < sizeof...(Ts)) {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<match_idx<0, Ts...>>(*from)
                );
            } else {
                return impl::construct<Union<Ts...>>(
                    impl::unpack_type<convert_idx<0, Ts...>>(*from)
                );
            }
        } else {
            return impl::construct<Union<Ts...>>(None);
        }
    }
};


/// NOTE: all other operations are only enabled if one or more members of the union
/// support them, and will return a new union type holding all of the valid results,
/// or a standard type if the resulting union would be a singleton.


template <impl::py_union Self, StaticStr Name>
    requires (impl::UnionGetAttr<Self, Name>::enable)
struct __getattr__<Self, Name> : Returns<typename impl::UnionGetAttr<Self, Name>::type> {
    using type = impl::UnionGetAttr<Self, Name>::type;

    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static type exec(Self self) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__getattr__<T, Name>::enable) {
                return getattr<Name>(
                    reinterpret_cast<T>(std::forward<Self>(self)->m_value)
                );
            } else {
                throw AttributeError(
                    "'" + impl::demangle(typeid(T).name()) + "' object has no "
                    "attribute '" + Name + "'"
                );
            }
        }
    };

    template <size_t I>
    static type exec(Self self) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            return context<S>::template exec<I>(std::forward<Self>(self));
        } else {
            return exec<I + 1>(std::forward<Self>(self));
        }
    }

    static type operator()(Self self) {
        return exec<0>(std::forward<Self>(self));
    }
};


template <impl::py_union Self, StaticStr Name, typename Value>
    requires (impl::UnionSetAttr<Self, Name, Value>::enable)
struct __setattr__<Self, Name, Value> : Returns<void> {
    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static void exec(Self self, Value&& value) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__setattr__<T, Name, Value>::enable) {
                setattr<Name>(
                    reinterpret_cast<T>(std::forward<Self>(self)->m_value),
                    std::forward<Value>(value)
                );
            } else {
                throw AttributeError(
                    "cannot set attribute '" + Name + "' on object of type '" +
                    impl::demangle(typeid(T).name()) + "'"
                );
            }
        }
    };

    template <size_t I>
    static void exec(Self self, Value&& value) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            context<S>::template exec<I>(
                std::forward<Self>(self),
                std::forward<Value>(value)
            );
        } else {
            exec<I + 1>(std::forward<Self>(self), std::forward<Value>(value));
        }
    }

    static void operator()(Self self, Value&& value) {
        exec<0>(std::forward<Self>(self), std::forward<Value>(value));
    }
};


template <impl::py_union Self, StaticStr Name>
    requires (impl::UnionDelAttr<Self, Name>::enable)
struct __delattr__<Self, Name> : Returns<void> {
    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static void exec(Self self) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__delattr__<T, Name>::enable) {
                delattr<Name>(
                    reinterpret_cast<T>(std::forward<Self>(self)->m_value)
                );
            } else {
                throw AttributeError(
                    "cannot delete attribute '" + Name + "' on object of type '" +
                    impl::demangle(typeid(T).name()) + "'"
                );
            }
        }
    };

    template <size_t I>
    static void exec(Self self) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            context<S>::template exec<I>(std::forward<Self>(self));
        } else {
            exec<I + 1>(std::forward<Self>(self));
        }
    }

    static void operator()(Self self) {
        exec<0>(std::forward<Self>(self));
    }
};


template <impl::py_union Self>
struct __repr__<Self> : Returns<std::string> {
    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static std::string exec(Self self) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__repr__<T>::enable) {
                return repr(reinterpret_cast<T>(std::forward<Self>(self)->m_value));
            } else {
                return repr(std::forward<Self>(self)->m_value);
            }
        }
    };

    template <size_t I>
    static std::string exec(Self self) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            return context<S>::template exec<I>(std::forward<Self>(self));
        } else {
            return exec<I + 1>(std::forward<Self>(self));
        }
    }

    static std::string operator()(Self self) {
        return exec<0>(std::forward<Self>(self));
    }
};


template <impl::py_union Self, typename... Args>
    requires (impl::UnionCall<Self, Args...>::enable)
struct __call__<Self, Args...> : Returns<typename impl::UnionCall<Self, Args...>::type> {
    using type = impl::UnionCall<std::remove_cvref_t<Self>, Args...>::type;

    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static type exec(Self self, Args&&... args) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__call__<T, Args...>::enable) {
                return reinterpret_cast<T>(std::forward<Self>(self)->m_value)(
                    std::forward<Args>(args)...
                );
            } else {
                throw TypeError(
                    "'" + impl::demangle(typeid(T).name()) + "' object is not "
                    "callable with the given arguments"
                );
            }
        }
    };

    template <size_t I>
    static type exec(Self self, Args&&... args) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            return context<S>::template exec<I>(
                std::forward<Self>(self),
                std::forward<Args>(args)...
            );
        } else {
            return exec<I + 1>(
                std::forward<Self>(self),
                std::forward<Args>(args)...
            );
        }
    }

    static type operator()(Self self, Args&&... args) {
        return exec<0>(std::forward<Self>(self), std::forward<Args>(args)...);
    }
};


template <impl::py_union Self, typename... Key>
    requires (impl::UnionGetItem<Self, Key...>::enable)
struct __getitem__<Self, Key...> : Returns<typename impl::UnionGetItem<Self, Key...>::type> {
    using type = impl::UnionGetItem<std::remove_cvref_t<Self>, Key...>::type;

    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static type exec(Self self, Key&&... key) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__getitem__<T, Key...>::enable) {
                return reinterpret_cast<T>(
                    std::forward<Self>(self)->m_value
                )[std::forward<Key>(key)...];
            } else {
                throw KeyError(
                    "'" + impl::demangle(typeid(T).name()) + "' object cannot be "
                    "subscripted with the given key(s)"
                );
            }
        }
    };

    template <size_t I>
    static type exec(Self self, Key&&... key) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            return context<S>::template exec<I>(
                std::forward<Self>(self),
                std::forward<Key>(key)...
            );
        } else {
            return exec<I + 1>(
                std::forward<Self>(self),
                std::forward<Key>(key)...
            );
        }
    }

    static type operator()(Self self, Key&&... key) {
        return exec<0>(std::forward<Self>(self), std::forward<Key>(key)...);
    }
};


template <impl::py_union Self, typename Value, typename... Key>
    requires (impl::UnionSetItem<Self, Value, Key...>::enable)
struct __setitem__<Self, Value, Key...> : Returns<void> {
    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static void exec(Self self, Value&& value, Key&&... key) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__setitem__<T, Key..., Value>::enable) {
                reinterpret_cast<T>(
                    std::forward<Self>(self)->m_value
                )[std::forward<Key>(key)...] = std::forward<Value>(value);
            } else {
                throw KeyError(
                    "'" + impl::demangle(typeid(T).name()) + "' object does not "
                    "support item assignment with the given key(s)"
                );
            }
        }
    };

    template <size_t I>
    static void exec(Self self, Value&& value, Key&&... key) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            context<S>::template exec<I>(
                std::forward<Self>(self),
                std::forward<Value>(value),
                std::forward<Key>(key)...
            );
        } else {
            exec<I + 1>(
                std::forward<Self>(self),
                std::forward<Value>(value),
                std::forward<Key>(key)...
            );
        }
    }

    static void operator()(Self self, Key&&... key, Value&& value) {
        exec<0>(
            std::forward<Self>(self),
            std::forward<Value>(value),
            std::forward<Key>(key)...
        );
    }
};


template <impl::py_union Self, typename... Key>
    requires (impl::UnionDelItem<Self, Key...>::enable)
struct __delitem__<Self, Key...> : Returns<void> {
    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static void exec(Self self, Key&&... key) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__delitem__<T, Key...>::enable) {
                del(
                    reinterpret_cast<T>(
                        std::forward<Self>(self)->m_value
                    )[std::forward<Key>(key)...]
                );
            } else {
                throw KeyError(
                    "'" + impl::demangle(typeid(T).name()) + "' object does not "
                    "support item deletion with the given key(s)"
                );
            }
        }
    };

    template <size_t I>
    static void exec(Self self, Key&&... key) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            context<S>::template exec<I>(
                std::forward<Self>(self),
                std::forward<Key>(key)...
            );
        } else {
            exec<I + 1>(
                std::forward<Self>(self),
                std::forward<Key>(key)...
            );
        }
    }

    static void operator()(Self self, Key&&... key) {
        exec<0>(std::forward<Self>(self), std::forward<Key>(key)...);
    }
};


template <impl::py_union Self> requires (impl::UnionLen<Self>::enable)
struct __len__<Self> : Returns<size_t> {
    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static size_t exec(Self self) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__len__<T>::enable) {
                return len(
                    reinterpret_cast<T>(std::forward<Self>(self)->m_value)
                );
            } else {
                throw TypeError(
                    "'" + impl::demangle(typeid(T).name()) + "' object does not "
                    "have a length"
                );
            }
        }
    };

    template <size_t I>
    static size_t exec(Self self) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            return context<S>::template exec<I>(std::forward<Self>(self));
        } else {
            return exec<I + 1>(std::forward<Self>(self));
        }
    }

    static size_t operator()(Self self) {
        return exec<0>(std::forward<Self>(self));
    }
};


template <impl::py_union Self> requires (impl::UnionIter<Self>::enable)
struct __iter__<Self> : Returns<typename impl::UnionIter<Self>::type> {
    /// NOTE: default implementation delegates to Python, which reinterprets each value
    /// as the given type(s).  That handles all cases appropriately, with a small
    /// performance hit for the extra interpreter overhead that isn't present for
    /// static types.
};


template <impl::py_union Self> requires (impl::UnionReversed<Self>::enable)
struct __reversed__<Self> : Returns<typename impl::UnionReversed<Self>::type> {
    /// NOTE: same as `__iter__`, but returns a reverse iterator instead.
};


template <impl::py_union Self, typename Key> requires (impl::UnionContains<Self, Key>::enable)
struct __contains__<Self, Key> : Returns<bool> {
    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static bool exec(Self self, Key&& key) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__contains__<T, Key>::enable) {
                return reinterpret_cast<T>(
                    std::forward<Self>(self)->m_value
                ).contains(std::forward<Key>(key));
            } else {
                throw TypeError(
                    "'" + impl::demangle(typeid(T).name()) + "' object does not "
                    "support contains checks"
                );
            }
        }
    };

    template <size_t I>
    static bool exec(Self self, Key&& key) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            return context<S>::template exec<I>(
                std::forward<Self>(self),
                std::forward<Key>(key)
            );
        } else {
            return exec<I + 1>(
                std::forward<Self>(self),
                std::forward<Key>(key)
            );
        }
    }

    static bool operator()(Self self, Key&& key) {
        return exec<0>(std::forward<Self>(self), std::forward<Key>(key));
    }
};


template <impl::py_union Self> requires (impl::UnionHash<Self>::enable)
struct __hash__<Self> : Returns<size_t> {
    template <typename>
    struct context {};
    template <typename... Types>
    struct context<Union<Types...>> {
        template <size_t I> requires (I < sizeof...(Types))
        static size_t exec(Self self) {
            using T = impl::qualify_lvalue<impl::unpack_type<I, Types...>, Self>;
            if constexpr (__hash__<T>::enable) {
                return hash(
                    reinterpret_cast<T>(std::forward<Self>(self)->m_value)
                );
            } else {
                throw TypeError(
                    "'" + impl::demangle(typeid(T).name()) + "' object is not "
                    "hashable"
                );
            }
        }
    };

    template <size_t I>
    static size_t exec(Self self) {
        using S = std::remove_cvref_t<Self>;
        if (I == self->m_index) {
            return context<S>::template exec<I>(std::forward<Self>(self));
        } else {
            return exec<I + 1>(std::forward<Self>(self));
        }
    }

    static size_t operator()(Self self) {
        return exec<0>(std::forward<Self>(self));
    }
};


template <impl::py_union Self> requires (impl::UnionAbs<Self>::enable)
struct __abs__<Self> : Returns<typename impl::UnionAbs<Self>::type> {
    using type = impl::UnionAbs<Self>::type;
    static type operator()(Self self) {
        return impl::UnionAbs<Self>{}(
            std::forward<Self>(self),
            []<typename S>(S&& self) -> type {
                return abs(std::forward<S>(self));
            },
            []<typename S>(S&& self) -> type {
                throw TypeError(
                    "bad operand type for abs(): '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            }
        );
    }
};


template <impl::py_union Self> requires (impl::UnionInvert<Self>::enable)
struct __invert__<Self> : Returns<typename impl::UnionInvert<Self>::type> {
    using type = impl::UnionInvert<Self>::type;
    static type operator()(Self self) {
        return impl::UnionInvert<Self>{}(
            std::forward<Self>(self),
            []<typename S>(S&& self) -> type {
                return ~std::forward<S>(self);
            },
            []<typename S>(S&& self) -> type {
                throw TypeError(
                    "bad operand type for unary ~: '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            }
        );
    }
};


template <impl::py_union Self> requires (impl::UnionPos<Self>::enable)
struct __pos__<Self> : Returns<typename impl::UnionPos<Self>::type> {
    using type = impl::UnionPos<Self>::type;
    static type operator()(Self self) {
        return impl::UnionPos<Self>{}(
            std::forward<Self>(self),
            []<typename S>(S&& self) -> type {
                return +std::forward<S>(self);
            },
            []<typename S>(S&& self) -> type {
                throw TypeError(
                    "bad operand type for unary +: '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            }
        );
    }
};


template <impl::py_union Self> requires (impl::UnionNeg<Self>::enable)
struct __neg__<Self> : Returns<typename impl::UnionNeg<Self>::type> {
    using type = impl::UnionNeg<Self>::type;
    static type operator()(Self self) {
        return impl::UnionNeg<Self>{}(
            std::forward<Self>(self),
            []<typename S>(S&& self) -> type {
                return -std::forward<S>(self);
            },
            []<typename S>(S&& self) -> type {
                throw TypeError(
                    "bad operand type for unary -: '" +
                    impl::demangle(typeid(S).name()) + "'"
                );
            }
        );
    }
};


template <impl::py_union Self> requires (impl::UnionIncrement<Self>::enable)
struct __increment__<Self> : Returns<Self> {
    using type = impl::UnionIncrement<Self>::type;
    static type operator()(Self self) {
        return impl::UnionIncrement<Self>{}(
            std::forward<Self>(self),
            []<typename S>(S&& self) -> void {
                ++std::forward<S>(self);
            },
            []<typename S>(S&& self) -> void {
                throw TypeError(
                    "'" + impl::demangle(typeid(S).name()) + "' object cannot be "
                    "incremented"
                );
            }
        );
    }
};


template <impl::py_union Self> requires (impl::UnionDecrement<Self>::enable)
struct __decrement__<Self> : Returns<Self> {
    using type = impl::UnionDecrement<Self>::type;
    static type operator()(Self self) {
        return impl::UnionDecrement<Self>{}(
            std::forward<Self>(self),
            []<typename S>(S&& self) -> void {
                --std::forward<S>(self);
            },
            []<typename S>(S&& self) -> void {
                throw TypeError(
                    "'" + impl::demangle(typeid(S).name()) + "' object cannot be "
                    "decremented"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionLess<L, R>::enable)
struct __lt__<L, R> : Returns<typename impl::UnionLess<L, R>::type> {
    using type = impl::UnionLess<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionLess<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) < std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for <: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionLessEqual<L, R>::enable)
struct __le__<L, R> : Returns<typename impl::UnionLessEqual<L, R>::type> {
    using type = impl::UnionLessEqual<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionLessEqual<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) <= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for <=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionEqual<L, R>::enable)
struct __eq__<L, R> : Returns<typename impl::UnionEqual<L, R>::type> {
    using type = impl::UnionEqual<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionEqual<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) == std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for ==: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionNotEqual<L, R>::enable)
struct __ne__<L, R> : Returns<typename impl::UnionNotEqual<L, R>::type> {
    using type = impl::UnionNotEqual<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionNotEqual<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) != std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for !=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionGreaterEqual<L, R>::enable)
struct __ge__<L, R> : Returns<typename impl::UnionGreaterEqual<L, R>::type> {
    using type = impl::UnionGreaterEqual<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionGreaterEqual<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) >= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for >=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionGreater<L, R>::enable)
struct __gt__<L, R> : Returns<typename impl::UnionGreater<L, R>::type> {
    using type = impl::UnionGreater<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionGreater<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) > std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for >: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionAdd<L, R>::enable)
struct __add__<L, R> : Returns<typename impl::UnionAdd<L, R>::type> {
    using type = impl::UnionAdd<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionAdd<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) + std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for +: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionSub<L, R>::enable)
struct __sub__<L, R> : Returns<typename impl::UnionSub<L, R>::type> {
    using type = impl::UnionSub<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionSub<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) - std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for -: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionMul<L, R>::enable)
struct __mul__<L, R> : Returns<typename impl::UnionMul<L, R>::type> {
    using type = impl::UnionMul<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionMul<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) * std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for *: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionTrueDiv<L, R>::enable)
struct __truediv__<L, R> : Returns<typename impl::UnionTrueDiv<L, R>::type> {
    using type = impl::UnionTrueDiv<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionTrueDiv<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) / std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for /: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionFloorDiv<L, R>::enable)
struct __floordiv__<L, R> : Returns<typename impl::UnionFloorDiv<L, R>::type> {
    using type = impl::UnionFloorDiv<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionFloorDiv<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return floordiv(std::forward<L2>(lhs), std::forward<R2>(rhs));
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for //: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionMod<L, R>::enable)
struct __mod__<L, R> : Returns<typename impl::UnionMod<L, R>::type> {
    using type = impl::UnionMod<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionMod<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) % std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for %: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionLShift<L, R>::enable)
struct __lshift__<L, R> : Returns<typename impl::UnionLShift<L, R>::type> {
    using type = impl::UnionLShift<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionLShift<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) << std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for <<: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionRShift<L, R>::enable)
struct __rshift__<L, R> : Returns<typename impl::UnionRShift<L, R>::type> {
    using type = impl::UnionRShift<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionRShift<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) >> std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for >>: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionAnd<L, R>::enable)
struct __and__<L, R> : Returns<typename impl::UnionAnd<L, R>::type> {
    using type = impl::UnionAnd<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionAnd<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) & std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for &: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionXor<L, R>::enable)
struct __xor__<L, R> : Returns<typename impl::UnionXor<L, R>::type> {
    using type = impl::UnionXor<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionXor<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) ^ std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for ^: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionOr<L, R>::enable)
struct __or__<L, R> : Returns<typename impl::UnionOr<L, R>::type> {
    using type = impl::UnionOr<L, R>::type;
    static type operator()(L lhs, R rhs) {
        return impl::UnionOr<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                return std::forward<L2>(lhs) | std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> type {
                throw TypeError(
                    "unsupported operand types for |: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceAdd<L, R>::enable)
struct __iadd__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceAdd<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) += std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for +=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceSub<L, R>::enable)
struct __isub__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceSub<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) -= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for -=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceMul<L, R>::enable)
struct __imul__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceMul<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) *= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for *=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceTrueDiv<L, R>::enable)
struct __itruediv__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceTrueDiv<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) /= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for /=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceFloorDiv<L, R>::enable)
struct __ifloordiv__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceFloorDiv<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                ifloordiv(std::forward<L2>(lhs), std::forward<R2>(rhs));
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for //=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceMod<L, R>::enable)
struct __imod__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceMod<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) %= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for %=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceLShift<L, R>::enable)
struct __ilshift__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceLShift<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) <<= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for <<=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceRShift<L, R>::enable)
struct __irshift__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceRShift<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) >>= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for >>=: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceAnd<L, R>::enable)
struct __iand__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceAnd<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) &= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for &: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceXor<L, R>::enable)
struct __ixor__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceXor<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) ^= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for ^: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


template <typename L, typename R>
    requires (impl::UnionInplaceOr<L, R>::enable)
struct __ior__<L, R> : Returns<L> {
    static L operator()(L lhs, R rhs) {
        return impl::UnionInplaceOr<L, R>{}(
            std::forward<L>(lhs),
            std::forward<R>(rhs),
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                std::forward<L2>(lhs) |= std::forward<R2>(rhs);
            },
            []<typename L2, typename R2>(L2&& lhs, R2&& rhs) -> void {
                throw TypeError(
                    "unsupported operand types for |: '" +
                    impl::demangle(typeid(L2).name()) + "' and '" +
                    impl::demangle(typeid(R2).name()) + "'"
                );
            }
        );
    }
};


////////////////////////////
////    INTERSECTION    ////
////////////////////////////


/// TODO: a class which accepts a variadic number of `py::Arg` objects as template
/// parameters, and forces the underlying object to implement the corresponding
/// methods.  It can forward attribute access for that particular method, and is
/// also used to implement the intersection types that are exposed to python via the
/// & operator.


/// TODO: perhaps this can also accept normal types with the same semantics, and
/// I would just account for the extra syntax in the template subscription function.



inline void test() {
    Optional<Object> x;
    auto y = x + None;
    // auto z = len(x);
    if (x.is(None)) {
        x = Ellipsis;
    }
    ++x;
    if (x <= Ellipsis) {
        x += NotImplemented;
    };
    // std::optional<EllipsisType> z = x;
}


}


namespace std {

    template <typename... Types>
    struct variant_size<py::Union<Types...>> :
        std::integral_constant<size_t, sizeof...(Types)>
    {};

    template <size_t I, typename... Types>
    struct variant_alternative<I, py::Union<Types...>> {
        using type = py::impl::unpack_type<I, Types...>;
    };

    template <typename T, py::impl::py_union Self>
        requires (py::impl::holds_alternative<T, std::remove_cvref_t<Self>>::enable)
    bool holds_alternative(Self&& self) {
        return py::impl::holds_alternative<T, std::remove_cvref_t<Self>>{}(
            std::forward<Self>(self)
        );
    }

    template <typename T, py::impl::py_union Self>
        requires (py::impl::holds_alternative<T, std::remove_cvref_t<Self>>::enable)
    auto get(Self&& self) -> py::impl::qualify_lvalue<T, Self> {
        using Return = py::impl::qualify_lvalue<T, Self>;
        if (holds_alternative<T>(self)) {
            return reinterpret_cast<Return>(std::forward<Self>(self)->m_value);
        } else {
            throw py::TypeError(
                "bad union access: '" + py::impl::demangle(typeid(T).name()) +
                "' is not the active type"
            );
        }
    }

    template <size_t I, py::impl::py_union Self>
        requires (py::impl::holds_alternative<
            typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>,
            std::remove_cvref_t<Self>
        >::enable)
    auto get(Self&& self) -> py::impl::qualify_lvalue<
        typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>,
        Self
    > {
        using Return = py::impl::qualify_lvalue<
            typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>,
            Self
        >;
        if (I == self->m_index) {
            return reinterpret_cast<Return>(self->m_value);
        } else {
            throw py::TypeError(
                "bad union access: '" +
                py::impl::demangle(typeid(std::remove_cvref_t<Return>).name()) +
                "' is not the active type"
            );
        }
    }

    template <typename T, py::impl::py_union Self>
        requires (py::impl::holds_alternative<T, std::remove_cvref_t<Self>>::enable)
    auto get_if(Self&& self) -> py::impl::qualify_pointer<T, Self> {
        return holds_alternative<T>(self) ?
            reinterpret_cast<py::impl::qualify_pointer<T, Self>>(&self->m_value) :
            nullptr;
    }

    template <size_t I, py::impl::py_union Self>
        requires (py::impl::holds_alternative<
            typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>,
            std::remove_cvref_t<Self>
        >::enable)
    auto get_if(Self&& self) -> py::impl::qualify_pointer<
        typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>,
        Self
    > {
        return I == self->m_index ?
            reinterpret_cast<py::impl::qualify_pointer<
                typename std::variant_alternative_t<I, std::remove_cvref_t<Self>>,
                Self
            >>(&self->m_value) :
            nullptr;
    }

    /// TODO: std::visit?

}


inline void test() {
    volatile py::Optional<py::EllipsisType> x;
    auto& y = std::get<py::NoneType>(x);
}


#endif
