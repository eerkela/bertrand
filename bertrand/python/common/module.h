#ifndef BERTRAND_PYTHON_COMMON_MODULE_H
#define BERTRAND_PYTHON_COMMON_MODULE_H

#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"
#include "func.h"


namespace py {


namespace impl {

    /* A unique subclass of Python's base module type that allows us to add descriptors
    for computed properties and overload sets. */
    template <StaticStr Name>
    struct PyModule {
        PyModuleObject base;

        /* Initialize the module and assign its type. */
        static Module<Name> __ready__(const Str& doc) {
            PyObject* _mod = PyModule_Create(&module_def);
            if (_mod == nullptr) {
                Exception::from_python();
            }
            Module<Name> mod = reinterpret_steal<Module<Name>>(_mod);
            PyObject* _type = PyType_FromSpec(&type_spec);
            if (_type == nullptr) {
                return nullptr;
            }
            Type<Module<Name>> type = reinterpret_steal<Type<Module<Name>>>(_type);
            setattr<"__class__">(mod, type);
            setattr<"__doc__">(mod, doc);
            return mod;
        }

        template <StaticStr SubName>
        static auto __submodule__(Module<Name>& parent, const Str& doc) {
            auto mod = Module<Name + "." + SubName>::__ready__(doc);

            // add the submodule to the interpreter's import list
            PyObject* _mod_ptr = PyImport_AddModuleObject(
                impl::TemplateString<Name + "." + SubName>::ptr
            );
            if (_mod_ptr == nullptr) {
                Exception::from_python();
            }
            Py_DECREF(_mod_ptr);

            // set the submodule as an attribute of the parent module
            if (PyObject_SetAttr(
                ptr(parent),
                impl::TemplateString<SubName>::ptr,
                ptr(mod)
            )) {
                Exception::from_python();
            }
            return mod;
        }

        // TODO: perhaps add a __var__ method for adding a global variable to the
        // module scope.  It could take separate overloads for const vs mutable
        // references, and generate a setter for the second case, and only a setter
        // for the first.

        // TODO: I might be able to do something similar with functions, in which
        // case I would automatically generate an overload set for each function as
        // it is added, or append to it as needed.  Alternatively, I can use the same
        // attach() method that I'm exposing to the public.

        // TODO: maybe also a __type__ method, which would add a type directly to the
        // module if it is not generic, or generate a front-facing abstract type if it
        // is.  That should pretty much automate the process of handling generic types.

        inline static PyModuleDef module_def = {
            .m_base = PyModuleDef_HEAD_INIT,
            .m_name = Name,
            .m_doc = "A Python wrapper around the '" + Name + "' C++ module.",
            .m_size = -1,
        };

        inline static PyType_Slot type_slots[] = {
            {Py_tp_base, &PyModule_Type},
            {0, nullptr}
        };

        inline static PyType_Spec type_spec = {
            .name = typeid(PyModule).name(),
            .basicsize = sizeof(PyModuleObject),
            .itemsize = 0,
            .flags =
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE |
                Py_TPFLAGS_DISALLOW_INSTANTIATION,
            .slots = type_slots,
        };

    };

}


/* A Python module with a unique type, which supports the addition of descriptors for
computed properties, overload sets, etc. */
template <StaticStr Name>
class Module : public Object, public impl::ModuleTag {
    using Base = Object;

public:

    Module(Handle h, borrowed_t t) : Base(h, t) {}
    Module(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Module, __init__<Module, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Module, std::remove_cvref_t<Args>...>::enable
        )
    Module(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Module, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Module, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Module, __explicit_init__<Module, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Module, std::remove_cvref_t<Args>...>::enable
        )
    explicit Module(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Module, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

};


/* Default-initializing a py::Module is functionally equivalent to an import
statement. */
template <StaticStr Name>
struct __init__<Module<Name>>                               : Returns<Module<Name>> {
    static auto operator()() {
        PyObject* mod = PyImport_Import(impl::TemplateString<Name>::ptr);
        if (mod == nullptr) {
            throw Exception();
        }
        return reinterpret_steal<Module<Name>>(mod);
    }
};


template <StaticStr Name>
class Type<Module<Name>> : public Object {
    using Base = Object;

public:

    Type(Handle h, borrowed_t t) : Base(h, t) {}
    Type(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Type, __init__<Type, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Type, std::remove_cvref_t<Args>...>::enable
        )
    Type(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Type, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Type, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Type, __explicit_init__<Type, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Type, std::remove_cvref_t<Args>...>::enable
        )
    explicit Type(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Type, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

};


template <StaticStr Name>
struct __init__<Type<Module<Name>>> {
    static auto operator()() {
        return reintepret_steal<Type<Module<Name>>>(
            reinterpret_cast<PyObject*>(Py_TYPE(ptr(Module<Name>())))
        );
    }
};


}


#endif  // BERTRAND_PYTHON_COMMON_MODULE_H
