#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_CONTROL_H
#define BERTRAND_PYTHON_COMMON_CONTROL_H


#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"
#include "func.h"


namespace bertrand {
namespace py {


namespace impl {

    template <typename T>
    struct unwrap_proxy_helper { using type = T; };
    template <proxy_like T>
    struct unwrap_proxy_helper<T> { using type = typename T::Wrapped; };
    template <typename T>
    using unwrap_proxy = typename unwrap_proxy_helper<T>::type;

}


template <std::derived_from<Object> T>
struct __as_object__<T>                                     : Returns<T> {};
template <typename R, typename... A>
struct __as_object__<R(A...)>                               : Returns<Function<R(A...)>> {};
template <typename R, typename... A>
struct __as_object__<R(*)(A...)>                            : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...)>                         : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) noexcept>                : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const>                   : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const noexcept>          : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) volatile>                : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) volatile noexcept>       : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const volatile>          : Returns<Function<R(A...)>> {};
template <typename R, typename C, typename... A>
struct __as_object__<R(C::*)(A...) const volatile noexcept> : Returns<Function<R(A...)>> {};
template <typename R, typename... A>
struct __as_object__<std::function<R(A...)>>                : Returns<Function<R(A...)>> {};
// template <std::derived_from<pybind11::function> T>
// struct __as_object__<T>                                  : Returns<Function<>>> {};
template <>
struct __as_object__<std::nullptr_t>                        : Returns<NoneType> {};
template <>
struct __as_object__<std::nullopt_t>                        : Returns<NoneType> {};
template <std::derived_from<pybind11::none> T>
struct __as_object__<T>                                     : Returns<NoneType> {};
template <std::derived_from<pybind11::ellipsis> T>
struct __as_object__<T>                                     : Returns<EllipsisType> {};
template <std::derived_from<pybind11::slice> T>
struct __as_object__<T>                                     : Returns<Slice> {};
template <std::derived_from<pybind11::module_> T>
struct __as_object__<T>                                     : Returns<Module> {};
template <>
struct __as_object__<bool>                                  : Returns<Bool> {};
template <std::derived_from<pybind11::bool_> T>
struct __as_object__<T>                                     : Returns<Bool> {};
template <std::integral T> requires (!std::same_as<bool, T>)
struct __as_object__<T>                                     : Returns<Int> {};
template <std::derived_from<pybind11::int_> T>
struct __as_object__<T>                                     : Returns<Int> {};
template <std::floating_point T>
struct __as_object__<T>                                     : Returns<Float> {};
template <std::derived_from<pybind11::float_> T>
struct __as_object__<T>                                     : Returns<Float> {};
template <impl::complex_like T> requires (!std::derived_from<T, Object>)
struct __as_object__<T>                                     : Returns<Complex> {};
template <>
struct __as_object__<const char*>                           : Returns<Str> {};
template <size_t N>
struct __as_object__<const char(&)[N]>                      : Returns<Str> {};
template <std::derived_from<std::string> T>
struct __as_object__<T>                                     : Returns<Str> {};
template <std::derived_from<std::string_view> T>
struct __as_object__<T>                                     : Returns<Str> {};
template <std::derived_from<pybind11::str> T>
struct __as_object__<T>                                     : Returns<Str> {};
template <>
struct __as_object__<void*>                                 : Returns<Bytes> {};
template <std::derived_from<pybind11::bytes> T>
struct __as_object__<T>                                     : Returns<Bytes> {};
template <std::derived_from<pybind11::bytearray> T>
struct __as_object__<T>                                     : Returns<ByteArray> {};
template <typename... Args>
struct __as_object__<std::chrono::duration<Args...>>        : Returns<Timedelta> {};
// TODO: std::time_t?
template <typename... Args>
struct __as_object__<std::chrono::time_point<Args...>>      : Returns<Datetime> {};
template <typename First, typename Second>
struct __as_object__<std::pair<First, Second>>              : Returns<Tuple<Object>> {};  // TODO: should return Struct?
template <typename... Args>
struct __as_object__<std::tuple<Args...>>                   : Returns<Tuple<Object>> {};  // TODO: should return Struct?
template <typename T, size_t N>
struct __as_object__<std::array<T, N>>                      : Returns<Tuple<impl::as_object_t<T>>> {};
template <std::derived_from<pybind11::tuple> T>
struct __as_object__<T>                                     : Returns<Tuple<Object>> {};
template <typename T, typename... Args>
struct __as_object__<std::vector<T, Args...>>               : Returns<List<impl::as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::deque<T, Args...>>                : Returns<List<impl::as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::list<T, Args...>>                 : Returns<List<impl::as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::forward_list<T, Args...>>         : Returns<List<impl::as_object_t<T>>> {};
template <std::derived_from<pybind11::list> T>
struct __as_object__<T>                                     : Returns<List<Object>> {};
template <typename T, typename... Args>
struct __as_object__<std::unordered_set<T, Args...>>        : Returns<Set<impl::as_object_t<T>>> {};
template <typename T, typename... Args>
struct __as_object__<std::set<T, Args...>>                  : Returns<Set<impl::as_object_t<T>>> {};
template <std::derived_from<pybind11::set> T>
struct __as_object__<T>                                     : Returns<Set<Object>> {};
template <std::derived_from<pybind11::frozenset> T>
struct __as_object__<T>                                     : Returns<FrozenSet<Object>> {};
template <typename K, typename V, typename... Args>
struct __as_object__<std::unordered_map<K, V, Args...>>     : Returns<Dict<impl::as_object_t<K>, impl::as_object_t<V>>> {};
template <typename K, typename V, typename... Args>
struct __as_object__<std::map<K, V, Args...>>               : Returns<Dict<impl::as_object_t<K>, impl::as_object_t<V>>> {};
template <std::derived_from<pybind11::dict> T>
struct __as_object__<T>                                     : Returns<Dict<Object, Object>> {};
template <std::derived_from<pybind11::type> T>
struct __as_object__<T>                                     : Returns<Type> {};


/* Implicitly convert a Python object to a pybind11::handle. */
template <typename Self>
struct __implicit_cast__<Self, pybind11::handle> : Returns<pybind11::handle> {
    static pybind11::handle operator()(const Self& self) {
        return self.ptr();
    }
};


/* Implicitly convert a Python object to a pybind11::object */
template <typename Self>
struct __implicit_cast__<Self, pybind11::object> : Returns<pybind11::object> {
    static pybind11::object operator()(const Self& self) {
        return pybind11::reinterpret_borrow<pybind11::object>(self.ptr());
    }
};


/* Implicitly convert a Python object to an equivalent pybind11 type. */
template <typename Self, impl::pybind11_like T> requires (Self::template typecheck<T>())
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return pybind11::reinterpret_borrow<T>(self.ptr());
    }
};


/* Implicitly convert a Python object to one of its subclasses by applying a type
check. */
template <typename Self, std::derived_from<Self> T>
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        if (!T::typecheck(self)) {
            throw impl::noconvert<T>(self.ptr());
        }
        return reinterpret_borrow<T>(self.ptr());
    }
};


/* Implicitly convert a Python object to a proxy type by converting to the contained
type and then moving the result into the proxy's buffer. */
template <typename Self, impl::proxy_like T>
    requires (__implicit_cast__<Self, impl::unwrap_proxy<T>>::enable)
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return T(__implicit_cast__<Self, impl::unwrap_proxy<T>>::operator()(self));
    }
};


/* Implicitly convert a py::Object to any type that is not a pointer or reference type,
an internal pybind11 type, or a subclass of Object using pybind11's cast() mechanism. */
template <impl::not_proxy_like T>
    requires (
        !std::is_pointer_v<T> &&
        !std::is_reference_v<T> &&
        !std::same_as<T, pybind11::handle> &&
        !std::derived_from<T, pybind11::object> &&
        !std::derived_from<T, pybind11::arg> &&
        !std::derived_from<T, Object>
    )
struct __implicit_cast__<Object, T> : Returns<T> {
    static T operator()(const Object& self) {
        try {
            return Handle(self.ptr()).template cast<T>();
        } catch (...) {
            Exception::from_pybind11();
        }
    }
};


/* Implicitly convert a py::Bool to a C++ bool. */
template <std::derived_from<Bool> Self>
struct __implicit_cast__<Self, bool> : Returns<bool> {
    static bool operator()(const Self& self) {
        int result = PyObject_IsTrue(self.ptr());
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
};


/* Implicitly convert a py::Bool to a C++ integer type. */
template <std::derived_from<Bool> Self, std::integral T>
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return __implicit_cast__<Self, bool>::operator()(self);
    }
};


/* Implicitly convert a py::Bool to a C++ floating point type. */
template <std::derived_from<Bool> Self, std::floating_point T>
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return __implicit_cast__<Self, bool>::operator()(self);
    }
};


/* Implicitly convert a py::Int to a C++ integer type. */
template <std::derived_from<Int> Self, std::integral T>
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        if constexpr (sizeof(T) <= sizeof(long)) {
            if constexpr (std::signed_integral<T>) {
                long result = PyLong_AsLong(self.ptr());
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            } else {
                unsigned long result = PyLong_AsUnsignedLong(self.ptr());
                if (result == (unsigned long) -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            }
        } else {
            if constexpr (std::signed_integral<T>) {
                long long result = PyLong_AsLongLong(self.ptr());
                if (result == -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
            } else {
                unsigned long long result = PyLong_AsUnsignedLongLong(self.ptr());
                if (result == (unsigned long long) -1 && PyErr_Occurred()) {
                    Exception::from_python();
                }
                return result;
            }
        }
    }
};


/* Implicitly promote a py::Int to a C++ floating point type. */
template <std::derived_from<Int> Self, std::floating_point T>
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return PyLong_AsDouble(self.ptr());
    }
};


/* Implicitly convert a py::Float to a C++ floating point type. */
template <std::derived_from<Float> Self, std::floating_point T>
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return PyFloat_AS_DOUBLE(self.ptr());
    }
};


/* Implicitly convert a py::Complex to a C++ std::complex<>. */
template <std::derived_from<Complex> Self, typename T>
struct __implicit_cast__<Self, std::complex<T>> : Returns<std::complex<T>> {
    static std::complex<T> operator()(const Self& self) {
        Py_complex complex = PyComplex_AsCComplex(self.ptr());
        if (complex.real == -1.0 && PyErr_Occurred()) {
            Exception::from_python();
        }
        return std::complex<T>(complex.real, complex.imag);
    }
};


/* Implicitly convert a py::Str into a C++ std::string. */
template <std::derived_from<Str> Self>
struct __implicit_cast__<Self, std::string> : Returns<std::string> {
    static std::string operator()(const Self& self) {
        Py_ssize_t length;
        const char* result = PyUnicode_AsUTF8AndSize(self.ptr(), &length);
        if (result == nullptr) {
            Exception::from_python();
        }
        return {result, static_cast<size_t>(length)};
    }
};


/* Implicitly convert a py::Tuple into a pybind11::tuple regardless of its template
signature. */
template <std::derived_from<impl::TupleTag> Self, impl::tuple_like T>
    requires (impl::pybind11_like<T> && !Self::template typecheck<T>())
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return reinterpret_borrow<T>(self.ptr());
    }
};


/* Implicitly convert a py::Tuple into a C++ std::pair if and only if the types are
convertible and it is of length 2. */
template <std::derived_from<impl::TupleTag> Self, typename First, typename Second>
    requires (
        std::convertible_to<typename Self::value_type, First> &&
        std::convertible_to<typename Self::value_type, Second>
    )
struct __implicit_cast__<Self, std::pair<First, Second>> : Returns<std::pair<First, Second>> {
    static std::pair<First, Second> operator()(const Self& self) {
        if (self.size() != 2) {
            throw IndexError(
                "conversion to std::pair requires tuple of size 2, not " +
                std::to_string(self.size())
            );
        }
        return {
            impl::implicit_cast<First>(self.GET_ITEM(0)),
            impl::implicit_cast<Second>(self.GET_ITEM(1))
        };
    }
};


/* Implicitly convert a py::Tuple into a C++ std::tuple if and only if the types are
convertible and the length matches the tuple's template signature. */
template <std::derived_from<impl::TupleTag> Self, typename... Args>
    requires (std::convertible_to<typename Self::value_type, Args> && ...)
