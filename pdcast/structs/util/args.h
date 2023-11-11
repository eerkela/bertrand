// include guard: BERTRAND_STRUCTS_UTIL_ARGS_H
#ifndef BERTRAND_STRUCTS_UTIL_ARGS_H
#define BERTRAND_STRUCTS_UTIL_ARGS_H

#include <sstream>  // std::ostringstream
#include <string_view>  // std::string_view
#include <unordered_map>  // std::unordered_map
#include <vector>
#include <Python.h>  // CPython API
#include "func.h"  // FuncTraits


namespace bertrand {
namespace structs {
namespace util {


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
class PyArgs;


/* A parser for C methods implementing the METH_VARARGS protocol. */
template <>
class PyArgs<CallProtocol::ARGS> {
    const Py_ssize_t n_args;
    PyObject* args;
    Py_ssize_t idx;

public:

    /* Copy/move constructors/assignment operators deleted for simplicity. */
    PyArgs(const PyArgs&) = delete;
    PyArgs(PyArgs&&) = delete;
    PyArgs& operator=(const PyArgs&) = delete;
    PyArgs& operator=(PyArgs&&) = delete;

    /* Create a parser for the given arguments. */
    PyArgs(PyObject* args) : n_args(PyTuple_GET_SIZE(args)), args(args), idx(0) {}

    /* Extract an argument from the pool, using an optional conversion function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType parse(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        static constexpr bool pyobject = std::is_same_v<ReturnType, PyObject*>;

        // check for positional argument
        if (idx < n_args) {
            if constexpr (pyobject) {
                return PyTuple_GET_ITEM(args, idx++);
            } else {
                return convert(PyTuple_GET_ITEM(args, idx++));
            }
        }

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
        static constexpr bool pyobject = std::is_same_v<Type, PyObject*>;
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );

        // check for positional argument
        if (idx < n_args) {
            if constexpr (pyobject) {
                return PyTuple_GET_ITEM(args, idx++);
            } else {
                return convert(PyTuple_GET_ITEM(args, idx++));
            }
        }

        return default_value;
    }

};


/* A parser for C methods implementing the METH_VARARGS | METH_KEYWORDS protocol. */
template <>
class PyArgs<CallProtocol::KWARGS> {
    const Py_ssize_t n_kwargs;
    const Py_ssize_t n_args;
    PyObject* args;
    PyObject* kwargs;
    Py_ssize_t idx;

public:

    /* Copy/move constructors/assignment operators deleted for simplicity. */
    PyArgs(const PyArgs&) = delete;
    PyArgs(PyArgs&&) = delete;
    PyArgs& operator=(const PyArgs&) = delete;
    PyArgs& operator=(PyArgs&&) = delete;

    /* Create a parser for the given arguments. */
    PyArgs(PyObject* args, PyObject* kwargs) :
        n_kwargs(kwargs == nullptr ? 0 : PyDict_Size(kwargs)),
        n_args(PyTuple_GET_SIZE(args)),
        args(args),
        kwargs(kwargs),
        idx(0)
    {}

    /* Extract an argument from the pool, using an optional conversion function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType parse(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        static constexpr bool pyobject = std::is_same_v<ReturnType, PyObject*>;

        // check for positional argument
        if (idx < n_args) {
            if constexpr (pyobject) {
                return PyTuple_GET_ITEM(args, idx++);
            } else {
                return convert(PyTuple_GET_ITEM(args, idx++));
            }
        }

        // check for keyword argument
        if (kwargs != nullptr) {
            PyObject* item = PyDict_GetItemString(kwargs, name.data());
            if (item != nullptr) {
                if constexpr (pyobject) {
                    return item;
                } else {
                    return convert(item);
                }
            }
        }

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
        static constexpr bool pyobject = std::is_same_v<Type, PyObject*>;
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );

        // check for positional argument
        if (idx < n_args) {
            if constexpr (pyobject) {
                return PyTuple_GET_ITEM(args, idx++);
            } else {
                return convert(PyTuple_GET_ITEM(args, idx++));
            }
        }

        // check for keyword argument
        if (kwargs != nullptr) {
            PyObject* item = PyDict_GetItemString(kwargs, name.data());
            if (item != nullptr) {
                if constexpr (pyobject) {
                    return item;
                } else {
                    return convert(item);
                }
            }
        }

        return default_value;
    }

};


/* A parser for C methods implementing the METH_FASTCALL protocol. */
template <>
class PyArgs<CallProtocol::FASTCALL> {
    const Py_ssize_t n_args;
    PyObject* const* args;
    Py_ssize_t idx;

public:

