#ifndef BERTRAND_STRUCTS_UTIL_CONTAINER_H
#define BERTRAND_STRUCTS_UTIL_CONTAINER_H

#include <array>  // std::array
#include <cstddef>  // size_t
#include <cstdio>  // std::FILE, std::fopen
#include <deque>  // std::deque
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <string>  // std::string
#include <string_view>  // std::string_view
#include <tuple>  // std::tuple
#include <type_traits>  // std::enable_if_t<>
#include <valarray>  // std::valarray
#include <vector>  // std::vector
#include <Python.h>  // CPython API
#include "base.h"  // is_pyobject<>
#include "except.h"  // catch_python(), TypeError, KeyError, IndexError
#include "func.h"  // FuncTraits, identity
#include "iter.h"  // iter()


// TODO: PyUnicode_AsUTF8() returns a pointer to a buffer that's internal to the string.
// This buffer is only valid as long as the string is alive.  We need to make sure that
// the string remains alive for as long as the buffer is in use.  This is not currently
// enforced, but it should be.


/* NOTE: Python is great, but working with its C API is not.  As such, this file
 * contains a collection of wrappers around the CPython API that make it easier to work
 * with Python objects in C++.  The goal is to make the API more pythonic and less
 * error-prone, while still providing access to the full range of Python's
 * capabilities.
 *
 * Included in this file are:
 *  1.  RAII-based wrappers for PyObject* pointers that automatically handle reference
 *      counts and behave more like their Python counterparts.
 *  2.  Automatic logging of reference counts to ensure that they remain balanced over
 *      the entire program.
 *  3.  Wrappers for built-in Python functions, which are overloaded to handle STL
 *      types and conform to C++ conventions.
 *  4.  Generic programming support for mixed C++/Python objects, including automatic
 *      conversions for basic C++ types into their Python equivalents where possible.
 */


namespace bertrand {
namespace python {


/* Reference counting protocols for correctly managing PyObject* lifetimes. */
enum class Ref {
    NEW,    // increment on construction, decrement on destruction.
    STEAL,  // decrement on destruction.  Assumes ownership over object.
    BORROW  // do not modify refcount.  Object is assumed to outlive the wrapper.
};


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


template <Ref ref>
struct Object;

template <Ref ref>
class Type;

template <Ref ref>
class Function;

template <Ref ref>
class Method;

template <Ref ref>
class ClassMethod;

template <Ref ref>
class Module;

template <Ref ref>
class Frame;

template <Ref ref>
class Code;

template <Ref ref>
class Bool;

template <Ref ref>
class Int;

template <Ref ref>
class Float;

template <Ref ref>
class Complex;

template <Ref ref>
class Slice;

template <Ref ref>
class Tuple;

template <Ref ref>
class List;

template <Ref ref>
class Set;

template <Ref ref>
class Dict;

template <Ref ref>
class FastSequence;

template <Ref ref>
class String;


struct Iterator;
struct IteratorPair;


//////////////////////////
////    EXCEPTIONS    ////
//////////////////////////


/* Python exceptions are almost 100% analogous to C++ exceptions except that they are
 * not integrated with C++'s try/catch semantics.  This makes them more brittle and
 * difficult to work with, especially when mixed with C++ exceptions.  These utilities
 * allow the translation of Python exceptions to C++ equivalents and vice versa,
 * greatly simplifying the process.  It also automatically writes all error messages to
 * the DEBUG log for completeness.  They can be used as follows:
 *
 *     PyObject* api_function() {
 *         PyObject* result = PyObject_SomeCPythonAPIFunction();
 *         if (result == nullptr) {
 *             throw catch_python();  // converts the most recent Python error into a C++ exception
 *         }
 *         return result;
 *     }
 *
 *     PyObject* python_function() {
 *         try {
 *             return api_function();
 *         } catch (...) {
 *             throw_python();  // converts the C++ exception back into a Python error
 *             return nullptr;
 *         }
 *    }
 *
 * In this case, the type, message, and traceback of the original exception will be
 * preserved and restored to the Python interpreter upon returning from `py_function()`.
 * This is especially useful for extensions that are directly exposed to the Python
 * runtime, which otherwise cannot handle C++ exceptions.  By wrapping any code that
 * might raise a C++ exception in a try/catch of this form, we can swap seamlessly
 * between the two systems without losing any information.
 *
 * The second way to use these exceptions is as a direct replacement for the STL
 * exception types.  This makes it easier for Python developers to work with C++ code
 * and vice versa, since they can use the same exception semantics in both languages,
 * without resorting to external documentation.  For this use case, the catch_cpp()
 * function can be used to convert an unknown STL exception into a more familiar
 * python::Exception, which conforms with the rest of the library.  Here's an example:
 *
 *     void can_throw() {
 *         try {
 *             throw std::runtime_error("This is a C++ exception");
 *         } catch (...) {
 *             throw catch_cpp();  // which gets converted into a python::RuntimeError
 *         }
 *
 *     void cpp_function() {
 *         try {
 *             can_throw();
 *         } catch (const python::RuntimeError& e) {
 *             return;  // and can be caught like any other python::Exception
 *         }
 *
 * reference:
 *      https://docs.python.org/3/c-api/exceptions.html#standard-exceptions
 *
 * NOTE: the catch_python() and throw_python() functions require the GIL to be held, so
 * they should only be used within the context of a PyGILState_Ensure() block.
 */
#define PYTHON_SIMPLIFIED_ERROR_STATE (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)


/* Base class for all Python-compatible C++ exceptions. */
struct Exception : public std::exception {
    std::string message;
    PyObject* type;
    PyObject* trace;

    // NOTE: all constructors steal a reference to type and trace

    explicit Exception(
        const char* what,
        PyObject* type = nullptr,
        PyObject* trace = nullptr
    ) : message(what), type(type), trace(trace)
    {
        if constexpr (DEBUG) {
            if (type == nullptr) {
                LOG(err, "RuntimeError: ", what);
            } else {
                LOG(err, reinterpret_cast<PyTypeObject*>(type)->tp_name, ": ", what);
            }
        }
    }

    explicit Exception(
        const std::string& what,
        PyObject* type = nullptr,
        PyObject* trace = nullptr
    ) : message(what), type(type), trace(trace)
    {
        if constexpr (DEBUG) {
            if (type == nullptr) {
                LOG(err, "RuntimeError: ", what);
            } else {
                LOG(err, reinterpret_cast<PyTypeObject*>(type)->tp_name, ": ", what);
            }
        }
    }

    explicit Exception(
        const std::string_view& what,
        PyObject* type = nullptr,
        PyObject* trace = nullptr
    ) : message(what), type(type), trace(trace)
    {
        if constexpr (DEBUG) {
            if (type == nullptr) {
                LOG(err, "RuntimeError: ", what);
            } else {
                LOG(err, reinterpret_cast<PyTypeObject*>(type)->tp_name, ": ", what);
            }
        }
    }

    Exception(const Exception& other) :
        message(other.message),
        type(Py_XNewRef(other.type)),
        trace(Py_XNewRef(other.trace))
    {}

    Exception(Exception&& other) noexcept :
        message(std::move(other.message)),
        type(other.type),
        trace(other.trace)
    {
        other.type = nullptr;
        other.trace = nullptr;
    }

    ~Exception() noexcept override {
        Py_XDECREF(type);
        Py_XDECREF(trace);
    }

    inline const char* what() const noexcept override {
        return message.c_str();
    }

    inline const std::string str() const noexcept {
        return message;
    }

    inline const std::string_view view() const noexcept {
        return message;
    }

    void to_python() const noexcept {
        #if PYTHON_SIMPLIFIED_ERROR_STATE

            if (type == nullptr) {
                PYLOG(err, "RuntimeError: ", what());
                PyErr_SetString(PyExc_RuntimeError, what());
                return;
            }

            if constexpr (DEBUG) {
                PYLOG(
                    err, reinterpret_cast<PyTypeObject*>(type)->tp_name, ": ",
                    what()
                );
            }

            if (trace == nullptr) {
                PyErr_SetString(type, what());
                return;
            }

            PyErr_SetRaisedException(Py_NewRef(trace));

        #else

            if (type == nullptr) {
                PYLOG(err, "RuntimeError: ", what());
                PyErr_SetString(PyExc_RuntimeError, what());
                return;
            }

            if constexpr (DEBUG) {
                PYLOG(
                    err, reinterpret_cast<PyTypeObject*>(type)->tp_name, ": ",
                    what()
                );
            }

            if (trace == nullptr) {
                PyErr_SetString(type, what());
                return;
            }

            // restore traceback if one exists
            PyObject* msg = PyUnicode_FromStringAndSize(message.c_str(), message.size());
            if (msg == nullptr) {
                PyErr_SetString(type, what());
            } else {
                PyErr_Restore(Py_NewRef(type), msg, Py_NewRef(trace));
            }

        #endif
    }

};


struct MemoryError : public Exception {

    explicit MemoryError(const char* what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_MemoryError), trace)
    {}

    explicit MemoryError(const std::string& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_MemoryError), trace)
    {}

    explicit MemoryError(const std::string_view& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_MemoryError), trace)
    {}

    MemoryError(const Exception& e) :
        Exception(e.str(), Py_NewRef(PyExc_MemoryError), Py_NewRef(e.trace))
    {}

    MemoryError(Exception&& e) :
        Exception(e.str(), Py_NewRef(PyExc_MemoryError), e.trace)
    {
        e.trace = nullptr;
    }

};


struct TypeError : public Exception {

    explicit TypeError(const char* what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_TypeError), trace)
    {}

    explicit TypeError(const std::string& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_TypeError), trace)
    {}

    explicit TypeError(const std::string_view& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_TypeError), trace)
    {}

    TypeError(const Exception& e) :
        Exception(e.str(), Py_NewRef(PyExc_TypeError), Py_NewRef(e.trace))
    {}

    TypeError(Exception&& e) :
        Exception(e.str(), Py_NewRef(PyExc_TypeError), e.trace)
    {
        e.trace = nullptr;
    }

};


struct ValueError : public Exception {

    explicit ValueError(const char* what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_ValueError), trace)
    {}

    explicit ValueError(const std::string& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_ValueError), trace)
    {}

    explicit ValueError(const std::string_view& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_ValueError), trace)
    {}

    ValueError(const Exception& e) :
        Exception(e.str(), Py_NewRef(PyExc_ValueError), Py_NewRef(e.trace))
    {}

    ValueError(Exception&& e) :
        Exception(e.str(), Py_NewRef(PyExc_ValueError), e.trace)
    {
        e.trace = nullptr;
    }

};


struct KeyError : public Exception {

    explicit KeyError(const char* what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_KeyError), trace)
    {}

    explicit KeyError(const std::string& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_KeyError), trace)
    {}

    explicit KeyError(const std::string_view& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_KeyError), trace)
    {}

    KeyError(const Exception& e) :
        Exception(e.str(), Py_NewRef(PyExc_KeyError), Py_NewRef(e.trace))
    {}

    KeyError(Exception&& e) :
        Exception(e.str(), Py_NewRef(PyExc_KeyError), e.trace)
    {
        e.trace = nullptr;
    }

};


struct AttributeError : public Exception {

    explicit AttributeError(const char* what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_AttributeError), trace)
    {}

    explicit AttributeError(const std::string& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_AttributeError), trace)
    {}

    explicit AttributeError(const std::string_view& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_AttributeError), trace)
    {}

    AttributeError(const Exception& e) :
        Exception(e.str(), Py_NewRef(PyExc_AttributeError), Py_NewRef(e.trace))
    {}

    AttributeError(Exception&& e) :
        Exception(e.str(), Py_NewRef(PyExc_AttributeError), e.trace)
    {
        e.trace = nullptr;
    }

};


struct IndexError : public Exception {

    explicit IndexError(const char* what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_IndexError), trace)
    {}

    explicit IndexError(const std::string& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_IndexError), trace)
    {}

    explicit IndexError(const std::string_view& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_IndexError), trace)
    {}

    IndexError(const Exception& e) :
        Exception(e.str(), Py_NewRef(PyExc_IndexError), Py_NewRef(e.trace))
    {}

    IndexError(Exception&& e) :
        Exception(e.str(), Py_NewRef(PyExc_IndexError), e.trace)
    {
        e.trace = nullptr;
    }

};


struct RuntimeError : public Exception {

    explicit RuntimeError(const char* what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_RuntimeError), trace)
    {}

    explicit RuntimeError(const std::string& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_RuntimeError), trace)
    {}

    explicit RuntimeError(const std::string_view& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_RuntimeError), trace)
    {}

    RuntimeError(const Exception& e) :
        Exception(e.str(), Py_NewRef(PyExc_RuntimeError), Py_NewRef(e.trace))
    {}

    RuntimeError(Exception&& e) :
        Exception(e.str(), Py_NewRef(PyExc_RuntimeError), e.trace)
    {
        e.trace = nullptr;
    }

};


struct FileNotFoundError : public Exception {

    explicit FileNotFoundError(const char* what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_FileNotFoundError), trace)
    {}

    explicit FileNotFoundError(const std::string& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_FileNotFoundError), trace)
    {}

    explicit FileNotFoundError(const std::string_view& what, PyObject* trace = nullptr) :
        Exception(what, Py_NewRef(PyExc_FileNotFoundError), trace)
    {}

    FileNotFoundError(const Exception& e) :
        Exception(e.str(), Py_NewRef(PyExc_FileNotFoundError), Py_NewRef(e.trace))
    {}

    FileNotFoundError(Exception&& e) :
        Exception(e.str(), Py_NewRef(PyExc_FileNotFoundError), e.trace)
    {
        e.trace = nullptr;
    }

};


/* Convert the most recent Python error into an equivalent C++ exception, preserving
the error message.  The optional template argument can be used to modify the exception
type that is returned from this function.  If no argument is given, a generic
Exception will be returned, which converts to the same type as the original Python
exception when `throw_python()` is called.  Requires the GIL. */
template <typename Exc = Exception>
Exc catch_python() {
    static_assert(
        std::is_convertible_v<Exc, Exception>,
        "Exception type must inherit from python::Exception"
    );

    #if PYTHON_SIMPLIFIED_ERROR_STATE

        PyObject* traceback = PyErr_GetRaisedException();
        if (traceback == nullptr) {
            return Exc("Unknown error (no exception was raised)");
        }
        PyObject* type = Py_NewRef(Py_TYPE(traceback));

        // get exception message
        std::string msg("Unknown error");
        PyObject* str = PyObject_Str(traceback);
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

    #else

        // catch the most recent Python error
        PyObject* type;
        PyObject* value;
        PyObject* traceback;
        PyErr_Fetch(&type, &value, &traceback);
        PyErr_NormalizeException(&type, &value, &traceback);

        // Get message from exception
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
        Py_XDECREF(value);

    #endif

    if constexpr (std::is_same_v<Exc, Exception>) {
        return Exc(msg, type, traceback);
    } else {
        Py_XDECREF(type);
        return Exc(msg, traceback);
    }
}


/* Convert the most recent C++ exception into an equivalent Python error, preserving
the error message, type, and traceback, then pushes the error onto the PyErr stack so
that it can be caught and handled by the Python interpreter.  Requires the GIL. */
void throw_python() {
    try {
        throw;
    }

    // custom, Python-style Exceptions
    catch (const Exception& e) {
        e.to_python();
    }

    // Cython-style exception handling (matches existing rules)
    // https://cython.readthedocs.io/en/latest/src/userguide/wrapping_CPlusPlus.html#exceptions
    catch (const std::bad_alloc& e) {  // bad_alloc -> MemoryError
        LOG(err, "MemoryError: ", e.what());
        PyErr_SetString(PyExc_MemoryError, e.what());

    } catch (const std::bad_cast& e) {  // bad_cast -> TypeError
        LOG(err, "TypeError: ", e.what());
        PyErr_SetString(PyExc_TypeError, e.what());

    } catch (const std::bad_typeid& e) {  // bad_typeid -> TypeError
        LOG(err, "TypeError: ", e.what());
        PyErr_SetString(PyExc_TypeError, e.what());

    } catch (const std::domain_error& e) {  // domain_error -> ValueError
        LOG(err, "ValueError: ", e.what());
        PyErr_SetString(PyExc_ValueError, e.what());

    } catch (const std::invalid_argument& e) {  // invalid_argument -> ValueError
        LOG(err, "ValueError: ", e.what());
        PyErr_SetString(PyExc_ValueError, e.what());

    } catch (const std::ios_base::failure& e) {  // ios_base::failure -> IOError
        LOG(err, "IOError: ", e.what());
        PyErr_SetString(PyExc_IOError, e.what());

    } catch (const std::out_of_range& e) {  // out_of_range -> IndexError
        LOG(err, "IndexError: ", e.what());
        PyErr_SetString(PyExc_IndexError, e.what());

    } catch (const std::overflow_error& e) {  // overflow_error -> OverflowError
        LOG(err, "OverflowError: ", e.what());
        PyErr_SetString(PyExc_OverflowError, e.what());

    } catch (const std::range_error& e) {  // range_error -> ArithmeticError
        LOG(err, "ArithmeticError: ", e.what());
        PyErr_SetString(PyExc_ArithmeticError, e.what());

    } catch (const std::underflow_error& e) {  // underflow_error -> ArithmeticError
        LOG(err, "ArithmeticError: ", e.what());
        PyErr_SetString(PyExc_ArithmeticError, e.what());
    }

    // all other exceptions map to RuntimeError
    catch (const std::exception& e) {
        LOG(err, "RuntimeError: ", e.what());
        PyErr_SetString(PyExc_RuntimeError, e.what());

    } catch (...) {
        LOG(err, "RuntimeError: Unknown C++ error (could not get exception message)");
        PyErr_SetString(
            PyExc_RuntimeError,
            "Unknown C++ error (could not get exception message)"
        );
    }
}


