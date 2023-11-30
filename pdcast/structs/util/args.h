// include guard: BERTRAND_STRUCTS_UTIL_ARGS_H
#ifndef BERTRAND_STRUCTS_UTIL_ARGS_H
#define BERTRAND_STRUCTS_UTIL_ARGS_H

#include <cstdlib>  // std::malloc, std::free
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <string_view>  // std::string_view
#include <unordered_set>  // std::unordered_set
#include <Python.h>  // CPython API
#include "except.h"  // TypeError
#include "func.h"  // FuncTraits
#include "iter.h"  // iter()


namespace bertrand {
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


/* A utility class that allows for efficient parsing of Python arguments passed to an
extension method. */
template <CallProtocol>
struct PyArgs;


/* A parser for C methods implementing the METH_VARARGS protocol. */
template <>
struct PyArgs<CallProtocol::ARGS> {
    const std::string_view& name;
    PyObject* args;
    const Py_ssize_t n_args;
    Py_ssize_t arg_idx;

    /* Create a parser for the given arguments. */
    PyArgs(const std::string_view& name, PyObject* args) :
        name(name), args(args), n_args(PyTuple_GET_SIZE(args)), arg_idx(0)
    {}

    #define PARSE_POSITIONAL \
        /* check for positional argument */ \
        if (arg_idx < n_args) { \
            PyObject* val = PyTuple_GET_ITEM(args, arg_idx++); \
            if constexpr (std::is_same_v<ReturnType, PyObject*>) { \
                return val; \
            } else { \
                return convert(val); \
            } \
        } \

    /* Extract an argument from the pool using an optional conversion function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType parse(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        PARSE_POSITIONAL

        // no matching argument
        std::ostringstream msg;
        msg << this->name << "() missing required positional argument: '" << name;
        msg << "'";
        throw std::runtime_error(msg.str());
    }

    /* Extract an argument from the pool using an optional conversion function and
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
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );
        PARSE_POSITIONAL

        // no matching argument
        return default_value;
    }

    /* finalize the argument list, throwing an error if any unexpected positional
    arguments were supplied. */
    inline void finalize() {
        if (arg_idx < n_args) {
            std::ostringstream msg;
            msg << name << "() takes " << arg_idx << " positional ";
            msg << (arg_idx == 1 ? "argument" : "arguments") << " but ";
            msg << n_args << (n_args == 1 ? " was" : " were") << " given";
            throw TypeError(msg.str());
        }
    }

    #undef PARSE_POSITIONAL

};


/* A parser for C methods implementing the METH_VARARGS | METH_KEYWORDS protocol. */
template <>
struct PyArgs<CallProtocol::KWARGS> {
    const std::string_view& name;
    PyObject* args;
    PyObject* kwargs;
    std::unordered_set<std::string_view> found;
    const Py_ssize_t n_args;
    const Py_ssize_t n_kwargs;
    Py_ssize_t arg_idx;

    /* Create a parser for the given arguments. */
    PyArgs(const std::string_view& name, PyObject* args, PyObject* kwargs) :
        name(name), args(args), kwargs(kwargs), n_args(PyTuple_GET_SIZE(args)),
        n_kwargs(kwargs == nullptr ? 0 : PyDict_Size(kwargs)), arg_idx(0)
    {}

    //////////////////////////
    ////    POSITIONAL    ////
    //////////////////////////

    #define PARSE_POSITIONAL \
        /* check for positional argument */ \
        if (arg_idx < n_args) { \
            PyObject* val = PyTuple_GET_ITEM(args, arg_idx++); \
            if constexpr (std::is_same_v<ReturnType, PyObject*>) { \
                return val; \
            } else { \
                return convert(val); \
            } \
        } \

    #define PARSE_KEYWORD \
        /* check for keyword argument */ \
        if (kwargs != nullptr) { \
            PyObject* val = PyDict_GetItemString(kwargs, name.data()); \
            if (val != nullptr) { \
                found.insert(name); \
                if constexpr (std::is_same_v<ReturnType, PyObject*>) { \
                    return val; \
                } else { \
                    return convert(val); \
                } \
            } \
        } \

    /* Extract a positional argument from the pool using an optional conversion
    function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType positional(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        PARSE_POSITIONAL

        // no matching argument
        std::ostringstream msg;
        msg << this->name << "() missing required positional argument: '" << name;
        msg << "'";
        throw std::runtime_error(msg.str());
    }

    /* Extract a positional argument from the pool using an optional conversion
    function and default value. */
    template <
        typename Func = PyObject*,
        typename Type = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType positional(
        const std::string_view& name,
        Func convert,
        Type default_value
    ) {
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );
        PARSE_POSITIONAL

        // no matching argument
        return default_value;
    }

    /* Finalize the positional arguments to the function. */
    inline void finalize_positional() {
        if (arg_idx < n_args) {
            std::ostringstream msg;
            msg << name << "() takes " << arg_idx << " positional ";
            msg << (arg_idx == 1 ? "argument" : "arguments") << " but ";
            msg << n_args << (n_args == 1 ? " was" : " were") << " given";
            throw TypeError(msg.str());
        }
    }