struct __implicit_cast__<Self, std::tuple<Args...>> : Returns<std::tuple<Args...>> {
    static std::tuple<Args...> operator()(const Self& self) {
        if (self.size() != sizeof...(Args)) {
            throw IndexError(
                "conversion to std::tuple requires tuple of size " +
                std::to_string(sizeof...(Args)) + ", not " +
                std::to_string(self.size())
            );
        }
        return [&]<size_t... N>(std::index_sequence<N...>) {
            return std::make_tuple(
                impl::implicit_cast<Args>(self.GET_ITEM(N))...
            );
        }(std::index_sequence_for<Args...>{});
    }
};


/* Implicitly convert a py::Tuple into a C++ std::array if and only if the types are
convertible and the length matches the array's template signature. */
template <std::derived_from<impl::TupleTag> Self, typename T, size_t N>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __implicit_cast__<Self, std::array<T, N>> : Returns<std::array<T, N>> {
    static std::array<T, N> operator()(const Self& self) {
        if (N != self.size()) {
            throw IndexError(
                "conversion to std::array requires tuple of size " +
                std::to_string(N) + ", not " + std::to_string(self.size())
            );
        }
        std::array<T, N> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = impl::implicit_cast<T>(self.GET_ITEM(i));
        }
        return result;
    }
};


