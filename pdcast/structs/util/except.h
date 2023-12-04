#ifndef BERTRAND_STRUCTS_UTIL_EXCEPT_H
#define BERTRAND_STRUCTS_UTIL_EXCEPT_H

#include <ios>  // std::ios::failure
#include <new>  // std::bad_alloc
#include <stdexcept>  // std::invalid_argument, std::domain_error, ...
#include <string>  // std::string
#include <type_traits>  // std::is_convertible_v<>
#include <typeinfo>  // std::bad_typeid, std::bad_cast
#include <Python.h>  // CPython API


/* NOTE: This file contains utilities for translating exceptions between Python and
 * C++.  Cython can automatically translate C++ exceptions into Python exceptions, but
 * the reverse is not true, and the rules around doing so are somewhat limited.  By
 * using custom exceptions, we can customize the Cython translation to provide more
 * Pythonic and informative error messages.
 *
 * reference:
 *      https://docs.python.org/3/c-api/exceptions.html?highlight=pyexc#standardwarningcategories
 */


namespace bertrand {


/* Base class for all Python-compatible C++ exceptions. */
class Exception : public std::exception {
private:
    const std::string message;
    PyObject* py_exc;

public:
    using std::exception::exception;
    using std::exception::operator=;

    /* Allow construction from a custom error message. */
    explicit Exception(const char* what, PyObject* py_exc) :
        message(what), py_exc(py_exc)
    {}
    explicit Exception(const std::string& what, PyObject* py_exc) :
        message(what), py_exc(py_exc)
    {}
    explicit Exception(const std::string_view& what, PyObject* py_exc) :
        message(what), py_exc(py_exc)
    {}

    /* Get error message. */
    inline const char* what() const noexcept override { return message.c_str(); }
    inline const std::string str() const noexcept { return message; }
    inline const std::string_view view() const noexcept { return message; }

    /* Convert to an equivalent Python error. */
    inline void to_python() const noexcept { PyErr_SetString(py_exc, this->what()); }
};


/* Python-style MemoryError. */
struct MemoryError : public Exception {
    using Exception::Exception;
    using Exception::operator=;
    using Exception::what;

    explicit MemoryError(const char* what) : Exception(what, PyExc_MemoryError) {}
    explicit MemoryError(const std::string& what) : Exception(what, PyExc_MemoryError) {}
    explicit MemoryError(const std::string_view& what) : Exception(what, PyExc_MemoryError) {}
};


/* Python-style TypeError. */
struct TypeError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    TypeError(const char* what) : Exception(what, PyExc_TypeError) {}
    TypeError(const std::string& what) : Exception(what, PyExc_TypeError) {}
    TypeError(const std::string_view& what) : Exception(what, PyExc_TypeError) {}
};


/* Python-style ValueError. */
struct ValueError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    ValueError(const char* what) : Exception(what, PyExc_ValueError) {}
    ValueError(const std::string& what) : Exception(what, PyExc_ValueError) {}
    ValueError(const std::string_view& what) : Exception(what, PyExc_ValueError) {}
};


/* Python-style KeyError. */
struct KeyError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    KeyError(const char* what) : Exception(what, PyExc_KeyError) {}
    KeyError(const std::string& what) : Exception(what, PyExc_KeyError) {}
    KeyError(const std::string_view& what) : Exception(what, PyExc_KeyError) {}
};


/* Python-style IndexError. */
struct IndexError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    IndexError(const char* what) : Exception(what, PyExc_IndexError) {}
    IndexError(const std::string& what) : Exception(what, PyExc_IndexError) {}
    IndexError(const std::string_view& what) : Exception(what, PyExc_IndexError) {}
};


/* Python-style RuntimeError. */
struct RuntimeError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    RuntimeError(const char* what) : Exception(what, PyExc_RuntimeError) {}
    RuntimeError(const std::string& what) : Exception(what, PyExc_RuntimeError) {}
    RuntimeError(const std::string_view& what) : Exception(what, PyExc_RuntimeError) {}
};


// TODO: can maybe even store the traceback and just use PyErr_Restore() to send it to
// Python


/* Convert the most recent Python error into an equivalent C++ exception, preserving
the error message.  Requires the GIL. */
template <typename Exc = Exception>
Exc catch_python() {
    static_assert(
        std::is_convertible_v<Exc, Exception>,
        "Exception type must inherit from util::Exception"
    );

    // catch the most recent Python error
    PyObject* type;
    PyObject* value;
    PyObject* traceback;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);

    // Get the error message from the exception if one exists
    std::string msg("Unknown error");
    if (type != nullptr) {
        PyObject* str = PyObject_Str(value);
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
        Py_XDECREF(str);
    }

    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);
    if constexpr (std::is_same_v<Exc, Exception>) {
        return Exc(msg, type);
    } else {
        return Exc(msg);
    }
}


/* Convert the most recent C++ exception into an equivalent Python error, preserving
the error message.  Requires the GIL. */
inline void throw_python() {
    try {
        throw;
    }

    // custom, Python-style Exceptions
    catch (const Exception& e) {
        e.to_python();
    }

    // Cython-style exception handling (matches existing rules)
    // https://cython.readthedocs.io/en/latest/src/userguide/wrapping_CPlusPlus.html#exceptions
    catch (const std::bad_alloc& e) {
        PyErr_SetString(PyExc_MemoryError, e.what());  // bad_alloc -> MemoryError
    } catch (const std::bad_cast& e) {
        PyErr_SetString(PyExc_TypeError, e.what());  // bad_cast -> TypeError
    } catch (const std::bad_typeid& e) {
        PyErr_SetString(PyExc_TypeError, e.what());  // bad_typeid -> TypeError
    } catch (const std::domain_error& e) {
        PyErr_SetString(PyExc_ValueError, e.what());  // domain_error -> ValueError
    } catch (const std::invalid_argument& e) {  // invalid_argument -> ValueError
        PyErr_SetString(PyExc_ValueError, e.what());  
    } catch (const std::ios_base::failure& e) {
        PyErr_SetString(PyExc_IOError, e.what());  // ios_base::failure -> IOError
    } catch (const std::out_of_range& e) {
        PyErr_SetString(PyExc_IndexError, e.what());  // out_of_range -> IndexError
    } catch (const std::overflow_error& e) {
        PyErr_SetString(PyExc_OverflowError, e.what());  // overflow_error -> OverflowError
    } catch (const std::range_error& e) {
        PyErr_SetString(PyExc_ArithmeticError, e.what());  // range_error -> ArithmeticError
    } catch (const std::underflow_error& e) {
        PyErr_SetString(PyExc_ArithmeticError, e.what());  // underflow_error -> ArithmeticError
    }

    // all other exceptions map to RuntimeError
    catch (const std::exception& exc) {
        PyErr_SetString(PyExc_RuntimeError, exc.what());
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ error");
    }
}


}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_EXCEPT_H