    /* Copy/move constructors/assignment operators deleted for simplicity. */
    PyArgs(const PyArgs&) = delete;
    PyArgs(PyArgs&&) = delete;
    PyArgs& operator=(const PyArgs&) = delete;
    PyArgs& operator=(PyArgs&&) = delete;

    /* Create a parser for the given arguments. */
    PyArgs(PyObject* const* args, Py_ssize_t nargs) :
        n_args(nargs), args(args), idx(0)
    {}

    /* Extract an argument from the pool, using an optional conversion function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType parse(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        static constexpr bool pyobject = std::is_same_v<ReturnType, PyObject*>;

        // check for positional argument
        if (idx < n_args) {
            if constexpr (pyobject) {
                return args[idx++];
            } else {
                return convert(args[idx++]);
            }
        }

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
        static constexpr bool pyobject = std::is_same_v<Type, PyObject*>;
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );

        // check for positional argument
        if (idx < n_args) {
            if constexpr (pyobject) {
                return args[idx++];
            } else {
                return convert(args[idx++]);
            }
        }

        return default_value;
    }

};


/* A parser for C methods implementing the METH_FASTCALL | METH_KEYWORDS protocol. */
template <>
class PyArgs<CallProtocol::VECTORCALL> {
    const Py_ssize_t n_kwargs;
    const Py_ssize_t n_args;
    PyObject* const* args;
    std::string_view* kwnames;
    Py_ssize_t idx;

public:

    /* Copy/move constructors/assignment operators deleted for simplicity. */
    PyArgs(const PyArgs&) = delete;
    PyArgs(PyArgs&&) = delete;
    PyArgs& operator=(const PyArgs&) = delete;
    PyArgs& operator=(PyArgs&&) = delete;

    /* Create a parser for the given arguments. */
    PyArgs(PyObject* const* args, Py_ssize_t nargs, PyObject* kwnames) :
        n_kwargs(kwnames == nullptr ? 0 : PyTuple_GET_SIZE(kwnames)),
        n_args(nargs - n_kwargs),
        args(args),
        idx(0)
    {
        if (n_kwargs == 0) {
            this->kwnames = nullptr;
        } else {
            this->kwnames = static_cast<std::string_view*>(
                malloc(n_kwargs * sizeof(std::string_view))
            );
            if (this->kwnames == nullptr) throw std::bad_alloc();
            for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
                Py_ssize_t len;
                PyObject* str = PyTuple_GET_ITEM(kwnames, i);
                new (this->kwnames + i) std::string_view{
                    PyUnicode_AsUTF8AndSize(str, &len),
                    static_cast<size_t>(len)
                };
            }
        }
    }

    /* Free the memory allocated for the keyword argument names. */
    ~PyArgs() noexcept { free(kwnames); }  // no need to call destructor

    /* Extract an argument from the pool, using an optional conversion function. */
    template <
        typename Func = PyObject*,
        typename ReturnType = typename FuncTraits<Func, PyObject*>::ReturnType
    >
    ReturnType parse(
        const std::string_view& name,
        Func convert = nullptr
    ) {
        static constexpr bool pyobject = std::is_same_v<ReturnType, PyObject*>;

        // check for positional argument
        if (idx < n_args) {
            if constexpr (pyobject) {
                return args[idx++];
            } else {
                return convert(args[idx++]);
            }
        }

        // check for keyword argument
        for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
            if (kwnames[i] == name) {
                if constexpr (pyobject) {
                    return args[n_args + i];
                } else {
                    return convert(args[n_args + i]);
                }
            }
        }

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
        static constexpr bool pyobject = std::is_same_v<Type, PyObject*>;
        static_assert(
            std::is_same_v<ReturnType, Type>,
            "Conversion function must return same type as default value."
        );

        // check for positional argument
        if (idx < n_args) {
            if constexpr (pyobject) {
                return args[idx++];
            } else {
                return convert(args[idx++]);
            }
        }

        // check for keyword argument
        for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
            if (kwnames[i] == name) {
                if constexpr (pyobject) {
                    return args[n_args + i];
                } else {
                    return convert(args[n_args + i]);
                }
            }
        }

        return default_value;
    }

};


} // namespace util
} // namespace structs
} // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_ARGS_H