/* Convert an STL C++ exception into an equivalent python::Exception, preserving the
error message if possible. */
template <typename Exc = Exception>
Exc catch_cpp() {
    static_assert(
        std::is_convertible_v<Exc, Exception>,
        "Exception type must inherit from python::Exception"
    );

    try {
        throw;

    } catch (const Exception& e) {
        return Exc(e);
    }

    // Cython-style exception handling (matches existing rules)
    // https://cython.readthedocs.io/en/latest/src/userguide/wrapping_CPlusPlus.html#exceptions
    catch (const std::bad_alloc& e) {  // bad_alloc -> MemoryError
        return MemoryError(e.what());

    } catch (const std::bad_cast& e) {  // bad_cast -> TypeError
        return TypeError(e.what());

    } catch (const std::bad_typeid& e) {  // bad_typeid -> TypeError
        return TypeError(e.what());

    } catch (const std::domain_error& e) {  // domain_error -> ValueError
        return ValueError(e.what());

    } catch (const std::invalid_argument& e) {  // invalid_argument -> ValueError
        return ValueError(e.what());

    } catch (const std::ios_base::failure& e) {  // ios_base::failure -> IOError
        return IOError(e.what());

    } catch (const std::out_of_range& e) {  // out_of_range -> IndexError
        return IndexError(e.what());

    } catch (const std::overflow_error& e) {  // overflow_error -> OverflowError
        return OverflowError(e.what());

    } catch (const std::range_error& e) {  // range_error -> ArithmeticError
        return ArithmeticError(e.what());

    } catch (const std::underflow_error& e) {  // underflow_error -> ArithmeticError
        return ArithmeticError(e.what());
    }

    // all other exceptions map to RuntimeError
    catch (const std::exception& e) {
        return RuntimeError(e.what());

    } catch (...) {
        return RuntimeError("Unknown C++ error (could not get exception message)");
    }
}


//////////////////////
////    OBJECT    ////
//////////////////////


