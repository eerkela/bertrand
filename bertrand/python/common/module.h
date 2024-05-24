#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_MODULE_H
#define BERTRAND_PYTHON_COMMON_MODULE_H

#include "declarations.h"
#include "except.h"
#include "object.h"


namespace bertrand {
namespace py {


template <>
struct __hash__<Module>                                         : Returns<size_t> {};
template <StaticStr name> requires (!impl::getattr_helper<name>::enable)
struct __getattr__<Module, name>                                : Returns<Object> {};
template <StaticStr name, typename Value> requires (!impl::setattr_helper<name>::enable)
struct __setattr__<Module, name, Value>                         : Returns<void> {};
template <StaticStr name> requires (!impl::delattr_helper<name>::enable)
struct __delattr__<Module, name>                                : Returns<void> {};


/* Represents an imported Python module in C++. */
class Module : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return impl::module_like<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();
        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyModule_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Module(Handle h, const borrowed_t& t) : Base(h, t) {}
    Module(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    Module(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Module(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Module>(accessor).release(), stolen_t{})
    {}

    /* Default module constructor deleted for clarity. */
    Module() = delete;

    /* Explicitly create a new module object from a statically-allocated (but
    uninitialized) PyModuleDef struct. */
    explicit Module(const char* name, const char* doc, PyModuleDef* def) :
        Base(nullptr, stolen_t{})
    {
        def = new (def) PyModuleDef{
            /* m_base */ PyModuleDef_HEAD_INIT,
            /* m_name */ name,
            /* m_doc */ pybind11::options::show_user_defined_docstrings() ? doc : nullptr,
            /* m_size */ -1,
            /* m_methods */ nullptr,
            /* m_slots */ nullptr,
            /* m_traverse */ nullptr,
            /* m_clear */ nullptr,
            /* m_free */ nullptr
        };
        m_ptr = PyModule_Create(def);
        try {
            if (m_ptr == nullptr) {
                if (PyErr_Occurred()) {
                    Exception::from_python();
                }
                pybind11::pybind11_fail(
                    "Internal error in pybind11::module_::create_extension_module()"
                );
            }
        } catch (...) {
            Exception::from_pybind11();
        }
    }

    //////////////////////////////////
    ////    PYBIND11 INTERFACE    ////
    //////////////////////////////////

    /* Equivalent to pybind11::module_::def(). */
    template <typename Func, typename... Extra>
    Module& def(const char* name_, Func&& f, const Extra&... extra) {
        try {
            pybind11::cpp_function func(
                std::forward<Func>(f),
                pybind11::name(name_),
                pybind11::scope(*this),
                pybind11::sibling(
                    pybind11::getattr(*this, name_, Py_None)
                ),
                extra...
            );
            // NB: allow overwriting here because cpp_function sets up a chain with the
            // intention of overwriting (and has already checked internally that it isn't
            // overwriting non-functions).
            add_object(name_, func, true /* overwrite */);
            return *this;
        } catch (...) {
            Exception::from_pybind11();
        }
    }

    /* Equivalent to pybind11::module_::def_submodule(). */
    Module def_submodule(const char* name, const char* doc = nullptr);

    /* Reload the module or throw an error. */
    void reload() {
        PyObject *obj = PyImport_ReloadModule(this->ptr());
        if (obj == nullptr) {
            Exception::from_python();
        }
        *this = reinterpret_steal<Module>(obj);
    }

    /* Equivalent to pybind11::module_::add_object(). */
    PYBIND11_NOINLINE void add_object(
        const char* name,
        Handle obj,
        bool overwrite = false
    ) {
        try {
            if (!overwrite && pybind11::hasattr(*this, name)) {
                pybind11::pybind11_fail(
                    "Error during initialization: multiple incompatible "
                    "definitions with name \"" + std::string(name) + "\""
                );
            }
            PyModule_AddObjectRef(ptr(), name, obj.ptr());
        } catch (...) {
            Exception::from_pybind11();
        }
    }

};


/* Equivalent to Python `import module`.  Only recognizes absolute imports. */
template <StaticStr name>
Module import() {
    PyObject* obj = PyImport_Import(impl::TemplateString<name>::ptr);
    if (obj == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Module>(obj);
}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_MODULE_H
