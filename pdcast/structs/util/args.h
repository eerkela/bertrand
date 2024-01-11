#ifndef BERTRAND_STRUCTS_UTIL_ARGS_H
#define BERTRAND_STRUCTS_UTIL_ARGS_H

#include <cstdlib>  // std::malloc, std::free
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <string_view>  // std::string_view
#include <unordered_set>  // std::unordered_set
#include <vector>  // std::vector
#include <Python.h>  // CPython API
#include "except.h"  // TypeError
#include "func.h"  // FuncTraits
#include "iter.h"  // iter()


namespace bertrand {
namespace util {


inline TypeError missing_positional(
    const std::string_view& func_name,
    const std::string_view& arg_name
) {
    std::ostringstream msg;
    msg << func_name << "() missing required positional argument: '" << arg_name;
    msg << "'";
    throw TypeError(msg.str());
}


inline TypeError missing_keyword(
    const std::string_view& func_name,
    const std::string_view& arg_name
) {
    std::ostringstream msg;
    msg << func_name << "() missing required keyword-only argument: '" << arg_name;
    msg << "'";
    throw TypeError(msg.str());
}


inline TypeError missing_argument(
    const std::string_view& func_name,
    const std::string_view& arg_name
) {
    std::ostringstream msg;
    msg << func_name << "() missing required argument: '" << arg_name << "'";
    throw TypeError(msg.str());
}


inline TypeError extra_positional(
    const std::string_view& func_name,
    Py_ssize_t expected,
    Py_ssize_t observed
) {
    std::ostringstream msg;
    msg << func_name << "() takes " << expected << " positional ";
    msg << (expected == 1 ? "argument" : "arguments") << " but " << observed;
    msg << (observed == 1 ? " was" : " were") << " given";
    throw TypeError(msg.str());
}


inline TypeError extra_keyword(
    const std::string_view& func_name,
    const std::string_view& keyword
) {
    std::ostringstream msg;
    msg << func_name << "() got an unexpected keyword argument: '" << keyword << "'";
    throw TypeError(msg.str());
}


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

    PyArgs(const std::string_view& name, PyObject* args) :
        name(name), args(args), n_args(PyTuple_GET_SIZE(args)), arg_idx(0)
    {}

    inline PyObject* parse(const std::string_view& name) {
        if (arg_idx < n_args) {
            return PyTuple_GET_ITEM(args, arg_idx++);
        }
        throw missing_positional(this->name, name);
    }

    template <typename Func>
    inline auto parse(const std::string_view& name, Func convert)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        if (arg_idx < n_args) {
            if constexpr (std::is_same_v<Func, identity>) {
                return PyTuple_GET_ITEM(args, arg_idx++);
            } else {
                return convert(PyTuple_GET_ITEM(args, arg_idx++));
            }
        }
        throw missing_positional(this->name, name);
    }

    template <typename Func, typename Default>
    inline auto parse(const std::string_view& name, Func convert, Default default_value)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        using ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType;
        static_assert(
            std::is_convertible_v<Default, ReturnType>,
            "Conversion function must return same type as default value."
        );
        if (arg_idx < n_args) {
            if constexpr (std::is_same_v<Func, identity>) {
                return PyTuple_GET_ITEM(args, arg_idx++);
            } else {
                return convert(PyTuple_GET_ITEM(args, arg_idx++));
            }
        }
        return default_value;
    }

    inline void finalize() {
        if (arg_idx < n_args) {
            throw extra_positional(this->name, arg_idx, n_args);
        }
    }

};


/* A parser for C methods implementing the METH_VARARGS | METH_KEYWORDS protocol. */
template <>
struct PyArgs<CallProtocol::KWARGS> {
    const std::string_view& name;
    PyObject* args;
    PyObject* kwargs;
    std::vector<std::string_view> found;
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
        if (arg_idx < n_args) { \
                if constexpr (std::is_same_v<Func, identity>) { \
                    return PyTuple_GET_ITEM(args, arg_idx++); \
                } else { \
                    return convert(PyTuple_GET_ITEM(args, arg_idx++)); \
                } \
            } \

    inline PyObject* positional(const std::string_view& name) {
        if (arg_idx < n_args) {
            return PyTuple_GET_ITEM(args, arg_idx++);
        }
        throw missing_positional(this->name, name);
    }

