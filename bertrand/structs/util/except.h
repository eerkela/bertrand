#ifndef BERTRAND_STRUCTS_UTIL_EXCEPT_H
#define BERTRAND_STRUCTS_UTIL_EXCEPT_H

#include <ios>  // std::ios::failure
#include <new>  // std::bad_alloc
#include <stdexcept>  // std::invalid_argument, std::domain_error, ...
#include <string>  // std::string
#include <type_traits>  // std::is_convertible_v<>
#include <typeinfo>  // std::bad_typeid, std::bad_cast
#include <Python.h>  // CPython API
#include "base.h"  // DEBUG, LOG()


/* NOTE: This file contains utilities for translating exceptions between Python and
 * C++ while preserving the exception type, error message, and traceback.  It also
 * automatically logs the error message to the DEBUG log.
 *
 * reference:
 *      https://docs.python.org/3/c-api/exceptions.html#standard-exceptions
 *
 * NOTE: Python 3.12 changes the API for fetching and restoring errors, so we need to
 * use conditional compilation to support both versions.
 */
 #define PYTHON_SIMPLIFIED_ERROR_STATE (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)


namespace bertrand {


/* Base class for all Python-compatible C++ exceptions. */
class Exception : public std::exception {
private:
    std::string message;
    PyObject* exc_type;
    PyObject* exc_tb;

public:
    using std::exception::exception;
    using std::exception::operator=;

    // NOTE: all constructors steal a reference to exc_type and exc_tb

    explicit Exception(
        const char* what,
        PyObject* exc_type = nullptr,
        PyObject* exc_tb = nullptr
    ) : message(what), exc_type(exc_type), exc_tb(exc_tb)
    {
        if constexpr (DEBUG) {
            if (exc_type == nullptr) {
                LOG(err, "RuntimeError: ", what);
            } else {
                LOG(err, reinterpret_cast<PyTypeObject*>(exc_type)->tp_name, ": ", what);
            }
        }
    }

    explicit Exception(
        const std::string& what,
        PyObject* exc_type = nullptr,
        PyObject* exc_tb = nullptr
    ) : message(what), exc_type(exc_type), exc_tb(exc_tb)
    {
        if constexpr (DEBUG) {
            if (exc_type == nullptr) {
                LOG(err, "RuntimeError: ", what);
            } else {
                LOG(err, reinterpret_cast<PyTypeObject*>(exc_type)->tp_name, ": ", what);
            }
        }
    }

    explicit Exception(
        const std::string_view& what,
        PyObject* exc_type = nullptr,
        PyObject* exc_tb = nullptr
    ) : message(what), exc_type(exc_type), exc_tb(exc_tb)
    {
        if constexpr (DEBUG) {
            if (exc_type == nullptr) {
                LOG(err, "RuntimeError: ", what);
            } else {
                LOG(err, reinterpret_cast<PyTypeObject*>(exc_type)->tp_name, ": ", what);
            }
        }
    }

    ~Exception() noexcept override {
        Py_XDECREF(exc_type);
        Py_XDECREF(exc_tb);
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

            if (exc_type == nullptr) {
                PYLOG(err, "RuntimeError: ", what());
                PyErr_SetString(PyExc_RuntimeError, what());
                return;
            }

            if constexpr (DEBUG) {
                PYLOG(
                    err, reinterpret_cast<PyTypeObject*>(exc_type)->tp_name, ": ",
                    what()
                );
            }

            if (exc_tb == nullptr) {
                PyErr_SetString(exc_type, what());
                return;
            }

            PyErr_SetRaisedException(Py_NewRef(exc_tb));

        #else

            if (exc_type == nullptr) {
                PYLOG(err, "RuntimeError: ", what());
                PyErr_SetString(PyExc_RuntimeError, what());
                return;
            }

            if constexpr (DEBUG) {
                PYLOG(
                    err, reinterpret_cast<PyTypeObject*>(exc_type)->tp_name, ": ",
                    what()
                );
            }

            if (exc_tb == nullptr) {
                PyErr_SetString(exc_type, what());
                return;
            }

            // restore traceback if one exists
            PyObject* msg = PyUnicode_FromStringAndSize(message.c_str(), message.size());
            if (msg == nullptr) {
                PyErr_SetString(exc_type, what());
            } else {
                PyErr_Restore(Py_NewRef(exc_type), msg, Py_NewRef(exc_tb));
            }

        #endif
    }

};


struct MemoryError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    MemoryError(const char* what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_MemoryError), exc_tb)
    {}

    MemoryError(const std::string& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_MemoryError), exc_tb)
    {}

    MemoryError(const std::string_view& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_MemoryError), exc_tb)
    {}

};


