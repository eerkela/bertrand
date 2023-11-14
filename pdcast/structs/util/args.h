// include guard: BERTRAND_STRUCTS_UTIL_ARGS_H
#ifndef BERTRAND_STRUCTS_UTIL_ARGS_H
#define BERTRAND_STRUCTS_UTIL_ARGS_H

#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <string_view>  // std::string_view
#include <Python.h>  // CPython API
#include "except.h"  // TypeError
#include "func.h"  // FuncTraits



namespace bertrand {
namespace structs {
namespace util {


////////////////////////////////
////    ARGUMENT PARSERS    ////
////////////////////////////////


/* enum describing the different call protocols that can be parsed. */
enum class CallProtocol {
    ARGS,
    KWARGS,
    FASTCALL,
    VECTORCALL
};


/* CRTP Base class containing the public interface for all Python argument parsers. */
template <typename Derived>
class BaseArgs {
protected:
    const Py_ssize_t n_args;
    const Py_ssize_t n_kwargs;
    Py_ssize_t idx;

    /* Initialize basic arg/kwarg counts for the parser. */
    BaseArgs(const Py_ssize_t n_args, const Py_ssize_t n_kwargs = 0) :
        n_args(n_args), n_kwargs(n_kwargs), idx(0)
    {};

public:

    /* Copy/move constructors/assignment operators deleted for simplicity. */
    BaseArgs(const BaseArgs&) = delete;
    BaseArgs(BaseArgs&&) = delete;
    BaseArgs& operator=(const BaseArgs&) = delete;
    BaseArgs& operator=(BaseArgs&&) = delete;

    /* Extract an argument from the pool, using an optional conversion function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType parse(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        using Out = std::optional<ReturnType>;

        // call subclass hook
        Derived* self = static_cast<Derived*>(this);
        Out result = self->template hook<Func, ReturnType>(name, convert);
        if (result.has_value()) {
            ++idx;
            return result.value();
        }

        // no matching argument
        std::ostringstream msg;
        msg << "Missing required argument: " << name;
        throw std::runtime_error(msg.str());
    }

    /* Extract an argument from the pool, using an optional conversion function and
    default value. */
    template <
        typename Func = PyObject*,
        typename Type = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType parse(
        const std::string_view& name,
        Func convert,
        Type default_value
    ) {
        using Out = std::optional<ReturnType>;
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );

        // call subclass hook
        Derived* self = static_cast<Derived*>(this);
        Out result = self->template hook<Func, ReturnType>(name, convert);
        return result.value_or(default_value);
    }

    /* Finalize the argument list, throwing an error if any additional arguments are
    present.  */
    inline void finalize() {
        // TODO: this error message could probably be more informative.  Ordinary
        // Python functions will raise a TypeError with a message like "f() takes 2
        // positional arguments but 3 were given", or "__init__() got an unexpected 
        // keyword argument 'spec'", etc.
        if (idx < n_args + n_kwargs) {
            std::ostringstream msg;
            msg << "Function takes at most " << idx << " arguments, but ";
            msg << n_args + n_kwargs << " were given";
            throw TypeError(msg.str());
        }
    }

    /* Get the number of positional arguments that were supplied to the parser. */
    inline Py_ssize_t positional() const noexcept { return n_args; }

    /* Get the number of keyword arguments that were supplied to the parser. */
    inline Py_ssize_t keyword() const noexcept { return n_kwargs; }

};


/* A utility class that allows for efficient parsing of Python arguments passed to an
extension method. */
template <CallProtocol>
class PyArgs;


/* A parser for C methods implementing the METH_VARARGS protocol. */
template <>
class PyArgs<CallProtocol::ARGS> : public BaseArgs<PyArgs<CallProtocol::ARGS>> {
    using Base = BaseArgs<PyArgs<CallProtocol::ARGS>>;

    friend Base;
    PyObject* args;

    /* Hook for parse() overloads. */
    template <typename Func, typename ReturnType>
    std::optional<ReturnType> hook(const std::string_view& name, Func convert) {
        static constexpr bool pyobject = std::is_same_v<ReturnType, PyObject*>;

        // check for positional argument
        if (this->idx < this->n_args) {
            PyObject* val = PyTuple_GET_ITEM(args, this->idx++);
            if constexpr (pyobject) {
                return std::make_optional(val);
            } else {
                return std::make_optional(convert(val));
            }
        }

        // no matching argument
        return std::nullopt;
    }

public:
    /* Create a parser for the given arguments. */
    PyArgs(PyObject* args) : Base(PyTuple_GET_SIZE(args)), args(args) {}
};


/* A parser for C methods implementing the METH_VARARGS | METH_KEYWORDS protocol. */
template <>
class PyArgs<CallProtocol::KWARGS> : public BaseArgs<PyArgs<CallProtocol::KWARGS>> {
    using Base = BaseArgs<PyArgs<CallProtocol::KWARGS>>;

    friend Base;
    PyObject* args;
    PyObject* kwargs;