    template <typename Func>
    inline auto positional(const std::string_view& name, Func convert)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        PARSE_POSITIONAL
        throw missing_positional(this->name, name);
    }

    template <typename Func, typename Default>
    inline auto positional(const std::string_view& name, Func convert, Default default_value)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        using ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType;
        static_assert(
            std::is_convertible_v<Default, ReturnType>,
            "Conversion function must return same type as default value."
        );
        PARSE_POSITIONAL
        return default_value;
    }

    inline void finalize_positional() {
        if (arg_idx < n_args) {
            throw extra_positional(this->name, arg_idx, n_args);
        }
    }

    ///////////////////////
    ////    KEYWORD    ////
    ///////////////////////

    #define PARSE_KEYWORD \
        if (kwargs != nullptr) { \
            if (n_kwargs < 5) { \
                Py_ssize_t pos = 0; \
                PyObject* key; \
                PyObject* val; \
                while (PyDict_Next(kwargs, &pos, &key, &val)) { \
                    Py_ssize_t len; \
                    std::string_view keyword{ \
                        PyUnicode_AsUTF8AndSize(key, &len), \
                        static_cast<size_t>(len) \
                    }; \
                    if (keyword == name) { \
                        found.push_back(keyword); \
                        if constexpr (std::is_same_v<Func, identity>) { \
                            return val; \
                        } else { \
                            return convert(val); \
                        } \
                    } \
                } \
            } else { \
                PyObject* val = PyDict_GetItemString(kwargs, name.data()); \
                if (val != nullptr) { \
                    found.push_back(name); \
                    if constexpr (std::is_same_v<Func, identity>) { \
                        return val; \
                    } else { \
                        return convert(val); \
                    } \
                } \
            } \
        } \

    PyObject* keyword(const std::string_view& name) {
        if (kwargs != nullptr) {
            if (n_kwargs < 5) {
                Py_ssize_t pos = 0;
                PyObject* key;
                PyObject* val;
                while (PyDict_Next(kwargs, &pos, &key, &val)) {
                    Py_ssize_t len;
                    std::string_view keyword{
                        PyUnicode_AsUTF8AndSize(key, &len),
                        static_cast<size_t>(len)
                    };
                    if (keyword == name) {
                        found.push_back(keyword);
                        return val;
                    }
                }
            } else {
                PyObject* val = PyDict_GetItemString(kwargs, name.data());
                if (val != nullptr) {
                    found.push_back(name);
                    return val;
                }
            }
        }
        throw missing_keyword(this->name, name);
    }

    template <typename Func>
    auto keyword(const std::string_view& name, Func convert)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        PARSE_KEYWORD
        throw missing_keyword(this->name, name);
    }

    template <typename Func, typename Default>
    auto keyword(const std::string_view& name, Func convert, Default default_value)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        using ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType;
        static_assert(
            std::is_convertible_v<Default, ReturnType>,
            "Conversion function must return same type as default value."
        );
        PARSE_KEYWORD
        return default_value;
    }

    void finalize_keyword() {
        Py_ssize_t observed = static_cast<Py_ssize_t>(found.size());
        if (observed < n_kwargs) {
            auto was_found = [&](const std::string_view& keyword) {
                for (const std::string_view& name : found) {
                    if (keyword == name) {
                        return true;
                    }
                }
                return false;
            };
            for (PyObject* key : iter(kwargs)) {
                Py_ssize_t len;
                std::string_view keyword{
                    PyUnicode_AsUTF8AndSize(key, &len),
                    static_cast<size_t>(len)
                };
                if (!was_found(keyword)) {
                    throw extra_keyword(this->name, keyword);
                }
            }
        }
    }

    //////////////////////
    ////    EITHER    ////
    //////////////////////

    PyObject* parse(const std::string_view& name) {
        if (kwargs != nullptr) {
            if (n_kwargs < 5) {
                Py_ssize_t pos = 0;
                PyObject* key;
                PyObject* val;
                while (PyDict_Next(kwargs, &pos, &key, &val)) {
                    Py_ssize_t len;
                    std::string_view keyword{
                        PyUnicode_AsUTF8AndSize(key, &len),
                        static_cast<size_t>(len)
                    };
                    if (keyword == name) {
                        found.push_back(keyword);
                        return val;
                    }
                }
            } else {
                PyObject* val = PyDict_GetItemString(kwargs, name.data());
                if (val != nullptr) {
                    found.push_back(name);
                    return val;
                }
            }
        }
        if (arg_idx < n_args) {
            return PyTuple_GET_ITEM(args, arg_idx++);
        }
        throw missing_argument(this->name, name);
    }

    template <typename Func>
    auto parse(const std::string_view& name, Func convert)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        PARSE_KEYWORD
        PARSE_POSITIONAL
        throw missing_argument(this->name, name);
    }

    template <typename Func, typename Default>
    auto parse(const std::string_view& name, Func convert, Default default_value)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        using ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType;
        static_assert(
            std::is_convertible_v<Default, ReturnType>,
            "Conversion function must return same type as default value."
        );
        PARSE_KEYWORD
        PARSE_POSITIONAL
        return default_value;
    }

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

    PyArgs(const std::string_view& name, PyObject* const* args, Py_ssize_t nargs) :
        name(name), args(args), n_args(nargs), arg_idx(0)
    {}

    inline PyObject* parse(const std::string_view& name) {
        if (arg_idx < n_args) {
            return args[arg_idx++];
        }
        throw missing_positional(this->name, name);
    }

    template <typename Func>
    inline auto parse(const std::string_view& name, Func convert)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        if (arg_idx < n_args) {
            if constexpr (std::is_same_v<Func, identity>) {
                return args[arg_idx++];
            } else {
                return convert(args[arg_idx++]);
            }
        }
        throw missing_positional(this->name, name);
    }

    template <typename Func, typename Default>
    inline auto parse(const std::string_view& name, Func convert, Default default_value)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        using ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType;
        static_assert(
            std::is_convertible_v<Default, ReturnType>,
            "Conversion function must return same type as default value."
        );
        if (arg_idx < n_args) {
            if constexpr (std::is_same_v<Func, identity>) {
                return args[arg_idx++];
            } else {
                return convert(args[arg_idx++]);
            }
        }
        return default_value;
    }

    inline void finalize() {
        if (arg_idx < n_args) {
            throw extra_positional(this->name, arg_idx, n_args);
        }
    }

};


