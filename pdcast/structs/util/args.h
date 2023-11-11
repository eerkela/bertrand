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


/* A utility class that parses the arguments passed to a C method implementing the
METH_FASTCALL | METH_KEYWORD protocol. */
class PyArgs {
    const Py_ssize_t n_kwargs;
    const Py_ssize_t n_args;
    PyObject* const* args;
    std::string_view* kwnames;
    Py_ssize_t idx;

public:

    /* A builder-style argument parser for Python methods following the FASTCALL and
    KEYWORD protocols. */
    PyArgs(
        PyObject* const* args,
        Py_ssize_t nargs,
        PyObject* kwnames
    ) : n_kwargs(kwnames == nullptr ? 0 : PyTuple_GET_SIZE(kwnames)),
        n_args(nargs - n_kwargs),
        args(args),
        idx(0)
    {
        this->kwnames = static_cast<std::string_view*>(
            malloc(n_kwargs * sizeof(std::string_view))
        );
        if (this->kwnames == nullptr) {
            throw std::bad_alloc();
        }
        for (Py_ssize_t i = 0; i < n_kwargs; ++i) {
            Py_ssize_t len;
            PyObject* str = PyTuple_GET_ITEM(kwnames, i);
            new (this->kwnames + i) std::string_view{
                PyUnicode_AsUTF8AndSize(str, &len),
                static_cast<size_t>(len)
            };
        }
    }

    /* Free the memory allocated for the keyword argument names. */
    ~PyArgs() noexcept { free(kwnames); }  // no need to call destructor

    /* Extract an argument from the pool, using an optional default value and/or
    conversion function. */
    template <typename Type = PyObject*, typename Func = PyObject*>
    Type parse(
        std::string_view name,
        Type default_value = nullptr,
        Func convert = nullptr
    ) {
        using Traits = FuncTraits<Func, PyObject*>;
        static_assert(
            std::is_same_v<typename Traits::ReturnType, Type>,
            "Conversion function must return same type as default value."
        );
        static constexpr bool pyobject = std::is_same_v<Type, PyObject*>;

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

        // return default value
        if constexpr (pyobject) {
            if (default_value == nullptr) {
                std::ostringstream msg;
                msg << "Missing required argument: " << name;
                throw std::runtime_error(msg.str());
            }   
        }
        return default_value;
    }

};


} // namespace util
} // namespace structs
} // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_ARGS_H
