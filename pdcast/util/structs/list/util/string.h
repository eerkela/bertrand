// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_UTIL_STRING_H
#define BERTRAND_STRUCTS_UTIL_STRING_H

#include <array>  // std::array
#include <sstream>  // std::ostringstream
#include <string>  // std::string
#include <string_view>  // std::string_view
#include <typeinfo>  // typeid()
#include <Python.h>  // CPython API
#include "except.h"  // catch_python()


/* NOTE: This file contains utilities for working with C++ and Python strings for
 * debugging, error reporting, and compile-time naming for Python objects.
 */


namespace bertrand {
namespace structs {
namespace util {


/* Compile-time string concatenation for Python-style dotted type names. */
namespace string {

    /* Concatenate multiple std::string_views at compile-time. */
    template <const std::string_view&... Strings>
    class _concat {

        /* Join all strings into a single std::array of chars with static storage. */
        static constexpr auto array = [] {
            // Get array with size equal to total length of strings 
            constexpr size_t len = (Strings.size() + ... + 0);
            std::array<char, len + 1> array{};

            // Append each string to the array
            auto append = [i = 0, &array](const auto& s) mutable {
                for (auto c : s) array[i++] = c;
            };
            (append(Strings), ...);
            array[len] = 0;  // null-terminate
            return array;
        }();

    public:
        /* Get the concatenated string as a std::string_view. */
        static constexpr std::string_view value { array.data(), array.size() - 1 };
    };

    /* Syntactic sugar for _concat<...>::value */
    template <const std::string_view&... Strings>
    static constexpr auto concat = _concat<Strings...>::value;

    /* A trait that determines which specialization of repr() is appropriate for a
    given type. */
    template <typename T>
    class Repr {
        using True = std::true_type;
        using False = std::false_type;
        using Stream = std::ostringstream;

        enum class Strategy {
            python,
            to_string,
            stream,
            iterable,
            type_id
        };

        /* Check if the templated type is a Python object. */
        template<typename U>
        static auto _python(U u) -> decltype(
            PyObject_Repr(std::forward<U>(u)), True{}
        );
        static auto _python(...) -> False;

        /* Check if the templated type is a valid input to std::to_string. */
        template<typename U>
        static auto _to_string(U u) -> decltype(
            std::to_string(std::forward<U>(u)), True{}
        );
        static auto _to_string(...) -> False;

        /* Check if the templated type supports std::ostringstream insertion. */
        template<typename U>
        static auto _streamable(U u) -> decltype(
            std::declval<Stream&>() << std::forward<U>(u), True{}
        );
        static auto _streamable(...) -> False;

        /* Check if the templated type is iterable. */
        template<typename U>
        static auto _iterable(U u) -> decltype(
            std::begin(std::forward<U>(u)), std::end(std::forward<U>(u)), True{}
        );
        static auto _iterable(...) -> False;

        /* Determine the Repr() overload to use for objects of the templated type. */
        static constexpr Strategy category = [] {
            if constexpr (decltype(_python(std::declval<T>()))::value) {
                return Strategy::python;
            } else if constexpr (decltype(_to_string(std::declval<T>()))::value) {
                return Strategy::to_string;
            } else if constexpr (decltype(_streamable(std::declval<T>()))::value) {
                return Strategy::stream;
            } else if constexpr (decltype(_iterable(std::declval<T>()))::value) {
                return Strategy::iterable;
            } else {
                return Strategy::type_id;
            }
        }();

    public:
        static constexpr bool python = (category == Strategy::python);
        static constexpr bool streamable = (category == Strategy::stream);
        static constexpr bool to_string = (category == Strategy::to_string);
        static constexpr bool iterable = (category == Strategy::iterable);
        static constexpr bool type_id = (category == Strategy::type_id);
    };

}


/* Get a string representation of a Python object using PyObject_Repr(). */
template <typename T, std::enable_if_t<string::Repr<T>::python, int> = 0>
std::string repr(const T& obj) {
    if (obj == nullptr) {
        return std::string("NULL");
    }
    PyObject* py_repr = PyObject_Repr(obj);
    if (py_repr == nullptr) {
        throw catch_python<std::runtime_error>();
    }
    const char* c_repr = PyUnicode_AsUTF8(py_repr);
    if (c_repr == nullptr) {
        throw catch_python<std::runtime_error>();
    }
    Py_DECREF(py_repr);
    return std::string(c_repr);
}


/* Get a string representation of a C++ object using `std::to_string()`. */
template <typename T, std::enable_if_t<string::Repr<T>::to_string, int> = 0>
std::string repr(const T& obj) {
    return std::to_string(obj);
}


/* Get a string representation of a C++ object by streaming it into a
`std::ostringstream`. */
template <typename T, std::enable_if_t<string::Repr<T>::streamable, int> = 0>
std::string repr(const T& obj) {
    std::ostringstream stream;
    stream << obj;
    return stream.str();
}


/* Get a string representation of an iterable C++ object by recursively unpacking
it. */
template <typename T, std::enable_if_t<string::Repr<T>::iterable, int> = 0>
std::string repr(const T& obj) {
    std::ostringstream stream;
    stream << '[';
    for (auto iter = std::begin(obj); iter != std::end(obj);) {
        stream << repr(*iter);
        if (++iter != std::end(obj)) {
            stream << ", ";
        }
    }
    stream << ']';
    return stream.str();
}


/* Get a string representation of an arbitrary C++ object by getting its mangled type
name.  NOTE: this is the default implementation if no specialization can be found. */
template <typename T, std::enable_if_t<string::Repr<T>::type_id, int> = 0>
std::string repr(const T& obj) {
    return std::string(typeid(obj).name());
}


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_CORE_UTIL_H