/* A parser for C methods implementing the METH_FASTCALL | METH_KEYWORDS protocol. */
template <>
struct PyArgs<CallProtocol::VECTORCALL> {

    struct Keyword {
        std::string_view name;
        bool found;
    };

    const std::string_view& name;
    PyObject* const* args;
    Keyword* kwnames;
    const Py_ssize_t n_args;
    const Py_ssize_t n_kwargs;
    Py_ssize_t arg_idx;
    Py_ssize_t kwarg_idx;

    PyArgs(
        const std::string_view& name,
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) : name(name), args(args), kwnames(nullptr), n_args(nargs),
        n_kwargs(kwnames == nullptr ? 0 : PyTuple_GET_SIZE(kwnames)), arg_idx(0),
        kwarg_idx(0)
    {
        if (n_kwargs != 0) {
            this->kwnames = static_cast<Keyword*>(
                std::malloc(n_kwargs * sizeof(Keyword))
            );
            if (this->kwnames == nullptr) {
                throw std::bad_alloc();
            }
            for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
                Keyword& keyword = this->kwnames[i];
                keyword.found = false;
                Py_ssize_t len;
                PyObject* str = PyTuple_GET_ITEM(kwnames, i);
                new (&keyword.name) std::string_view{
                    PyUnicode_AsUTF8AndSize(str, &len),
                    static_cast<size_t>(len)
                };
            }
        }
    }

    ~PyArgs() noexcept {
        if (kwnames != nullptr) {
            free(kwnames);
        }
    }

    //////////////////////////
    ////    POSITIONAL    ////
    //////////////////////////

    #define PARSE_POSITIONAL \
        if (arg_idx < n_args) { \
            if constexpr (std::is_same_v<Func, identity>) { \
                return args[arg_idx++]; \
            } else { \
                return convert(args[arg_idx++]); \
            } \
        } \

    inline PyObject* positional(const std::string_view& name) {
        if (arg_idx < n_args) {
            return args[arg_idx++];
        }
        throw missing_positional(this->name, name);
    }

    template <typename Func>
    inline auto positional(const std::string_view& name, Func convert)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        PARSE_POSITIONAL
        throw missing_positional(this->name, name);
    }

    template <typename Func, typename Default>
    inline auto positional(const std::string_view& name, Func convert, Default default_value)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        using ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType;
        static_assert(
            std::is_convertible_v<Default, ReturnType>,
            "Conversion function must return same type as default value."
        );
        PARSE_POSITIONAL
        return default_value;
    }

    inline void finalize_positional() {
        if (arg_idx < n_args) {
            throw extra_positional(this->name, arg_idx, n_args);
        }
    }

    ///////////////////////
    ////    KEYWORD    ////
    ///////////////////////

    #define PARSE_KEYWORD \
        for (Py_ssize_t i = 0; i < n_kwargs; ++i) { \
            Keyword& keyword = kwnames[i]; \
            if (!keyword.found && keyword.name == name) { \
                keyword.found = true; \
                ++kwarg_idx; \
                if constexpr (std::is_same_v<Func, identity>) { \
                    return args[n_args + i]; \
                } else { \
                    return convert(args[n_args + i]); \
                } \
            } \
        } \

    PyObject* keyword(const std::string_view& name) {
        for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
            Keyword& keyword = kwnames[i];
            if (!keyword.found && keyword.name == name) {
                keyword.found = true;
                ++kwarg_idx;
                return args[n_args + i];
            }
        }
        throw missing_keyword(this->name, name);
    }

    template <typename Func>
    auto keyword(const std::string_view& name, Func convert)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        PARSE_KEYWORD
        throw missing_keyword(this->name, name);
    }

    template <typename Func, typename Default>
    auto keyword(const std::string_view& name, Func convert, Default default_value)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        using ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType;
        static_assert(
            std::is_convertible_v<Default, ReturnType>,
            "Conversion function must return same type as default value."
        );
        PARSE_KEYWORD
        return default_value;
    }

    void finalize_keyword() {
        if (kwarg_idx < n_kwargs) {
            for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
                Keyword& keyword = kwnames[i];
                if (!keyword.found) {
                    throw extra_keyword(this->name, keyword.name);
                }
            }
        }
    }

    //////////////////////
    ////    EITHER    ////
    //////////////////////

    PyObject* parse(const std::string_view& name) {
        for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
            Keyword& keyword = kwnames[i];
            if (!keyword.found && keyword.name == name) {
                keyword.found = true;
                ++kwarg_idx;
                return args[n_args + i];
            }
        }
        if (arg_idx < n_args) {
            return args[arg_idx++];
        }
        throw missing_argument(this->name, name);
    }

    template <typename Func>
    auto parse(const std::string_view& name, Func convert)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        PARSE_KEYWORD
        PARSE_POSITIONAL
        throw missing_argument(this->name, name);
    }

    template <typename Func, typename Default>
    auto parse(const std::string_view& name, Func convert, Default default_value)
        -> remove_rvalue_t<typename FuncTraits<Func, PyObject*>::ReturnType>
    {
        using ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType;
        static_assert(
            std::is_convertible_v<Default, ReturnType>,
            "Conversion function must return same type as default value."
        );
        PARSE_KEYWORD
        PARSE_POSITIONAL
        return default_value;
    }

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
    return Py_IsNone(obj) ? nullptr : obj;
}


/* Check if a Python object is truthy. */
inline static bool is_truthy(PyObject* obj) {
    int result = PyObject_IsTrue(obj);
    if (result == -1) {
        throw catch_python();
    }
    return static_cast<bool>(result);
}


/* Convert a python integer into a long long index. */
inline static long long parse_int(PyObject* obj) {
    PyObject* integer = PyNumber_Index(obj);
    if (integer == nullptr) {
        throw catch_python();
    }
    long long result = PyLong_AsLongLong(integer);
    Py_DECREF(integer);
    if (result == -1 && PyErr_Occurred()) {
        throw catch_python();
    }
    return result;
}


/* Convert a python integer into an optional long long index. */
inline static std::optional<long long> parse_opt_int(PyObject* obj) {
    return obj == Py_None ? std::nullopt : std::make_optional(parse_int(obj));
}


} // namespace util


using util::CallProtocol;
using util::PyArgs;


} // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_ARGS_H