struct TypeError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    TypeError(const char* what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_TypeError), exc_tb)
    {}

    TypeError(const std::string& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_TypeError), exc_tb)
    {}

    TypeError(const std::string_view& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_TypeError), exc_tb)
    {}

};


struct ValueError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    ValueError(const char* what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_ValueError), exc_tb)
    {}

    ValueError(const std::string& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_ValueError), exc_tb)
    {}

    ValueError(const std::string_view& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_ValueError), exc_tb)
    {}

};


struct KeyError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    KeyError(const char* what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_KeyError), exc_tb)
    {}

    KeyError(const std::string& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_KeyError), exc_tb)
    {}

    KeyError(const std::string_view& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_KeyError), exc_tb)
    {}

};


struct AttributeError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    AttributeError(const char* what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_AttributeError), exc_tb)
    {}

    AttributeError(const std::string& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_AttributeError), exc_tb)
    {}

    AttributeError(const std::string_view& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_AttributeError), exc_tb)
    {}

};


struct IndexError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    IndexError(const char* what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_IndexError), exc_tb)
    {}

    IndexError(const std::string& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_IndexError), exc_tb)
    {}

    IndexError(const std::string_view& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_IndexError), exc_tb)
    {}

};


struct RuntimeError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    RuntimeError(const char* what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_RuntimeError), exc_tb)
    {}

    RuntimeError(const std::string& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_RuntimeError), exc_tb)
    {}

    RuntimeError(const std::string_view& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_RuntimeError), exc_tb)
    {}

};


struct FileNotFoundError : public Exception {
    using Exception::Exception;
    using Exception::operator=;

    FileNotFoundError(const char* what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_FileNotFoundError), exc_tb)
    {}

    FileNotFoundError(const std::string& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_FileNotFoundError), exc_tb)
    {}

    FileNotFoundError(const std::string_view& what, PyObject* exc_tb = nullptr) :
        Exception(what, Py_NewRef(PyExc_FileNotFoundError), exc_tb)
    {}

};


/* Convert the most recent Python error into an equivalent C++ exception, preserving
the error message.  Requires the GIL. */
template <typename Exc = Exception>
Exc catch_python() {
    static_assert(
        std::is_convertible_v<Exc, Exception>,
        "Exception type must inherit from util::Exception"
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
the error message.  Requires the GIL. */
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
    catch (const std::bad_alloc& e) {
        LOG(err, "MemoryError: ", e.what());
        PyErr_SetString(PyExc_MemoryError, e.what());  // bad_alloc -> MemoryError

    } catch (const std::bad_cast& e) {
        LOG(err, "TypeError: ", e.what());
        PyErr_SetString(PyExc_TypeError, e.what());  // bad_cast -> TypeError

    } catch (const std::bad_typeid& e) {
        LOG(err, "TypeError: ", e.what());
        PyErr_SetString(PyExc_TypeError, e.what());  // bad_typeid -> TypeError

    } catch (const std::domain_error& e) {
        LOG(err, "ValueError: ", e.what());
        PyErr_SetString(PyExc_ValueError, e.what());  // domain_error -> ValueError

    } catch (const std::invalid_argument& e) {
        LOG(err, "ValueError: ", e.what());
        PyErr_SetString(PyExc_ValueError, e.what());  // invalid_argument -> ValueError

    } catch (const std::ios_base::failure& e) {
        LOG(err, "IOError: ", e.what());
        PyErr_SetString(PyExc_IOError, e.what());  // ios_base::failure -> IOError

    } catch (const std::out_of_range& e) {
        LOG(err, "IndexError: ", e.what());
        PyErr_SetString(PyExc_IndexError, e.what());  // out_of_range -> IndexError

    } catch (const std::overflow_error& e) {
        LOG(err, "OverflowError: ", e.what());
        PyErr_SetString(PyExc_OverflowError, e.what());  // overflow_error -> OverflowError

    } catch (const std::range_error& e) {
        LOG(err, "ArithmeticError: ", e.what());
        PyErr_SetString(PyExc_ArithmeticError, e.what());  // range_error -> ArithmeticError

    } catch (const std::underflow_error& e) {
        LOG(err, "ArithmeticError: ", e.what());
        PyErr_SetString(PyExc_ArithmeticError, e.what());  // underflow_error -> ArithmeticError
    }

    // all other exceptions map to RuntimeError
    catch (const std::exception& exc) {
        LOG(err, "RuntimeError: ", exc.what());
        PyErr_SetString(PyExc_RuntimeError, exc.what());
    } catch (...) {
        LOG(err, "RuntimeError: Unknown C++ error");
        PyErr_SetString(PyExc_RuntimeError, "Unknown C++ error");
    }
}


}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_EXCEPT_H
