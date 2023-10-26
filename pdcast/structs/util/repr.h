// include guard: BERTRAND_STRUCTS_UTIL_REPR_H
#ifndef BERTRAND_STRUCTS_UTIL_REPR_H
#define BERTRAND_STRUCTS_UTIL_REPR_H

#include <sstream>  // std::ostringstream
#include <string>  // std::string
#include <typeinfo>  // typeid()
#include <Python.h>  // PyObject_Repr, PyUnicode_AsUTF8
#include "except.h"  // catch_python


/* Python's repr() function is extremely useful for debugging, but there is no direct
 * equivalent for C++ objects.  This makes debugging C++ code more difficult,
 * especially when working with objects of unknown type (doubly so when Python objects
 * might be mixed in with static C++ types).
 *
 * This function attempts to solve that problem by providing a single, overloadable
 * template function that uses specialization and SFINAE to determine the best way to
 * stringify an arbitrary object.  This allows us to use the same interface for all
 * objects (whether Python or C++), and to easily extend the functionality to new types
 * as needed.  At the moment, this can accept any object that is:
 *      - convertible to PyObject*, in which case `PyObject_Repr()` is used.
 *      - convertible to std::string, in which case `std::to_string()` is used.
 *      - streamable into a std::ostringstream, in which case `operator<<` is used.
 *      - iterable, in which case each element is recursively unpacked according to the
 *        same rules as listed here.
 *      - none of the above, in which case the raw type name is returned using
 *        `typeid().name()`.
 */


namespace bertrand {
namespace structs {
namespace util {


/* A trait that determines which specialization of repr() is appropriate for a
given type. */
template <typename T>
class ReprTraits {
    using True = std::true_type;
    using False = std::false_type;
    using Stream = std::ostringstream;

    enum class Use {
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
    static constexpr Use category = [] {
        if constexpr (decltype(_python(std::declval<T>()))::value) {
            return Use::python;
        } else if constexpr (decltype(_to_string(std::declval<T>()))::value) {
            return Use::to_string;
        } else if constexpr (decltype(_streamable(std::declval<T>()))::value) {
            return Use::stream;
        } else if constexpr (decltype(_iterable(std::declval<T>()))::value) {
            return Use::iterable;
        } else {
            return Use::type_id;
        }
    }();

public:
    static constexpr bool python = (category == Use::python);
    static constexpr bool streamable = (category == Use::stream);
    static constexpr bool to_string = (category == Use::to_string);
    static constexpr bool iterable = (category == Use::iterable);
    static constexpr bool type_id = (category == Use::type_id);
};


/* Get a string representation of a Python object using PyObject_Repr(). */
template <typename T, std::enable_if_t<ReprTraits<T>::python, int> = 0>
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
template <typename T, std::enable_if_t<ReprTraits<T>::to_string, int> = 0>
std::string repr(const T& obj) {
    return std::to_string(obj);
}


/* Get a string representation of a C++ object by streaming it into a
`std::ostringstream`. */
template <typename T, std::enable_if_t<ReprTraits<T>::streamable, int> = 0>
std::string repr(const T& obj) {
    std::ostringstream stream;
    stream << obj;
    return stream.str();
}


/* Get a string representation of an iterable C++ object by recursively unpacking
it. */
template <typename T, std::enable_if_t<ReprTraits<T>::iterable, int> = 0>
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
template <typename T, std::enable_if_t<ReprTraits<T>::type_id, int> = 0>
std::string repr(const T& obj) {
    return std::string(typeid(obj).name());
}


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_REPR_H