/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Object<Ref::NEW> as_object(PyObject* obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Type<Ref::NEW> as_object(PyTypeObject* obj) {
    return {obj};
}

/* Convert an arbitrary C++ object into an equivalent Python object. */
inline Function<Ref::NEW> as_object(PyFunctionObject* obj) {
    return {obj};
}

/* Convert an arbitrary C++ object into an equivalent Python object. */
inline Code<Ref::NEW> as_object(PyCodeObject* obj) {
    return {obj};
}

/* Convert an arbitrary C++ object into an equivalent Python object. */
inline Frame<Ref::NEW> as_object(PyFrameObject* obj) {
    return {obj};
}

/* Convert an arbitrary C++ object into an equivalent Python object. */
inline Int<Ref::NEW> as_object(PyLongObject* obj) {
    return {obj};
}

/* Convert an arbitrary C++ object into an equivalent Python object. */
inline Float<Ref::NEW> as_object(PyFloatObject* obj) {
    return {obj};
}

/* Convert an arbitrary C++ object into an equivalent Python object. */
inline Complex<Ref::NEW> as_object(PyComplexObject* obj) {
    return {obj};
}

/* Convert an arbitrary C++ object into an equivalent Python object. */
inline String<Ref::NEW> as_object(PyUnicodeObject* obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Bool as_object(bool obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Int as_object(long long obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Int as_object(unsigned long long obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline Float as_object(double obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline String as_object(const char* obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline String as_object(const std::string& obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
inline String as_object(const std::string_view& obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename First, typename Second>
inline Tuple as_object(const std::pair<First, Second>& obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename... Ts>
inline Tuple as_object(const std::tuple<Ts...>& obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename T>
inline List as_object(const std::vector<T>& obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename T>
inline List as_object(const std::list<T>& obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename T>
inline Set as_object(const std::unordered_set<T>& obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename T>
inline Set as_object(const std::set<T>& obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename K, typename V>
inline Dict as_object(const std::unordered_map<K, V>& obj) {
    return {obj};
}

/* Convert an arbitrary C++ object to an aquivalent Python object. */
template <typename K, typename V>
inline Dict as_object(const std::map<K, V>& obj) {
    return {obj};
}


/* A smart wrapper around a PyObject* pointer that automatically manages reference
counts according to a templated reference protocol.

python::Objects can be used in a variety of ways, and are designed to make interacting
with the CPython API as simple as possible from a C++ perspective.  In most cases, they
can be used identically to their Python counterparts, with the same semantics in both
languages.  They are both implicitly constructible from and convertible to PyObject*
pointers, allowing them to be passed transparently to most Python C API functions,
which are exposed in simplified form as ordinary member methods.  They can also be
iterated over, indexed, and called as if they were Python objects, with identical
semantics to normal Python.  They can also be added, subtracted, multiplied, and so on,
all delegating to the appropriate Python special methods.

What's more, Objects can be specified as parameter types in C++ functions, meaning
they can directly replace PyObject* pointers in most cases.  This has the advantage of
automatically applying any implicit conversions that are available for the given
inputs, making it possible to pass C++ types directly to Python functions without any
explicit conversions.  In combination with automatic reference counting, this means
that users should be able to write fully generic C++ code that looks and feels almost
exactly like standard Python, without having to worry about manual reference counting
or low-level memory management.

The reference protocol is specified as a template parameter, and can be one of three
values:
    1.  Ref::NEW: The wrapper increments the reference count on construction and
        decrements it on destruction.  This is the most common behavior for function
        parameter types, as it both allows Python objects to be passed to C++ functions
        without altering their net reference count, and allows the wrapper to construct
        temporary objects from C++ inputs while respecting automatic reference
        counting.
    2.  Ref::STEAL: The wrapper does not modify the reference count on construction,
        but decrements it on destruction.  This is the most common behavior for return
        types, as it allows the wrapper to transfer ownership of the underlying
        PyObject* to the caller without having to worry about reference counting.
        As such, it is also the default behavior for the wrapper, allowing users to
        omit the template parameter when capturing the result of a function call.
    3.  Ref::BORROW: The wrapper does not modify the reference count at all.  This is
        the highest performance option, but can be unsafe if the wrapper outlives the
        underlying PyObject*.  It also means that C++ inputs are not allowed, since
        the conversion would require a new reference to be created.  As such, it is only
        recommended for simple functions that do not need to own their inputs, or for
        temporary references that are guaranteed to outlive the wrapper.

Subclasses of this type can be used to wrap specific Python types, such as built-in
integers, floats, and strings, as well as containers like lists, tuples, and
dictionaries.  These subclasses provide additional methods and operators that are
particular to the given type, and use stricter conversion rules that allow for greater
type safety.  For C++ objects, this will often translate into compile-time errors if
the wrong type is used, ensuring correctness at the C++ level.  For Python objects, it
often translates into a null check followed by an isinstance() call against the
specified type, which are executed at runtime.  In the case of built-in types, this can
be significantly faster than the equivalent Python code, thanks to the use of lower
level API calls rather than isinstance() directly.
*/
template <Ref ref = Ref::STEAL>
struct Object {
    PyObject* obj;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to a null pointer. */
    inline Object() noexcept : obj(nullptr) {}

    /* Implicit conversion constructor from an unwrapped PyObject* pointer.  This
    applies the templated reference protocol to the input on construction. */
    inline Object(PyObject* obj) noexcept : obj(obj) {
        if constexpr (ref == Ref::NEW) {
            python::xincref(obj);
        }
    }

    /* Copy constructor.  For owning references, this acquires a new reference to the
    object.  Borrowed references do not modify the reference count. */
    template <Ref R>
    inline Object(const Object<R>& other) noexcept : obj(other.obj) {
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            python::xincref(obj);
        }
    }

    /* Move constructor.  For owning references, this transfers ownership.  Ownership
    cannot be transferred from a borrowed reference to an owning reference or vice
    versa. */
    template <Ref R>
    inline Object(Object<R>&& other) noexcept : obj(other.obj) {
        static_assert(
            !(ref == Ref::BORROW && (R == Ref::NEW || R == Ref::STEAL)),
            "cannot move an owning reference into a non-owning reference"
        );
        static_assert(
            !(ref == Ref::NEW || ref == Ref::STEAL) && R == Ref::BORROW,
            "cannot move a non-owning reference into an owning reference"
        );
        other.obj = nullptr;
    }

    /* Copy assignment operator.  For owning references, this decrements the reference
    count of the previous value and increments that of the new value.  Borrowed
    references do not modify the reference count. */
    template <Ref R>
    Object& operator=(const Object<R>& other) {
        if constexpr (R == ref) {
            if (this == &other) {
                return *this;
            }
        }
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            python::xdecref(obj);
            python::xincref(other.obj);
        }
        obj = other.obj;
        return *this;
    }

    /* Move assignment operator.  For owning references, this decrements the reference
    count of the previous value and then transfers ownership of the new value.
    Ownership cannot be transferred from a non-owning reference to an owning one or
    vice versa. */
    template <Ref R>
    Object& operator=(Object<R>&& other) {
        static_assert(
            !(ref == Ref::BORROW && (R == Ref::NEW || R == Ref::STEAL)),
            "cannot move an owning reference into a non-owning reference"
        );
        static_assert(
            !(ref == Ref::NEW || ref == Ref::STEAL) && R == Ref::BORROW,
            "cannot move a non-owning reference into an owning reference"
        )
        if constexpr (R == ref) {
            if (this == &other) {
                return *this;
            }
        }
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            python::xdecref(obj);
        }
        obj = other.obj;
        other.obj = nullptr;
        return *this;
    }

    /* For owning references, release the Python object on destruction.  Otherwise,
    do nothing. */
    inline ~Object() noexcept {
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            python::xdecref(obj);
        }
    }

    /* Retrieve the wrapped object and relinquish ownership over it. */
    inline PyObject* unwrap() {
        PyObject* result = obj;
        obj = nullptr;
        return result;
    }

    /* Implicitly convert a python::Object into a PyObject* pointer.  Returns a
    borrowed reference to the python object.  This allows the Object wrapper to be
    passed directly to Python API functions as needed. */
    inline operator PyObject*() const noexcept {
        return obj;
    }

    /* Dereference a python::Object to dereference the underlying PyObject* pointer. */
    inline PyObject operator*() const noexcept {
        return *obj;
    }

    /* Implicitly convert a python::Object into a boolean, testing the underlying value
    for truthiness. Note that if the Object wraps a null pointer, then this will always
    evaluate false.  Use Object.obj == nullptr to disambiguate. */
    inline operator bool() const noexcept {
        if (obj == nullptr) {
            return false;
        }
        return python::truthy(obj);
    }

    /* Implicitly convert a python::Object into a C long.  This calls the object's
    __index__() special method, allowing the conversion to work for any integer-like
    object, not just built-in integers.  Throws an OverflowError if the object is out
    of range for a long. */
    inline operator long() const {
        long result = PyLong_AsLong(obj);
        if (result == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return result;
    }

    /* Implicitly convert a python::Object into a C long long.  This calls the object's
    __index__() special method, allowing the conversion to work for any integer-like
    object, not just built-in integers.  Throws an OverflowError if the object is out
    of range for a long long. */
    inline operator long long() const {
        long long result = PyLong_AsLongLong(obj);
        if (result == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return result;
    }

    /* Implicitly convert a python::Object into a C unsigned long.  This calls the
    object's __index__() special method, allowing the conversion to work for any
    integer-like object, not just built-in integers.  Throws an OverflowError if the
    object is negative or out of range for an unsigned long. */
    inline operator unsigned long() const {
        unsigned long result = PyLong_AsUnsignedLong(obj);
        if (result == (unsigned long long)-1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return result;
    }

    /* Implicitly convert a python::Object into a C unsigned long long.  This calls the
    object's __index__() special method, allowing the conversion to work for any
    integer-like object, not just built-in integers.  Throws an OverflowError if the
    object is negative or out of range for an unsigned long long. */
    inline operator unsigned long long() const {
        unsigned long long result = PyLong_AsUnsignedLongLong(obj);
        if (result == (unsigned long long)-1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return result;
    }

    /* Implicitly convert a python::Object into a C double.  This first calls the
    object's __float__() special method, falling back to __index__() if it is not
    defined.  As such, it can be used on any numeric Python object.  Throws an
    OverflowError if the object is out of range for a double. */
    inline operator double() const {
        double result = PyFloat_AsDouble(obj);
        if (result == -1.0 && PyErr_Occurred()) {
            throw catch_python();
        }
        return result;
    }

    /* Implicitly convert a python::Object into a C++ string.  This is analogous to
    calling str() on the object and then converting the result to a std::string.*/
    inline operator std::string() const {
        PyObject* string = PyObject_Str(obj);
        if (string == nullptr) {
            throw catch_python();
        }
        Py_ssize_t size;
        const char* result = PyUnicode_AsUTF8AndSize(string, &size);
        if (result == nullptr) {
            Py_DECREF(string);
            throw catch_python();
        }
        result = std::string(result, static_cast<size_t>(size));
        Py_DECREF(string);  // ensure character buffer remains valid during conversion
        return result;
    }

    //////////////////////////////////
    ////    REFERENCE COUNTING    ////
    //////////////////////////////////

    /* Get the object's reference count.  Object must not be null. */
    inline Py_ssize_t refcount() const noexcept {
        return Py_REFCNT(obj);
    }

    /* Increment the object's reference count.  Object must not be null. */
    inline void incref() noexcept {
        LOG(ref, "incref(", obj, ")");
        Py_INCREF(obj);
    }

    /* Decrement the object's reference count.  Object must not be null. */
    inline void decref() noexcept {
        LOG(ref, "decref(", obj, ")");
        Py_DECREF(obj);
    }

    /* Increment the object's reference count, allowing for null pointers. */
    inline void xincref() noexcept {
        LOG(ref, "xincref(", obj, ")");
        Py_XINCREF(obj);
    }

    /* Decrement the object's reference count, allowing for null pointers. */
    inline void xdecref() noexcept {
        LOG(ref, "xdecref(", obj, ")");
        Py_XDECREF(obj);
    }

    /////////////////////////////
    ////    TYPE CHECKING    ////
    /////////////////////////////

    /* Get the type of the Python object. */
    inline Type<Ref::STEAL> type() const {
        return {obj};
    }

    /* Check whether the object is an instance of the given type.  This can accept
    type objects or tuples of type objects, just like a python-level isinstance()
    check. */
    inline bool isinstance(PyObject* type) const {
        int result = PyObject_IsInstance(obj, type);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Check whether the object is an instance of the given type.  This can accept
    type objects or tuples of type objects, just like a python-level isinstance()
    check. */
    inline bool isinstance(PyTypeObject* type) const {
        return isinstance(reinterpret_cast<PyObject*>(type));
    }

    /* Check whether this object is a subclass of the given type.  This can accept
    type objects or tuples of type objects, just like a python-level issubclass()
    check. */
    inline bool issubclass(PyObject* base) const {
        int result = PyObject_IsSubclass(obj, base);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Check whether this object is a subclass of the given type.  This can accept
    type objects or tuples of type objects, just like a python-level issubclass()
    check. */
    inline bool issubclass(PyTypeObject* base) const {
        return issubclass(reinterpret_cast<PyObject*>(base));
    }

    ////////////////////////////////
    ////    ATTRIBUTE ACCESS    ////
    ////////////////////////////////

    /* Check if the object has an attribute with the given name.  Silently ignores
    any errors emanating from an object's __getattr__() or __getattribute__() methods.
    For proper error handling, use getattr() instead. */
    inline bool hasattr(PyObject* attr) const noexcept {
        return PyObject_HasAttr(obj, attr);
    }

    /* Check if the object has an attribute with the given name.  Silently ignores
    any errors emanating from an object's __getattr__() or __getattribute__() methods.
    For proper error handling, use getattr() instead. */
    inline bool hasattr(const char* attr) const noexcept {
        return PyObject_HasAttrString(obj, attr);
    }

    /* Check if the object has an attribute with the given name.  Silently ignores
    any errors emanating from an object's __getattr__() or __getattribute__() methods.
    For proper error handling, use getattr() instead. */
    inline bool hasattr(const std::string& attr) const noexcept {
        return hasattr(attr.c_str());
    }

    /* Check if the object has an attribute with the given name.  Silently ignores
    any errors emanating from an object's __getattr__() or __getattribute__() methods.
    For proper error handling, use getattr() instead. */
    inline bool hasattr(const std::string_view& attr) const noexcept {
        return hasattr(attr.data());
    }

    /* Get an attribute from the object.  Throws an AttributeError if the attribute
    does not exist, or another error if one originates from __getattr__() or
    __getattribute__(). */
    inline Object<Ref::STEAL> getattr(PyObject* attr) const {
        PyObject* result = PyObject_GetAttr(obj, attr);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Get an attribute from the object.  Throws an AttributeError if the attribute
    does not exist, or another error if one originates from __getattr__() or
    __getattribute__(). */
    inline Object<Ref::STEAL> getattr(const char* attr) const {
        PyObject* result = PyObject_GetAttrString(obj, attr);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Get an attribute from the object.  Throws an AttributeError if the attribute
    does not exist, or another error if one originates from __getattr__() or
    __getattribute__(). */
    inline Object<Ref::STEAL> getattr(const std::string& attr) const {
        return getattr(attr.c_str());
    }

    /* Get an attribute from the object.  Throws an AttributeError if the attribute
    does not exist, or another error if one originates from __getattr__() or
    __getattribute__(). */
    inline Object<Ref::STEAL> getattr(const std::string_view& attr) const {
        return getattr(attr.data());
    }

    /* Get an attribute from the object or return a default value if it does not exist.
    Can throw if an error originates from __getattr__() or __getattribute__(). */
    inline Object<Ref::STEAL> getattr(
        PyObject* attr, Object<Ref::BORROW> default_value
    ) const {
        PyObject* result = PyObject_GetAttr(obj, attr);
        if (result == nullptr) {
            if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
                return {Py_XNewRef(default_value.obj)};
            }
        }
        return {result};
    }

    /* Get an attribute from the object or return a default value if it does not exist.
    Can throw if an error originates from __getattr__() or __getattribute__(). */
    inline Object<Ref::STEAL> getattr(
        const char* attr, Object<Ref::BORROW> default_value
    ) const {
        PyObject* result = PyObject_GetAttrString(obj, attr);
        if (result == nullptr) {
            if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Clear();
                return {Py_XNewRef(default_value.obj)};
            }
        }
        return {result};
    }

    /* Get an attribute from the object or return a default value if it does not exist.
    Can throw if an error originates from __getattr__() or __getattribute__(). */
    inline Object<Ref::STEAL> getattr(
        const std::string& attr, Object<Ref::BORROW> default_value
    ) const {
        return getattr(attr.c_str(), default_value);
    }

    /* Get an attribute from the object or return a default value if it does not exist.
    Can throw if an error originates from __getattr__() or __getattribute__(). */
    inline Object<Ref::STEAL> getattr(
        const std::string_view& attr, Object<Ref::BORROW> default_value
    ) const {
        return getattr(attr.data(), default_value);
    }

    /* Set an attribute on the object.  Can throw if an error originates from
    __setattr__(). */
    inline void setattr(PyObject* attr, Object<Ref::BORROW> value) {
        if (PyObject_SetAttr(obj, attr, value.obj)) {
            throw catch_python();
        }
    }

    /* Set an attribute on the object.  Can throw if an error originates from
    __setattr__(). */
    inline void setattr(const char* attr, Object<Ref::BORROW> value) {
        if (PyObject_SetAttrString(obj, attr, value.obj)) {
            throw catch_python();
        }
    }

    /* Set an attribute on the object.  Can throw if an error originates from
    __setattr__(). */
    inline void setattr(const std::string& attr, Object<Ref::BORROW> value) {
        setattr(attr.c_str(), value);
    }

    /* Set an attribute on the object.  Can throw if an error originates from
    __setattr__(). */
    inline void setattr(const std::string_view& attr, Object<Ref::BORROW> value) {
        setattr(attr.data(), value);
    }

    /* Delete an attribute from an object.  Can throw if an error originates from
    __delattr__(). */
    inline void delattr(PyObject* attr) {
        if (PyObject_DelAttr(obj, attr)) {
            throw catch_python();
        }
    }

    /* Delete an attribute from an object.  Can throw if an error originates from
    __delattr__(). */
    inline void delattr(const char* attr) {
        if (PyObject_DelAttrString(obj, attr)) {
            throw catch_python();
        }
    }

    /* Delete an attribute from an object.  Can throw if an error originates from
    __delattr__(). */
    inline void delattr(const std::string& attr) {
        delattr(attr.c_str());
    }

    /* Delete an attribute from an object.  Can throw if an error originates from
    __delattr__(). */
    inline void delattr(const std::string_view& attr) {
        delattr(attr.data());
    }

    /* Get a list of strings representing named attributes of the object. */
    inline List<Ref::STEAL> dir() const {
        if (obj == nullptr) {
            throw TypeError("cannot call dir() on a null object");
        }
        PyObject* result = PyObject_Dir(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /////////////////////////////
    ////    CALL PROTOCOL    ////
    /////////////////////////////

    /* Check if a Python object is callable.  Equivalent to Python callable(). */
    inline bool callable() const noexcept {
        return PyCallable_Check(obj);
    }

    /* Call the object using C-style positional arguments.  Returns a new reference. */
    template <typename... Args>
    Object<Ref::STEAL> operator()(Args&&... args) const {
        if constexpr (sizeof...(Args) == 0) {
            PyObject* result = PyObject_CallNoArgs(obj);
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};

        } else if constexpr (sizeof...(Args) == 1) {
            PyObject* result = PyObject_CallOneArg(obj, std::forward<Args>(args)...);
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};

        } else {
            PyObject* result = PyObject_CallFunctionObjArgs(
                obj, std::forward<Args>(args)..., nullptr
            );
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};
        }
    }

    /* Call the object using Python-style positional arguments.  Returns a new
    reference. */
    inline Object<Ref::STEAL> call(PyObject* args) const {
        PyObject* result = PyObject_CallObject(obj, args);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Call the object using Python-style positional and keyword arguments.  Returns a
    new reference. */
    inline Object<Ref::STEAL> call(PyObject* args, PyObject* kwargs) const {
        PyObject* result = PyObject_Call(obj, args, kwargs);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Call the object using Python's vectorcall protocol.  Returns a new reference. */
    inline Object<Ref::STEAL> call(
        PyObject* const* args,
        size_t npositional,
        PyObject* kwnames
    ) const {
        PyObject* result = PyObject_Vectorcall(obj, args, npositional, kwnames);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /////////////////////////
    ////    ITERATION    ////
    /////////////////////////

    /* A C++ wrapper around a Python iterator that enables it to be used in idiomatic
    C++ loops.  Dereferences to Object<Ref::BORROW>, the reference for which is managed
    internally by the iterator. */
    class Iterator {
        friend Object;
        PyObject* iterator;
        PyObject* curr;

        Iterator() : iterator(nullptr), curr(nullptr) {}

        Iterator(PyObject* it) : iterator(it), curr(nullptr) {
            curr = PyIter_Next(iterator);
            if (curr == nullptr && PyErr_Occurred()) {
                Py_DECREF(iterator);
                throw catch_python();
            }
        }

    public:
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = Object<Ref::BORROW>;
        using pointer               = value_type*;
        using reference             = value_type&;

        Iterator(const Iterator& other) :
            iterator(Py_XNewRef(other.iterator)), curr(Py_XNewRef(other.curr))
        {}

        Iterator(Iterator&& other) : iterator(other.iterator), curr(other.curr) {
            other.iterator = nullptr;
            other.curr = nullptr;
        }

        Iterator& operator=(const Iterator& other) {
            if (this == &other) {
                return *this;
            }
            Py_XINCREF(iterator);
            Py_XINCREF(curr);
            iterator = other.iterator;
            curr = other.curr;
            return *this;
        }

        Iterator& operator=(Iterator&& other) {
            if (this == &other) {
                return *this;
            }
            iterator = other.iterator;
            curr = other.curr;
            other.iterator = nullptr;
            other.curr = nullptr;
            return *this;
        }

        ~Iterator() {
            Py_XDECREF(iterator);
            Py_XDECREF(curr);
        }

        /* Get current item. */
        inline Object<Ref::BORROW> operator*() const {
            return {curr};
        }

        /* Advance to next item. */
        inline Iterator& operator++() {
            Py_DECREF(curr);
            curr = PyIter_Next(iterator);
            if (curr == nullptr && PyErr_Occurred()) {
                throw catch_python();
            }
            return *this;
        }

        /* Terminate iteration. */
        template <typename F>
        inline bool operator!=(const Iterator<F>& other) const {
            return curr != other.curr;
        }

    };

    inline Iterator begin() const {
        PyObject* iter = PyObject_GetIter(obj);
        if (iter == nullptr) {
            throw catch_python();
        }
        return {iter};
    }

    inline Iterator rbegin() const {
        PyObject* attr = PyObject_GetAttrString(obj, "__reversed__");
        if (attr == nullptr && PyErr_Occurred()) {
            throw catch_python();
        }

        PyObject* iter = PyObject_CallNoArgs(attr);
        Py_DECREF(attr);
        if (iter == nullptr && PyErr_Occurred()) {
            throw catch_python();
        }
        return {iter};
    }

    inline Iterator cbegin() const { return begin(); }
    inline Iterator crbegin() const { return rbegin(); }
    inline Iterator end() const { return {}; }
    inline Iterator rend() const { return {}; }
    inline Iterator cend() const { return end(); }
    inline Iterator crend() const { return rend(); }

    /* An encapsulated pair of C++ iterators that can be iterated over directly, rather
    than requiring separate begin() and end() iterators. */
    class IteratorPair {
        friend Object;
        Iterator _begin;
        Iterator _end;

        IteratorPair(Iterator&& begin, Iterator&& end) :
            _begin(std::move(begin)), _end(std::move(end))
        {}

    public:
        using iterator_category     = Iterator::iterator_category;
        using difference_type       = Iterator::difference_type;
        using value_type            = Iterator::value_type;
        using pointer               = Iterator::pointer;
        using reference             = Iterator::reference;

        /* Copy constructor. */
        IteratorPair(const IteratorPair& other) :
            _begin(other._begin), _end(other._end)
        {}

        /* Move constructor. */
        IteratorPair(IteratorPair&& other) :
            _begin(std::move(other._begin)), _end(std::move(other._end))
        {}

        /* Copy assignment operator. */
        IteratorPair& operator=(const IteratorPair& other) {
            if (this == &other) {
                return *this;
            }
            _begin = other._begin;
            _end = other._end;
            return *this;
        }

        /* Move assignment operator. */
        IteratorPair& operator=(IteratorPair&& other) {
            if (this == &other) {
                return *this;
            }
            _begin = std::move(other._begin);
            _end = std::move(other._end);
            return *this;
        }

        /* Get the first iterator. */
        inline Iterator& begin() const {
            return _begin;
        }

        /* Get the second iterator. */
        inline Iterator& end() const {
            return _end;
        }

        /* Dereference the begin iterator. */
        inline Object<Ref::BORROW> operator*() const {
            return *_begin;
        }

        /* Advance the begin iterator. */
        inline IteratorPair& operator++() {
            ++_begin;
            return *this;
        }

        /* Terminate sequence. */
        inline bool operator!=(const IteratorPair& other) const {
            return _begin != other._begin;
        }

    };

    /* Get the length of a Python object.  Returns nullopt if the object does not
    support the sequence protocol. */
    inline std::optional<size_t> len() const {
        if (!PyObject_HasAttrString(obj, "__len__")) {
            return std::nullopt;
        }
        Py_ssize_t result = PyObject_Length(obj);
        if (result == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return std::make_optional(static_cast<size_t>(result));
    }

    /* Get a C++ iterator over a Python object.  Returns an iterator pair that contains
    both a begin() and end() member, any of which can be used in idiomatic C++ loops.
    Can throw if the object is not iterable. */
    inline IteratorPair iter() const {
        return {begin(), end()};
    }

    /* Get a pair of iterators that can be used to iterate over the object in reverse.
    Can throw if the object is not reverse iterable. */
    inline IteratorPair reversed() const {
        return {rbegin(), rend()};
    }

    /* Get the next item from an iterator.  The object must be an iterator.  Can throw
    if there was an error retrieving the next item, or if there are no more items in
    the iterator, in which case a StopIteration exception is thrown, just like in
    Python. */
    inline Object<Ref::STEAL> next() const {
        PyObject* result = PyIter_Next(obj);
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                throw catch_python();
            }
            throw StopIteration();
        }
        return {result};
    }

    /* Get the next item from an iterator, or a default value if the iterator is
    exhausted.  The argument must be an iterator.  Borrows a reference to the default
    value.  Can throw if there was an error retrieving the next item. */
    inline Object<Ref::STEAL> next(Object<Ref::BORROW> default_value) {
        PyObject* result = PyIter_Next(obj);
        if (result == nullptr) {
            if (PyErr_Occurred()) {
                throw catch_python();
            }
            return {Py_XNewRef(default_value.obj)};
        }
        return {result};
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* A proxy for a key used to index the object. */
    class Element {
        friend Object;
        PyObject* obj;
        PyObject* key;

        Element(PyObject* obj, PyObject* key) : obj(obj), key(key) {}

    public:

        /* Get the item with the specified key.  Can throw if the object does not
        support the `__getitem__()` method. */
        inline Object<Ref::STEAL> get() const {
            PyObject* result = PyObject_GetItem(obj, key);
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};
        }

        /* Set the item with the specified key.  Releases a reference to the previous
        value in case of an error, and then holds a reference to the new value.  Can
        throw if the object does not support the `__setitem__()` method. */
        inline void set(Object<Ref::BORROW> value) {
            if (PyObject_SetItem(obj, key, value)) {
                throw catch_python();
            }
        }

        /* Delete the item with the specified key.  Releases a reference to the value.
        Can throw if the object does not support the `__delitem__()` method. */
        inline void del() {
            if (PyObject_DelItem(obj, key)) {
                throw catch_python();
            }
        }

        /* Implicitly convert an ElementProxy to the item with the given key, allowing
        it to be assigned to an lvalue or passed as an argument to a function. */
        inline operator Object<Ref::STEAL>() const {
            return get();
        }

        /* Assign to an ElementProxy, allowing python-like insertion syntax. */
        inline void operator=(Object<Ref::BORROW> value) {
            set(value);
        }

    };

    /* Index into the object, returning a proxy that forwards to its __getitem__(),
    __setitem__(), and __delitem__() methods. */
    inline Element operator[](PyObject* key) noexcept {
        return {obj, key};
    }

    /* Index into the object, returning a proxy that forwards to its __getitem__(),
    __setitem__(), and __delitem__() methods. */
    inline const Element operator[](PyObject* key) const noexcept {
        return {obj, key};
    }

    ///////////////////////////////
    ////    UNARY OPERATORS    ////
    ///////////////////////////////

    /* Get the hash of the object.  Can throw if the object does not support hashing. */
    inline size_t hash() const {
        // ASCII string special case (taken from CPython source)
        // see: cpython/objects/setobject.c; set_contains_key()
        Py_ssize_t result;
        if (
            !PyUnicode_CheckExact(obj) ||
            (result = _PyASCIIObject_CAST(obj)->hash) == -1
        ) {
            result = PyObject_Hash(obj);  // fall back to PyObject_Hash()
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
        }
        return static_cast<size_t>(result);
    }

    /* Get the absolute value of the object.  Can throw if the object is not numeric. */
    inline Object<Ref::STEAL> abs() const {
        PyObject* result = PyNumber_Absolute(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    inline Object<Ref::STEAL> operator+() const {
        PyObject* result = PyNumber_Positive(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    inline Object<Ref::STEAL> operator-() const {
        PyObject* result = PyNumber_Negative(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    inline Object<Ref::STEAL> operator~() const {
        PyObject* result = PyNumber_Invert(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Get a string representation of a Python object.  Equivalent to Python str(). */
    inline String<Ref::STEAL> str() const {
        PyObject* string = PyObject_Str(obj);
        if (string == nullptr) {
            throw catch_python();
        }
        return {string};
    }

    /* Get a string representation of a Python object.  Equivalent to Python repr(). */
    inline String<Ref::STEAL> repr() const {
        PyObject* string = PyObject_Repr(obj);
        if (string == nullptr) {
            throw catch_python();
        }
        return {string};
    }

    /* Get a string representation of a Python object with non-ASCII characters escaped.
    Equivalent to Python ascii(). */
    inline String<Ref::STEAL> ascii() const {
        PyObject* string = PyObject_ASCII(obj);
        if (string == nullptr) {
            throw catch_python();
        }
        return {string};
    }

    /* Convert an integer or integer-like object (one that implements __index__()) into
    a binary string representation.  Equivalent to Python bin(). */
    inline String<Ref::STEAL> bin() const {
        PyObject* string = PyNumber_ToBase(integer, 2);
        if (string == nullptr) {
            throw catch_python();
        }
        return {string};
    }

    /* Convert an integer or integer-like object (one that implements __index__()) into
    an octal string representation.  Equivalent to Python oct(). */
    inline String<Ref::STEAL> oct() const {
        PyObject* string = PyNumber_ToBase(integer, 8);
        if (string == nullptr) {
            throw catch_python();
        }
        return {string};
    }

    /* Convert an integer or integer-like object (one that implements __index__()) into
    a hexadecimal string representation.  Equivalent to Python hext(). */
    inline String<Ref::STEAL> hex() const {
        PyObject* string = PyNumber_ToBase(obj, 16);
        if (string == nullptr) {
            throw catch_python();
        }
        return {string};
    }

    /* Convert a Python integer into a unicode character.  Can throw if the object is
    not an integer, or if it is outside the range for a valid unicode character. */
    inline String<Ref::STEAL> chr() const {
        long long val = PyLong_AsLongLong(obj);
        if (val == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return {val};
    }

    /* Convert a unicode character into an integer.  Can throw if the argument is null
    or not a string of length 1. */
    inline long long ord() const {
        if (obj == nullptr) {
            throw TypeError("ord() argument must not be null");
        }

        if (!PyUnicode_Check(obj)) {
            std::ostringstream msg;
            msg << "ord() expected a string of length 1, but ";
            msg << Py_TYPE(obj)->tp_name << "found";
            throw TypeError(msg.str());
        }

        Py_ssize_t length = PyUnicode_GET_LENGTH(obj);
        if (length != 1) {
            std::ostringstream msg;
            msg << "ord() expected a character, but string of length " << length;
            msg << " found";
            throw TypeError(msg.str());
        }

        return PyUnicode_READ_CHAR(obj, 0);
    }

    ////////////////////////////////
    ////    BINARY OPERATORS    ////
    ////////////////////////////////

    /* Apply a Python-level identity comparison to the object.  Equivalent to
    `obj is other`.  Always succeeds. */
    inline bool is(Object<Ref::BORROW> other) const noexcept {
        return obj == other.obj;
    }

    /* Check whether the object contains a given value.  Equivalent to `value in obj`.
    Can throw if the object does not support the sequence protocol. */
    template <typename T>
    inline bool contains(T&& value) const {
        int result = PyObject_Contains(obj, as_object(std::forward<T>(value)));
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator<(T&& other) const {
        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_LT
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator<=(T&& other) const {
        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_LE
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator==(T&& other) const {
        using U = std::decay_t<T>;
        if constexpr (
            std::is_base_of_v<Object<Ref::NEW>, U> ||
            std::is_base_of_v<Object<Ref::STEAL>, U> ||
            std::is_base_of_v<Object<Ref::BORROW>, U>
        ) {
            if (obj == other.obj) {
                return true;
            };
        } else if constexpr (std::is_pointer_v<U>) {
            if (obj == other) {
                return true;
            }
        }

        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_EQ
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator!=(T&& other) const {
        using U = std::decay_t<T>;
        if constexpr (
            std::is_base_of_v<Object<Ref::NEW>, U> ||
            std::is_base_of_v<Object<Ref::STEAL>, U> ||
            std::is_base_of_v<Object<Ref::BORROW>, U>
        ) {
            if (obj == other.obj) {
                return false;
            };
        } else if constexpr (std::is_pointer_v<U>) {
            if (obj == other) {
                return false;
            }
        }

        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_NE
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator>=(T&& other) const {
        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_GE
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    template <typename T>
    inline bool operator>(T&& other) const {
        int result = PyObject_RichCompareBool(
            obj, as_object(std::forward<T>(other)), Py_GT
        );
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    ///////////////////////////////
    ////    NUMBER PROTOCOL    ////
    ///////////////////////////////

    template <typename T>
    inline Object<Ref::STEAL> floor_divide(T&& other) const {
        PyObject* result = PyNumber_FloorDivide(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& inplace_floor_divide(T&& other) {
        PyObject* result = PyNumber_InPlaceFloorDivide(
            obj, as_object(std::forward<T>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Tuple<Ref::STEAL> divmod(T&& divisor) const {
        PyObject* result = PyNumber_Divmod(obj, as_object(std::forward<T>(divisor)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object<Ref::STEAL> pow(T&& exponent) const {
        PyObject* result = PyNumber_Power(
            obj,
            as_object(std::forward<T>(exponent)),
            &Py_None
        );
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T, typename U>
    inline Object<Ref::STEAL> pow(T&& exponent, U&& modulus) const {
        PyObject* result = PyNumber_Power(
            obj,
            as_object(std::forward<T>(exponent)),
            as_object(std::forward<U>(modulus))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& inplace_power(T&& other) {
        PyObject* result = PyNumber_InPlacePower(
            obj, as_object(std::forward<T>(other)), &Py_None
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T, typename U>
    inline Object& inplace_power(T&& other, U&& other) {
        PyObject* result = PyNumber_InPlacePower(
            obj,
            as_object(std::forward<T>(other)),
            as_object(std::forward<U>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> matrix_multiply(T&& other) const {
        PyObject* result = PyNumber_MatrixMultiply(
            obj,
            as_object(std::forward<T>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& inplace_matrix_multiply(T&& other) {
        PyObject* result = PyNumber_InPlaceMatrixMultiply(
            obj,
            as_object(std::forward<T>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator+(T&& other) const {
        PyObject* result = PyNumber_Add(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& operator+=(T&& other) {
        PyObject* result = PyNumber_InPlaceAdd(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator-(T&& other) const {
        PyObject* result = PyNumber_Subtract(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& operator-=(T&& other) {
        PyObject* result = PyNumber_InPlaceSubtract(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator*(T&& other) const {
        PyObject* result = PyNumber_Multiply(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& operator*=(T&& other) {
        PyObject* result = PyNumber_InPlaceMultiply(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator/(T&& other) const {
        PyObject* result = PyNumber_TrueDivide(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& operator/=(T&& other) {
        PyObject* result = PyNumber_InPlaceTrueDivide(
            obj, as_object(std::forward<T>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator%(T&& other) const {
        PyObject* result = PyNumber_Remainder(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& operator%=(T&& other) {
        PyObject* result = PyNumber_InPlaceRemainder(
            obj, as_object(std::forward<T>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator<<(T&& other) const {
        PyObject* result = PyNumber_Lshift(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& operator<<=(T&& other) {
        PyObject* result = PyNumber_InPlaceLshift(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator>>(T&& other) const {
        PyObject* result = PyNumber_Rshift(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& operator>>=(T&& other) {
        PyObject* result = PyNumber_InPlaceRshift(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator|(T&& other) const {
        PyObject* result = PyNumber_Or(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& operator|=(T&& other) {
        PyObject* result = PyNumber_InPlaceOr(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator&(T&& other) const {
        PyObject* result = PyNumber_And(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& operator&=(T&& other) {
        PyObject* result = PyNumber_InPlaceAnd(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    template <typename T>
    inline Object<Ref::STEAL> operator^(T&& other) const {
        PyObject* result = PyNumber_Xor(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    template <typename T>
    inline Object& operator^=(T&& other) {
        PyObject* result = PyNumber_InPlaceXor(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

};


/* Dump a string representation of the object to an output stream.  This is equivalent
to calling repr() on the object and then streaming the result to the output stream. */
template <Ref ref>
inline ostream& operator<<(ostream& os, const Object<ref>& obj) {
    PyObject* string = PyObject_Repr(obj);
    if (string == nullptr) {
        throw catch_python();
    }
    Py_ssize_t size;
    const char* result = PyUnicode_AsUTF8AndSize(string, &size);
    if (result == nullptr) {
        Py_DECREF(string);
        throw catch_python();
    }
    os << std::string(result, static_cast<size_t>(size));
    Py_DECREF(string);  // ensure character buffer remains valid while we stream it
    return os;
}


////////////////////////////////////
////    NON-MEMBER FUNCTIONS    ////
////////////////////////////////////


/* Check whether the object is an instance of the given type.  This can accept type
objects or tuples of type objects, just like a python-level isinstance() check. */
template <typename T>
inline bool isinstance(Object<Ref::BORROW> obj, T&& type) {
    return obj.isinstance(std::forward<T>(type));
}


/* Check whether this object is a subclass of the given type.  This can accept type
objects or tuples of type objects, just like a python-level issubclass() check. */
template <typename T>
inline bool issubclass(Type<Ref::BORROW> derived, T&& base) {
    return derived.issubclass(std::forward<T>(base));
}


/* Check if the object has an attribute with the given name.  Silently ignores any
errors emanating from an object's __getattr__() or __getattribute__() methods.  For
proper error handling, use getattr() instead. */
template <typename T>
inline bool hasattr(Object<Ref::BORROW> obj, T&& attr) noexcept {
    return obj.hasattr(std::forward<T>(attr));
}


/* Get an attribute from an object.  Throws an AttributeError if the attribute does not
exist, or another error if one originates from __getattr__() or __getattribute__(). */
template <typename T>
inline Object getattr(Object<Ref::BORROW> obj, T&& attr) {
    return obj.getattr(std::forward<T>(attr));
}


/* Get an attribute from the object or return a default value if it does not exist.
Can throw if an error originates from __getattr__() or __getattribute__(). */
template <typename T, typename U>
inline Object getattr(Object<Ref::BORROW> obj, T&& attr, U&& default_value) {
    return obj.getattr(std::forward<T>(attr), std::forward<U>(default_value));
}


/* Set an attribute on the object.  Can throw if an error originates from
__setattr__(). */
template <typename T, typename U>
inline void setattr(Object<Ref::BORROW> obj, T&& attr, U&& value) {
    obj.setattr(std::forward<T>(attr), std::forward<U>(value));
}


/* Get a list of strings representing named attributes of the object, or of the local
namespace if the argument is null. */
inline List dir(Object<Ref::BORROW> obj) {
    if (obj == nullptr) {
        PyObject* result = PyObject_Dir(nullptr);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }
    return obj.dir();
}


/* Check if a Python object is callable.  Equivalent to Python callable(). */
inline bool callable(Object<Ref::BORROW> obj) noexcept {
    return obj.callable();
}


/* Get the length of a Python object.  Returns nullopt if the object does not support
the sequence protocol. */
inline std::optional<size_t> len(Object<Ref::BORROW> obj) {
    return obj.len();
}


/* Get a C++ iterator over a Python object.  Returns an iterator pair that contains
both a begin() and end() member, any of which can be used in idiomatic C++ loops.  Can
throw if the object is not iterable. */
inline auto iter(Object<Ref::BORROW> obj) {
    return obj.iter();
}


/* Get a C++ reverse iterator over a Python object.  Returns an iterator pair that
contains both a begin() and end() member, any of which can be used in idiomatic C++
loops.  Can throw if the object is not reverse iterable. */
inline auto reversed(Object<Ref::BORROW> obj) {
    return obj.reversed();
}


/* Get the next item from an iterator.  The argument must be an iterator.  Can throw if
there was an error retrieving the next item, or if there are no more items in the
iterator, in which case a StopIteration exception is thrown, just like in Python. */
inline Object next(Object<Ref::BORROW> iter) {
    return iter.next();
}


/* Get the next item from an iterator, or a default value if the iterator is exhausted.
The argument must be an iterator.  Borrows a reference to the default value.  Can throw
if there was an error retrieving the next item. */
inline Object next(Object<Ref::BORROW> iter, Object<Ref::BORROW> default_value) {
    return iter.next(default_value);
}


/* Hash a Python object.  Can throw if the object does not support hashing. */
inline size_t hash(Object<Ref::BORROW> obj) {
    return obj.hash();
}


/* Get the absolute value of a Python object.  Can throw if the object is not numeric. */
inline Object abs(Object<Ref::BORROW> obj) {
    return obj.abs();
}


/* Get a string representation of a Python object.  Equivalent to Python str(). */
inline String str(Object<Ref::BORROW> obj) {
    return obj.str();
}


/* Get a string representation of a Python object.  Equivalent to Python repr(). */
inline String repr(Object<Ref::BORROW> obj) {
    return obj.repr();
}


/* Get a string representation of a Python object with non-ASCII characters escaped.
Equivalent to Python ascii(). */
inline String ascii(Object<Ref::BORROW> obj) {
    return obj.ascii();
}


/* Convert an integer or integer-like object (one that implements __index__()) into a
binary string representation.  Equivalent to Python bin(). */
inline String bin(Object<Ref::BORROW> obj) {
    return obj.bin();
}


/* Convert an integer or integer-like object (one that implements __index__()) into an
octal string representation.  Equivalent to Python oct(). */
inline String oct(Object<Ref::BORROW> obj) {
    return obj.oct();
}


/* Convert an integer or integer-like object (one that implements __index__()) into a
hexadecimal string representation.  Equivalent to Python hext(). */
inline String hex(Object<Ref::BORROW> obj) {
    return obj.hex();
}


/* Convert a Python integer into a unicode character.  Can throw if the object is not
an integer, or if it is outside the range for a valid unicode character. */
inline String chr(Object<Ref::BORROW> obj) {
    return obj.chr();
}


/* Convert a C integer into a unicode character.  Can throw if the integer is outside
the range for a valid unicode character. */
inline String chr(long long val) {
    PyObject* string = PyUnicode_FromFormat("%llc", val);
    if (string == nullptr) {
        throw catch_python();
    }
    return {string};
}


/* Convert a unicode character into an integer.  Can throw if the argument is null or
not a string of length 1. */
long long ord(Object<Ref::BORROW> obj) {
    return obj.ord();
}


/* Divide a pair of python objects and return a 2-tuple containing the quotient and
remainder.  Can throw if either the quotient or remainder could not be computed. */
template <typename T>
inline Tuple divmod(Object<Ref::BORROW> obj, T&& divisor) {
    return obj.divmod(std::forward<T>(divisor));
}


/* Raise a python object to the given power. */
template <typename T>
inline Object pow(Object<Ref::BORROW> obj, T&& exponent) {
    return obj.pow(std::forward<T>(power));
}


/* Raise a python object to the given power, using an optional modulus. */
template <typename T, typename U>
inline Object pow(Object<Ref::BORROW> obj, T&& exponent, U&& modulus) {
    return obj.pow(std::forward<T>(exponent), std::forward<U>(modulus));
}


///////////////////////////////
////    PRIMITIVE TYPES    ////
///////////////////////////////


/* An extension of python::Object that represents a bytecode execution frame. */
template <Ref ref = Ref::STEAL>
class Frame : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Default constructor.  Initializes to the current execution frame. */
    Frame() : Base([&] {
        PyFrameObject* frame = PyEval_GetFrame();
        if (frame == nullptr) {
            throw RuntimeError("no frame is currently executing");
        }
        if constexpr (ref == Ref::STEAL) {
            return python::newref(frame);  // ensure net zero reference count
        } else {
            return frame;
        }
    }()) {}

    /* Construct a Python frame from an existing CPython frame. */
    explicit Frame(PyFrameObject* frame) : Base([&] {
        if (frame == nullptr) {
            throw TypeError("expected a frame");
        }
        return reinterpret_cast<PyObject*>(frame);
    }()) {}

    /* Construct a Python frame from an existing CPython frame. */
    explicit Frame(PyObject* obj) : Base([&] {
        if (obj == nullptr || !PyFrame_Check(obj)) {
            throw TypeError("expected a frame");
        }
        return obj;
    }()) {}

    /* Implicitly convert a python::Frame into a PyFrameObject* pointer.  Returns a
    borrowed reference. */
    inline operator PyFrameObject*() const noexcept {
        return reinterpret_cast<PyFrameObject*>(this->obj);
    }

    /////////////////////////////////
    ////    PyFrame_* METHODS    ////
    /////////////////////////////////

    /* Get the line number that the frame is currently executing. */
    inline int line_number() const noexcept {
        return PyFrame_GetLineNumber(static_cast<PyFrameObject*>(*this));
    }

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

        /* Get the next outer frame from this one. */
        inline Frame<Ref::STEAL> back() const {
            return {PyFrame_GetBack(static_cast<PyFrameObject*>(*this))};
        }

        /* Get the code object associated with this frame. */
        inline Code<Ref::STEAL> code() const {
            return {PyFrame_GetCode(static_cast<PyFrameObject*>(*this))};
        }

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

        /* Get the frame's builtin namespace. */
        inline Dict<Ref::STEAL> builtins() const {
            return {PyFrame_GetBuiltins(static_cast<PyFrameObject*>(*this))};
        }

        /* Get the frame's globals namespace. */
        inline Dict<Ref::STEAL> globals() const {
            return {PyFrame_GetGlobals(static_cast<PyFrameObject*>(*this))};
        }

        /* Get the frame's locals namespace. */
        inline Dict<Ref::STEAL> locals() const {
            return {PyFrame_GetLocals(static_cast<PyFrameObject*>(*this))};
        }

        /* Get the generator, coroutine, or async generator that owns this frame, or
        nullopt if this frame is not owned by a generator. */
        inline std::optional<Object<Ref::STEAL>> generator() const {
            PyObject* result = PyFrame_GetGenerator(static_cast<PyFrameObject*>(*this));
            if (result == nullptr) {
                return std::nullopt;
            } else {
                return std::make_optional(Object<Ref::STEAL>(result));
            }
        }

        /* Get the "precise instruction" of the frame object, which is an index into
        the bytecode of the last instruction executed by the frame's code object. */
        inline int last_instruction() const noexcept {
            return PyFrame_GetLasti(static_cast<PyFrameObject*>(*this));
        }

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object<Ref::STEAL> get(PyObject* name) const {
            PyObject* result = PyFrame_GetVar(static_cast<PyFrameObject*>(*this), name);
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};
        }

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object<Ref::STEAL> get(const char* name) const {
            PyObject* result = PyFrame_GetVarString(
                static_cast<PyFrameObject*>(*this),
                name
            );
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};
        }

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object<Ref::STEAL> get(const std::string& name) const {
            return get(name.c_str());
        }

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object<Ref::STEAL> get(const std::string_view& name) const {
            return get(name.data());
        }

    #endif

};


/* An extension of python::Object that represents a Python module. */
template <Ref ref = Ref::STEAL>
class Module : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Create a Python module from a PyModuleDef* struct. */
    explicit Module(PyModuleDef* def) : Base([&] {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a new module requires the use of Ref::STEAL to avoid "
            "memory leaks"
        );
        PyObject* result = PyModule_Create(def);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }()) {}

    /* Create a Python module from a PyModuleDef* struct with an optional required API
    version.  If the `api_version` argument does not match the version of the running
    interpreter, a RuntimeWarning will be emitted.  Users should prefer the standard
    PyModuleDef* constructor in almost all circumstances. */
    explicit Module(PyModuleDef* def, int api_version) : Base([&] {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a new module requires the use of Ref::STEAL to avoid "
            "memory leaks"
        );
        PyObject* result = PyModule_Create2(def);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }()) {}

    /* Adopt an existing CPython module or construct one with the given name. */
    explicit Module(PyObject* obj) : Base([&] {
        if (obj == nullptr) {
            throw TypeError("expected a module");
        }
        if (PyModule_Check(obj)) {
            return obj;
        }
        if (ref != Ref::STEAL) {
            throw TypeError(
                "Constructing a new module requires the use of Ref::STEAL to avoid "
                "memory leaks"
            );
        }
        PyObject* result = PyModule_NewObject(obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }()) {}

    /* Construct a Python module with the given name. */
    explicit Module(const char* name) : Base([&] {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a new module requires the use of Ref::STEAL to avoid "
            "memory leaks"
        );
        PyObject* result = PyModule_New(name);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }()) {}

    /* Construct a Python module with the given name. */
    explicit Module(const std::string& name) : Module(name.c_str()) {}

    /* Construct a Python module with the given name. */
    explicit Module(const std::string_view& name) : Module(name.data()) {}

    /* Implicitly convert a python::Module into the PyModuleDef* from which it was
    created.  Can return null if the module wasn't created from a definition. */
    inline operator PyModuleDef*() const noexcept {
        return PyModule_GetDef(this->obj);
    }

    //////////////////////////////////
    ////    PyModule_* METHODS    ////
    //////////////////////////////////

    /* Get the module's name as a C string. */
    inline const char* name() const noexcept {
        return PyModule_GetName(this->obj);
    }

    /* Get the module's namespace. */
    inline Dict<Ref::STEAL> dict() const {
        return {PyModule_GetDict(this->obj)};
    }

    /* Get the module's filename as a C string. */
    inline const char* filename() const noexcept {
        return PyModule_GetFilename(this->obj);
    }

};


/* An extension of python::Object that represents a Python type object. */
template <Ref ref = Ref::STEAL>
class Type : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Default constructor.  Initializes to the base PyType_Type class. */
    Type() : Base([&] {
        if constexpr (ref == Ref::STEAL) {
            return python::newref(reinterpret_cast<PyObject*>(&PyType_Type));
        } else {
            return reinterpret_cast<PyObject*>(&PyType_Type);
        }
    }()) {}

    /* Construct a Python type from an existing CPython type. */
    explicit Type(PyTypeObject* type) : Base([&] {
        if (type == nullptr) {
            throw TypeError("expected a type");
        }
        return reinterpret_cast<PyObject*>(type);
    }()) {}

    /* Get the type of an arbitrary Python object. */
    explicit Type(PyObject* obj) : Base([&] {
        if (obj == nullptr) {
            throw TypeError("expected a type");
        }
        if (PyType_Check(obj)) {
            return obj;
        }
        // detect type of object
        if constexpr (ref == Ref::STEAL) {
            return python::newref(Py_TYPE(obj));  // ensure net zero reference count
        } else {
            return Py_TYPE(obj);
        }
    }()) {}

    /* Create a new dynamic type by calling the built-in type() metaclass. */
    Type(PyObject* name, PyObject* bases, PyObject* dict) : Base([&] {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a new dynamic Type requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
        PyObject* result = PyObject_CallFunctionObjArgs(
            reinterpret_cast<PyObject*>(&PyType_Type), name, bases, dict, nullptr
        );
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }()) {}

    /* Create a new heap type from a CPython PyType_Spec.  Note that this is not
    exactly interchangeable with a standard call to the type metaclass directly.
    Notably, it does not invoke any of the __init__(), __new__(), __init_subclass__(),
    or __set_name__() methods for the type or any of its bases. */
    Type(PyType_Spec* spec) : Base([&] {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a new heap Type requires the use of Ref::STEAL to avoid "
            "memory leaks"
        );
        PyObject* result = PyType_FromSpec(spec);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }()) {}

    /* Create a new heap type from a CPython PyType_Spec and bases.  See PyType_Spec*
    overload for more information. */
    Type(PyType_Spec* spec, PyObject* bases) : Base([&] {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a new heap Type requires the use of Ref::STEAL to avoid "
            "memory leaks"
        );
        PyObject* result = PyType_FromSpecWithBases(spec, bases);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;

    }()) {}

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

        /* Create a new heap type from a module name, CPython PyType_Spec, and bases.
        See PyType_Spec* overload for more information. */
        Type(PyObject* module, PyType_Spec* spec, PyObject* bases) : Base([&] {
            static_assert(
                ref == Ref::STEAL,
                "Constructing a new heap Type requires the use of Ref::STEAL to avoid "
                "memory leaks"
            );
            PyObject* result = PyType_FromModuleAndSpec(module, spec, bases);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;
        }()) {}

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Create a new heap type from a full CPython metaclass, module name,
        PyType_Spec and bases.  See PyType_Spec* overload for more information. */
        Type(PyTypeObject* metaclass, PyObject* module, PyType_Spec* spec, PyObject* bases) : Base([&] {
            static_assert(
                ref == Ref::STEAL,
                "Constructing a new heap Type requires the use of Ref::STEAL to avoid "
                "memory leaks"
            );
            PyObject* result = PyType_FromMetaClass(metaclass, module, spec, bases);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;
        }()) {}

    #endif

    /* Implicitly convert a python::Type into a PyTypeObject* pointer.  Returns a
    borrowed reference. */
    inline operator PyTypeObject*() const noexcept {
        return reinterpret_cast<PyTypeObject*>(this->obj);
    }

    ////////////////////////////////
    ////    PyType_* METHODS    ////
    ////////////////////////////////

    /* Finalize a type object, filling in any inherited slots.  This should be called
    on all type objects to finish their initialization. */
    inline static void ready(PyTypeObject* type) {
        if (PyType_Ready(type) < 0) {
            throw catch_python();
        }
    }

    /* Clear the lookup cache for the type and all of its subtypes.  This method must
    be called after any manual modification of the attributes or base classes of the
    type. */
    inline void clear_cache() const noexcept {
        PyType_Modified(static_cast<PyTypeObject*>(*this));
    }

    /* Check whether this object is an actual subtype of a specified type.  This avoids
    calling __subclasscheck__() on the parent type. */
    inline bool is_subtype(PyTypeObject* base) const noexcept {
        return PyType_IsSubtype(static_cast<PyTypeObject*>(*this), base);
    }

    /* Check whether a particular flag is set on the type. */
    inline bool has_flag(int flag) const noexcept {
        return PyType_HasFeature(static_cast<PyTypeObject*>(*this), flag);
    }

    /* Get the function pointer stored in a given slot.  The result may be null if the
    type does not implement the requested slot, and users must cast the result to a
    pointer of the appropriate type. */
    inline void* slot(int id) const noexcept {
        return PyType_GetSlot(static_cast<PyTypeObject*>(*this), id);
    }

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

        /* Get the module that the type is defined in.  Can throw if called on a static
        type rather than a heap type (one that was created using PyType_FromModuleAndSpec()
        or higher). */
        inline Module<Ref::STEAL> get_module() const noexcept {
            PyObject* result = PyType_GetModule(static_cast<PyTypeObject*>(*this));
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};
        }

    #endif

};


/* An extension of python::Object that represents a Python function. */
template <Ref ref = Ref::STEAL>
class Function : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Python function from an existing CPython function. */
    explicit Function(PyFunctionObject* func) : Base([&] {
        if (func == nullptr) {
            throw TypeError("expected a function");
        }
        return reinterpret_cast<PyObject*>(func);
    }()) {}

    /* Construct a Python function from an existing CPython function. */
    explicit Function(PyObject* obj) : Base([&] {
        if (obj == nullptr || !PyFunction_Check(obj)) {
            throw TypeError("expected a function");
        }
        return obj;
    }()) {}

    /* Create a new Python function from a code object and a globals dictionary. */
    Function(PyCodeObject* code, PyObject* globals) : Base([&] {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a new Function requires the use of Ref::STEAL to avoid "
            "memory leaks"
        );
        PyObject* result = PyFunction_New(code, globals);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }()) {}

    /* Create a new Python function from a code object, globals dictionary, and a
    qualified name. */
    Function(PyCodeObject* code, PyObject* globals, PyObject* qualname) : Base([&] {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a new Function requires the use of Ref::STEAL to avoid "
            "memory leaks"
        );
        PyObject* result = PyFunction_NewWithQualName(code, globals, qualname);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }()) {}

    /* Implicitly convert a python::List into a PyFunctionObject* pointer. */
    inline operator PyFunctionObject*() const noexcept {
        return reinterpret_cast<PyFunctionObject*>(this->obj);
    }

    ////////////////////////////////////
    ////    PyFunction_* METHODS    ////
    ////////////////////////////////////

    /* Get the name of the file from which the code was compiled. */
    inline std::string file_name() const {
        return code().file_name();
    }

    /* Get the module that the function is defined in. */
    inline std::optional<Module<Ref::BORROW>> get_module() const {
        PyObject* mod = PyFunction_GetModule(this->obj);
        if (mod == nullptr) {
            return std::nullopt;
        } else {
            return std::make_optional(Module<Ref::BORROW>(module));
        }
    }

    /* Get the first line number of the function. */
    inline size_t line_number() const noexcept {
        return code().line_number();
    }

    /* Get the function's base name. */
    inline std::string name() const {
        return code().name();
    }

    /* Get the function's qualified name. */
    inline std::string qualname() const {
        return code().qualname();
    }

    /* Get the function's code object. */
    inline Code<Ref::BORROW> code() const noexcept {
        return Code<Ref::BORROW>(PyFunction_GetCode(this->obj));
    }

    /* Get the closure associated with the function.  This is a tuple of cell objects
    containing data captured by the function. */
    inline Tuple<Ref::BORROW> closure() const noexcept {
        PyObject* closure = PyFunction_GetClosure(this->obj);
        if (closure == nullptr) {
            return {};  // TODO: returning a default-constructed Tuple with Ref::BORROW is not allowed.
        } else {
            return {closure};
        }
    }

    /* Set the closure associated with the function.  Input must be Py_None or a
    tuple. */
    inline void closure(PyObject* closure) {
        if (PyFunction_SetClosure(this->obj, closure)) {
            throw catch_python();
        }
    }

    /* Get the globals dictionary associated with the function object. */
    inline Dict<Ref::BORROW> globals() const noexcept {
        return Dict<Ref::BORROW>(PyFunction_GetGlobals(this->obj));
    }

    /* Get the required stack space for the code object. */
    inline size_t stack_size() const noexcept {
        return code().stack_size();
    }

    /* Get the total number of positional arguments for the function, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const noexcept {
        return code().n_args();
    }

    /* Get the number of local variables used by the function (including all
    parameters). */
    inline size_t n_locals() const noexcept {
        return code().n_locals();
    }

    /* Get the number of positional-only arguments for the function, including those
    with default values.  Does not include variable positional or keyword arguments. */
    inline size_t positional_only() const noexcept {
        return code().positional_only();
    }

    /* Get the number of keyword-only arguments for the function, including those with
    default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t keyword_only() const noexcept {
        return code().keyword_only();
    }

    /* Get the default values for the function's arguments. */
    inline Tuple<Ref::BORROW> defaults() const noexcept {
        PyObject* defaults = PyFunction_GetDefaults(this->obj);
        if (defaults == nullptr) {
            return {};  // TODO: returning a default-constructed Tuple with Ref::BORROW is not allowed.
        } else {
            return {defaults};
        }
    }

    /* Set the default values for the function's arguments.  Input must be Py_None or
    a tuple. */
    inline void defaults(PyObject* defaults) {
        if (PyFunction_SetDefaults(this->obj, defaults)) {
            throw catch_python();
        }
    }

    /* Get the annotations for the function object.  This is a mutable dictionary or
    nullopt if no annotations are present. */
    inline Dict<Ref::BORROW> annotations() const noexcept {
        PyObject* annotations = PyFunction_GetAnnotations(this->obj);
        if (annotations == nullptr) {
            return {};  // TODO: returning a default-constructed Dict with Ref::BORROW is not allowed.
        } else {
            return {annotations};
        }
    }

    /* Set the annotations for the function object.  Input must be Py_None or a
    dictionary. */
    inline void annotations(PyObject* annotations) {
        if (PyFunction_SetAnnotations(this->obj, annotations)) {
            throw catch_python();
        }
    }

};


/* An extension of python::Object that represents a bound Python method. */
template <Ref ref = Ref::STEAL>
class Method : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Python method from an existing CPython method. */
    explicit Method(PyObject* obj) : Base(obj) {
        if (!PyMethod_Check(obj)) {
            throw TypeError("expected a method");
        }
    }

    /* Construct a Python method by binding an object to a function. */
    Method(PyObject* function, PyObject* self) :
        Base([&] {
            static_assert(
                ref == Ref::STEAL,
                "Constructing a new Method requires the use of Ref::STEAL to avoid "
                "memory leaks"
            );
            if (self == nullptr) {
                throw TypeError("expected a self object");
            }
            PyObject* result = PyMethod_New(function, self);
            if (result == nullptr) {
                throw catch_python();
            }
            return result;
        }())
    {}

    //////////////////////////////////
    ////    PyMethod_* METHODS    ////
    //////////////////////////////////

    /* Get the instance to which the method is bound. */
    inline Object<Ref::BORROW> self() const noexcept {
        return Object<Ref::BORROW>(PyMethod_GET_SELF(this->obj));
    }

    /* Get the function object associated with the method. */
    inline Function<Ref::BORROW> function() const noexcept {
        return Function<Ref::BORROW>(PyMethod_GET_FUNCTION(this->obj));
    }

    /* Get the code object wrapped by this method. */
    inline Code<Ref::BORROW> code() const noexcept {
        return function().code();
    }

    /* Get the method's base name. */
    inline std::string name() const {
        return function().name();
    }

    /* Get the method's qualified name. */
    inline std::string qualname() const {
        return function().qualname();
    }

    /* Get the total number of positional arguments for the method, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const noexcept {
        return function().n_args();
    }

    /* Get the number of positional-only arguments for the method, including those
    with default values.  Does not include variable positional or keyword arguments. */
    inline size_t positional_only() const noexcept {
        return function().positional_only();
    }

    /* Get the number of keyword-only arguments for the method, including those with
    default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t keyword_only() const noexcept {
        return function().keyword_only();
    }

    /* Get the number of local variables used by the method (including all
    parameters). */
    inline size_t n_locals() const noexcept {
        return function().n_locals();
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string file_name() const {
        return function().file_name();
    }

    /* Get the first line number of the method. */
    inline size_t line_number() const noexcept {
        return function().line_number();
    }

    /* Get the required stack space for the code object. */
    inline size_t stack_size() const noexcept {
        return function().stack_size();
    }

    /* Get the globals dictionary associated with the method object. */
    inline Dict<Ref::BORROW> globals() const noexcept {
        return function().globals();
    }

    /* Get the module that the method is defined in. */
    inline std::optional<Object<Ref::BORROW>> module() const noexcept {
        return function().module();
    }

    /* Get the default values for the method's arguments. */
    inline std::optional<Tuple<Ref::BORROW>> defaults() const noexcept {
        return function().defaults();
    }

    /* Set the default values for the method's arguments.  Input must be Py_None or a
    tuple. */
    inline void defaults(PyObject* defaults) {
        function().defaults(defaults);
    }

    /* Get the closure associated with the method.  This is a tuple of cell objects
    containing data captured by the method. */
    inline std::optional<Tuple<Ref::BORROW>> closure() const noexcept {
        return function().closure();
    }

    /* Set the closure associated with the method.  Input must be Py_None or a tuple. */
    inline void closure(PyObject* closure) {
        function().closure(closure);
    }

    /* Get the annotations for the method object.  This is a mutable dictionary or
    nullopt if no annotations are present. */
    inline std::optional<Dict<Ref::BORROW>> annotations() const noexcept {
        return function().annotations();
    }

    /* Set the annotations for the method object.  Input must be Py_None or a
    dictionary. */
    inline void annotations(PyObject* annotations) {
        function().annotations(annotations);
    }

};


// TODO: PyInstanceMethod_Type


/* An extension of python::Object that represents a Python class method. */
template <Ref ref = Ref::STEAL>
class ClassMethod : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python class method from an existing CPython class method. */
    ClassMethod(PyObject* obj) : Base(obj) {
        if (!PyInstanceMethod_Check(obj)) {
            throw TypeError("expected a class method");
        }
    }

    /* Copy constructor. */
    ClassMethod(const ClassMethod& other) : Base(other) {}

    /* Move constructor. */
    ClassMethod(ClassMethod&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    ClassMethod& operator=(const ClassMethod& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    ClassMethod& operator=(ClassMethod&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    //////////////////////////////////////////
    ////    PyInstanceMethod_* METHODS    ////
    //////////////////////////////////////////

    /* Get the function object associated with the class method. */
    inline Function<Ref::BORROW> function() const noexcept {
        return Function<Ref::BORROW>(PyInstanceMethod_GET_FUNCTION(this->obj));
    }

    /* Get the code object wrapped by this class method. */
    inline Code<Ref::BORROW> code() const noexcept {
        return function().code();
    }

    /* Get the class method's base name. */
    inline std::string name() const {
        return function().name();
    }

    /* Get the class method's qualified name. */
    inline std::string qualname() const {
        return function().qualname();
    }

    /* Get the total number of positional arguments for the class method, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const noexcept {
        return function().n_args();
    }

    /* Get the number of positional-only arguments for the class method, including
    those with default values.  Does not include variable positional or keyword
    arguments. */
    inline size_t positional_only() const noexcept {
        return function().positional_only();
    }

    /* Get the number of keyword-only arguments for the class method, including those
    with default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t keyword_only() const noexcept {
        return function().keyword_only();
    }

    /* Get the number of local variables used by the class method (including all
    parameters). */
    inline size_t n_locals() const noexcept {
        return function().n_locals();
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string file_name() const {
        return function().file_name();
    }

    /* Get the first line number of the class method. */
    inline size_t line_number() const noexcept {
        return function().line_number();
    }

    /* Get the required stack space for the code object. */
    inline size_t stack_size() const noexcept {
        return function().stack_size();
    }

    /* Get the globals dictionary associated with the class method object. */
    inline Dict<Ref::BORROW> globals() const noexcept {
        return function().globals();
    }

    /* Get the module that the class method is defined in. */
    inline std::optional<Object<Ref::BORROW>> module() const noexcept {
        return function().module();
    }

    /* Get the default values for the class method's arguments. */
    inline std::optional<Tuple<Ref::BORROW>> defaults() const noexcept {
        return function().defaults();
    }

    /* Set the default values for the class method's arguments.  Input must be Py_None
    or a tuple. */
    inline void defaults(PyObject* defaults) {
        function().defaults(defaults);
    }

    /* Get the closure associated with the class method.  This is a tuple of cell
    objects containing data captured by the class method. */
    inline std::optional<Tuple<Ref::BORROW>> closure() const noexcept {
        return function().closure();
    }

    /* Set the closure associated with the class method.  Input must be Py_None or a
    tuple. */
    inline void closure(PyObject* closure) {
        function().closure(closure);
    }

    /* Get the annotations for the class method object.  This is a mutable dictionary
    or nullopt if no annotations are present. */
    inline std::optional<Dict<Ref::BORROW>> annotations() const noexcept {
        return function().annotations();
    }

    /* Set the annotations for the class method object.  Input must be Py_None or a
    dictionary. */
    inline void annotations(PyObject* annotations) {
        function().annotations(annotations);
    }

};


/* Get the current frame's builtin namespace as a reference-counted dictionary.  Can
throw if no frame is currently executing. */
inline Dict<Ref::NEW> builtins() {
    PyObject* result = PyEval_GetBuiltins();
    if (result == nullptr) {
        throw catch_python();
    }
    return Dict<Ref::NEW>(result);
}


/* Get the current frame's global namespace as a reference-counted dictionary.  Can
throw if no frame is currently executing. */
inline Dict<Ref::NEW> globals() {
    PyObject* result = PyEval_GetGlobals();
    if (result == nullptr) {
        throw catch_python();
    }
    return Dict<Ref::NEW>(result);
}


/* Get the current frame's local namespace as a reference-counted dictionary.  Can
throw if no frame is currently executing. */
inline Dict<Ref::NEW> locals() {
    PyObject* result = PyEval_GetLocals();
    if (result == nullptr) {
        throw catch_python();
    }
    return Dict<Ref::NEW>(result);
}


/* Return the name of `func` if it is a callable function, class or instance object.
Otherwise, return type(func).__name__. */
inline std::string func_name(PyObject* func) {
    if (func == nullptr) {
        throw TypeError("func_name() argument must not be null");
    }
    return std::string(PyEval_GetFuncName(func));
}


/* Return a string describing the kind of function that was passed in.  Return values
include "()" for functions and methods, " constructor", " instance", and " object".
When concatenated with func_name(), the result will be a description of `func`. */
inline std::string func_kind(PyObject* func) {
    if (func == nullptr) {
        throw TypeError("func_kind() argument must not be null");
    }
    return std::string(PyEval_GetFuncDesc(func));
}


//////////////////////////
////    EVALUATION    ////
//////////////////////////


/* Code evaluation using the C++ API is very confusing.  The following functions
 * attempt to replicate the behavior of Python's built-in compile(), exec(), and eval()
 * functions, but with a more C++-friendly interface.
 *
 * NOTE: these functions should not be used on unfiltered user input, as they trigger
 * the execution of arbitrary Python code.  This can lead to security vulnerabilities
 * if not handled properly.
 */


/* An extension of python::Object that represents a Python code object. */
template <Ref ref = Ref::STEAL>
class Code : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Python code object from an existing CPython code object. */
    Code(PyCodeObject* obj) : Base(reinterpret_cast<PyObject*>(obj)) {}

    /* Construct a Python code object from an arbitrary Python object. */
    Code(PyObject* obj) : Base(obj) {
        if (!PyCode_Check(obj)) {
            throw TypeError("expected a code object");
        }
    }

    /* Parse and compile a source string into a Python code object.  The filename is
    used in to construct the code object and may appear in tracebacks or exception
    messages.  The mode is used to constrain the code wich can be compiled, and must be
    one of `Py_eval_input`, `Py_file_input`, or `Py_single_input` for multiline
    strings, file contents, and single-line, REPL-style statements respectively. */
    Code(
        const char* source,
        const char* filename = nullptr,
        int mode = Py_eval_input
    ) : Base()
    {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Code object requires the use of Ref::STEAL to avoid memory "
            "leaks"
        );

        if (filename == nullptr) {
            filename = "<anonymous file>";
        }
        if (mode != Py_file_input && mode != Py_eval_input && mode != Py_single_input) {
            std::ostringstream msg;
            msg << "invalid compilation mode: " << mode << " <- must be one of ";
            msg << "Py_file_input, Py_eval_input, or Py_single_input";
            throw ValueError(msg.str());
        }

        obj = Py_CompileString(source, filename, mode);
        if (obj == nullptr) {
            throw catch_python();
        }
    }

    /* See const char* overload. */
    Code(
        const char* source,
        const std::string& filename,
        int mode = Py_eval_input
    ) : Code(source, filename.c_str(), mode)
    {}

    /* See const char* overload. */
    Code(
        const char* source,
        const std::string_view& filename,
        int mode = Py_eval_input
    ) : Code(source, filename.data(), mode)
    {}

    /* See const char* overload. */
    Code(
        const std::string& source,
        const char* filename = nullptr,
        int mode = Py_eval_input
    ) : Code(source.c_str(), filename, mode)
    {}

    /* See const char* overload. */
    Code(
        const std::string& source,
        const std::string& filename,
        int mode = Py_eval_input
    ) : Code(source.c_str(), filename.c_str(), mode)
    {}

    /* See const char* overload. */
    Code(
        const std::string_view& source,
        const char* filename = nullptr,
        int mode = Py_eval_input
    ) : Code(source.data(), filename, mode)
    {}

    /* See const char* overload. */
    Code(
        const std::string_view& source,
        const std::string& filename,
        int mode = Py_eval_input
    ) : Code(source.data(), filename.c_str(), mode)
    {}

    /* See const char* overload. */
    Code(
        const std::string_view& source,
        const std::string_view& filename,
        int mode = Py_eval_input
    ) : Code(source.data(), filename.data(), mode)
    {}

    /* Implicitly convert a python::List into a PyCodeObject* pointer. */
    inline operator PyCodeObject*() const noexcept {
        return reinterpret_cast<PyCodeObject*>(this->obj);
    }

    ////////////////////////////////
    ////    PyCode_* METHODS    ////
    ////////////////////////////////

    /* Execute the code object with the given local and global variables. */
    inline PyObject* operator()(PyObject* globals, PyObject* locals) const {
        PyObject* result = PyEval_EvalCode(this->obj, globals, locals);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }

    /* Get the function's base name. */
    inline std::string name() const {
        String<Ref::BORROW> name(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_name
        );
        return name.str();
    }

    /* Get the function's qualified name. */
    inline std::string qualname() const {
        String<Ref::BORROW> qualname(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_qualname
        );
        return qualname.str();
    }

    /* Get the total number of positional arguments for the function, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_argcount
        );
    }

    /* Get the number of positional-only arguments for the function, including those
    with default values.  Does not include variable positional or keyword arguments. */
    inline size_t positional_only() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_posonlyargcount
        );
    }

    /* Get the number of keyword-only arguments for the function, including those with
    default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t keyword_only() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_kwonlyargcount
        );
    }

    /* Get the number of local variables used by the function (including all
    parameters). */
    inline size_t n_locals() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_nlocals
        );
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string file_name() const {
        Object<Ref::BORROW> filename(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_filename
        );
        Py_ssize_t size;
        const char* result = PyUnicode_AsUTF8AndSize(filename, &size);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result, static_cast<size_t>(size)};
    }

    /* Get the first line number of the function. */
    inline size_t line_number() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_firstlineno
        );
    }

    /* Get the required stack space for the code object. */
    inline size_t stack_size() const noexcept {
        return static_cast<size_t>(
            reinterpret_cast<PyCodeObject*>(this->obj)->co_stacksize
        );
    }

};


/* Parse and compile a source string into a Python code object.  The filename is used
in to construct the code object and may appear in tracebacks or exception messages.
The mode is used to constrain the code wich can be compiled, and must be one of
`Py_eval_input`, `Py_file_input`, or `Py_single_input` for multiline strings, file
contents, and single-line, REPL-style statements respectively. */
template <typename... Args>
inline Code compile(Args&&... args) {
    return {std::forward<Args>(args)...};
}


/* Execute a pre-compiled Python code object. */
inline PyObject* exec(PyObject* code, PyObject* globals, PyObject* locals) {
    PyObject* result = PyEval_EvalCode(code, globals, locals);
    if (result == nullptr) {
        throw catch_python();
    }
    return result;
}


/* Execute an interpreter frame using its associated context.  The code object within
the frame will be executed, interpreting bytecode and executing calls as needed until
it reaches the end of its code path. */
inline PyObject* exec(PyFrameObject* frame) {
    PyObject* result = PyEval_EvalFrame(frame);
    if (result == nullptr) {
        throw catch_python();
    }
    return result;
}


/* Launch a subinterpreter to execute a python script stored in a .py file. */
void run(const char* filename) {
    // NOTE: Python recommends that on windows platforms, we open the file in binary
    // mode to avoid issues with the newline character.
    #if defined(_WIN32) || defined(_WIN64)
        std::FILE* file = _wfopen(filename.c_str(), "rb");
    #else
        std::FILE* file = std::fopen(filename.c_str(), "r");
    #endif

    if (file == nullptr) {
        std::ostringstream msg;
        msg << "could not open file '" << filename << "'";
        throw FileNotFoundError(msg.str());
    }

    // NOTE: PyRun_SimpleFileEx() launches an interpreter, executes the file, and then
    // closes the file connection automatically.  It returns 0 on success and -1 on
    // failure, with no way of recovering the original error message if one is raised.
    if (PyRun_SimpleFileEx(file, filename.c_str(), 1)) {
        std::ostringstream msg;
        msg << "error occurred while running file '" << filename << "'";
        throw RuntimeError(msg.str());
    }
}


/* Launch a subinterpreter to execute a python script stored in a .py file. */
inline void run(const std::string& filename) {
    run(filename.c_str());
}


/* Launch a subinterpreter to execute a python script stored in a .py file. */
inline void run(const std::string_view& filename) {
    run(filename.data());
}


/* Evaluate an arbitrary Python statement encoded as a string. */
inline PyObject* eval(const char* statement, PyObject* globals, PyObject* locals) {
    PyObject* result = PyRun_String(statement, Py_eval_input, globals, locals);
    if (result == nullptr) {
        throw catch_python();
    }
    return result;
}


/* Evaluate an arbitrary Python statement encoded as a string. */
inline PyObject* eval(
    const std::string& statement,
    PyObject* globals,
    PyObject* locals
) {
    return eval(statement.c_str(), globals, locals);
}


/* Evaluate an arbitrary Python statement encoded as a string. */
inline PyObject* eval(
    const std::string_view& statement,
    PyObject* globals,
    PyObject* locals
) {
    return eval(statement.data(), globals, locals);
}


///////////////////////
////    NUMBERS    ////
///////////////////////


/* An extension of python::Object that represents a Python boolean. */
template <Ref ref = Ref::STEAL>
class Bool : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Python boolean from an existing CPython boolean. */
    explicit Bool(PyObject* obj) : Base(obj) {
        if (!PyBool_Check(obj)) {
            throw TypeError("expected a boolean");
        }
    }

    /* Construct a Python boolean from a C++ boolean. */
    Bool(long value) : Base(PyBool_FromLong(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Bool from a long int requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Get the Python boolean type. */
    static Type<Ref::NEW> type() noexcept {
        return Type<Ref::NEW>(&PyBool_Type);
    }

    inline operator bool() const noexcept {
        return PyLong_AsLong(this->obj);
    }

};


/* An extension of python::Object that represents a Python integer. */
template <Ref ref = Ref::STEAL>
class Int : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a Python integer from an existing CPython integer. */
    explicit Int(PyObject* obj) : Base(obj) {
        if (!PyLong_Check(obj)) {
            throw TypeError("expected an integer");
        }
    }

    /* Construct a Python integer from a C long. */
    Int(long value) : Base(PyLong_FromLong(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a long requires the use of Ref::STEAL to avoid "
            "memory leaks"
        );
    }

    /* Construct a Python integer from a C long long. */
    Int(long long value) : Base(PyLong_FromLongLong(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a long long requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C unsigned long. */
    Int(unsigned long value) : Base(PyLong_FromUnsignedLong(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from an unsigned long requires the use of Ref::STEAL "
            "to avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C unsigned long long. */
    Int(unsigned long long value) : Base(PyLong_FromUnsignedLongLong(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from an unsigned long long requires the use of "
            "Ref::STEAL to avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C double. */
    Int(double value) : Base(PyLong_FromDouble(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a double requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C string. */
    Int(const char* value, int base) :
        Base(PyLong_FromString(value, nullptr, base))
    {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a string requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C++ string. */
    Int(const std::string& value, int base) :
        Base(PyLong_FromString(value.c_str(), nullptr, base))
    {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a string requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python integer from a C++ string view. */
    Int(const std::string_view& value, int base) :
        Base(PyLong_FromString(value.data(), nullptr, base))
    {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a string view requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python integer from a PyUnicode string. */
    Int(PyObject* value, int base) : Base(PyLong_FromUnicodeObject(value, base)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing an Int from a PyUnicode object requires the use of "
            "Ref::STEAL to avoid memory leaks"
        );
    }

    /* Implicitly convert a python::List into a PyLongObject* pointer. */
    inline operator PyLongObject*() const noexcept {
        return reinterpret_cast<PyLongObject*>(this->obj);
    }

    /* Implicitly convert a python::Int into a C long. */
    inline operator long() const {
        long value = PyLong_AsLong(this->obj);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

    /* Implicitly convert a python::Int into a C long long. */
    inline operator long long() const {
        long long value = PyLong_AsLongLong(this->obj);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

    /* Implicitly convert a python::Int into a C unsigned long. */
    inline operator unsigned long() const {
        unsigned long value = PyLong_AsUnsignedLong(this->obj);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

    /* Implicitly convert a python::Int into a C unsigned long long. */
    inline operator unsigned long long() const {
        unsigned long long value = PyLong_AsUnsignedLongLong(this->obj);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return value;
    }

};


/* An extension of python::Object that represents a Python float. */
template <Ref ref = Ref::STEAL>
class Float : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python float from an existing CPython float. */
    Float(PyObject* obj) : Base(obj) {
        if (!PyFloat_Check(obj)) {
            throw TypeError("expected a float");
        }
    }

    /* Construct a Python float from a C double. */
    Float(double value) : Base(PyFloat_FromDouble(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Float from a double requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python float from a C++ string. */
    Float(const std::string& value) : Base([&value] {
        PyObject* string = PyUnicode_FromStringAndSize(value.c_str(), value.size());
        if (string == nullptr) {
            throw catch_python();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        return result;
    }()) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Float from a string requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python float from a C++ string view. */
    Float(const std::string_view& value) : Base([&value] {
        PyObject* string = PyUnicode_FromStringAndSize(value.data(), value.size());
        if (string == nullptr) {
            throw catch_python();
        }
        PyObject* result = PyFloat_FromString(string);
        Py_DECREF(string);
        return result;
    }()) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Float from a string view requires the use of Ref::STEAL "
            "to avoid memory leaks"
        );
    }

    /* Copy constructor. */
    Float(const Float& other) : Base(other) {}

    /* Move constructor. */
    Float(Float&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Float& operator=(const Float& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Float& operator=(Float&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a python::List into a PyFloatObject* pointer. */
    inline operator PyFloatObject*() const noexcept {
        return reinterpret_cast<PyFloatObject*>(this->obj);
    }

    /* Implicitly convert a python::Float into a C double. */
    inline operator double() const {
        return PyFloat_AS_DOUBLE(this->obj);
    }

};


/* An extension of python::Object that represents a complex number in Python. */
template <Ref ref = Ref::STEAL>
class Complex : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python complex number from an existing CPython complex number. */
    Complex(PyObject* obj) : Base(obj) {
        if (!PyComplex_Check(obj)) {
            throw TypeError("expected a complex number");
        }
    }

    /* Construct a Python complex number from separate real/imaginary components as
    doubles. */
    Complex(double real, double imag) : Base(PyComplex_FromDoubles(real, imag)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a Complex from a double requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Copy constructor. */
    Complex(const Complex& other) : Base(other) {}

    /* Move constructor. */
    Complex(Complex&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Complex& operator=(const Complex& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Complex& operator=(Complex&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a python::List into a PyComplexObject* pointer. */
    inline operator PyComplexObject*() const noexcept {
        return reinterpret_cast<PyComplexObject*>(this->obj);
    }

    /* Get the real component of the complex number as a C double. */
    inline double real() const {
        return PyComplex_RealAsDouble(this->obj);
    }

    /* Get the imaginary component of the complex number as a C double. */
    inline double imag() const {
        return PyComplex_ImagAsDouble(this->obj);
    }

    /* Implicitly convert a python::Complex into a C double representing the real
    component. */
    inline operator double() const {
        return real();
    }

};


/* An extension of python::Object that represents a Python slice. */
template <Ref ref = Ref::STEAL>
class Slice : public Object<ref> {
    using Base = Object<ref>;

    PyObject* _start;
    PyObject* _stop;
    PyObject* _step;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty Python slice. */
    Slice() :
        Base(PySlice_New(nullptr, nullptr, nullptr)), _start(Py_NewRef(Py_None)),
        _stop(Py_NewRef(Py_None)), _step(Py_NewRef(Py_None))
    {
        static_assert(
            ref == Ref::NEW,
            "Constructing an empty Slice requires the use of Ref::NEW to avoid "
            "memory leaks"
        );
    }

    /* Construct a python::Slice around an existing CPython slice. */
    Slice(PyObject* obj) : Base(obj) {
        if (!PySlice_Check(obj)) {
            throw TypeError("expected a slice");
        }

        _start = PyObject_GetAttrString(obj, "start");
        if (_start == nullptr) {
            throw catch_python();
        }
        _stop = PyObject_GetAttrString(obj, "stop");
        if (_stop == nullptr) {
            Py_DECREF(_start);
            throw catch_python();
        }
        _step = PyObject_GetAttrString(obj, "step");
        if (_step == nullptr) {
            Py_DECREF(_start);
            Py_DECREF(_stop);
            throw catch_python();
        }
    }

    /* Copy constructor. */
    Slice(const Slice& other) :
        Base(other), _start(other._start), _stop(other._stop), _step(other._step)
    {
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XINCREF(other._start);
            Py_XINCREF(other._stop);
            Py_XINCREF(other._step);
        }
    }

    /* Move constructor. */
    Slice(Slice&& other) :
        Base(std::move(other)), _start(other._start), _stop(other._stop),
        _step(other._step)
     {
        other._start = nullptr;
        other._stop = nullptr;
        other._step = nullptr;
    }

    /* Copy assignment. */
    Slice& operator=(const Slice& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XDECREF(_start);
            Py_XDECREF(_stop);
            Py_XDECREF(_step);
        }
        _start = other._start;
        _stop = other._stop;
        _step = other._step;
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XINCREF(_start);
            Py_XINCREF(_stop);
            Py_XINCREF(_step);
        }
        return *this;
    }

    /* Move assignment. */
    Slice& operator=(Slice&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XDECREF(_start);
            Py_XDECREF(_stop);
            Py_XDECREF(_step);
        }
        _start = other._start;
        _stop = other._stop;
        _step = other._step;
        other._start = nullptr;
        other._stop = nullptr;
        other._step = nullptr;
        return *this;
    }

    /* Release the Python slice on destruction. */
    ~Slice() {
        if constexpr (ref == Ref::NEW || ref == Ref::STEAL) {
            Py_XDECREF(_start);
            Py_XDECREF(_stop);
            Py_XDECREF(_step);
        }
    }

    /////////////////////////////////
    ////    PySlice_* METHODS    ////
    /////////////////////////////////

    /* Get the start index of the slice. */
    inline Object<Ref::BORROW> start() const {
        return Object<Ref::BORROW>(_start);
    }

    /* Get the stop index of the slice. */
    inline Object<Ref::BORROW> stop() const {
        return Object<Ref::BORROW>(_stop);
    }

    /* Get the step index of the slice. */
    inline Object<Ref::BORROW> step() const {
        return Object<Ref::BORROW>(_step);
    }

    /* Normalize the slice for a given sequence length, returning a 4-tuple containing
    the start, stop, step, and number of elements included in the slice. */
    inline auto normalize(Py_ssize_t length) const
        -> std::tuple<Py_ssize_t, Py_ssize_t, Py_ssize_t, size_t>
    {
        Py_ssize_t nstart, nstop, nstep, nlength;
        if (PySlice_GetIndicesEx(this->obj, length, &nstart, &nstop, &nstep, &nlength)) {
            throw catch_python();
        }
        return std::make_tuple(nstart, nstop, nstep, nlength);
    }

};


//////////////////////////
////    CONTAINERS    ////
//////////////////////////


/* An extension of python::Object that represents a Python tuple. */
template <Ref ref = Ref::STEAL>
class Tuple : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty Python tuple with the specified size. */
    Tuple(Py_ssize_t size) : Base(PyTuple_New(size)) {
        static_assert(
            ref == Ref::NEW,
            "Constructing an empty Tuple requires the use of Ref::NEW to avoid "
            "memory leaks"
        );
    }

    /* Construct a python::Tuple around an existing CPython tuple. */
    Tuple(PyObject* obj) : Base(obj) {
        if (!PyTuple_Check(obj)) {
            throw TypeError("expected a tuple");
        }
    }

    /* Copy constructor. */
    Tuple(const Tuple& other) : Base(other) {}

    /* Move constructor. */
    Tuple(Tuple&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Tuple& operator=(const Tuple& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Tuple& operator=(Tuple&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    /////////////////////////////////
    ////    PyTuple_* METHODS    ////
    /////////////////////////////////

    /* Implicitly convert a python::List into a PyTupleObject* pointer. */
    inline operator PyTupleObject*() const noexcept {
        return reinterpret_cast<PyTupleObject*>(this->obj);
    }

    /* Construct a new Python tuple containing the given objects. */
    template <typename... Args>
    inline static Tuple<Ref::STEAL> pack(Args&&... args) {
        static_assert(
            ref == Ref::STEAL,
            "pack() must use Ref::STEAL to avoid memory leaks"
        );

        PyObject* tuple = PyTuple_Pack(sizeof...(Args), std::forward<Args>(args)...);
        if (tuple == nullptr) {
            throw catch_python();
        }
        return {tuple};
    }

    /* Get the size of the tuple. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyTuple_GET_SIZE(this->obj));
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->obj);
    }

    ////////////////////////////////////
    ////    PySequence_* METHODS    ////
    ////////////////////////////////////

    /* Check if the tuple contains a specific item. */
    inline bool contains(PyObject* value) const noexcept {
        int result = PySequence_Contains(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Get the index of the first occurrence of the specified item. */
    inline size_t index(PyObject* value) const {
        Py_ssize_t result = PySequence_Index(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<size_t>(result);
    }

    /* Count the number of occurrences of the specified item. */
    inline size_t count(PyObject* value) const {
        Py_ssize_t result = PySequence_Count(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<size_t>(result);
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* A proxy for a key used to index the tuple. */
    class Element {
        PyObject* tuple;
        Py_ssize_t index;

        friend Tuple;

        Element(PyObject* tuple, Py_ssize_t index) : tuple(tuple), index(index) {}

    public:

        /* Get the item at this index.  Returns a new reference. */
        inline Object<Ref::STEAL> get() const {
            PyObject* result = PyTuple_GetItem(tuple, index);
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};
        }

        /* Set the item at this index.  Borrows a reference to the new value and
        releases a previous one if a conflict occurs. */
        inline void set(PyObject* value) {
            if (PyTuple_SetItem(tuple, index, Py_XNewRef(value))) {
                throw catch_python();
            };
        }

    };

    inline Element operator[](size_t index) {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    inline const Element operator[](size_t index) const {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    /* Directly access an item within the tuple, without bounds checking or
    constructing a proxy. */
    inline Object<Ref::BORROW> GET_ITEM(Py_ssize_t index) const {
        return Object<Ref::BORROW>(PyTuple_GET_ITEM(this->obj, index));
    }

    /* Directly set an item within the tuple, without bounds checking or constructing a
    proxy.  Steals a reference to `value` and does not clear the previous item if one is
    present. */
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyTuple_SET_ITEM(this->obj, index, value);
    }

    /* Get a new Tuple representing a slice from this Tuple. */
    inline Tuple<Ref::STEAL> get_slice(size_t start, size_t stop) const {
        if (start > size()) {
            throw IndexError("start index out of range");
        }
        if (stop > size()) {
            throw IndexError("stop index out of range");
        }
        if (start > stop) {
            throw IndexError("start index greater than stop index");
        }
        return {PyTuple_GetSlice(this->obj, start, stop)};
    }

};


/* An extension of python::Object that represents a Python list. */
template <Ref ref = Ref::STEAL>
class List : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty Python list of the specified size. */
    List(Py_ssize_t size) : Base(PyList_New(size)) {
        static_assert(
            ref == Ref::NEW,
            "Constructing an empty List requires the use of Ref::NEW to avoid "
            "memory leaks"
        );
    }

    /* Construct a python::List around an existing CPython list. */
    List(PyObject* obj) : Base(obj) {
        if (!PyList_Check(obj)) {
            throw TypeError("expected a list");
        }
    }

    /* Copy constructor. */
    List(const List& other) : Base(other) {}

    /* Move constructor. */
    List(List&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    List& operator=(const List& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    List& operator=(List&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ////////////////////////////////
    ////    PyList_* METHODS    ////
    ////////////////////////////////

    /* Implicitly convert a python::List into a PyListObject* pointer. */
    inline operator PyListObject*() const noexcept {
        return reinterpret_cast<PyListObject*>(this->obj);
    }

    /* Get the size of the list. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyList_GET_SIZE(this->obj));
    }

    /* Get the underlying PyObject* array. */
    inline PyObject** data() const noexcept {
        return PySequence_Fast_ITEMS(this->obj);
    }

    /* Append an element to a mutable list.  Borrows a reference to the value. */
    inline void append(PyObject* value) {
        if (PyList_Append(this->obj, value)) {
            throw catch_python();
        }
    }

    /* Insert an element into a mutable list at the specified index.  Borrows a
    reference to the value. */
    inline void insert(Py_ssize_t index, PyObject* value) {
        if (PyList_Insert(this->obj, index, value)) {
            throw catch_python();
        }
    }

    /* Sort a mutable list. */
    inline void sort() {
        if (PyList_Sort(this->obj)) {
            throw catch_python();
        }
    }

    /* Reverse a mutable list. */
    inline void reverse() {
        if (PyList_Reverse(this->obj)) {
            throw catch_python();
        }
    }

    /* Return a shallow copy of the list. */
    inline List<Ref::STEAL> copy() const {
        return {PyList_GetSlice(this->obj, 0, size())};
    }

    /* Convert the list into an equivalent tuple. */
    inline Tuple<Ref::STEAL> as_tuple() const {
        return {PyList_AsTuple(this->obj)};
    }

    ////////////////////////////////////
    ////    PySequence_* METHODS    ////
    ////////////////////////////////////

    /* Check if the tuple contains a specific item. */
    inline bool contains(PyObject* value) const noexcept {
        int result = PySequence_Contains(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Get the index of the first occurrence of the specified item. */
    inline size_t index(PyObject* value) const {
        Py_ssize_t result = PySequence_Index(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<size_t>(result);
    }

    /* Count the number of occurrences of the specified item. */
    inline size_t count(PyObject* value) const {
        Py_ssize_t result = PySequence_Count(this->obj, value);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<size_t>(result);
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* An assignable proxy for a particular index of the list. */
    class Element {
        PyObject* list;
        Py_ssize_t index;

        friend List;

        Element(PyObject* list, Py_ssize_t index) : list(list), index(index) {}

    public:

        /* Get the item at this index.  Returns a new reference. */
        inline Object<Ref::STEAL> get() const {
            PyObject* result = PyList_GetItem(list, index);
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};
        }

        /* Set the item at this index.  Borrows a reference to the new value and
        releases a reference to the previous one if a conflict occurs. */
        inline void set(PyObject* value) {
            if (PyList_SetItem(list, index, Py_XNewRef(value))) {
                throw catch_python();
            };
        }

    };

    inline Element operator[](size_t index) {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    inline const Element operator[](size_t index) const {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    /* Directly access an item within the list, without bounds checking or
    constructing a proxy.  Borrows a reference to the current value. */
    inline Object<Ref::BORROW> GET_ITEM(Py_ssize_t index) const {
        return Object<Ref::BORROW>(PyList_GET_ITEM(this->obj, index));
    }

    /* Directly set an item within the list, without bounds checking or constructing a
    proxy.  Steals a reference to the new value and does not release the previous one
    if a conflict occurs.  This is dangerous, and should only be used when constructing
    a new list, where the current values are known to be empty. */
    inline void SET_ITEM(Py_ssize_t index, PyObject* value) {
        PyList_SET_ITEM(this->obj, index, value);
    }

    /* Get a new list representing a slice within this list. */
    inline List<Ref::STEAL> get_slice(size_t start, size_t stop) const {
        if (start > size()) {
            throw IndexError("start index out of range");
        }
        if (stop > size()) {
            throw IndexError("stop index out of range");
        }
        if (start > stop) {
            throw IndexError("start index greater than stop index");
        }
        return {PyList_GetSlice(this->obj, start, stop)};
    }

    /* Set a slice within a mutable list.  Releases references to the current values
    and then borrows new references to the new ones. */
    inline void set_slice(size_t start, size_t stop, PyObject* value) {
        if (start > size()) {
            throw IndexError("start index out of range");
        }
        if (stop > size()) {
            throw IndexError("stop index out of range");
        }
        if (start > stop) {
            throw IndexError("start index greater than stop index");
        }
        if (PyList_SetSlice(this->obj, start, stop, value)) {
            throw catch_python();
        }
    }

};


/* An extension of python::Object that represents a Python set. */
template <Ref ref = Ref::STEAL>
class Set : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty Python set. */
    Set() : Base(PySet_New(nullptr)) {
        static_assert(
            ref == Ref::NEW,
            "Constructing an empty Set requires the use of Ref::NEW to avoid "
            "memory leaks"
        );
    }

    /* Construct a python::Set around an existing CPython set. */
    Set(PyObject* obj) : Base(obj) {
        if (!PyAnySet_Check(obj)) {
            throw TypeError("expected a set");
        }
    }

    /* Copy constructor. */
    Set(const Set& other) : Base(other) {}

    /* Move constructor. */
    Set(Set&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Set& operator=(const Set& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Set& operator=(Set&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    /////////////////////////////
    ////   PySet_* METHODS   ////
    /////////////////////////////

    /* Implicitly convert a python::List into a PySetObject* pointer. */
    inline operator PySetObject*() const noexcept {
        return reinterpret_cast<PySetObject*>(this->obj);
    }

    /* Get the size of the set. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PySet_GET_SIZE(this->obj));
    }

    /* Check if the set contains a particular key. */
    inline bool contains(PyObject* key) const {
        int result = PySet_Contains(this->obj, key);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Add an element to a mutable set.  Borrows a reference to the key. */
    inline void add(PyObject* key) {
        if (PySet_Add(this->obj, key)) {
            throw catch_python();
        }
    }

    /* Remove an element from a mutable set.  Releases a reference to the key */
    inline void remove(PyObject* key) {
        int result = PySet_Discard(this->obj, key);
        if (result == -1) {
            throw catch_python();
        }
        if (result == 0) {
            PyObject* py_repr = PyObject_Repr(key);
            if (py_repr == nullptr) {
                throw catch_python();
            }
            Py_ssize_t size;
            const char* c_repr = PyUnicode_AsUTF8AndSize(py_repr, &size);
            Py_DECREF(py_repr);
            if (c_repr == nullptr) {
                throw catch_python();
            }
            std::string result(c_repr, size);
            throw KeyError(result);
        }
    }

    /* Remove an element from a mutable set if it is present.  Releases a reference to
    the key if found. */
    inline void discard(PyObject* key) {
        if (PySet_Discard(this->obj, key) == -1) {
            throw catch_python();
        }
    }

    /* Remove and return an arbitrary element from a mutable set.  Transfers a
    reference to the caller. */
    inline Object<Ref::STEAL> pop() {
        PyObject* result = PySet_Pop(this->obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Return a shallow copy of the set. */
    inline Set<Ref::STEAL> copy() const {
        return {PySet_New(this->obj)};
    }

    /* Remove all elements from a mutable set. */
    inline void clear() {
        if (PySet_Clear(this->obj)) {
            throw catch_python();
        }
    }

};


/* An extension of python::Object that represents a Python dict. */
template <Ref ref = Ref::STEAL>
class Dict : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty Python dict. */
    Dict() : Base(PyDict_New()) {
        static_assert(
            ref == Ref::NEW,
            "Constructing an empty Dict requires the use of Ref::NEW to avoid "
            "memory leaks"
        );
    }

    /* Construct a python::Dict around an existing CPython dict. */
    Dict(PyObject* obj) : Base(obj) {
        if (!PyDict_Check(obj)) {
            throw TypeError("expected a dict");
        }
    }

    /* Copy constructor. */
    Dict(const Dict& other) : Base(other) {}

    /* Move constructor. */
    Dict(Dict&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    Dict& operator=(const Dict& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    Dict& operator=(Dict&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ////////////////////////////////
    ////    PyDict_* METHODS    ////
    ////////////////////////////////

    /* Implicitly convert a python::List into a PyDictObject* pointer. */
    inline operator PyDictObject*() const noexcept {
        return reinterpret_cast<PyDictObject*>(this->obj);
    }

    /* Get the size of the dict. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyDict_Size(this->obj));
    }

    /* Check if the dict contains a particular key. */
    inline bool contains(PyObject* key) const {
        int result = PyDict_Contains(this->obj, key);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Return a shallow copy of the dictionary. */
    inline Dict<Ref::STEAL> copy() const {
        return {PyDict_Copy(this->obj)};
    }

    /* Remove all elements from a mutable dict. */
    inline void clear() {
        if (PyDict_Clear(this->obj)) {
            throw catch_python();
        }
    }

    /* Get the value associated with a key or set it to the default value if it is not
    already present.  Returns a new reference. */
    inline Object<Ref::STEAL> set_default(PyObject* key, PyObject* default_value) {
        PyObject* result = PyDict_SetDefault(this->obj, key, default_value);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Update this dictionary with another Python mapping, overriding the current
    values on collision.  Borrows a reference to any keys/values that weren't in the
    original dictionary or conflict with those that are already present.  Releases a
    reference to any values that were overwritten. */
    inline void update(PyObject* other) {
        if (PyDict_Merge(this->obj, other, 1)) {
            throw catch_python();
        }
    }

    /* Equivalent to update(), except that the other container is assumed to contain
    key-value pairs of length 2. */
    inline void update_pairs(PyObject* other) {
        if (PyDict_MergeFromSeq2(this->obj, other, 1)) {
            throw catch_python();
        }
    }

    /* Update this dictionary with another Python mapping, keeping the current values
    on collision.  Borrows a reference to any keys/values that weren't in the original
    dictionary. */
    inline void merge(PyObject* other) {
        if (PyDict_Merge(this->obj, other, 0)) {
            throw catch_python();
        }
    }

    /* Equivalent to merge(), except that the other container is assumed to contain
    key-value pairs of length 2. */
    inline void merge_pairs(PyObject* other) {
        if (PyDict_MergeFromSeq2(this->obj, other, 0)) {
            throw catch_python();
        }
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* A proxy for a key used to index the dict. */
    template <typename Key>
    class Element {
        PyObject* dict;
        Key& key;

        friend Dict;

        /* Construct an Element proxy for the specified key.  Borrows a reference to
        the key. */
        Element(PyObject* dict, Key& key) : dict(dict), key(key) {}
    
    public:

        /* Get the item with the specified key or throw an error if it is not found.
        Returns a new reference. */
        inline Object<Ref::STEAL> get() const {
            PyObject* value;
            if constexpr (std::is_same_v<Key, const char*>) {
                value = PyDict_GetItemString(dict, key);
            } else {
                value = PyDict_GetItem(dict, key);
            }
            if (value == nullptr) {
                PyObject* py_repr = PyObject_Repr(key);
                if (py_repr == nullptr) {
                    throw catch_python();
                }
                Py_ssize_t size;
                const char* c_repr = PyUnicode_AsUTF8AndSize(py_repr, &size);
                Py_DECREF(py_repr);
                if (c_repr == nullptr) {
                    throw catch_python();
                }
                std::string result(c_repr, size);
                throw KeyError(result);
            }
            return {value};
        }

        /* Set the item with the specified key.  Borrows a reference to the new value
        and releases a reference to the previous one if a conflict occurs. */
        inline void set(PyObject* value) {
            if constexpr (std::is_same_v<Key, const char*>) {
                if (PyDict_SetItemString(dict, key, value)) {
                    throw catch_python();
                }
            } else {
                if (PyDict_SetItem(dict, key, value)) {
                    throw catch_python();
                }
            }
        }

        /* Delete the item with the specified key.  Releases a reference to both the
        key and value. */
        inline void del() {
            if constexpr (std::is_same_v<Key, const char*>) {
                if (PyDict_DelItemString(dict, key)) {
                    throw catch_python();
                }
            } else {
                if (PyDict_DelItem(dict, key)) {
                    throw catch_python();
                }
            }
        }

    };

    inline Element<PyObject*> operator[](PyObject* key) {
        return {this->obj, key};
    }

    inline const Element<PyObject*> operator[](PyObject* key) const {
        return {this->obj, key};
    }

    inline Element<const char*> operator[](const char* key) {
        return {this->obj, key};
    }

    inline const Element<const char*> operator[](const char* key) const {
        return {this->obj, key};
    }

    /////////////////////////
    ////    ITERATION    ////
    /////////////////////////

    /* An iterator that yield key-value pairs rather than just keys. */
    class Iterator {
        PyObject* dict;
        Py_ssize_t pos;  // required by PyDict_Next
        std::pair<PyObject*, PyObject*> curr;

        friend Dict;

        /* Construct a key-value iterator over the dictionary. */
        Iterator(PyObject* dict) : dict(dict), pos(0) {
            if (!PyDict_Next(dict, &pos, &curr.first, &curr.second)) {
                curr.first = nullptr;
                curr.second = nullptr;
            }
        }

        /* Construct an empty iterator to terminate the loop. */
        Iterator() : dict(nullptr), pos(0), curr(nullptr, nullptr) {}

    public:
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = std::pair<PyObject*, PyObject*>;
        using pointer               = value_type*;
        using reference             = value_type&;

        /* Copy constructor. */
        Iterator(const Iterator& other) :
            dict(other.dict), pos(other.pos), curr(other.curr)
        {}

        /* Move constructor. */
        Iterator(Iterator&& other) :
            dict(other.dict), pos(other.pos), curr(std::move(other.curr))
        {
            other.curr.first = nullptr;
            other.curr.second = nullptr;
        }

        /* Get the current key-value pair. */
        inline std::pair<PyObject*, PyObject*> operator*() {
            return curr;
        }

        /* Get the current key-value pair for a const dictionary. */
        inline const std::pair<PyObject*, PyObject*>& operator*() const {
            return curr;
        }

        /* Advance to the next key-value pair. */
        inline Iterator& operator++() {
            if (!PyDict_Next(dict, &pos, &curr.first, &curr.second)) {
                curr.first = nullptr;
                curr.second = nullptr;
            }
            return *this;
        }

        /* Compare to terminate the loop. */
        inline bool operator!=(const Iterator& other) const {
            return curr.first != other.curr.first && curr.second != other.curr.second;
        }

    };

    inline auto begin() { return Iterator(this->obj); }
    inline auto begin() const { return Iterator(this->obj); }
    inline auto cbegin() const { return Iterator(this->obj); }
    inline auto end() { return Iterator(); }
    inline auto end() const { return Iterator(); }
    inline auto cend() const { return Iterator(); }

    /* Return a python::List containing all the keys in this dictionary. */
    inline List<Ref::STEAL> keys() const {
        return {PyDict_Keys(this->obj)};
    }

    /* Return a python::List containing all the values in this dictionary. */
    inline List<Ref::STEAL> values() const {
        return {PyDict_Values(this->obj)};
    }

    /* Return a python::List containing all the key-value pairs in this dictionary. */
    inline List<Ref::STEAL> items() const {
        return {PyDict_Items(this->obj)};
    }

};


/* A wrapper around a fast Python sequence (list or tuple) that manages reference
counts and simplifies access. */
template <Ref ref = Ref::STEAL>
class FastSequence : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a PySequence from an iterable or other sequence. */
    FastSequence(PyObject* obj) : Base(obj) {
        if (!PyTuple_Check(obj) && !PyList_Check(obj)) {
            throw TypeError("expected a tuple or list");
        }
    }

    /* Copy constructor. */
    FastSequence(const FastSequence& other) : Base(other) {}

    /* Move constructor. */
    FastSequence(FastSequence&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    FastSequence& operator=(const FastSequence& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    FastSequence& operator=(FastSequence&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    /////////////////////////////////////////
    ////    PySequence_Fast_* METHODS    ////
    /////////////////////////////////////////

    /* Get the size of the sequence. */
    inline size_t size() const {
        return static_cast<size_t>(PySequence_Fast_GET_SIZE(this->obj));
    }

    /* Get underlying PyObject* array. */
    inline PyObject** data() const {
        return PySequence_Fast_ITEMS(this->obj);
    }

    /* Directly get an item within the sequence without boundschecking.  Returns a
    borrowed reference. */
    inline PyObject* GET_ITEM(Py_ssize_t index) const {
        return PySequence_Fast_GET_ITEM(this->obj, index);
    }

    /* Get the value at a particular index of the sequence.  Returns a borrowed
    reference. */
    inline PyObject* operator[](size_t index) const {
        if (index >= size()) {
            throw IndexError("index out of range");
        }
        return GET_ITEM(index);
    }

};


/* An extension of python::Object that represents a Python unicode string. */
template <Ref ref = Ref::STEAL>
class String : public Object<ref> {
    using Base = Object<ref>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a Python unicode string from an existing CPython unicode string. */
    String(PyObject* obj) : Base(obj) {
        if (!PyUnicode_Check(obj)) {
            throw TypeError("expected a unicode string");
        }
    }

    /* Construct a Python unicode string from a C-style character array. */
    String(const char* value) : Base(PyUnicode_FromString(value)) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a String from a C-style string requires the use of "
            "Ref::STEAL to avoid memory leaks"
        );
    }

    /* Construct a Python unicode string from a C++ string. */
    String(const std::string& value) : Base(PyUnicode_FromStringAndSize(
        value.c_str(), value.size()
    )) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a String from a string requires the use of Ref::STEAL to "
            "avoid memory leaks"
        );
    }

    /* Construct a Python unicode string from a C++ string view. */
    String(const std::string_view& value) : Base(PyUnicode_FromStringAndSize(
        value.data(), value.size()
    )) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a String from a string view requires the use of Ref::STEAL "
            "to avoid memory leaks"
        );
    }

    /* Construct a Python unicode string from a printf-style format string.  See the
    Python docs for PyUnicode_FromFormat() for more details. */
    template <typename... Args>
    String(const char* format, Args&&... args) : Base(PyUnicode_FromFormat(
        format, std::forward<Args>(args)...
    )) {
        static_assert(
            ref == Ref::STEAL,
            "Constructing a String from a format string requires the use of Ref::STEAL "
            "to avoid memory leaks"
        );
    }

    /* Copy constructor. */
    String(const String& other) : Base(other) {}

    /* Move constructor. */
    String(String&& other) : Base(std::move(other)) {}

    /* Copy assignment. */
    String& operator=(const String& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        return *this;
    }

    /* Move assignment. */
    String& operator=(String&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        return *this;
    }

    ///////////////////////////////////
    ////    PyUnicode_* METHODS    ////
    ///////////////////////////////////

    /* Implicitly convert a python::List into a PyUnicodeObject* pointer. */
    inline operator PyUnicodeObject*() const noexcept {
        return reinterpret_cast<PyUnicodeObject*>(this->obj);
    }

    /* Get the underlying unicode buffer. */
    inline void* data() const noexcept {
        return PyUnicode_DATA(this->obj);
    }

    /* Get the kind of the string, indicating the size of the unicode points. */
    inline int kind() const noexcept {
        return PyUnicode_KIND(this->obj);
    }

    /* Get the maximum code point that is suitable for creating another string based
    on this string. */
    inline Py_UCS4 max_char() const noexcept {
        return PyUnicode_MAX_CHAR_VALUE(this->obj);
    }

    /* Get the length of the string. */
    inline size_t size() const noexcept {
        return static_cast<size_t>(PyUnicode_GET_LENGTH(this->obj));
    }

    /* Fill this string with the given unicode character. */
    inline void fill(Py_UCS4 ch) {
        if (PyUnicode_FillChar(this->obj, ch)) {
            throw catch_python();
        }
    }

    /* Return a shallow copy of the string. */
    inline String<Ref::STEAL> copy() const {
        PyObject* result = PyUnicode_New(size(), max_char());
        if (result == nullptr) {
            throw catch_python();
        }
        if (PyUnicode_CopyCharacters(result, 0, this->obj, 0, size())) {
            Py_DECREF(result);
            throw catch_python();
        }
        return {result};
    }

    /* Check if the string contains a given substring. */
    inline bool contains(PyObject* substr) const {
        int result = PyUnicode_Contains(this->obj, substr);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Return a substring from this string. */
    inline String<Ref::STEAL> substring(Py_ssize_t start, Py_ssize_t end) const {
        return {PyUnicode_Substring(this->obj, start, end)};
    }

    /* Concatenate this string with another. */
    inline String<Ref::STEAL> concat(PyObject* other) const {
        return {PyUnicode_Concat(this->obj, other)};
    }

    /* Split this string on the given separator, returning the components in a
    python::List. */
    inline List<Ref::STEAL> split(PyObject* separator, Py_ssize_t maxsplit) const {
        return {PyUnicode_Split(this->obj, separator, maxsplit)};
    }

    /* Split this string at line breaks, returning the components in a python::List. */
    inline List<Ref::STEAL> splitlines(bool keepends) const {
        return {PyUnicode_Splitlines(this->obj, keepends)};
    }

    /* Join a sequence of strings with this string as a separator. */
    inline String<Ref::STEAL> join(PyObject* iterable) const {
        return {PyUnicode_Join(this->obj, iterable)};
    }

    /* Check whether the string starts with the given substring. */
    inline bool startswith(PyObject* prefix, Py_ssize_t start = 0) const {
        int result = PyUnicode_Tailmatch(this->obj, prefix, start, size(), -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Check whether the string starts with the given substring. */
    inline bool startswith(PyObject* prefix, Py_ssize_t start, Py_ssize_t stop) const {
        int result = PyUnicode_Tailmatch(this->obj, prefix, start, stop, -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Check whether the string ends with the given substring. */
    inline bool endswith(PyObject* suffix, Py_ssize_t start = 0) const {
        int result = PyUnicode_Tailmatch(this->obj, suffix, start, size(), 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Check whether the string ends with the given substring. */
    inline bool endswith(PyObject* suffix, Py_ssize_t start, Py_ssize_t stop) const {
        int result = PyUnicode_Tailmatch(this->obj, suffix, start, stop, 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the first occurrence of a substring within the string. */
    inline Py_ssize_t find(PyObject* sub, Py_ssize_t start = 0) const {
        Py_ssize_t result = PyUnicode_Find(this->obj, sub, start, size(), 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the first occurrence of a substring within the string. */
    inline Py_ssize_t find(PyObject* sub, Py_ssize_t start, Py_ssize_t stop) const {
        Py_ssize_t result = PyUnicode_Find(this->obj, sub, start, stop, 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the first occurrence of a character within the string. */
    inline Py_ssize_t find(Py_UCS4 ch, Py_ssize_t start = 0) const {
        Py_ssize_t result = PyUnicode_FindChar(this->obj, ch, start, size(), 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the first occurrence of a character within the string. */
    inline Py_ssize_t find(Py_UCS4 ch, Py_ssize_t start, Py_ssize_t stop) const {
        Py_ssize_t result = PyUnicode_FindChar(this->obj, ch, start, stop, 1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the last occurrence of a substring within the string. */
    inline Py_ssize_t rfind(PyObject* sub, Py_ssize_t start = 0) const {
        Py_ssize_t result = PyUnicode_Find(this->obj, sub, start, size(), -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the last occurrence of a substring within the string. */
    inline Py_ssize_t rfind(PyObject* sub, Py_ssize_t start, Py_ssize_t stop) const {
        Py_ssize_t result = PyUnicode_Find(this->obj, sub, start, stop, -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the last occurrence of a character within the string. */
    inline Py_ssize_t rfind(Py_UCS4 ch, Py_ssize_t start = 0) const {
        Py_ssize_t result = PyUnicode_FindChar(this->obj, ch, start, size(), -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Find the last occurrence of a character within the string. */
    inline Py_ssize_t rfind(Py_UCS4 ch, Py_ssize_t start, Py_ssize_t stop) const {
        Py_ssize_t result = PyUnicode_FindChar(this->obj, ch, start, stop, -1);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Count the number of occurrences of a substring within the string. */
    inline Py_ssize_t count(PyObject* sub, Py_ssize_t start = 0) const {
        Py_ssize_t result = PyUnicode_Count(this->obj, sub, start, size());
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Count the number of occurrences of a substring within the string. */
    inline Py_ssize_t count(PyObject* sub, Py_ssize_t start, Py_ssize_t stop) const {
        Py_ssize_t result = PyUnicode_Count(this->obj, sub, start, stop);
        if (result == -1) {
            throw catch_python();
        }
        return result;
    }

    /* Return a new string with at most maxcount occurrences of a substring replaced
    with another substring. */
    inline String<Ref::STEAL> replace(
        PyObject* substr, PyObject* replstr, Py_ssize_t maxcount = -1
    ) const {
        return {PyUnicode_Replace(this->obj, substr, replstr, maxcount)};
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    /* A proxy for a key used to index the string. */
    class Element {
        PyObject* string;
        Py_ssize_t index;

        friend String;

        Element(PyObject* string, Py_ssize_t index) : string(string), index(index) {}

    public:
            
        /* Get the character at this index. */
        inline Py_UCS4 get() const {
            return PyUnicode_ReadChar(string, index);
        }

        /* Set the character at this index. */
        inline void set(Py_UCS4 ch) {
            PyUnicode_WriteChar(string, index, ch);
        }

    };

    inline Element operator[](Py_ssize_t index) {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    inline const Element operator[](Py_ssize_t index) const {
        return {this->obj, static_cast<Py_ssize_t>(index)};
    }

    /* Directly access a character within the string, without bounds checking or
    constructing a proxy. */
    inline Py_UCS4 READ_CHAR(Py_ssize_t index) const {
        return PyUnicode_READ_CHAR(this->obj, index);
    }

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a python::String into a C-style character array. */
    inline operator const char*() const {
        const char* result = PyUnicode_AsUTF8(this->obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }

    /* Implicitly convert a python::String into a C++ string. */
    inline operator std::string() const {
        Py_ssize_t length;
        const char* data = PyUnicode_AsUTF8AndSize(this->obj, &length);
        if (data == nullptr) {
            throw catch_python();
        }
        return std::string(data, static_cast<size_t>(length));
    }

    /* Implicitly convert a python::String into a C++ string view. */
    inline operator std::string_view() const {
        Py_ssize_t length;
        const char* data = PyUnicode_AsUTF8AndSize(this->obj, &length);
        if (data == nullptr) {
            throw catch_python();
        }
        return std::string_view(data, static_cast<size_t>(length));
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    /* Concatenate this string with another. */
    inline String<Ref::STEAL> operator+(PyObject* other) const {
        return concat(other);
    }

    /* Check if this string is less than another string. */
    inline bool operator<(PyObject* other) const {
        if (PyUnicode_Check(other)) {
            int result = PyUnicode_Compare(this->obj, other);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return result < 0;
        }
        return Base::operator<(other);
    }

    /* Check if this string is less than or equal to another string. */
    inline bool operator<=(PyObject* other) const {
        if (PyUnicode_Check(other)) {
            int result = PyUnicode_Compare(this->obj, other);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return result <= 0;
        }
        return Base::operator<=(other);
    }

    /* Check if this string is greater than another string. */
    inline bool operator>(PyObject* other) const {
        if (PyUnicode_Check(other)) {
            int result = PyUnicode_Compare(this->obj, other);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return result > 0;
        }
        return Base::operator>(other);
    }

    /* Check if this string is greater than or equal to another string. */
    inline bool operator>=(PyObject* other) const {
        if (PyUnicode_Check(other)) {
            int result = PyUnicode_Compare(this->obj, other);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return result >= 0;
        }
        return Base::operator>=(other);
    }

    /* Check if this string is equal to another string. */
    inline bool operator==(PyObject* other) const {
        if (PyUnicode_Check(other)) {
            int result = PyUnicode_Compare(this->obj, other);
            if (result == -1 && PyErr_Occurred()) {
                throw catch_python();
            }
            return result == 0;
        }
        return Base::operator==(other);
    }

};


}  // namespace python


/* A trait that controls which C++ types are passed through the sequence() helper
without modification.  These must be vector or array types that support a definite
`.size()` method as well as integer-based indexing via the `[]` operator.  If a type
does not appear here, then it is converted into a std::vector instead. */
template <typename T>
struct SequenceFilter : std::false_type {};

template <typename T, typename Alloc>
struct SequenceFilter<std::vector<T, Alloc>> : std::true_type {};

template <typename T, size_t N> 
struct SequenceFilter<std::array<T, N>> : std::true_type {};

template <typename T, typename Alloc>
struct SequenceFilter<std::deque<T, Alloc>> : std::true_type {};

template <>
struct SequenceFilter<std::string> : std::true_type {};
template <>
struct SequenceFilter<std::wstring> : std::true_type {};
template <>
struct SequenceFilter<std::u16string> : std::true_type {};
template <>
struct SequenceFilter<std::u32string> : std::true_type {};

template <>
struct SequenceFilter<std::string_view> : std::true_type {};
template <>
struct SequenceFilter<std::wstring_view> : std::true_type {};
template <>
struct SequenceFilter<std::u16string_view> : std::true_type {};
template <>
struct SequenceFilter<std::u32string_view> : std::true_type {};

template <typename T>
struct SequenceFilter<std::valarray<T>> : std::true_type {};

#if __cplusplux >= 202002L  // C++20 or later

    template <typename T>
    struct SequenceFilter<std::span<T>> : std::true_type {};

    template <>
    struct SequenceFilter<std::u8string> : std::true_type {};

    template <>
    struct SequenceFilter<std::u8string_view> : std::true_type {};

#endif



/* Unpack an arbitrary Python iterable or C++ container into a sequence that supports
random access.  If the input already supports these, then it is returned directly. */
template <typename Iterable>
auto sequence(Iterable&& iterable) {

    if constexpr (is_pyobject<Iterable>) {
        PyObject* seq = PySequence_Fast(iterable, "expected a sequence");
        return python::FastSequence<python::Ref::STEAL>(seq);

    } else {
        using Traits = ContainerTraits<Iterable>;
        static_assert(Traits::forward_iterable, "container must be forward iterable");

        // if the container already supports random access, then return it directly
        if constexpr (
            SequenceFilter<
                std::remove_cv_t<
                    std::remove_reference_t<Iterable>
                >
            >::value
        ) {
            return std::forward<Iterable>(iterable);
        } else {
            auto proxy = iter(iterable);
            auto it = proxy.begin();
            auto end = proxy.end();
            using Deref = decltype(*std::declval<decltype(it)>());
            return std::vector<Deref>(it, end);
        }
    }
}


}  // namespace bertrand


#undef PYTHON_SIMPLIFIED_ERROR_STATE
#endif  // BERTRAND_STRUCTS_UTIL_CONTAINER_H
