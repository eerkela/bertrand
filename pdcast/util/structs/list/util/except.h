// include guard: BERTRAND_STRUCTS_UTIL_EXCEPT_H
#ifndef BERTRAND_STRUCTS_UTIL_EXCEPT_H
#define BERTRAND_STRUCTS_UTIL_EXCEPT_H

#include <stdexcept>  // std::runtime_error
#include <string>  // std::string
#include <type_traits>  // std::is_constructible_v<>
#include <typeinfo>  // std::bad_typeid
#include <Python.h>  // CPython API


/* NOTE: This file contains utilities for translating exceptions between Python and
 * C++.  Cython can automatically translate C++ exceptions into Python exceptions, but
 * the reverse is not true, and the rules around doing so are somewhat limited.  By
 * using custom exceptions, we can customize the Cython translation to provide more
 * Pythonic and informative error messages.
 */


namespace bertrand {
namespace structs {
namespace util {


/* Subclass of std::bad_typeid() to allow automatic conversion to Python TypeError() by
Cython. */
class type_error : public std::bad_typeid {
private:
    std::string message;

public:
    using std::bad_typeid::bad_typeid;  // inherit default constructors

    /* Allow construction from a custom error message. */
    type_error(const std::string& what) : message(what) {}
    type_error(const char* what) : message(what) {}
    const char* what() const noexcept override { return message.c_str(); }
};


/* Convert the most recent Python error into an instance of the templated C++
exception, preserving the error message. */
template <typename Exception>
Exception catch_python() {
    // sanity check
    static_assert(
        std::is_constructible_v<Exception, std::string>,
        "Exception type must be constructible from std::string"
    );

    // Get the most recent Python error
    PyObject* exc_type;  // PyErr_Fetch() initializes these and clears error indicator
    PyObject* exc_value;
    PyObject* exc_traceback;
    PyErr_Fetch(&exc_type, &exc_value, &exc_traceback);
    PyErr_NormalizeException(&exc_type, &exc_value, &exc_traceback);

    // Get the error message from the exception if one exists
    std::string msg("Unknown error");
    if (exc_type != nullptr) {
        PyObject* str = PyObject_Str(exc_value);
        if (str == nullptr) {
            msg = "Unknown error (could not get exception message)";
        } else if (!PyUnicode_Check(str)) {
            msg = "Unknown error (exception message is not a string)";
        } else {
            const char* utf8_str = PyUnicode_AsUTF8(str);
            if (utf8_str == nullptr) {
                msg = "Unknown error (exception message failed UTF-8 conversion)";
            } else {
                msg = utf8_str;
            }
        }
        Py_XDECREF(str);  // release python string
    }

    // Decrement reference counts
    Py_XDECREF(exc_type);
    Py_XDECREF(exc_value);
    Py_XDECREF(exc_traceback);

    // return as templated exception type
    return Exception(msg);
}


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_EXCEPT_H
