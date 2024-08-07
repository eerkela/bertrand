#ifndef BERTRAND_PYTHON_H
#define BERTRAND_PYTHON_H


/* This header is used to include supplemental macros for the bertrand.python module,
 * which allows users to write their own Python modules in C++.  It is implicitly
 * included whenever an automated binding file is generated by bertrand's build system.
 */


#include <pybind11/pybind11.h>
import bertrand.python;


/* A replacement for PYBIND11_MODULE that reinterprets the resulting module as a
py::Module object.  The contents are taken directly from PYBIND11_MODULE, except that
we exchange their module type for our own. */
#define BERTRAND_MODULE(name, variable) \
    static ::pybind11::module_::module_def PYBIND11_CONCAT(pybind11_module_def_, name)  \
        PYBIND11_MAYBE_UNUSED;                                                          \
    PYBIND11_MAYBE_UNUSED                                                               \
    static void PYBIND11_CONCAT(pybind11_init_, name)(::py::Module&);                   \
    PYBIND11_PLUGIN_IMPL(name) {                                                        \
        PYBIND11_CHECK_PYTHON_VERSION                                                   \
        PYBIND11_ENSURE_INTERNALS_READY                                                 \
        ::py::Module m = ::pybind11::module_::create_extension_module(                  \
            PYBIND11_TOSTRING(name), nullptr, &PYBIND11_CONCAT(pybind11_module_def_, name));\
        try {                                                                           \
            PYBIND11_CONCAT(pybind11_init_, name)(m);                                   \
            return m.ptr();                                                             \
        }                                                                               \
        PYBIND11_CATCH_INIT_EXCEPTIONS                                                  \
    }                                                                                   \
    void PYBIND11_CONCAT(pybind11_init_, name)(::py::Module& variable)


#endif