    ///////////////////////
    ////    KEYWORD    ////
    ///////////////////////

    /* Extract a keyword argument from the pool using an optional conversion
    function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType keyword(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        PARSE_KEYWORD

        // no matching argument
        std::ostringstream msg;
        msg << this->name << "() missing required keyword-only argument: '" << name;
        msg << "'";
        throw std::runtime_error(msg.str());
    }

    /* Extract a keyword argument from the pool using an optional conversion
    function and default value. */
    template <
        typename Func = PyObject*,
        typename Type = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType keyword(
        const std::string_view& name,
        Func convert,
        Type default_value
    ) {
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );
        PARSE_KEYWORD

        // no matching argument
        return default_value;
    }

    /* Finalize the keyword arguments to the function. */
    inline void finalize_keyword() {
        if (static_cast<Py_ssize_t>(found.size()) < n_kwargs) {
            for (PyObject* key : iter(kwargs)) {
                std::string_view keyword{PyUnicode_AsUTF8(key)};
                if (found.find(keyword) == found.end()) {
                    std::ostringstream msg;
                    msg << name << "() got an unexpected keyword argument: '";
                    msg << keyword << "'";
                    throw TypeError(msg.str());
                }
            }
        }
    }

    //////////////////////
    ////    EITHER    ////
    //////////////////////

    /* Extract an argument from the pool using an optional conversion function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType parse(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        PARSE_KEYWORD
        PARSE_POSITIONAL

        // no matching argument
        std::ostringstream msg;
        msg << this->name << "() missing required argument: '" << name << "'";
        throw std::runtime_error(msg.str());
    }

    /* Extract an argument from the pool using an optional conversion function and
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
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );
        PARSE_KEYWORD
        PARSE_POSITIONAL

        // no matching argument
        return default_value;
    }

    /* Finalize the argument list, throwing an error if any unexpected arguments are
    present. */
    inline void finalize() {
        finalize_positional();
        finalize_keyword();
    }

    #undef PARSE_POSITIONAL
    #undef PARSE_KEYWORD

};


/* A parser for C methods implementing the METH_FASTCALL protocol. */
template <>
struct PyArgs<CallProtocol::FASTCALL> {
    const std::string_view& name;
    PyObject* const* args;
    const Py_ssize_t n_args;
    Py_ssize_t arg_idx;

    /* Create a parser for the given arguments. */
    PyArgs(const std::string_view& name, PyObject* const* args, Py_ssize_t nargs) :
        name(name), args(args), n_args(nargs), arg_idx(0)
    {}

    #define PARSE_POSITIONAL \
        /* check for positional argument */ \
        if (arg_idx < n_args) { \
            PyObject* val = args[arg_idx++]; \
            if constexpr (std::is_same_v<ReturnType, PyObject*>) { \
                return val; \
            } else { \
                return convert(val); \
            } \
        } \

    /* Extract an argument from the pool using an optional conversion function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType parse(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        PARSE_POSITIONAL

        // no matching argument
        std::ostringstream msg;
        msg << this->name << "() missing required positional argument: '" << name;
        msg << "'";
        throw std::runtime_error(msg.str());
    }

    /* Extract an argument from the pool using an optional conversion function and
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
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );
        PARSE_POSITIONAL

        // no matching argument
        return default_value;
    }

    /* Finalize the argument list, throwing an error if any unexpected positional
    arguments are present. */
    inline void finalize() {
        if (arg_idx < n_args) {
            std::ostringstream msg;
            msg << name << "() takes " << arg_idx << " positional ";
            msg << (arg_idx == 1 ? "argument" : "arguments") << " but ";
            msg << n_args << (n_args == 1 ? " was" : " were") << " given";
            throw TypeError(msg.str());
        }
    }

    #undef PARSE_POSITIONAL

};


/* A parser for C methods implementing the METH_FASTCALL | METH_KEYWORDS protocol. */
template <>
struct PyArgs<CallProtocol::VECTORCALL> {
    const std::string_view& name;
    PyObject* const* args;
    std::string_view* kwnames;
    bool* found;
    const Py_ssize_t n_args;
    const Py_ssize_t n_kwargs;
    Py_ssize_t arg_idx;
    Py_ssize_t kwarg_idx;

