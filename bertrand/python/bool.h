#ifndef BERTRAND_PYTHON_BOOL_H
#define BERTRAND_PYTHON_BOOL_H

#include "common.h"


namespace py {


template <typename T>
struct __issubclass__<T, Bool>                              : Returns<bool> {
    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() { return impl::bool_like<T>; }
};


template <typename T>
struct __isinstance__<T, Bool>                              : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Bool>();
        } else if constexpr (issubclass<T, Bool>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyBool_Check(obj.ptr());
        } else {
            return false;
        }
    }
};


/* Represents a statically-typed Python boolean in C++. */
class Bool : public Object {
    using Base = Object;
    using Self = Bool;

public:
    static const Type type;

    Bool(Handle h, borrowed_t t) : Base(h, t) {}
    Bool(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Bool, __init__<Bool, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Bool, std::remove_cvref_t<Args>...>::enable
        )
    Bool(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Bool, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Bool, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Bool, __explicit_init__<Bool, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Bool, std::remove_cvref_t<Args>...>::enable
        )
    explicit Bool(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Bool, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args> requires (impl::invocable<Self, "bit_length", Args...>)
    [[nodiscard]] decltype(auto) bit_length(Args&&... args) const {
        return impl::call_method<"bit_length">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "bit_count", Args...>)
    [[nodiscard]] decltype(auto) bit_count(Args&&... args) const {
        return impl::call_method<"bit_count">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "to_bytes", Args...>)
    [[nodiscard]] decltype(auto) to_bytes(Args&&... args) const {
        return impl::call_method<"to_bytes">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "from_bytes", Args...>)
    [[nodiscard]] static decltype(auto) from_bytes(Args&&... args) {
        return impl::call_static<Self, "from_bytes">(std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "as_integer_ratio", Args...>)
    [[nodiscard]] decltype(auto) as_integer_ratio(Args&&... args) const {
        return impl::call_method<"as_integer_ratio">(*this, std::forward<Args>(args)...);
    }

    template <typename... Args> requires (impl::invocable<Self, "is_integer", Args...>)
    [[nodiscard]] decltype(auto) is_integer(Args&&... args) const {
        return impl::call_method<"is_integer">(*this, std::forward<Args>(args)...);
    }

};


template <>
struct __init__<Bool>                                       : Returns<Bool> {
    static auto operator()() {
        return reinterpret_borrow<Bool>(Py_False);
    }
};


template <impl::cpp_like T> requires (impl::bool_like<T>)
struct __init__<Bool, T>                                    : Returns<Bool> {
    static auto operator()(const T& value) {
        return reinterpret_borrow<Bool>(value ? Py_True : Py_False);
    }
};


template <impl::python_like T> requires (!impl::bool_like<T>)
struct __explicit_init__<Bool, T>                           : Returns<Bool> {
    static auto operator()(const T& obj) {
        int result = PyObject_IsTrue(obj.ptr());
        if (result == -1) {
            Exception::from_python();
        }
        return reinterpret_borrow<Bool>(result ? Py_True : Py_False);
    }
};


template <impl::cpp_like T>
    requires (
        !impl::bool_like<T> &&
        impl::explicitly_convertible_to<T, bool>
    )
struct __explicit_init__<Bool, T>                           : Returns<Bool> {
    static auto operator()(const T& value) {
        return reinterpret_borrow<Bool>(
            static_cast<bool>(value) ? Py_True : Py_False
        );
    }
};


template <impl::cpp_like T>
    requires (
        !impl::bool_like<T> &&
        !impl::explicitly_convertible_to<T, bool> &&
        impl::has_size<T>
    )
struct __explicit_init__<Bool, T>                           : Returns<Bool> {
    static auto operator()(const T& obj) {
        return reinterpret_borrow<Bool>(std::size(obj) > 0 ? Py_True : Py_False);
    }
};


template <impl::cpp_like T>
    requires (
        !impl::bool_like<T> &&
        !impl::explicitly_convertible_to<T, bool> &&
        !impl::has_size<T> &&
        impl::has_empty<T>
    )
struct __explicit_init__<Bool, T>                           : Returns<Bool> {
    static auto operator()(const T& obj) {
        return reinterpret_borrow<Bool>(obj.empty() ? Py_False : Py_True);
    }
};


template <typename... Args>
struct __explicit_init__<Bool, std::tuple<Args...>>         : Returns<Bool> {
    static auto operator()(const std::tuple<Args...>& obj) {
        return reinterpret_borrow<Bool>(sizeof...(Args) > 0 ? Py_True : Py_False);
    }
};


template <size_t N>
struct __explicit_init__<Bool, char[N]>                     : Returns<Bool> {
    static auto operator()(const char(&string)[N]) {
        // NOTE: N includes a null terminator
        return reinterpret_borrow<Bool>(N > 1 ? Py_True : Py_False);
    }
};


template <>
struct __explicit_init__<Bool, const char*>                 : Returns<Bool> {
    static auto operator()(const char* str) {
        return reinterpret_borrow<Bool>(
            std::strcmp(str, "") != 0 ? Py_True : Py_False
        );
    }
};


template <std::derived_from<Bool> From>
struct __cast__<From, bool>                                 : Returns<bool> {
    static bool operator()(const From& from) {
        int result = PyObject_IsTrue(from.ptr());
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
};


template <std::derived_from<Bool> From, std::integral To>
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(const From& from) {
        return impl::implicit_cast<bool>(from);
    }
};


template <std::derived_from<Bool> From, std::floating_point To>
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(const From& from) {
        return impl::implicit_cast<bool>(from);
    }
};


inline const Bool True = reinterpret_borrow<Bool>(Py_True);
inline const Bool False = reinterpret_borrow<Bool>(Py_False);


}  // namespace py


#endif