/* Implicitly convert a py::Tuple into a C++ std::vector if the contained type is
convertible. */
template <std::derived_from<impl::TupleTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __implicit_cast__<Self, std::vector<T, Args...>> : Returns<std::vector<T, Args...>> {
    static std::vector<T, Args...> operator()(const Self& self) {
        std::vector<T, Args...> result;
        result.reserve(self.size());
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


/* Implicitly convert a py::Tuple into a C++ std::list if the contained type is
convertible. */
template <std::derived_from<impl::TupleTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __implicit_cast__<Self, std::list<T, Args...>> : Returns<std::list<T, Args...>> {
    static std::list<T, Args...> operator()(const Self& self) {
        std::list<T, Args...> result;
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


/* Implicitly convert a py::Tuple into a C++ std::forward_list if the contained type is
convertible. */
template <std::derived_from<impl::TupleTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __implicit_cast__<Self, std::forward_list<T, Args...>> : Returns<std::forward_list<T, Args...>> {
    static std::forward_list<T, Args...> operator()(const Self& self) {
        std::forward_list<T, Args...> result;
        auto it = self.rbegin();
        auto end = self.rend();
        while (it != end) {
            result.push_front(impl::implicit_cast<T>(*it));
            ++it;
        }
        return result;
    }
};


/* Implicitly convert a py::Tuple into a C++ std::deque if the contained type is
convertible. */
template <std::derived_from<impl::TupleTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __implicit_cast__<Self, std::deque<T, Args...>> : Returns<std::deque<T, Args...>> {
    static std::deque<T, Args...> operator()(const Self& self) {
        std::deque<T, Args...> result;
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


/* Implicitly convert a py::List into a pybind11::list regardless of its template
signature. */
template <std::derived_from<impl::ListTag> Self, impl::list_like T>
    requires (impl::pybind11_like<T> && !Self::template typecheck<T>())
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        return reinterpret_borrow<T>(self.ptr());
    }
};


/* Implicitly convert a py::List into a C++ std::pair if and only if the types are
convertible and it is of length 2. */
template <std::derived_from<impl::ListTag> Self, typename First, typename Second>
    requires (
        std::convertible_to<typename Self::value_type, First> &&
        std::convertible_to<typename Self::value_type, Second>
    )
struct __implicit_cast__<Self, std::pair<First, Second>> : Returns<std::pair<First, Second>> {
    static std::pair<First, Second> operator()(const Self& self) {
        if (self.size() != 2) {
            throw IndexError(
                "conversion to std::pair requires list of size 2, not "
                + std::to_string(self.size())
            );
        }
        return {
            impl::implicit_cast<First>(self.GET_ITEM(0)),
            impl::implicit_cast<Second>(self.GET_ITEM(1))
        };
    }
};


/* Implicitly convert a py::List into a C++ std::tuple if and only if the types are
convertible and the length matches the tuple's template signature. */
template <std::derived_from<impl::ListTag> Self, typename... Args>
    requires (std::convertible_to<typename Self::value_type, Args> && ...)
struct __implicit_cast__<Self, std::tuple<Args...>> : Returns<std::tuple<Args...>> {
    static std::tuple<Args...> operator()(const Self& self) {
        if (self.size() != sizeof...(Args)) {
            throw IndexError(
                "conversion to std::tuple requires list of size " +
                std::to_string(sizeof...(Args)) + ", not " +
                std::to_string(self.size())
            );
        }
        return [&]<size_t... N>(std::index_sequence<N...>) {
            return std::make_tuple(
                impl::implicit_cast<Args>(self.GET_ITEM(N))...
            );
        }(std::index_sequence_for<Args...>{});
    }
};


/* Implicitly convert a py::List into a C++ std::array if and only if the types are
convertible and the length matches the array's template signature. */
template <std::derived_from<impl::ListTag> Self, typename T, size_t N>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __implicit_cast__<Self, std::array<T, N>> : Returns<std::array<T, N>> {
    static std::array<T, N> operator()(const Self& self) {
        if (N != self.size()) {
            throw IndexError(
                "conversion to std::array requires list of size " +
                std::to_string(N) + ", not " + std::to_string(self.size())
            );
        }
        std::array<T, N> result;
        for (size_t i = 0; i < N; ++i) {
            result[i] = impl::implicit_cast<T>(self.GET_ITEM(i));
        }
        return result;
    }
};


/* Implicitly convert a py::List into a C++ std::vector if the contained type is
convertible. */
template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __implicit_cast__<Self, std::vector<T, Args...>> : Returns<std::vector<T, Args...>> {
    static std::vector<T, Args...> operator()(const Self& self) {
        std::vector<T, Args...> result;
        result.reserve(self.size());
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


/* Implicitly convert a py::List into a C++ std::list if the contained type is
convertible. */
template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __implicit_cast__<Self, std::list<T, Args...>> : Returns<std::list<T, Args...>> {
    static std::list<T, Args...> operator()(const Self& self) {
        std::list<T, Args...> result;
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


/* Implicitly convert a py::List into a C++ std::forward_list if the contained type is
convertible. */
template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __implicit_cast__<Self, std::forward_list<T, Args...>> : Returns<std::forward_list<T, Args...>> {
    static std::forward_list<T, Args...> operator()(const Self& self) {
        std::forward_list<T, Args...> result;
        auto it = self.rbegin();
        auto end = self.rend();
        while (it != end) {
            result.push_front(impl::implicit_cast<T>(*it));
            ++it;
        }
        return result;
    }
};


/* Implicitly convert a py::List into a C++ std::deque if the contained type is
convertible. */
template <std::derived_from<impl::ListTag> Self, typename T, typename... Args>
    requires (std::convertible_to<typename Self::value_type, T>)
struct __implicit_cast__<Self, std::deque<T, Args...>> : Returns<std::deque<T, Args...>> {
    static std::deque<T, Args...> operator()(const Self& self) {
        std::deque<T, Args...> result;
        for (const auto& item : self) {
            result.push_back(impl::implicit_cast<T>(item));
        }
        return result;
    }
};


/* Implicitly convert a py::Set to a C++ set type. */
template <std::derived_from<impl::SetTag> Self, impl::cpp_like T>
    requires (impl::anyset_like<T>)
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        T result;
        for (const auto& item : self) {
            result.insert(static_cast<typename T::value_type>(item));
        }
        return result;
    }
};


/* Implicitly convert a py::FrozenSet to a C++ set type. */
template <std::derived_from<impl::FrozenSetTag> Self, impl::cpp_like T>
    requires (impl::anyset_like<T>)
struct __implicit_cast__<Self, T> : Returns<T> {
    static T operator()(const Self& self) {
        T result;
        for (const auto& item : self) {
            result.insert(static_cast<typename T::value_type>(item));
        }
        return result;
    }
};


/* Implicitly convert a py::Dict into a C++ mapping type. */
template <std::derived_from<impl::DictTag> Self, impl::cpp_like T>
    requires (impl::dict_like<T>)
struct __implicit_cast__<Self, T> {
    static T operator()(const Self& self) {
        T result;
        PyObject* k;
        PyObject* v;
        Py_ssize_t pos = 0;
        while (PyDict_Next(self.ptr(), &pos, &k, &v)) {
            auto key = reinterpret_borrow<typename Self::key_type>(k);
            auto value = reinterpret_borrow<typename Self::value_type>(v);
            result[impl::implicit_cast<typename T::key_type>(key)] =
                impl::implicit_cast<typename T::mapped_type>(value);
        }
        return result;
    }
};


/* Explicitly convert a Python object into an arbitrary C++ type by invoking
pybind11::cast. */
template <typename Self, typename T>
inline T __explicit_cast__<Self, T>::operator()(const Self& self) {
    try {
        return Handle(self.ptr()).template cast<T>();
    } catch (...) {
        Exception::from_pybind11();
    }
}


/* Explicitly convert a Python object into a std::string by calling `str(obj)`. */
template <std::derived_from<Object> Self> 
struct __explicit_cast__<Self, std::string> : Returns<std::string> {
    static std::string operator()(const Self& self) {
        PyObject* str = PyObject_Str(self.ptr());
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
    }
};


template <impl::proxy_like Self, StaticStr Name>
struct __getattr__<Self, Name> : __getattr__<impl::unwrap_proxy<Self>, Name> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__init__")
struct __getattr__<Self, Name>                              : Returns<Function<
    void(Arg<"args", Object>::args, Arg<"kwargs", Object>::kwargs)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__new__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"args", Object>::args, Arg<"kwargs", Object>::kwargs)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__call__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"args", Object>::args, Arg<"kwargs", Object>::kwargs)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__name__")
struct __getattr__<Self, Name>                              : Returns<Str> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__qualname__")
struct __getattr__<Self, Name>                              : Returns<Str> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__module__")
struct __getattr__<Self, Name>                              : Returns<Dict<Str, Object>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__self__")
struct __getattr__<Self, Name>                              : Returns<Object> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__wrapped__")
struct __getattr__<Self, Name>                              : Returns<Object> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__repr__")
struct __getattr__<Self, Name>                              : Returns<Function<Str()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__str__")
struct __getattr__<Self, Name>                              : Returns<Function<Str()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__bool__")
struct __getattr__<Self, Name>                              : Returns<Function<Bool()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__int__")
struct __getattr__<Self, Name>                              : Returns<Function<Int()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__index__")
struct __getattr__<Self, Name>                              : Returns<Function<Int()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__float__")
struct __getattr__<Self, Name>                              : Returns<Function<Float()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__complex__")
struct __getattr__<Self, Name>                              : Returns<Function<Complex()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__bytes__")
struct __getattr__<Self, Name>                              : Returns<Function<Bytes()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__hash__")
struct __getattr__<Self, Name>                              : Returns<Function<Int()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__slots__")
struct __getattr__<Self, Name>                              : Returns<Object> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__dict__")
struct __getattr__<Self, Name>                              : Returns<Dict<Str, Object>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__dir__")
struct __getattr__<Self, Name>                              : Returns<Function<List<Str>()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__doc__")
struct __getattr__<Self, Name>                              : Returns<Str> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__class__")
struct __getattr__<Self, Name>                              : Returns<Type> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__bases__")
struct __getattr__<Self, Name>                              : Returns<Tuple<Type>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__mro__")
struct __getattr__<Self, Name>                              : Returns<Tuple<Type>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__subclasses__")
struct __getattr__<Self, Name>                              : Returns<Function<List<Type>()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__subclasscheck__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"subclass", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__instancecheck__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"instance", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__init_subclass__")
struct __getattr__<Self, Name>                              : Returns<Function<
    void(Arg<"args", Object>::args, Arg<"kwargs", Object>::kwargs)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__class_getitem__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"item", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__set_name__")
struct __getattr__<Self, Name>                              : Returns<Function<
    void(Arg<"owner", Type>, Arg<"name", Str>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__get__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"instance", Object>, Arg<"owner", Type>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__set__")
struct __getattr__<Self, Name>                              : Returns<Function<
    void(Arg<"instance", Object>, Arg<"value", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__del__")
struct __getattr__<Self, Name>                              : Returns<Function<void()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__delete__")
struct __getattr__<Self, Name>                              : Returns<Function<
    void(Arg<"instance", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__getattribute__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"name", Str>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__getattr__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"name", Str>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__setattr__")
struct __getattr__<Self, Name>                              : Returns<Function<
    void(Arg<"name", Str>, Arg<"value", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__delattr__")
struct __getattr__<Self, Name>                              : Returns<Function<
    void(Arg<"name", Str>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__getitem__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"key", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__setitem__")
struct __getattr__<Self, Name>                              : Returns<Function<
    void(Arg<"key", Object>, Arg<"value", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__delitem__")
struct __getattr__<Self, Name>                              : Returns<Function<
    void(Arg<"key", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__missing__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"key", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__contains__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"key", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__enter__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__exit__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"exc_type", Type>, Arg<"exc_value", Object>, Arg<"traceback", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__aenter__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__aexit__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"exc_type", Type>, Arg<"exc_value", Object>, Arg<"traceback", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__iter__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__next__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__aiter__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__anext__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__reversed__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__len__")
struct __getattr__<Self, Name>                              : Returns<Function<Int()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__length_hint__")
struct __getattr__<Self, Name>                              : Returns<Function<Int()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__await__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__buffer__")
struct __getattr__<Self, Name>                              : Returns<Function<
    MemoryView(Arg<"flags", Int>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__release_buffer__")
struct __getattr__<Self, Name>                              : Returns<Function<
    void(Arg<"buffer", MemoryView>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__match_args__")
struct __getattr__<Self, Name>                              : Returns<Tuple<Str>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__objclass__")
struct __getattr__<Self, Name>                              : Returns<Object> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__format__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Str(Arg<"format_spec", Str>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__type_params__")
struct __getattr__<Self, Name>                              : Returns<Tuple<Object>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__weakref__")
struct __getattr__<Self, Name>                              : Returns<Object> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__abs__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__invert__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__pos__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__neg__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__round__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__trunc__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__floor__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ceil__")
struct __getattr__<Self, Name>                              : Returns<Function<Object()>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__lt__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__le__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__eq__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ne__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ge__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__gt__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Bool(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__add__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__radd__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__iadd__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__sub__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rsub__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__isub__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__mul__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rmul__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__imul__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__matmul__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rmatmul__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__imatmul__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__truediv__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rtruediv__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__itruediv__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__floordiv__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rfloordiv__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ifloordiv__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__mod__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rmod__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__imod__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__divmod__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Tuple<Object>(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__pow__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>, Arg<"mod", Object>::opt)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rpow__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>, Arg<"mod", Object>::opt)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ipow__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>, Arg<"mod", Object>::opt)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__lshift__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rlshift__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ilshift__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rshift__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rrshift__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__irshift__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__and__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rand__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__iand__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__or__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ror__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ior__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__xor__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rxor__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ixor__")
struct __getattr__<Self, Name>                              : Returns<Function<
    Object(Arg<"other", Object>)
>> {};


template <impl::proxy_like Self, StaticStr Name, impl::not_proxy_like Value>
struct __setattr__<Self, Name, Value> : __setattr__<impl::unwrap_proxy<Self>, Name, Value> {};
template <impl::not_proxy_like Self, StaticStr Name, impl::proxy_like Value>
struct __setattr__<Self, Name, Value> : __setattr__<Self, Name, impl::unwrap_proxy<Value>> {};
template <impl::proxy_like Self, StaticStr Name, impl::proxy_like Value>
struct __setattr__<Self, Name, Value> : __setattr__<impl::unwrap_proxy<Self>, Name, impl::unwrap_proxy<Value>> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__init__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__new__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__call__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__name__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__qualname__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__module__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__self__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__wrapped__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__repr__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__str__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__bool__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__int__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__index__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__float__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__complex__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__bytes__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__hash__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__slots__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__dict__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__dir__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__doc__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__class__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__bases__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__mro__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__subclasses__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__subclasscheck__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__instancecheck__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__init_subclass__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__class_getitem__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__set_name__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__get__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__set__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__del__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__delete__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__getattribute__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__getattr__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__setattr__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__delattr__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__getitem__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__setitem__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__delitem__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__missing__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__contains__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__enter__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__exit__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__aenter__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__aexit__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__iter__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__next__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__aiter__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__anext__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__reversed__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__len__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__length_hint__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__await__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__buffer__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__release_buffer__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__match_args__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__objclass__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__format__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__type_params__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__weakref__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__abs__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__invert__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__pos__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__neg__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__round__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__trunc__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__floor__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__ceil__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__lt__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__le__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__eq__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__ne__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__ge__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__gt__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__add__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__radd__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__iadd__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__sub__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rsub__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__isub__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__mul__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rmul__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__imul__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__matmul__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rmatmul__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__imatmul__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__truediv__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rtruediv__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__itruediv__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__floordiv__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rfloordiv__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__ifloordiv__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__mod__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rmod__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__imod__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__divmod__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rdivmod__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__pow__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rpow__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__ipow__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__lshift__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rlshift__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__ilshift__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rshift__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rrshift__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__irshift__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__and__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rand__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__iand__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__or__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__ror__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__ior__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__xor__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name, typename Value>
    requires (
        Name == "__rxor__" && __getattr__<Self, Name>::enable &&
        std::convertible_to<Value, typename __getattr__<Self, Name>::Return>
    )
struct __setattr__<Self, Name, Value>                       : Returns<void> {};


template <impl::proxy_like Self, StaticStr Name>
struct __delattr__<Self, Name> : __delattr__<impl::unwrap_proxy<Self>, Name> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__init__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__new__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__call__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__name__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__qualname__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__module__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__self__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__wrapped__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__repr__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__str__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__bool__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__int__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__index__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__float__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__complex__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__bytes__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__hash__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__slots__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__dict__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__dir__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__doc__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__class__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__bases__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__mro__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__subclasses__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__subclasscheck__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__instancecheck__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__init_subclass__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__class_getitem__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__set_name__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__get__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__set__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__del__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__delete__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__getattribute__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__getattr__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__setattr__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__delattr__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__getitem__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__setitem__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__delitem__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__missing__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__contains__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__enter__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__exit__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__aenter__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__aexit__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__iter__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__next__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__aiter__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__anext__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__reversed__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__len__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__length_hint__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__await__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__buffer__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__release_buffer__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__match_args__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__objclass__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__format__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__type_params__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__weakref__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__abs__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__invert__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__pos__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__neg__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__round__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__trunc__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__floor__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ceil__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__lt__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__le__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__eq__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ne__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ge__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__gt__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__add__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__radd__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__iadd__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__sub__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rsub__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__isub__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__mul__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rmul__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__imul__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__matmul__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rmatmul__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__imatmul__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__truediv__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rtruediv__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__itruediv__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__floordiv__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rfloordiv__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ifloordiv__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__mod__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rmod__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__imod__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__divmod__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rdivmod__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__pow__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rpow__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ipow__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__lshift__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rlshift__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ilshift__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rshift__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rrshift__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__irshift__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__and__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rand__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__iand__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__or__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ror__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ior__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__xor__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__rxor__")
struct __delattr__<Self, Name>                              : Returns<void> {};
template <std::derived_from<Object> Self, StaticStr Name> requires (Name == "__ixor__")
struct __delattr__<Self, Name>                              : Returns<void> {};


template <StaticStr Name>
struct __getattr__<Object, Name>                            : Returns<Object> {};
template <StaticStr Name, std::convertible_to<Object> Value>
struct __setattr__<Object, Name, Value>                     : Returns<void> {};
template <StaticStr Name>
struct __delattr__<Object, Name>                            : Returns<void> {};


template <std::derived_from<impl::FunctionTag> Self>
struct __getattr__<Self, "__func__">                        : Returns<Function<
    Object(Arg<"args", Object>::args, Arg<"kwargs", Object>::kwargs)
>> {};
template <std::derived_from<impl::FunctionTag> Self>
struct __getattr__<Self, "__code__">                        : Returns<Code> {};
template <std::derived_from<impl::FunctionTag> Self>
struct __getattr__<Self, "__globals__">                     : Returns<Dict<Str, Object>> {};
template <std::derived_from<impl::FunctionTag> Self>
struct __getattr__<Self, "__closure__">                     : Returns<Tuple<Object>> {};
template <std::derived_from<impl::FunctionTag> Self>
struct __getattr__<Self, "__defaults__">                    : Returns<Tuple<Object>> {};
template <std::derived_from<impl::FunctionTag> Self>
struct __getattr__<Self, "__kwdefaults__">                  : Returns<Dict<Str, Object>> {};
template <std::derived_from<impl::FunctionTag> Self>
struct __getattr__<Self, "__annotations__">                 : Returns<Dict<Str, Object>> {};


template <std::derived_from<Slice> Self>
struct __getattr__<Self, "indices">                             : Returns<Function<
    Tuple<Int>(Arg<"length", const Int&>)
>> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "start">                               : Returns<Object> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "stop">                                : Returns<Object> {};
template <std::derived_from<Slice> Self>
struct __getattr__<Self, "step">                                : Returns<Object> {};


template <StaticStr Name>
struct __getattr__<Module, Name>                            : Returns<Object> {};
template <StaticStr Name, std::convertible_to<Object> Value>
struct __setattr__<Module, Name, Value>                     : Returns<void> {};
template <StaticStr Name>
struct __delattr__<Module, Name>                            : Returns<void> {};


template <StaticStr Name>
struct __getattr__<Type, Name>                              : Returns<Object> {};
template <StaticStr Name, std::convertible_to<Object> Value>
struct __setattr__<Type, Name, Value>                       : Returns<void> {};
template <StaticStr Name>
struct __delattr__<Type, Name>                              : Returns<void> {};


template <StaticStr Name>
struct __getattr__<Super, Name>                             : Returns<Object> {};
template <StaticStr Name, std::convertible_to<Object> Value>
struct __setattr__<Super, Name, Value>                      : Returns<void> {};
template <StaticStr Name>
struct __delattr__<Super, Name>                             : Returns<void> {};





// TODO: others, in logical order





template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "capitalize">                      : Returns<Function<
    Bytes()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "center">                          : Returns<Function<
    Bytes(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillchar", const Bytes&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "count">                           : Returns<Function<
    Int(
        typename Arg<"sub", const Bytes&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "decode">                          : Returns<Function<
    Str(
        typename Arg<"encoding", const Str&>::opt,
        typename Arg<"errors", const Str&>::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "endswith">                        : Returns<Function<
    Bool(
        typename Arg<"suffix", const Bytes&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "expandtabs">                      : Returns<Function<
    Bytes(typename Arg<"tabsize", const Int&>::opt)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "find">                            : Returns<Function<
    Int(
        typename Arg<"sub", const Bytes&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "fromhex">                         : Returns<Function<
    Bytes(Arg<"string", const Str&>::pos)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "hex">                             : Returns<Function<
    Str(
        typename Arg<"sep", const Bytes&>::opt,
        typename Arg<"bytes_per_sep", const Int&>::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "index">                           : Returns<Function<
    Int(
        typename Arg<"sub", const Bytes&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "isalnum">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "isalpha">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "isascii">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "isdigit">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "islower">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "isspace">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "istitle">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "isupper">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "join">                            : Returns<Function<
    Bytes(typename Arg<"iterable", const Object&>::pos)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "ljust">                           : Returns<Function<
    Bytes(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillbyte", const Bytes&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "lower">                           : Returns<Function<
    Bytes()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "lstrip">                          : Returns<Function<
    Bytes(typename Arg<"chars", const Bytes&>::opt)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "maketrans">                       : Returns<Function<
    Dict<Bytes, Bytes>(
        typename Arg<"from", const Bytes&>::pos,
        typename Arg<"to", const Bytes&>::pos
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "partition">                       : Returns<Function<
    Tuple<Bytes>(typename Arg<"sep", const Bytes&>::pos)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "removeprefix">                    : Returns<Function<
    Bytes(typename Arg<"prefix", const Bytes&>::pos)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "removesuffix">                    : Returns<Function<
    Bytes(typename Arg<"suffix", const Bytes&>::pos)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "replace">                         : Returns<Function<
    Bytes(
        typename Arg<"old", const Bytes&>::pos,
        typename Arg<"new", const Bytes&>::pos,
        typename Arg<"count", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "rfind">                           : Returns<Function<
    Int(
        typename Arg<"sub", const Bytes&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "rindex">                          : Returns<Function<
    Int(
        typename Arg<"sub", const Bytes&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "rjust">                           : Returns<Function<
    Bytes(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillbyte", const Bytes&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "rpartition">                      : Returns<Function<
    Tuple<Bytes>(typename Arg<"sep", const Bytes&>::pos)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "rsplit">                          : Returns<Function<
    List<Bytes>(
        typename Arg<"sep", const Bytes&>::opt,
        typename Arg<"maxsplit", const Int&>::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "rstrip">                          : Returns<Function<
    Bytes(typename Arg<"chars", const Bytes&>::opt)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "split">                           : Returns<Function<
    List<Bytes>(
        typename Arg<"sep", const Bytes&>::opt,
        typename Arg<"maxsplit", const Int&>::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "splitlines">                      : Returns<Function<
    List<Bytes>(typename Arg<"keepends", const Bool&>::opt)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "startswith">                      : Returns<Function<
    Bool(
        typename Arg<"prefix", const Bytes&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "strip">                           : Returns<Function<
    Bytes(typename Arg<"chars", const Bytes&>::opt)
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "swapcase">                        : Returns<Function<
    Bytes()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "title">                           : Returns<Function<
    Bytes()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "translate">                       : Returns<Function<
    Bytes(
        typename Arg<"table", const Dict<Bytes, Bytes>&>::pos,
        typename Arg<"delete", const Bytes&>::opt
    )
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "upper">                           : Returns<Function<
    Bytes()
>> {};
template <std::derived_from<Bytes> Self>
struct __getattr__<Self, "zfill">                           : Returns<Function<
    Bytes(typename Arg<"width", const Int&>::pos)
>> {};


template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "capitalize">                      : Returns<Function<
    ByteArray()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "center">                          : Returns<Function<
    ByteArray(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillchar", const ByteArray&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "count">                           : Returns<Function<
    Int(
        typename Arg<"sub", const ByteArray&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "decode">                          : Returns<Function<
    Str(
        typename Arg<"encoding", const Str&>::opt,
        typename Arg<"errors", const Str&>::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "endswith">                        : Returns<Function<
    Bool(
        typename Arg<"suffix", const ByteArray&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "expandtabs">                      : Returns<Function<
    ByteArray(typename Arg<"tabsize", const Int&>::opt)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "find">                            : Returns<Function<
    Int(
        typename Arg<"sub", const ByteArray&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "fromhex">                         : Returns<Function<
    ByteArray(Arg<"string", const Str&>::pos)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "hex">                             : Returns<Function<
    Str(
        typename Arg<"sep", const ByteArray&>::opt,
        typename Arg<"bytes_per_sep", const Int&>::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "index">                           : Returns<Function<
    Int(
        typename Arg<"sub", const ByteArray&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "isalnum">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "isalpha">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "isascii">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "isdigit">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "islower">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "isspace">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "istitle">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "isupper">                         : Returns<Function<
    Bool()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "join">                            : Returns<Function<
    ByteArray(typename Arg<"iterable", const Object&>::pos)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "ljust">                           : Returns<Function<
    ByteArray(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillbyte", const ByteArray&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "lower">                           : Returns<Function<
    ByteArray()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "lstrip">                          : Returns<Function<
    ByteArray(typename Arg<"chars", const ByteArray&>::opt)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "maketrans">                       : Returns<Function<
    Dict<ByteArray, ByteArray>(
        typename Arg<"from", const ByteArray&>::pos,
        typename Arg<"to", const ByteArray&>::pos
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "partition">                       : Returns<Function<
    Tuple<ByteArray>(typename Arg<"sep", const ByteArray&>::pos)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "removeprefix">                    : Returns<Function<
    ByteArray(typename Arg<"prefix", const ByteArray&>::pos)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "removesuffix">                    : Returns<Function<
    ByteArray(typename Arg<"suffix", const ByteArray&>::pos)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "replace">                         : Returns<Function<
    ByteArray(
        typename Arg<"old", const ByteArray&>::pos,
        typename Arg<"new", const ByteArray&>::pos,
        typename Arg<"count", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "rfind">                           : Returns<Function<
    Int(
        typename Arg<"sub", const ByteArray&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "rindex">                          : Returns<Function<
    Int(
        typename Arg<"sub", const ByteArray&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "rjust">                           : Returns<Function<
    ByteArray(
        typename Arg<"width", const Int&>::pos,
        typename Arg<"fillbyte", const ByteArray&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "rpartition">                      : Returns<Function<
    Tuple<ByteArray>(typename Arg<"sep", const ByteArray&>::pos)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "rsplit">                          : Returns<Function<
    List<ByteArray>(
        typename Arg<"sep", const ByteArray&>::opt,
        typename Arg<"maxsplit", const Int&>::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "rstrip">                          : Returns<Function<
    ByteArray(typename Arg<"chars", const ByteArray&>::opt)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "split">                           : Returns<Function<
    List<ByteArray>(
        typename Arg<"sep", const ByteArray&>::opt,
        typename Arg<"maxsplit", const Int&>::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "splitlines">                      : Returns<Function<
    List<ByteArray>(typename Arg<"keepends", const Bool&>::opt)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "startswith">                      : Returns<Function<
    Bool(
        typename Arg<"prefix", const ByteArray&>::pos,
        typename Arg<"start", const Int&>::pos::opt,
        typename Arg<"end", const Int&>::pos::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "strip">                           : Returns<Function<
    ByteArray(typename Arg<"chars", const ByteArray&>::opt)
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "swapcase">                        : Returns<Function<
    ByteArray()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "title">                           : Returns<Function<
    ByteArray()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "translate">                       : Returns<Function<
    ByteArray(
        typename Arg<"table", const Dict<ByteArray, ByteArray>&>::pos,
        typename Arg<"delete", const ByteArray&>::opt
    )
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "upper">                           : Returns<Function<
    ByteArray()
>> {};
template <std::derived_from<ByteArray> Self>
struct __getattr__<Self, "zfill">                           : Returns<Function<
    ByteArray(typename Arg<"width", const Int&>::pos)
>> {};











template <impl::proxy_like Self, impl::not_proxy_like Key>
struct __getitem__<Self, Key> : __getitem__<impl::unwrap_proxy<Self>, Key> {};
template <impl::not_proxy_like Self, impl::proxy_like Key>
struct __getitem__<Self, Key> : __getitem__<Self, impl::unwrap_proxy<Key>> {};
template <impl::proxy_like Self, impl::proxy_like Key>
struct __getitem__<Self, Key> : __getitem__<impl::unwrap_proxy<Self>, impl::unwrap_proxy<Key>> {};
template <std::convertible_to<Object> Key>
struct __getitem__<Object, Key>                             : Returns<Object> {};
template <std::convertible_to<Object> Key>
struct __getitem__<Type, Key>                               : Returns<Object> {};
template <std::convertible_to<Object> Key>
struct __getitem__<Super, Key>                              : Returns<Object> {};
template <std::derived_from<Str> Self, impl::int_like Index>
struct __getitem__<Self, Index>                             : Returns<Str> {};
template <std::derived_from<Str> Self>
struct __getitem__<Self, Slice>                             : Returns<Str> {};
template <std::derived_from<Bytes> Self, impl::int_like Index>
struct __getitem__<Self, Index>                             : Returns<Int> {};
template <std::derived_from<Bytes> Self>
struct __getitem__<Self, Slice>                             : Returns<Bytes> {};
template <std::derived_from<ByteArray> Self, impl::int_like Index>
struct __getitem__<Self, Index>                             : Returns<Int> {};
template <std::derived_from<ByteArray> Self>
struct __getitem__<Self, Slice>                             : Returns<ByteArray> {};
template <std::derived_from<Range> Self, impl::int_like Index>
struct __getitem__<Self, Index>                             : Returns<Int> {};
template <std::derived_from<Range> Self>
struct __getitem__<Self, Slice>                             : Returns<Range> {};
template <std::derived_from<impl::TupleTag> Self, impl::int_like Index>
struct __getitem__<Self, Index>                             : Returns<typename Self::value_type> {};
template <std::derived_from<impl::TupleTag> Self>
struct __getitem__<Self, Slice>                             : Returns<Tuple<typename Self::value_type>> {};
template <std::derived_from<impl::ListTag> Self, impl::int_like Index>
struct __getitem__<Self, Index>                             : Returns<typename Self::value_type> {};
template <std::derived_from<impl::ListTag> Self>
struct __getitem__<Self, Slice>                             : Returns<List<typename Self::value_type>> {};
template <
    std::derived_from<impl::DictTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __getitem__<Self, Key>                               : Returns<typename Self::value_type> {};
template <
    std::derived_from<impl::MappingProxyTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __getitem__<Self, Key>                               : Returns<typename Self::value_type> {};


template <impl::proxy_like Self, impl::not_proxy_like Key, impl::not_proxy_like Value>
struct __setitem__<Self, Key, Value> : __setitem__<impl::unwrap_proxy<Self>, Key, Value> {};
template <impl::proxy_like Self, impl::proxy_like Key, impl::not_proxy_like Value>
struct __setitem__<Self, Key, Value> : __setitem__<impl::unwrap_proxy<Self>, impl::unwrap_proxy<Key>, Value> {};
template <impl::proxy_like Self, impl::not_proxy_like Key, impl::proxy_like Value>
struct __setitem__<Self, Key, Value> : __setitem__<impl::unwrap_proxy<Self>, Key, impl::unwrap_proxy<Value>> {};
template <impl::proxy_like Self, impl::proxy_like Key, impl::proxy_like Value>
struct __setitem__<Self, Key, Value> : __setitem__<impl::unwrap_proxy<Self>, impl::unwrap_proxy<Key>, impl::unwrap_proxy<Value>> {};
template <impl::not_proxy_like Self, impl::proxy_like Key, impl::not_proxy_like Value>
struct __setitem__<Self, Key, Value> : __setitem__<Self, impl::unwrap_proxy<Key>, Value> {};
template <impl::not_proxy_like Self, impl::not_proxy_like Key, impl::proxy_like Value>
struct __setitem__<Self, Key, Value> : __setitem__<Self, Key, impl::unwrap_proxy<Value>> {};
template <impl::not_proxy_like Self, impl::proxy_like Key, impl::proxy_like Value>
struct __setitem__<Self, Key, Value> : __setitem__<Self, impl::unwrap_proxy<Key>, impl::unwrap_proxy<Value>> {};
template <std::convertible_to<Object> Key, std::convertible_to<Object> Value>
struct __setitem__<Object, Key, Value>                      : Returns<void> {};
template <std::convertible_to<Object> Key, std::convertible_to<Object> Value>
struct __setitem__<Super, Key, Value>                       : Returns<void> {};
template <
    std::derived_from<impl::ListTag> Self,
    impl::int_like Key,
    std::convertible_to<typename Self::value_type> Value
>
struct __setitem__<Self, Key, Value>                        : Returns<void> {};
template <
    std::derived_from<impl::ListTag> Self,
    std::convertible_to<Self> Value
>
struct __setitem__<Self, Slice, Value>                      : Returns<void> {};
template <
    std::derived_from<impl::DictTag> Self,
    std::convertible_to<typename Self::key_type> Key,
    std::convertible_to<typename Self::value_type> Value
>
struct __setitem__<Self, Key, Value>                        : Returns<void> {};


template <impl::proxy_like Self, impl::not_proxy_like Key>
struct __delitem__<Self, Key> : __delitem__<impl::unwrap_proxy<Self>, Key> {};
template <impl::not_proxy_like Self, impl::proxy_like Key>
struct __delitem__<Self, Key> : __delitem__<Self, impl::unwrap_proxy<Key>> {};
template <impl::proxy_like Self, impl::proxy_like Key>
struct __delitem__<Self, Key> : __delitem__<impl::unwrap_proxy<Self>, impl::unwrap_proxy<Key>> {};
template <std::convertible_to<Object> Key>
struct __delitem__<Object, Key>                             : Returns<void> {};
template <std::convertible_to<Object> Key>
struct __delitem__<Super, Key>                              : Returns<void> {};
template <std::derived_from<impl::ListTag> Self, impl::int_like Key>
struct __delitem__<Self, Key>                               : Returns<void> {};
template <std::derived_from<impl::ListTag> Self>
struct __delitem__<Self, Slice>                             : Returns<void> {};
template <
    std::derived_from<impl::DictTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __delitem__<Self, Key>                               : Returns<void> {};


template <impl::proxy_like Self, impl::not_proxy_like Key>
struct __contains__<Self, Key> : __contains__<impl::unwrap_proxy<Self>, Key> {};
template <impl::not_proxy_like Self, impl::proxy_like Key>
struct __contains__<Self, Key> : __contains__<Self, impl::unwrap_proxy<Key>> {};
template <impl::proxy_like Self, impl::proxy_like Key>
struct __contains__<Self, Key> : __contains__<impl::unwrap_proxy<Self>, impl::unwrap_proxy<Key>> {};
template <std::convertible_to<Object> Key>
struct __contains__<Object, Key>                            : Returns<bool> {};
template <std::convertible_to<Object> Key>
struct __contains__<Super, Key>                             : Returns<bool> {};
template <std::derived_from<Str> Self>
struct __contains__<Self, Object>                           : Returns<bool> {};
template <std::derived_from<Str> Self, impl::str_like Key>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <std::derived_from<Bytes> Self>
struct __contains__<Self, Object>                           : Returns<bool> {};
template <std::derived_from<Bytes> Self, impl::int_like Key>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <std::derived_from<Bytes> Self, impl::anybytes_like Key>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <std::derived_from<ByteArray> Self>
struct __contains__<Self, Object>                           : Returns<bool> {};
template <std::derived_from<ByteArray> Self, impl::int_like Key>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <std::derived_from<ByteArray> Self, impl::anybytes_like Key>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <std::derived_from<Range> Self>
struct __contains__<Self, Object>                           : Returns<bool> {};
template <std::derived_from<Range> Self, impl::int_like T>
struct __contains__<Self, T>                                : Returns<bool> {};
template <
    std::derived_from<impl::TupleTag> Self,
    std::convertible_to<typename Self::value_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <
    std::derived_from<impl::ListTag> Self,
    std::convertible_to<typename Self::value_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <
    std::derived_from<impl::SetTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <
    std::derived_from<impl::FrozenSetTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <
    std::derived_from<impl::KeyTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <
    std::derived_from<impl::ValueTag> Self,
    std::convertible_to<typename Self::value_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <std::derived_from<impl::ItemTag> Self, impl::tuple_like Key>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <
    std::derived_from<impl::DictTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <
    std::derived_from<impl::MappingProxyTag> Self,
    std::convertible_to<typename Self::key_type> Key
>
struct __contains__<Self, Key>                              : Returns<bool> {};


template <impl::proxy_like Self>
struct __len__<Self> : __len__<impl::unwrap_proxy<Self>> {};
template <>
struct __len__<Object>                                      : Returns<size_t> {};
template <>
struct __len__<Super>                                       : Returns<size_t> {};
template <std::derived_from<Str> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<Bytes> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<ByteArray> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<Range> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<impl::TupleTag> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<impl::ListTag> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<impl::SetTag> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<impl::FrozenSetTag> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<impl::KeyTag> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<impl::ValueTag> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<impl::ItemTag> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<impl::DictTag> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <std::derived_from<impl::MappingProxyTag> Self>
struct __len__<Self>                                        : Returns<size_t> {};


template <impl::proxy_like Self>
struct __iter__<Self> : __iter__<impl::unwrap_proxy<Self>> {};
template <>
struct __iter__<Object>                                     : Returns<Object> {};
template <>
struct __iter__<Super>                                      : Returns<Object> {};
template <std::derived_from<Str> Self>
struct __iter__<Self>                                       : Returns<Str> {};
template <std::derived_from<Bytes> Self>
struct __iter__<Self>                                       : Returns<Int> {};
template <std::derived_from<ByteArray> Self>
struct __iter__<Self>                                       : Returns<Int> {};
template <std::derived_from<Range> Self>
struct __iter__<Self>                                       : Returns<Int> {};
template <std::derived_from<impl::TupleTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::value_type> {};
template <std::derived_from<impl::ListTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::value_type> {};
template <std::derived_from<impl::KeyTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::key_type> {};
template <std::derived_from<impl::ValueTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::value_type> {};
template <std::derived_from<impl::ItemTag> Self>
struct __iter__<Self>                                       : Returns<std::pair<typename Self::key_type, typename Self::value_type>> {};
template <std::derived_from<impl::DictTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::key_type> {};
template <std::derived_from<impl::MappingProxyTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::key_type> {};
template <std::derived_from<impl::SetTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::key_type> {};
template <std::derived_from<impl::FrozenSetTag> Self>
struct __iter__<Self>                                       : Returns<typename Self::key_type> {};


template <impl::proxy_like Self>
struct __reversed__<Self> : __reversed__<impl::unwrap_proxy<Self>> {};
template <>
struct __reversed__<Object>                                 : Returns<Object> {};
template <>
struct __reversed__<Super>                                  : Returns<Object> {};
template <std::derived_from<Str> Self>
struct __reversed__<Self>                                   : Returns<Str> {};
template <std::derived_from<Bytes> Self>
struct __reversed__<Self>                                   : Returns<Int> {};
template <std::derived_from<ByteArray> Self>
struct __reversed__<Self>                                   : Returns<Int> {};
template <std::derived_from<Range> Self>
struct __reversed__<Self>                                   : Returns<Int> {};
template <std::derived_from<impl::TupleTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::value_type> {};
template <std::derived_from<impl::ListTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::value_type> {};
template <std::derived_from<impl::SetTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::key_type> {};
template <std::derived_from<impl::FrozenSetTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::key_type> {};
template <std::derived_from<impl::KeyTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::key_type> {};
template <std::derived_from<impl::ValueTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::value_type> {};
template <std::derived_from<impl::ItemTag> Self>
struct __reversed__<Self>                                   : Returns<std::pair<typename Self::key_type, typename Self::value_type>> {};
template <std::derived_from<impl::DictTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::key_type> {};
template <std::derived_from<impl::MappingProxyTag> Self>
struct __reversed__<Self>                                   : Returns<typename Self::key_type> {};


template <typename Self, typename... Args> requires (impl::proxy_like<Args> || ...)
struct __call__<Self, Args...> : __call__<Self, impl::unwrap_proxy<Args>...> {};
template <impl::proxy_like Self, typename... Args>
struct __call__<Self, Args...> : __call__<impl::unwrap_proxy<Self>, Args...> {};
template <impl::proxy_like Self, typename... Args> requires (impl::proxy_like<Args> || ...)
struct __call__<Self, Args...> : __call__<impl::unwrap_proxy<Self>, impl::unwrap_proxy<Args>...> {};
template <typename ... Args>
struct __call__<Object, Args...>                            : Returns<Object> {};
template <typename... Args>
struct __call__<Type, Args...>                              : Returns<Object> {};
template <typename ... Args>
struct __call__<Super, Args...>                             : Returns<Object> {};


template <impl::proxy_like Self>
struct __hash__<Self> : __hash__<impl::unwrap_proxy<Self>> {};
template <>
struct __hash__<Handle>                                     : Returns<size_t> {};
template <>
struct __hash__<Capsule>                                    : Returns<size_t> {};
template <>
struct __hash__<WeakRef>                                    : Returns<size_t> {};
template <>
struct __hash__<Object>                                     : Returns<size_t> {};
template <>
struct __hash__<NoneType>                                   : Returns<size_t> {};
template <>
struct __hash__<NotImplementedType>                         : Returns<size_t> {};
template <>
struct __hash__<EllipsisType>                               : Returns<size_t> {};
template <>
struct __hash__<Module>                                     : Returns<size_t> {};
template <>
struct __hash__<Type>                                       : Returns<size_t> {};
template <>
struct __hash__<Super>                                      : Returns<size_t> {};
template <std::derived_from<Module> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<NoneType> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<NotImplementedType> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<EllipsisType> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<Bool> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<Int> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<Float> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<Complex> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<Str> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<Bytes> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<impl::TupleTag> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <std::derived_from<impl::FrozenSetTag> Self>
struct __hash__<Self>                                       : Returns<size_t> {};


template <impl::proxy_like Self>
struct __abs__<Self> : __abs__<impl::unwrap_proxy<Self>> {};
template <>
struct __abs__<Object>                                      : Returns<Object> {};
template <>
struct __abs__<Super>                                       : Returns<Object> {};
template <std::derived_from<Bool> Self>
struct __abs__<Self>                                        : Returns<Int> {};
template <std::derived_from<Int> Self>
struct __abs__<Self>                                        : Returns<Int> {};
template <std::derived_from<Float> Self>
struct __abs__<Self>                                        : Returns<Float> {};
template <std::derived_from<Complex> Self>
struct __abs__<Self>                                        : Returns<Complex> {};


template <impl::proxy_like Self>
struct __invert__<Self> : __invert__<impl::unwrap_proxy<Self>> {};
template <>
struct __invert__<Object>                                   : Returns<Object> {};
template <>
struct __invert__<Super>                                    : Returns<Object> {};
template <std::derived_from<Bool> Self>
struct __invert__<Self>                                     : Returns<Int> {};
template <std::derived_from<Int> Self>
struct __invert__<Self>                                     : Returns<Int> {};


template <impl::proxy_like Self>
struct __pos__<Self> : __pos__<impl::unwrap_proxy<Self>> {};
template <>
struct __pos__<Object>                                      : Returns<Object> {};
template <>
struct __pos__<Super>                                       : Returns<Object> {};
template <std::derived_from<Bool> Self>
struct __pos__<Self>                                        : Returns<Int> {};
template <std::derived_from<Int> Self>
struct __pos__<Self>                                        : Returns<Int> {};
template <std::derived_from<Float> Self>
struct __pos__<Self>                                        : Returns<Float> {};
template <std::derived_from<Complex> Self>
struct __pos__<Self>                                        : Returns<Complex> {};


template <impl::proxy_like Self>
struct __neg__<Self> : __neg__<impl::unwrap_proxy<Self>> {};
template <>
struct __neg__<Object>                                      : Returns<Object> {};
template <>
struct __neg__<Super>                                       : Returns<Object> {};
template <std::derived_from<Bool> Self>
struct __neg__<Self>                                        : Returns<Int> {};
template <std::derived_from<Int> Self>
struct __neg__<Self>                                        : Returns<Int> {};
template <std::derived_from<Float> Self>
struct __neg__<Self>                                        : Returns<Float> {};
template <std::derived_from<Complex> Self>
struct __neg__<Self>                                        : Returns<Complex> {};


template <impl::proxy_like Self>
struct __increment__<Self> : __increment__<impl::unwrap_proxy<Self>> {};
template <>
struct __increment__<Object>                                : Returns<Object> {};
template <>
struct __increment__<Super>                                 : Returns<Object> {};
template <std::derived_from<Int> Self>
struct __increment__<Self>                                  : Returns<Int> {};
template <std::derived_from<Float> Self>
struct __increment__<Self>                                  : Returns<Float> {};
template <std::derived_from<Complex> Self>
struct __increment__<Self>                                  : Returns<Complex> {};


template <impl::proxy_like Self>
struct __decrement__<Self> : __decrement__<impl::unwrap_proxy<Self>> {};
template <>
struct __decrement__<Object>                                : Returns<Object> {};
template <>
struct __decrement__<Super>                                 : Returns<Object> {};
template <std::derived_from<Int> Self>
struct __decrement__<Self>                                  : Returns<Int> {};
template <std::derived_from<Float> Self>
struct __decrement__<Self>                                  : Returns<Float> {};
template <std::derived_from<Complex> Self>
struct __decrement__<Self>                                  : Returns<Complex> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __lt__<L, R> : __lt__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __lt__<L, R> : __lt__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __lt__<L, R> : __lt__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __lt__<Object, R>                                    : Returns<bool> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __lt__<L, Object>                                    : Returns<bool> {};
template <std::convertible_to<Object> R>
struct __lt__<Super, R>                                     : Returns<bool> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __lt__<L, Super>                                     : Returns<bool> {};
template <std::derived_from<Slice> L, impl::slice_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::slice_like L, std::derived_from<Slice> R> requires (!std::derived_from<L, Slice>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::str_like L, std::derived_from<Str> R> requires (!std::derived_from<L, Str>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bytes> L, impl::anybytes_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::anybytes_like L, std::derived_from<Bytes> R> requires (!std::derived_from<L, Bytes>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<ByteArray> L, impl::anybytes_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::anybytes_like L, std::derived_from<ByteArray> R> requires (!std::derived_from<L, ByteArray>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::TupleTag> L, impl::tuple_like R>
    requires (impl::Broadcast<impl::lt_comparable, L, R>::value)
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::tuple_like L, std::derived_from<impl::TupleTag> R>
    requires (
        !std::derived_from<L, impl::TupleTag> &&
        impl::Broadcast<impl::lt_comparable, L, R>::value
    )
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::ListTag> L, impl::list_like R>
    requires (impl::Broadcast<impl::lt_comparable, L, R>::value)
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::list_like L, std::derived_from<impl::ListTag> R>
    requires (
        !std::derived_from<L, impl::ListTag> &&
        impl::Broadcast<impl::lt_comparable, L, R>::value
    )
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::SetTag> L, impl::anyset_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::SetTag> R>
    requires (!std::derived_from<L, impl::SetTag>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::FrozenSetTag> L, impl::anyset_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::FrozenSetTag> R>
    requires (!std::derived_from<L, impl::FrozenSetTag>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::KeyTag> L, std::derived_from<impl::KeyTag> R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::KeyTag> L, impl::anyset_like R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::KeyTag> R>
struct __lt__<L, R>                                         : Returns<bool> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __le__<L, R> : __le__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __le__<L, R> : __le__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __le__<L, R> : __le__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __le__<Object, R>                                    : Returns<bool> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __le__<L, Object>                                    : Returns<bool> {};
template <std::convertible_to<Object> R>
struct __le__<Super, R>                                     : Returns<bool> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __le__<L, Super>                                     : Returns<bool> {};
template <std::derived_from<Slice> L, impl::slice_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::slice_like L, std::derived_from<Slice> R> requires (!std::derived_from<L, Slice>)
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::str_like L, std::derived_from<Str> R> requires (!std::derived_from<L, Str>)
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bytes> L, impl::anybytes_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::anybytes_like L, std::derived_from<Bytes> R> requires (!std::derived_from<L, Bytes>)
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<ByteArray> L, impl::anybytes_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::anybytes_like L, std::derived_from<ByteArray> R> requires (!std::derived_from<L, ByteArray>)
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::TupleTag> L, impl::tuple_like R>
    requires (impl::Broadcast<impl::le_comparable, L, R>::value)
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::tuple_like L, std::derived_from<impl::TupleTag> R>
    requires (
        !std::derived_from<L, impl::TupleTag> &&
        impl::Broadcast<impl::le_comparable, L, R>::value
    )
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::ListTag> L, impl::list_like R>
    requires (impl::Broadcast<impl::le_comparable, L, R>::value)
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::list_like L, std::derived_from<impl::ListTag> R>
    requires (
        !std::derived_from<L, impl::ListTag> &&
        impl::Broadcast<impl::le_comparable, L, R>::value
    )
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::SetTag> L, impl::anyset_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::SetTag> R>
    requires (!std::derived_from<L, impl::SetTag>)
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::FrozenSetTag> L, impl::anyset_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::FrozenSetTag> R>
    requires (!std::derived_from<L, impl::FrozenSetTag>)
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::KeyTag> L, std::derived_from<impl::KeyTag> R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::KeyTag> L, impl::anyset_like R>
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::KeyTag> R>
struct __le__<L, R>                                         : Returns<bool> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __eq__<L, R> : __eq__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __eq__<L, R> : __eq__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __eq__<L, R> : __eq__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::derived_from<Object> L, std::convertible_to<Object> R>
struct __eq__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, std::derived_from<Object> R> requires (!std::derived_from<L, Object>)
struct __eq__<L, R>                                         : Returns<bool> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __ne__<L, R> : __ne__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ne__<L, R> : __ne__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ne__<L, R> : __ne__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::derived_from<Object> L, std::convertible_to<Object> R>
struct __ne__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, std::derived_from<Object> R> requires (!std::derived_from<L, Object>)
struct __ne__<L, R>                                         : Returns<bool> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __ge__<L, R> : __ge__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ge__<L, R> : __ge__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ge__<L, R> : __ge__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __ge__<Object, R>                                    : Returns<bool> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __ge__<L, Object>                                    : Returns<bool> {};
template <std::convertible_to<Object> R>
struct __ge__<Super, R>                                     : Returns<bool> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __ge__<L, Super>                                     : Returns<bool> {};
template <std::derived_from<Slice> L, impl::slice_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::slice_like L, std::derived_from<Slice> R> requires (!std::derived_from<L, Slice>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::str_like L, std::derived_from<Str> R> requires (!std::derived_from<L, Str>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bytes> L, impl::anybytes_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::anybytes_like L, std::derived_from<Bytes> R> requires (!std::derived_from<L, Bytes>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<ByteArray> L, impl::anybytes_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::anybytes_like L, std::derived_from<ByteArray> R> requires (!std::derived_from<L, ByteArray>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::TupleTag> L, impl::tuple_like R>
    requires (impl::Broadcast<impl::ge_comparable, L, R>::value)
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::tuple_like L, std::derived_from<impl::TupleTag> R>
    requires (
        !std::derived_from<L, impl::TupleTag> &&
        impl::Broadcast<impl::ge_comparable, L, R>::value
    )
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::ListTag> L, impl::list_like R>
    requires (impl::Broadcast<impl::ge_comparable, L, R>::value)
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::list_like L, std::derived_from<impl::ListTag> R>
    requires (
        !std::derived_from<L, impl::ListTag> &&
        impl::Broadcast<impl::ge_comparable, L, R>::value
    )
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::SetTag> L, impl::anyset_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::SetTag> R>
    requires (!std::derived_from<L, impl::SetTag>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::FrozenSetTag> L, impl::anyset_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::FrozenSetTag> R>
    requires (!std::derived_from<L, impl::FrozenSetTag>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::KeyTag> L, std::derived_from<impl::KeyTag> R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::KeyTag> L, impl::anyset_like R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::KeyTag> R>
struct __ge__<L, R>                                         : Returns<bool> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __gt__<L, R> : __gt__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __gt__<L, R> : __gt__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __gt__<L, R> : __gt__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __gt__<Object, R>                                    : Returns<bool> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __gt__<L, Object>                                    : Returns<bool> {};
template <std::convertible_to<Object> R>
struct __gt__<Super, R>                                     : Returns<bool> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __gt__<L, Super>                                     : Returns<bool> {};
template <std::derived_from<Slice> L, impl::slice_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::slice_like L, std::derived_from<Slice> R> requires (!std::derived_from<L, Slice>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::str_like L, std::derived_from<Str> R> requires (!std::derived_from<L, Str>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<Bytes> L, impl::anybytes_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::anybytes_like L, std::derived_from<Bytes> R> requires (!std::derived_from<L, Bytes>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<ByteArray> L, impl::anybytes_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::anybytes_like L, std::derived_from<ByteArray> R> requires (!std::derived_from<L, ByteArray>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::TupleTag> L, impl::tuple_like R>
    requires (impl::Broadcast<impl::gt_comparable, L, R>::value)
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::tuple_like L, std::derived_from<impl::TupleTag> R>
    requires (
        !std::derived_from<L, impl::TupleTag> &&
        impl::Broadcast<impl::gt_comparable, L, R>::value
    )
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::ListTag> L, impl::list_like R>
    requires (impl::Broadcast<impl::gt_comparable, L, R>::value)
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::list_like L, std::derived_from<impl::ListTag> R>
    requires (
        !std::derived_from<L, impl::ListTag> &&
        impl::Broadcast<impl::gt_comparable, L, R>::value
    )
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::SetTag> L, impl::anyset_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::SetTag> R>
    requires (!std::derived_from<L, impl::SetTag>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::FrozenSetTag> L, impl::anyset_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::FrozenSetTag> R>
    requires (!std::derived_from<L, impl::FrozenSetTag>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::KeyTag> L, std::derived_from<impl::KeyTag> R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::derived_from<impl::KeyTag> L, impl::anyset_like R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::anyset_like L, std::derived_from<impl::KeyTag> R>
struct __gt__<L, R>                                         : Returns<bool> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __add__<L, R> : __add__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __add__<L, R> : __add__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __add__<L, R> : __add__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __add__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __add__<L, Object>                                   : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __add__<Super, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __add__<L, Super>                                    : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __add__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __add__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __add__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Bool> L, impl::complex_like R>
struct __add__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __add__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __add__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __add__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Bool> R>
struct __add__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __add__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __add__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __add__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Int> L, impl::complex_like R>
struct __add__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __add__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __add__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __add__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Int> R>
struct __add__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __add__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __add__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __add__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::complex_like R>
struct __add__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __add__<L, R>                                        : Returns<Float> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __add__<L, R>                                        : Returns<Float> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __add__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Float> R>
struct __add__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::bool_like R>
struct __add__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::int_like R>
struct __add__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::float_like R>
struct __add__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::complex_like R>
struct __add__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Complex> R>
struct __add__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Complex> R>
struct __add__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Complex> R>
struct __add__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Complex> R> requires (!std::derived_from<L, Complex>)
struct __add__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __add__<L, R>                                        : Returns<Str> {};
template <impl::str_like L, std::derived_from<Str> R> requires (!std::derived_from<L, Str>)
struct __add__<L, R>                                        : Returns<Str> {};
template <std::derived_from<Bytes> L, impl::anybytes_like R>
struct __add__<L, R>                                        : Returns<Bytes> {};
template <impl::anybytes_like L, std::derived_from<Bytes> R> requires (!std::derived_from<L, Bytes>)
struct __add__<L, R>                                        : Returns<Bytes> {};
template <std::derived_from<ByteArray> L, impl::anybytes_like R>
struct __add__<L, R>                                        : Returns<ByteArray> {};
template <impl::anybytes_like L, std::derived_from<ByteArray> R> requires (!std::derived_from<L, ByteArray>)
struct __add__<L, R>                                        : Returns<ByteArray> {};
template <std::derived_from<impl::TupleTag> L, std::convertible_to<L> R>
struct __add__<L, R>                                        : Returns<Tuple<typename L::value_type>> {};
template <typename L, std::derived_from<impl::TupleTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __add__<L, R>                                        : Returns<Tuple<typename R::value_type>> {};
template <std::derived_from<impl::ListTag> L, std::convertible_to<L> R>
struct __add__<L, R>                                        : Returns<List<typename L::value_type>> {};
template <typename L, std::derived_from<impl::ListTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __add__<L, R>                                        : Returns<List<typename R::value_type>> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __iadd__<L, R> : __iadd__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __iadd__<L, R> : __iadd__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __iadd__<L, R> : __iadd__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __iadd__<Object, R>                                  : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __iadd__<Super, R>                                   : Returns<Super&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __iadd__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __iadd__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __iadd__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __iadd__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __iadd__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Complex> L, impl::bool_like R>
struct __iadd__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::int_like R>
struct __iadd__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::float_like R>
struct __iadd__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::complex_like R>
struct __iadd__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Str> L, impl::str_like R>
struct __iadd__<L, R>                                       : Returns<Str&> {};
template <std::derived_from<Bytes> L, impl::anybytes_like R>
struct __iadd__<L, R>                                       : Returns<Bytes&> {};
template <std::derived_from<ByteArray> L, impl::anybytes_like R>
struct __iadd__<L, R>                                       : Returns<ByteArray&> {};
template <std::derived_from<impl::TupleTag> L, std::convertible_to<L> R>
struct __iadd__<L, R>                                       : Returns<Tuple<typename L::value_type>&> {};
template <std::derived_from<impl::ListTag> L, std::convertible_to<L> R>
struct __iadd__<L, R>                                       : Returns<Tuple<typename L::value_type>&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __sub__<L, R> : __sub__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __sub__<L, R> : __sub__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __sub__<L, R> : __sub__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __sub__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __sub__<L, Object>                                   : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __sub__<Super, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __sub__<L, Super>                                    : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __sub__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __sub__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __sub__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Bool> L, impl::complex_like R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __sub__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __sub__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __sub__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Bool> R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __sub__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __sub__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __sub__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Int> L, impl::complex_like R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __sub__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __sub__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __sub__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Int> R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __sub__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __sub__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __sub__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::complex_like R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __sub__<L, R>                                        : Returns<Float> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __sub__<L, R>                                        : Returns<Float> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __sub__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Float> R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::bool_like R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::int_like R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::float_like R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::complex_like R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Complex> R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <impl::int_like L, std::derived_from<Complex> R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <impl::float_like L, std::derived_from<Complex> R>
struct __sub__<L, R>                                        : Returns<Complex> {};
template <impl::complex_like L, std::derived_from<Complex> R> requires (!std::derived_from<L, Complex>)
struct __sub__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<impl::SetTag> L, std::convertible_to<L> R>
struct __sub__<L, R>                                        : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::SetTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __sub__<L, R>                                        : Returns<Set<typename R::key_type>> {};
template <std::derived_from<impl::FrozenSetTag> L, std::convertible_to<L> R>
struct __sub__<L, R>                                        : Returns<FrozenSet<typename L::key_type>> {};
template <typename L, std::derived_from<impl::FrozenSetTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __sub__<L, R>                                        : Returns<FrozenSet<typename R::key_type>> {};
template <std::derived_from<impl::KeyTag> L, std::convertible_to<L> R>
struct __sub__<L, R>                                        : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::KeyTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __sub__<L, R>                                        : Returns<Set<typename R::key_type>> {};
template <std::derived_from<impl::KeyTag> L, std::convertible_to<Set<typename L::key_type>> R>
struct __sub__<L, R>                                        : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::KeyTag> R>
    requires (std::convertible_to<L, Set<typename R::key_type>>)
struct __sub__<L, R>                                        : Returns<Set<typename R::key_type>> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __isub__<L, R> : __isub__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __isub__<L, R> : __isub__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __isub__<L, R> : __isub__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __isub__<Object, R>                                  : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __isub__<Super, R>                                   : Returns<Super&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __isub__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __isub__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __isub__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __isub__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __isub__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Complex> L, impl::bool_like R>
struct __isub__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::int_like R>
struct __isub__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::float_like R>
struct __isub__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::complex_like R>
struct __isub__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<impl::SetTag> L, std::convertible_to<L> R>
struct __isub__<L, R>                                       : Returns<Set<typename L::key_type>&> {};
template <std::derived_from<impl::FrozenSetTag> L, std::convertible_to<L> R>
struct __isub__<L, R>                                       : Returns<FrozenSet<typename L::key_type>&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __mul__<L, R> : __mul__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __mul__<L, R> : __mul__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __mul__<L, R> : __mul__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __mul__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __mul__<L, Object>                                   : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __mul__<Super, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __mul__<L, Super>                                    : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __mul__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __mul__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Bool> L, impl::complex_like R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __mul__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __mul__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __mul__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Bool> R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __mul__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __mul__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Int> L, impl::complex_like R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __mul__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __mul__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __mul__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Int> R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __mul__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __mul__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::complex_like R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __mul__<L, R>                                        : Returns<Float> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __mul__<L, R>                                        : Returns<Float> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __mul__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Float> R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::bool_like R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::float_like R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::complex_like R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Complex> R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <impl::int_like L, std::derived_from<Complex> R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <impl::float_like L, std::derived_from<Complex> R>
struct __mul__<L, R>                                        : Returns<Complex> {};
template <impl::complex_like L, std::derived_from<Complex> R> requires (!std::derived_from<L, Complex>)
struct __mul__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Str> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<Str> {};
template <impl::int_like L, std::derived_from<Str> R>
struct __mul__<L, R>                                        : Returns<Str> {};
template <std::derived_from<Bytes> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<Bytes> {};
template <impl::int_like L, std::derived_from<Bytes> R>
struct __mul__<L, R>                                        : Returns<Bytes> {};
template <std::derived_from<ByteArray> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<ByteArray> {};
template <impl::int_like L, std::derived_from<ByteArray> R>
struct __mul__<L, R>                                        : Returns<ByteArray> {};
template <std::derived_from<impl::TupleTag> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<Tuple<typename L::value_type>> {};
template <impl::int_like L, std::derived_from<impl::TupleTag> R>
struct __mul__<L, R>                                        : Returns<Tuple<typename R::value_type>> {};
template <std::derived_from<impl::ListTag> L, impl::int_like R>
struct __mul__<L, R>                                        : Returns<List<typename L::value_type>> {};
template <impl::int_like L, std::derived_from<impl::ListTag> R>
struct __mul__<L, R>                                        : Returns<List<typename R::value_type>> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __imul__<L, R> : __imul__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __imul__<L, R> : __imul__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __imul__<L, R> : __imul__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __imul__<Object, R>                                  : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __imul__<Super, R>                                   : Returns<Super&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __imul__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __imul__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __imul__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Complex> L, impl::bool_like R>
struct __imul__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::float_like R>
struct __imul__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::complex_like R>
struct __imul__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Str> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<Str&> {};
template <std::derived_from<Bytes> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<Bytes&> {};
template <std::derived_from<impl::TupleTag> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<Tuple<typename L::value_type>&> {};
template <std::derived_from<impl::ListTag> L, impl::int_like R>
struct __imul__<L, R>                                       : Returns<Tuple<typename R::value_type>&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __truediv__<L, R> : __truediv__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __truediv__<L, R> : __truediv__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __truediv__<L, R> : __truediv__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __truediv__<Object, R>                               : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __truediv__<L, Object>                               : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __truediv__<Super, R>                                : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __truediv__<L, Super>                                : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <std::derived_from<Bool> L, impl::complex_like R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __truediv__<L, R>                                    : Returns<Float> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Bool> R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <std::derived_from<Int> L, impl::complex_like R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __truediv__<L, R>                                    : Returns<Float> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Int> R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <std::derived_from<Float> L, impl::complex_like R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __truediv__<L, R>                                    : Returns<Float> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __truediv__<L, R>                                    : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Float> R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::bool_like R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::int_like R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::float_like R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::complex_like R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Complex> R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <impl::int_like L, std::derived_from<Complex> R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <impl::float_like L, std::derived_from<Complex> R>
struct __truediv__<L, R>                                    : Returns<Complex> {};
template <impl::complex_like L, std::derived_from<Complex> R> requires (!std::derived_from<L, Complex>)
struct __truediv__<L, R>                                    : Returns<Complex> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __itruediv__<L, R> : __itruediv__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __itruediv__<L, R> : __itruediv__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __itruediv__<L, R> : __itruediv__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __itruediv__<Object, R>                              : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __itruediv__<Super, R>                               : Returns<Object&> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __itruediv__<L, R>                                   : Returns<Float&> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __itruediv__<L, R>                                   : Returns<Float&> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __itruediv__<L, R>                                   : Returns<Float&> {};
template <std::derived_from<Complex> L, impl::bool_like R>
struct __itruediv__<L, R>                                   : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::int_like R>
struct __itruediv__<L, R>                                   : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::float_like R>
struct __itruediv__<L, R>                                   : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::complex_like R>
struct __itruediv__<L, R>                                   : Returns<Complex&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __floordiv__<L, R> : __floordiv__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __floordiv__<L, R> : __floordiv__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __floordiv__<L, R> : __floordiv__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __floordiv__<Object, R>                              : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __floordiv__<L, Object>                              : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __floordiv__<Super, R>                               : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __floordiv__<L, Super>                               : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __floordiv__<L, R>                                   : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __floordiv__<L, R>                                   : Returns<Int> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __floordiv__<L, R>                                   : Returns<Float> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __floordiv__<L, R>                                   : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __floordiv__<L, R>                                   : Returns<Int> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __floordiv__<L, R>                                   : Returns<Float> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __floordiv__<L, R>                                   : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __floordiv__<L, R>                                   : Returns<Int> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __floordiv__<L, R>                                   : Returns<Float> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __floordiv__<L, R>                                   : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __floordiv__<L, R>                                   : Returns<Int> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __floordiv__<L, R>                                   : Returns<Float> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __floordiv__<L, R>                                   : Returns<Float> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __floordiv__<L, R>                                   : Returns<Float> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __floordiv__<L, R>                                   : Returns<Float> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __floordiv__<L, R>                                   : Returns<Float> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __floordiv__<L, R>                                   : Returns<Float> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __floordiv__<L, R>                                   : Returns<Float> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __ifloordiv__<L, R> : __ifloordiv__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ifloordiv__<L, R> : __ifloordiv__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ifloordiv__<L, R> : __ifloordiv__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __ifloordiv__<Object, R>                             : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __ifloordiv__<Super, R>                              : Returns<Object&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __ifloordiv__<L, R>                                  : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __ifloordiv__<L, R>                                  : Returns<Int&> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __ifloordiv__<L, R>                                  : Returns<Float&> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __ifloordiv__<L, R>                                  : Returns<Float&> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __ifloordiv__<L, R>                                  : Returns<Float&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __mod__<L, R> : __mod__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __mod__<L, R> : __mod__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __mod__<L, R> : __mod__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __mod__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __mod__<L, Object>                                   : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __mod__<Super, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __mod__<L, Super>                                    : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __mod__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __mod__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __mod__<L, R>                                        : Returns<Float> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __mod__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __mod__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __mod__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __mod__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __mod__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __mod__<L, R>                                        : Returns<Float> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __mod__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __mod__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __mod__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __mod__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __mod__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __mod__<L, R>                                        : Returns<Float> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __mod__<L, R>                                        : Returns<Float> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __mod__<L, R>                                        : Returns<Float> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __mod__<L, R>                                        : Returns<Float> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __imod__<L, R> : __imod__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __imod__<L, R> : __imod__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __imod__<L, R> : __imod__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __imod__<Object, R>                                  : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __imod__<Super, R>                                   : Returns<Object&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __imod__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __imod__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __imod__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __imod__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __imod__<L, R>                                       : Returns<Float&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __pow__<L, R> : __pow__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __pow__<L, R> : __pow__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __pow__<L, R> : __pow__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __pow__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __pow__<L, Object>                                   : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __pow__<Super, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __pow__<L, Super>                                    : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __pow__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __pow__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Bool> L, impl::float_like R>
struct __pow__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Bool> L, impl::complex_like R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __pow__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __pow__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Bool> R>
struct __pow__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Bool> R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __pow__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __pow__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::float_like R>
struct __pow__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Int> L, impl::complex_like R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __pow__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __pow__<L, R>                                        : Returns<Int> {};
template <impl::float_like L, std::derived_from<Int> R>
struct __pow__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Int> R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __pow__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __pow__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __pow__<L, R>                                        : Returns<Float> {};
template <std::derived_from<Float> L, impl::complex_like R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Float> R>
struct __pow__<L, R>                                        : Returns<Float> {};
template <impl::int_like L, std::derived_from<Float> R>
struct __pow__<L, R>                                        : Returns<Float> {};
template <impl::float_like L, std::derived_from<Float> R> requires (!std::derived_from<L, Float>)
struct __pow__<L, R>                                        : Returns<Float> {};
template <impl::complex_like L, std::derived_from<Float> R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::bool_like R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::int_like R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::float_like R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <std::derived_from<Complex> L, impl::complex_like R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <impl::bool_like L, std::derived_from<Complex> R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <impl::int_like L, std::derived_from<Complex> R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <impl::float_like L, std::derived_from<Complex> R>
struct __pow__<L, R>                                        : Returns<Complex> {};
template <impl::complex_like L, std::derived_from<Complex> R> requires (!std::derived_from<L, Complex>)
struct __pow__<L, R>                                        : Returns<Complex> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __ipow__<L, R> : __ipow__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ipow__<L, R> : __ipow__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ipow__<L, R> : __ipow__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __ipow__<Object, R>                                  : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __ipow__<Super, R>                                   : Returns<Object&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __ipow__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __ipow__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Float> L, impl::bool_like R>
struct __ipow__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Float> L, impl::int_like R>
struct __ipow__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Float> L, impl::float_like R>
struct __ipow__<L, R>                                       : Returns<Float&> {};
template <std::derived_from<Complex> L, impl::bool_like R>
struct __ipow__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::int_like R>
struct __ipow__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::float_like R>
struct __ipow__<L, R>                                       : Returns<Complex&> {};
template <std::derived_from<Complex> L, impl::complex_like R>
struct __ipow__<L, R>                                       : Returns<Complex&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __lshift__<L, R> : __lshift__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __lshift__<L, R> : __lshift__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __lshift__<L, R> : __lshift__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __lshift__<Object, R>                                : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __lshift__<L, Object>                                : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __lshift__<Super, R>                                 : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __lshift__<L, Super>                                 : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __lshift__<L, R>                                     : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __lshift__<L, R>                                     : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __lshift__<L, R>                                     : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __lshift__<L, R>                                     : Returns<Int> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __lshift__<L, R>                                     : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __lshift__<L, R>                                     : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __lshift__<L, R>                                     : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __lshift__<L, R>                                     : Returns<Int> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __ilshift__<L, R> : __ilshift__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ilshift__<L, R> : __ilshift__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ilshift__<L, R> : __ilshift__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __ilshift__<Object, R>                               : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __ilshift__<Super, R>                                : Returns<Object&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __ilshift__<L, R>                                    : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __ilshift__<L, R>                                    : Returns<Int&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __rshift__<L, R> : __rshift__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __rshift__<L, R> : __rshift__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __rshift__<L, R> : __rshift__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __rshift__<Object, R>                                : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __rshift__<L, Object>                                : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __rshift__<Super, R>                                 : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __rshift__<L, Super>                                 : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __rshift__<L, R>                                     : Returns<Int> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __rshift__<L, R>                                     : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __rshift__<L, R>                                     : Returns<Int> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __rshift__<L, R>                                     : Returns<Int> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __rshift__<L, R>                                     : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __rshift__<L, R>                                     : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __rshift__<L, R>                                     : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __rshift__<L, R>                                     : Returns<Int> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __irshift__<L, R> : __irshift__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __irshift__<L, R> : __irshift__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __irshift__<L, R> : __irshift__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __irshift__<Object, R>                               : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __irshift__<Super, R>                                : Returns<Object&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __irshift__<L, R>                                    : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __irshift__<L, R>                                    : Returns<Int&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __and__<L, R> : __and__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __and__<L, R> : __and__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __and__<L, R> : __and__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __and__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __and__<L, Object>                                   : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __and__<Super, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __and__<L, Super>                                    : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __and__<L, R>                                        : Returns<Bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __and__<L, R>                                        : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Bool>)
struct __and__<L, R>                                        : Returns<Bool> {};
template <impl::int_like L, std::derived_from<Bool> R>
struct __and__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __and__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __and__<L, R>                                        : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Int> R>
struct __and__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Int>)
struct __and__<L, R>                                        : Returns<Int> {};
template <std::derived_from<impl::SetTag> L, std::convertible_to<L> R>
struct __and__<L, R>                                        : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::SetTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __and__<L, R>                                        : Returns<Set<typename R::key_type>> {};
template <std::derived_from<impl::FrozenSetTag> L, std::convertible_to<L> R>
struct __and__<L, R>                                        : Returns<FrozenSet<typename L::key_type>> {};
template <typename L, std::derived_from<impl::FrozenSetTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __and__<L, R>                                        : Returns<FrozenSet<typename R::key_type>> {};
template <std::derived_from<impl::KeyTag> L, std::convertible_to<L> R>
struct __and__<L, R>                                        : Returns<Set<typename L::key_type>> {};
template <std::derived_from<impl::KeyTag> L, std::derived_from<impl::KeyTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __and__<L, R>                                        : Returns<Set<typename R::key_type>> {};
template <std::derived_from<impl::KeyTag> L, std::convertible_to<Set<typename L::key_type>> R>
struct __and__<L, R>                                        : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::KeyTag> R>
    requires (std::convertible_to<L, Set<typename R::key_type>>)
struct __and__<L, R>                                        : Returns<Set<typename R::key_type>> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __iand__<L, R> : __iand__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __iand__<L, R> : __iand__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __iand__<L, R> : __iand__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __iand__<Object, R>                                  : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __iand__<Super, R>                                   : Returns<Object&> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __iand__<L, R>                                       : Returns<Bool&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __iand__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __iand__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<impl::SetTag> L, std::convertible_to<L> R>
struct __iand__<L, R>                                       : Returns<Set<typename L::key_type>&> {};
template <std::derived_from<impl::FrozenSetTag> L, std::convertible_to<L> R>
struct __iand__<L, R>                                       : Returns<FrozenSet<typename L::key_type>&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __or__<L, R> : __or__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __or__<L, R> : __or__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __or__<L, R> : __or__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __or__<Object, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __or__<L, Object>                                    : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __or__<Super, R>                                     : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __or__<L, Super>                                     : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __or__<L, R>                                         : Returns<Bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __or__<L, R>                                         : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __or__<L, R>                                         : Returns<Bool> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __or__<L, R>                                         : Returns<Int> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __or__<L, R>                                         : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __or__<L, R>                                         : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Object>)
struct __or__<L, R>                                         : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Object>)
struct __or__<L, R>                                         : Returns<Int> {};
template <std::derived_from<impl::SetTag> L, std::convertible_to<L> R>
struct __or__<L, R>                                         : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::SetTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __or__<L, R>                                         : Returns<Set<typename R::key_type>> {};
template <std::derived_from<impl::FrozenSetTag> L, std::convertible_to<L> R>
struct __or__<L, R>                                         : Returns<FrozenSet<typename L::key_type>> {};
template <typename L, std::derived_from<impl::FrozenSetTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __or__<L, R>                                         : Returns<FrozenSet<typename R::key_type>> {};
template <std::derived_from<impl::KeyTag> L, std::convertible_to<L> R>
struct __or__<L, R>                                         : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::KeyTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __or__<L, R>                                         : Returns<Set<typename R::key_type>> {};
template <std::derived_from<impl::KeyTag> L, std::convertible_to<Set<typename L::key_type>> R>
struct __or__<L, R>                                         : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::KeyTag> R>
    requires (std::convertible_to<L, Set<typename R::key_type>>)
struct __or__<L, R>                                         : Returns<Set<typename R::key_type>> {};
template <std::derived_from<impl::DictTag> L, std::convertible_to<L> R>
struct __or__<L, R>                                         : Returns<Dict<typename L::key_type, typename L::value_type>> {};
template <typename L, std::derived_from<impl::DictTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __or__<L, R>                                         : Returns<Dict<typename R::key_type, typename R::value_type>> {};
template <std::derived_from<impl::MappingProxyTag> L, std::convertible_to<L> R>
struct __or__<L, R>                                         : Returns<Dict<typename L::key_type, typename L::value_type>> {};
template <typename L, std::derived_from<impl::MappingProxyTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __or__<L, R>                                         : Returns<Dict<typename R::key_type, typename R::value_type>> {};
template <std::derived_from<impl::MappingProxyTag> L, typename R>
    requires (std::convertible_to<R, Dict<typename L::key_type, typename L::value_type>>)
struct __or__<L, R>                                         : Returns<Dict<typename L::key_type, typename L::value_type>> {};
template <typename L, std::derived_from<impl::MappingProxyTag> R>
    requires (std::convertible_to<L, Dict<typename R::key_type, typename R::value_type>>)
struct __or__<L, R>                                         : Returns<Dict<typename R::key_type, typename R::value_type>> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __ior__<L, R> : __ior__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ior__<L, R> : __ior__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ior__<L, R> : __ior__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __ior__<Object, R>                                   : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __ior__<Super, R>                                    : Returns<Object&> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __ior__<L, R>                                        : Returns<Bool&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __ior__<L, R>                                        : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __ior__<L, R>                                        : Returns<Int&> {};
template <std::derived_from<impl::SetTag> L, std::convertible_to<L> R>
struct __ior__<L, R>                                        : Returns<Set<typename L::key_type>&> {};
template <std::derived_from<impl::FrozenSetTag> L, std::convertible_to<L> R>
struct __ior__<L, R>                                        : Returns<FrozenSet<typename L::key_type>&> {};
template <std::derived_from<impl::DictTag> L, std::convertible_to<L> R>
struct __ior__<L, R>                                        : Returns<Dict<typename L::key_type, typename L::value_type>&> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __xor__<L, R> : __xor__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __xor__<L, R> : __xor__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __xor__<L, R> : __xor__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __xor__<Object, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Object>)
struct __xor__<L, Object>                                   : Returns<Object> {};
template <std::convertible_to<Object> R>
struct __xor__<Super, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L> requires (!std::same_as<L, Super>)
struct __xor__<L, Super>                                    : Returns<Object> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __xor__<L, R>                                        : Returns<Bool> {};
template <std::derived_from<Bool> L, impl::int_like R>
struct __xor__<L, R>                                        : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __xor__<L, R>                                        : Returns<Bool> {};
template <impl::int_like L, std::derived_from<Bool> R> requires (!std::derived_from<L, Object>)
struct __xor__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __xor__<L, R>                                        : Returns<Int> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __xor__<L, R>                                        : Returns<Int> {};
template <impl::bool_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Object>)
struct __xor__<L, R>                                        : Returns<Int> {};
template <impl::int_like L, std::derived_from<Int> R> requires (!std::derived_from<L, Object>)
struct __xor__<L, R>                                        : Returns<Int> {};
template <std::derived_from<impl::SetTag> L, std::convertible_to<L> R>
struct __xor__<L, R>                                        : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::SetTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __xor__<L, R>                                        : Returns<Set<typename R::key_type>> {};
template <std::derived_from<impl::FrozenSetTag> L, std::convertible_to<L> R>
struct __xor__<L, R>                                        : Returns<FrozenSet<typename L::key_type>> {};
template <typename L, std::derived_from<impl::FrozenSetTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __xor__<L, R>                                        : Returns<FrozenSet<typename R::key_type>> {};
template <std::derived_from<impl::KeyTag> L, std::convertible_to<L> R>
struct __xor__<L, R>                                        : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::KeyTag> R>
    requires (!std::convertible_to<R, L> && std::convertible_to<L, R>)
struct __xor__<L, R>                                        : Returns<Set<typename R::key_type>> {};
template <std::derived_from<impl::KeyTag> L, std::convertible_to<Set<typename L::key_type>> R>
struct __xor__<L, R>                                        : Returns<Set<typename L::key_type>> {};
template <typename L, std::derived_from<impl::KeyTag> R>
    requires (std::convertible_to<L, Set<typename R::key_type>>)
struct __xor__<L, R>                                        : Returns<Set<typename R::key_type>> {};


template <impl::proxy_like L, impl::not_proxy_like R>
struct __ixor__<L, R> : __ixor__<impl::unwrap_proxy<L>, R> {};
template <impl::not_proxy_like L, impl::proxy_like R>
struct __ixor__<L, R> : __ixor__<L, impl::unwrap_proxy<R>> {};
template <impl::proxy_like L, impl::proxy_like R>
struct __ixor__<L, R> : __ixor__<impl::unwrap_proxy<L>, impl::unwrap_proxy<R>> {};
template <std::convertible_to<Object> R>
struct __ixor__<Object, R>                                  : Returns<Object&> {};
template <std::convertible_to<Object> R>
struct __ixor__<Super, R>                                   : Returns<Object&> {};
template <std::derived_from<Bool> L, impl::bool_like R>
struct __ixor__<L, R>                                       : Returns<Bool&> {};
template <std::derived_from<Int> L, impl::bool_like R>
struct __ixor__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<Int> L, impl::int_like R>
struct __ixor__<L, R>                                       : Returns<Int&> {};
template <std::derived_from<impl::SetTag> L, std::convertible_to<L> R>
struct __ixor__<L, R>                                       : Returns<Set<typename L::key_type>&> {};
template <std::derived_from<impl::FrozenSetTag> L, std::convertible_to<L> R>
struct __ixor__<L, R>                                       : Returns<FrozenSet<typename L::key_type>&> {};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_CONTROL_H
