#ifndef BERTRAND_PYTHON_INT_H
#define BERTRAND_PYTHON_INT_H

#include "common.h"


// TODO: prevent int overflow during conversion to C++ integers.  Raise an OverflowError
// if this occurs.


namespace py {


template <typename T>
struct __issubclass__<T, Int>                               : Returns<bool> {
    static consteval bool operator()() { return impl::int_like<T>; }
};


template <typename T>
struct __isinstance__<T, Int>                               : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::is_object_exact<T>) {
            return PyLong_Check(ptr(obj));
        } else {
            return issubclass<T, Int>();
        }
    }
};


/* Represents a statically-typed Python integer in C++. */
class Int : public Object {
    using Base = Object;
    using Self = Int;

public:
    static const Type type;

    Int(Handle h, borrowed_t t) : Base(h, t) {}
    Int(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Int, __init__<Int, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Int, std::remove_cvref_t<Args>...>::enable
        )
    Int(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Int, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Int, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Int, __explicit_init__<Int, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Int, std::remove_cvref_t<Args>...>::enable
        )
    explicit Int(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Int, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
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

    static const Int neg_two;
    static const Int neg_one;
    static const Int zero;
    static const Int one;
    static const Int two;

};


template <std::integral T> requires (!std::same_as<bool, T>)
struct __as_object__<T> : Returns<Int> {};


template <>
struct __init__<Int>                                        : Returns<Int> {
    static auto operator()() {
        PyObject* result = PyLong_FromLong(0);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Int>(result);
    }
};


template <std::integral T>
struct __init__<Int, T>                                     : Returns<Int> {
    static auto operator()(T value) {
        PyObject* result;
        if constexpr (std::signed_integral<T>) {
            if constexpr (sizeof(T) <= sizeof(long)) {
                result = PyLong_FromLong(value);
            } else {
                result = PyLong_FromLongLong(value);
            }
        } else {
            if constexpr (sizeof(T) <= sizeof(long)) {
                result = PyLong_FromUnsignedLong(value);
            } else {
                result = PyLong_FromUnsignedLongLong(value);
            }
        }
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Int>(result);
    }
};


template <impl::python_like T> requires (impl::bool_like<T>)
struct __init__<Int, T>                                     : Returns<Int> {
    static auto operator()(const T& value) {
        PyObject* result = PyNumber_Long(value.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Int>(result);
    }
};


template <impl::cpp_like T> requires (impl::float_like<T>)
struct __explicit_init__<Int, T>                            : Returns<Int> {
    static auto operator()(T value) {
        PyObject* result = PyLong_FromDouble(value);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Int>(result);
    }
};


template <impl::cpp_like T>
    requires (!impl::float_like<T>) && (
        impl::explicitly_convertible_to<T, bool> ||
        impl::explicitly_convertible_to<T, int64_t> ||
        impl::explicitly_convertible_to<T, int32_t> ||
        impl::explicitly_convertible_to<T, int16_t> ||
        impl::explicitly_convertible_to<T, int8_t> ||
        impl::explicitly_convertible_to<T, long long> ||
        impl::explicitly_convertible_to<T, long> ||
        impl::explicitly_convertible_to<T, int> ||
        impl::explicitly_convertible_to<T, short> ||
        impl::explicitly_convertible_to<T, char> ||
        impl::explicitly_convertible_to<T, uint64_t> ||
        impl::explicitly_convertible_to<T, uint32_t> ||
        impl::explicitly_convertible_to<T, uint16_t> ||
        impl::explicitly_convertible_to<T, uint8_t> ||
        impl::explicitly_convertible_to<T, unsigned long long> ||
        impl::explicitly_convertible_to<T, unsigned long> ||
        impl::explicitly_convertible_to<T, unsigned int> ||
        impl::explicitly_convertible_to<T, unsigned short> ||
        impl::explicitly_convertible_to<T, unsigned char>
    )
struct __explicit_init__<Int, T>                            : Returns<Int> {
    template <typename U>
    static auto forward(U value) { return __init__<Int, U>{}(value); }
    static auto operator()(const T& value) {
        // NOTE: start with largest types to preserve as much precision as possible
        if constexpr (impl::explicitly_convertible_to<T, uint64_t>) {
            return forward(static_cast<uint64_t>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, unsigned long long>) {
            return forward(static_cast<unsigned long long>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, int64_t>) {
            return forward(static_cast<int64_t>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, long long>) {
            return forward(static_cast<long long>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, uint32_t>) {
            return forward(static_cast<uint32_t>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, unsigned long>) {
            return forward(static_cast<unsigned long>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, unsigned int>) {
            return forward(static_cast<unsigned int>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, int32_t>) {
            return forward(static_cast<int32_t>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, long>) {
            return forward(static_cast<int>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, int>) {
            return forward(static_cast<int>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, uint16_t>) {
            return forward(static_cast<uint16_t>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, unsigned short>) {
            return forward(static_cast<unsigned short>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, int16_t>) {
            return forward(static_cast<int16_t>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, short>) {
            return forward(static_cast<short>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, uint8_t>) {
            return forward(static_cast<uint8_t>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, unsigned char>) {
            return forward(static_cast<unsigned char>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, int8_t>) {
            return forward(static_cast<int8_t>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, char>) {
            return forward(static_cast<char>(value));
        } else if constexpr (impl::explicitly_convertible_to<T, bool>) {
            return forward(static_cast<bool>(value));
        } else {
            static_assert(false, "unreachable");
        }
    }
};


template <std::convertible_to<int> Base>
struct __explicit_init__<Int, const char*, Base>            : Returns<Int> {
    static auto operator()(const char* str, int base) {
        PyObject* result = PyLong_FromString(str, nullptr, base);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Int>(result);
    }
};
template <>
struct __explicit_init__<Int, const char*>                  : Returns<Int> {
    static auto operator()(const char* str) { return Int(str, 0); }
};
template <size_t N>
struct __explicit_init__<Int, char[N]>                      : Returns<Int> {
    static auto operator()(const char* str) { return Int(str, 0); }
};


template <std::convertible_to<int> Base>
struct __explicit_init__<Int, std::string, Base>             : Returns<Int> {
    static auto operator()(const std::string& str, int base) {
        return Int(str.c_str(), base);
    }
};
template <>
struct __explicit_init__<Int, std::string>                  : Returns<Int> {
    static auto operator()(const std::string& str) { return Int(str, 0); }
};


template <std::convertible_to<int> Base>
struct __explicit_init__<Int, std::string_view, Base>       : Returns<Int> {
    static auto operator()(const std::string_view& str, int base) {
        return Int(str.data(), base);
    }
};
template <>
struct __explicit_init__<Int, std::string_view>             : Returns<Int> {
    static auto operator()(const std::string_view& str) { return Int(str, 0); }
};


template <impl::python_like T, std::convertible_to<int> Base> requires (impl::str_like<T>)
struct __explicit_init__<Int, T, Base>                      : Returns<Int> {
    static auto operator()(const T& str, int base) {
        PyObject* result = PyLong_FromUnicodeObject(str.ptr(), base);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Int>(result);
    }
};
template <impl::python_like T> requires (impl::str_like<T>)
struct __explicit_init__<Int, T>                            : Returns<Int> {
    static auto operator()(const T& str) { return Int(str, 0); }
};


template <impl::python_like T>
    requires (!impl::bool_like<T> && !impl::int_like<T> && !impl::str_like<T>)
struct __explicit_init__<Int, T>                            : Returns<Int> {
    static auto operator()(const T& obj) {
        PyObject* result = PyNumber_Long(obj.ptr());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Int>(result);
    }
};


template <std::derived_from<Int> From, std::integral To>
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(const From& from) {
        if constexpr (sizeof(To) <= sizeof(long)) {
            if constexpr (std::signed_integral<To>) {
                long result = PyLong_AsLong(from.ptr());
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            } else {
                unsigned long result = PyLong_AsUnsignedLong(from.ptr());
                if (result == (unsigned long) -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            }
        } else {
            if constexpr (std::signed_integral<To>) {
                long long result = PyLong_AsLongLong(from.ptr());
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
            } else {
                unsigned long long result = PyLong_AsUnsignedLongLong(from.ptr());
                if (result == (unsigned long long) -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            }
        }
    }
};


template <std::derived_from<Int> From, std::floating_point To>
struct __cast__<From, To>                                   : Returns<To> {
    static To operator()(const From& from) {
        return PyLong_AsDouble(from.ptr());
    }
};


// TODO: maybe I can add an explicit conversion to any type that is explicitly
// convertible from long long?


inline const Int Int::neg_two = -2;
inline const Int Int::neg_one = -1;
inline const Int Int::zero = 0;
inline const Int Int::one = 1;
inline const Int Int::two = 2;


}  // namespace py


#endif