    /* Hook for parse() overloads. */
    template <typename Func, typename ReturnType>
    std::optional<ReturnType> hook(const std::string_view& name, Func convert) {
        static constexpr bool pyobject = std::is_same_v<ReturnType, PyObject*>;

        // check for positional argument
        if (this->idx < this->n_args) {
            PyObject* val = PyTuple_GET_ITEM(args, this->idx++);
            if constexpr (pyobject) {
                return std::make_optional(val);
            } else {
                return std::make_optional(convert(val));
            }
        }

        // check for keyword argument
        if (kwargs != nullptr) {
            PyObject* val = PyDict_GetItemString(kwargs, name.data());
            if (val != nullptr) {
                ++this->idx;
                if constexpr (pyobject) {
                    return std::make_optional(val);
                } else {
                    return std::make_optional(convert(val));
                }
            }
        }

        // no matching argument
        return std::nullopt;
    }

public:
    /* Create a parser for the given arguments. */
    PyArgs(PyObject* args, PyObject* kwargs) :
        Base(PyTuple_GET_SIZE(args), kwargs == nullptr ? 0 : PyDict_Size(kwargs)),
        args(args),
        kwargs(kwargs)
    {}
};


/* A parser for C methods implementing the METH_FASTCALL protocol. */
template <>
class PyArgs<CallProtocol::FASTCALL> : public BaseArgs<PyArgs<CallProtocol::FASTCALL>> {
    using Base = BaseArgs<PyArgs<CallProtocol::FASTCALL>>;

    friend Base;
    PyObject* const* args;

    /* Hook for parse() overloads. */
    template <typename Func, typename ReturnType>
    std::optional<ReturnType> hook(const std::string_view& name, Func convert) {
        static constexpr bool pyobject = std::is_same_v<ReturnType, PyObject*>;

        // check for positional argument
        if (this->idx < this->n_args) {
            PyObject* val = args[this->idx++];
            if constexpr (pyobject) {
                return std::make_optional(val);
            } else {
                return std::make_optional(convert(val));
            }
        }

        // no matching argument
        return std::nullopt;
    }

public:

    /* Create a parser for the given arguments. */
    PyArgs(PyObject* const* args, Py_ssize_t nargs) : Base(nargs), args(args) {}

};


/* A parser for C methods implementing the METH_FASTCALL | METH_KEYWORDS protocol. */
template <>
class PyArgs<CallProtocol::VECTORCALL> : public BaseArgs<PyArgs<CallProtocol::VECTORCALL>> {
    using Base = BaseArgs<PyArgs<CallProtocol::VECTORCALL>>;

    friend Base;
    PyObject* const* args;
    std::string_view* kwnames;

    /* Hook for parse() overloads. */
    template <typename Func, typename ReturnType>
    std::optional<ReturnType> hook(const std::string_view& name, Func convert) {
        static constexpr bool pyobject = std::is_same_v<ReturnType, PyObject*>;

        // check for positional argument
        if (this->idx < this->n_args) {
            PyObject* val = args[this->idx++];
            if constexpr (pyobject) {
                return std::make_optional(val);
            } else {
                return std::make_optional(convert(val));
            }
        }

        // check for keyword argument
        for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
            if (kwnames[i] == name) {
                PyObject* val = args[this->n_args + i];
                ++this->idx;
                if constexpr (pyobject) {
                    return std::make_optional(val);
                } else {
                    return std::make_optional(convert(val));
                }
            }
        }

        // no matching argument
        return std::nullopt;
    }

public:

    /* Create a parser for the given arguments. */
    PyArgs(PyObject* const* args, Py_ssize_t nargs, PyObject* kwnames) :
        Base(nargs, kwnames == nullptr ? 0 : PyTuple_GET_SIZE(kwnames)),
        args(args),
        kwnames(nullptr)
    {
        if (n_kwargs == 0) {
            this->kwnames = nullptr;
        } else {
            this->kwnames = static_cast<std::string_view*>(
                malloc(n_kwargs * sizeof(std::string_view))
            );
            if (this->kwnames == nullptr) throw std::bad_alloc();
            for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
                PyObject* str = PyTuple_GET_ITEM(kwnames, i);
                Py_ssize_t len;
                new (this->kwnames + i) std::string_view{
                    PyUnicode_AsUTF8AndSize(str, &len),
                    static_cast<size_t>(len)
                };
            }

        }
    }

    /* Free the memory allocated for the keyword argument names. */
    ~PyArgs() noexcept {
        if (kwnames != nullptr) free(kwnames);  // no need to call destructors
    }

};


//////////////////////////////////
////    CONVERSION HELPERS    ////
//////////////////////////////////


/* Convert Python None into C++ nullptr. */
inline static PyObject* none_to_null(PyObject* obj) {
    return obj == Py_None ? nullptr : obj;
}


/* Check if a Python object is truthy. */
inline static bool is_truthy(PyObject* obj) {
    int result = PyObject_IsTrue(obj);
    if (result == -1) throw util::catch_python();
    return static_cast<bool>(result);
}


/* Convert a python integer into a long long index. */
inline static long long parse_int(PyObject* obj) {
    PyObject* integer = PyNumber_Index(obj);
    if (integer == nullptr) throw util::catch_python();
    long long result = PyLong_AsLongLong(integer);
    Py_DECREF(integer);
    if (result == -1 && PyErr_Occurred()) throw util::catch_python();
    return result;
}


/* Convert a python integer into an optional long long index. */
inline static std::optional<long long> parse_opt_int(PyObject* obj) {
    return obj == Py_None ? std::nullopt : std::make_optional(parse_int(obj));
}


} // namespace util
} // namespace structs
} // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_ARGS_H