    /* Create a parser for the given arguments. */
    PyArgs(
        const std::string_view& name,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) : name(name), args(args), kwnames(nullptr), found(nullptr), n_args(nargs),
        n_kwargs(kwnames == nullptr ? 0 : PyTuple_GET_SIZE(kwnames)), arg_idx(0),
        kwarg_idx(0)
    {
        if (n_kwargs != 0) {
            // unpack keyword arguments into array of string_views
            this->kwnames = static_cast<std::string_view*>(
                std::malloc(n_kwargs * sizeof(std::string_view))
            );
            if (this->kwnames == nullptr) throw std::bad_alloc();
            this->found = static_cast<bool*>(
                std::malloc(n_kwargs * sizeof(bool))
            );
            if (this->found == nullptr) throw std::bad_alloc();
            for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
                this->found[i] = false;
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
        if (kwnames != nullptr) {
            std::free(kwnames);  // no need to call destructors
            std::free(found);
        }
    }

    //////////////////////////
    ////    POSITIONAL    ////
    //////////////////////////

    #define PARSE_POSITIONAL \
        /* check for positional argument */ \
        if (arg_idx < n_args) { \
            PyObject* val = args[arg_idx++]; \
            if constexpr (std::is_same_v<ReturnType, PyObject*>) { \
                return val; \
            } else { \
                return convert(val); \
            } \
        } \

    #define PARSE_KEYWORD \
        /* check for keyword argument */ \
        for (Py_ssize_t i = 0; i < n_kwargs; ++i) { \
            if (kwnames[i] == name) { \
                found[i] = true; \
                ++kwarg_idx; \
                PyObject* val = args[n_args + i]; \
                if constexpr (std::is_same_v<ReturnType, PyObject*>) { \
                    return val; \
                } else { \
                    return convert(val); \
                } \
            } \
        } \

    /* Extract a positional argument from the pool using an optional conversion
    function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType positional(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        PARSE_POSITIONAL

        // no matching argument
        std::ostringstream msg;
        msg << this->name << "() missing required positional argument: '" << name;
        msg << "'";
        throw std::runtime_error(msg.str());
    }

    /* Extract a positional argument from the pool using an optional conversion
    function and default value. */
    template <
        typename Func = PyObject*,
        typename Type = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType positional(
        const std::string_view& name,
        Func convert,
        Type default_value
    ) {
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );
        PARSE_POSITIONAL

        // no matching argument
        return default_value;
    }

    /* Finalize the positional arguments to the function. */
    inline void finalize_positional() {
        if (arg_idx < n_args) {
            std::ostringstream msg;
            msg << name << "() takes " << arg_idx << " positional ";
            msg << (arg_idx == 1 ? "argument" : "arguments") << " but ";
            msg << n_args << (n_args == 1 ? " was" : " were") << " given";
            throw TypeError(msg.str());
        }
    }

    ///////////////////////
    ////    KEYWORD    ////
    ///////////////////////

    /* Extract a keyword argument from the pool using an optional conversion
    function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType keyword(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        PARSE_KEYWORD

        // no matching argument
        std::ostringstream msg;
        msg << this->name << "() missing required keyword argument: '" << name;
        msg << "'";
        throw std::runtime_error(msg.str());
    }

    /* Extract a keyword argument from the pool using an optional conversion
    function and default value. */
    template <
        typename Func = PyObject*,
        typename Type = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType keyword(
        const std::string_view& name,
        Func convert,
        Type default_value
    ) {
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );
        PARSE_KEYWORD

        // no matching argument
        return default_value;
    }

    /* Finalize the keyword arguments to the function. */
    inline void finalize_keyword() {
        if (kwarg_idx < n_kwargs) {
            for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
                if (!found[i]) {
                    std::ostringstream msg;
                    msg << name << "() got an unexpected keyword argument: '";
                    msg << kwnames[i] << "'";
                    throw TypeError(msg.str());
                }
            }
        }
    }

    //////////////////////
    ////    EITHER    ////
    //////////////////////

    /* Extract an argument from the pool using an optional conversion function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType parse(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        PARSE_KEYWORD
        PARSE_POSITIONAL

        // no matching argument
        std::ostringstream msg;
        msg << this->name << "() missing required argument: '" << name << "'";
        throw std::runtime_error(msg.str());
    }

    /* Extract an argument from the pool using an optional conversion function and
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
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );
        PARSE_KEYWORD
        PARSE_POSITIONAL

        // no matching argument
        return default_value;
    }

    /* Finalize the argument list, throwing an error if any unexpected arguments are
    present. */
    inline void finalize() {
        finalize_positional();
        finalize_keyword();
    }

    #undef PARSE_POSITIONAL
    #undef PARSE_KEYWORD

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
    if (result == -1) throw catch_python();
    return static_cast<bool>(result);
}


/* Convert a python integer into a long long index. */
inline static long long parse_int(PyObject* obj) {
    PyObject* integer = PyNumber_Index(obj);
    if (integer == nullptr) throw catch_python();
    long long result = PyLong_AsLongLong(integer);
    Py_DECREF(integer);
    if (result == -1 && PyErr_Occurred()) throw catch_python();
    return result;
}


/* Convert a python integer into an optional long long index. */
inline static std::optional<long long> parse_opt_int(PyObject* obj) {
    return obj == Py_None ? std::nullopt : std::make_optional(parse_int(obj));
}


} // namespace util
} // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_ARGS_H
