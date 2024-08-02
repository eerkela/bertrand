#ifndef BERTRAND_PYTHON_COMMON_MODULE_H
#define BERTRAND_PYTHON_COMMON_MODULE_H


#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"

// TODO: include func.h and expose a helper method that automatically creates an
// overload set.


namespace py {


//////////////////////
////    MODULE    ////
//////////////////////


template <typename CRTP, StaticStr ModName>
struct Object::__module__ : public impl::ModuleTag {
protected:

    /* Expose an immutable variable to Python in a way that shares memory and state.
    Note that this adds a getset descriptor to the module's type, and therefore can
    only be called during `__setup__()`. */
    template <StaticStr Name, typename T>
    static void var(const T& value) {
        if (setup_complete) {
            throw ImportError(
                "The var() helper for attribute '" + Name + "' of module '" + ModName +
                "' can only be called during the __setup__() method."
            );
        }
        static auto get = [](PyObject* self, void* value) -> PyObject* {
            try {
                return release(wrap(*reinterpret_cast<const T*>(value)));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        };
        static auto set = [](PyObject* self, PyObject* new_val, void* value) -> int {
            PyErr_SetString(
                PyExc_TypeError,
                "variable '" + Name + "' of module '" + ModName +
                "' is immutable."
            );
            return -1;
        };
        CRTP::tp_getset.push_back({
            Name,
            (getter) +get,  // converts a stateless lambda to a function pointer
            (setter) +set,
            nullptr,
            &value
        });
    }

    /* Expose a mutable variable to Python in a way that shares memory and state.  Note
    that this adds a getset descriptor to the module's type, and therefore can only be
    called during `__setup__()`. */
    template <StaticStr Name, typename T>
    static void var(T& value) {
        if (setup_complete) {
            throw ImportError(
                "The var() helper for attribute '" + Name + "' of module '" + ModName +
                "' can only be called during the __setup__() method."
            );
        }
        static auto get = [](PyObject* self, void* value) -> PyObject* {
            try {
                return release(wrap(*reinterpret_cast<T*>(value)));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        };
        static auto set = [](PyObject* self, PyObject* new_val, void* value) -> int {
            if (new_val == nullptr) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "variable '" + Name + "' of module '" + ModName +
                    "' cannot be deleted."
                );
                return -1;
            }
            try {
                using Wrapper = __as_object__<T>::type;
                auto obj = reinterpret_borrow<Object>(new_val);
                *reinterpret_cast<T*>(value) = static_cast<Wrapper>(obj);
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        };
        CRTP::tp_getset.push_back({
            Name,
            (getter) +get,
            (setter) +set,
            nullptr,
            &value
        });
    }

    // TODO: add types, whose logic is mostly already implemented in __python__::__ready__()

    /* Expose a py::Object type to Python. */
    template <StaticStr Name, typename Cls, typename... Bases>
        requires (
            std::derived_from<Cls, Object> &&
            (std::derived_from<Bases, Object> && ...)
        )
    void type() {
        if (!setup_complete) {
            throw ImportError(
                "The type() helper for class '" + Name + "' in module '" + ModName +
                "' can only be called after `__setup__()`."
            );
        }
        if (PyObject_HasAttr(
            reinterpret_cast<PyObject*>(this),
            impl::TemplateString<Name>::ptr
        )) {
            if constexpr (impl::is_generic<Cls>) {
                PyObject* attr = PyObject_GetAttr(
                    reinterpret_cast<PyObject*>(this),
                    impl::TemplateString<Name>::ptr
                );
                if (attr == nullptr) {
                    Exception::from_python();
                }
                if (!PyObject_IsInstance(
                    attr,
                    reinterpret_cast<PyObject*>(&impl::BertrandMeta::__type__)
                ) || !reinterpret_cast<impl::BertrandMeta*>(attr)->template_instantiations) {
                    Py_DECREF(attr);
                    throw AttributeError(
                        "Module '" + ModName + "' already has an attribute named '" +
                        Name + "'"
                    );
                }
                Py_DECREF(attr);
            } else {
                throw AttributeError(
                    "Module '" + ModName + "' already has an attribute named '" +
                    Name + "'"
                );
            }
        }

        // initialize the metaclass if it hasn't been already
        impl::BertrandMeta::__ready__();

        // if the wrapper type is not generic, it can be directly instantiated and
        // attached to the module
        if constexpr (!impl::is_generic<Cls>) {
            PyObject* bases = nullptr;
            if constexpr (sizeof...(Bases) > 0) {
                bases = PyTuple_New(sizeof...(Bases));
                if (bases == nullptr) {
                    Exception::from_python();
                }
                size_t i = 0;
                (PyTuple_SET_ITEM(bases, i++, release(Type<Bases>())), ...);
            }

            // generate a type spec and convert to a heap type using BertrandMeta
            impl::BertrandMeta* cls = reinterpret_cast<impl::BertrandMeta*>(
                PyType_FromMetaclass(
                    &impl::BertrandMeta::__type__,
                    reinterpret_cast<PyObject*>(this),
                    &type_spec(),
                    bases
                )
            );
            Py_XDECREF(bases);
            if (cls == nullptr) {
                Exception::from_python();
            }

            try {
                cls->template_instantiations = nullptr;
                if (cls->demangle()) {
                    Exception::from_python();
                }

            } catch (...) {
                Py_DECREF(cls);
                throw;
            }

            // store a borrowed reference to the type for later use
            __type__ = reinterpret_cast<PyTypeObject*>(cls);
            Py_DECREF(cls);  // module now owns only reference

        // if the wrapper type is generic, a public proxy type needs to be created
        // that can be indexed to resolve individual instantiations
        } else {
            // initialize or retrieve the proxy type under which the instantiation
            // will be stored.  the instantiation always inherits from this type,
            // in addition to any other bases that are specified
            impl::BertrandMeta* stub = impl::BertrandMeta::stub(
                parent,
                name,
                doc,
                bases
            );
            PyObject* _bases = PyTuple_New(bases.size() + 1);
            if (_bases == nullptr) {
                Py_DECREF(stub);
                Exception::from_python();
            }
            for (size_t i = 0; i < bases.size(); ++i) {
                PyTuple_SET_ITEM(_bases, i, Py_NewRef(bases[i]));
            }
            PyTuple_SET_ITEM(_bases, bases.size(), stub);  // steals a reference

            // use the metaclass to convert the type_spec into a heap type
            impl::BertrandMeta* cls = reinterpret_cast<impl::BertrandMeta*>(
                PyType_FromMetaclass(
                    &impl::BertrandMeta::__type__,
                    nullptr,  // instantiations aren't directly attached to a module
                    &type_spec(),
                    _bases
                )
            );
            Py_DECREF(_bases);
            if (cls == nullptr) {
                Exception::from_python();
            }

            try {
                cls->template_instantiations = nullptr;
                if (cls->demangle()) {
                    Exception::from_python();
                }

                // insert the class into the parent's template_instantiations
                PyObject* key = template_key<CppType>{}();
                if (PyDict_SetItem(
                    stub->template_instantiations,
                    key,
                    reinterpret_cast<PyObject*>(cls)
                )) {
                    Py_DECREF(key);
                    Exception::from_python();
                }
                Py_DECREF(key);

            } catch (...) {
                Py_DECREF(cls);
                throw;
            }

            // store a borrowed reference to the type for later use
            __type__ = reinterpret_cast<PyTypeObject*>(cls);
            Py_DECREF(cls);  // stub now owns the only reference
        }

        initialized = true;
    }



    /* Add a submodule to this module in order to model nested namespaces at the C++
    level.  Note that a `py::Module` specialization with the corresponding name
    (appended to the current name) must exist.  Its module initialization logic will be
    invoked in a recursive fashion. */
    template <StaticStr Name>
    void submodule() {
        static constexpr StaticStr FullName = ModName + "." + Name;
        using Mod = Module<ModName + "." + Name>;
        if constexpr (!impl::is_module<Mod>) {
            throw TypeError(
                "No module specialization found for submodule '" + FullName +
                "'.  Please define a specialization of py::Module with this name and "
                "fill in its `__module__` initialization struct."
            );
        }
        if (!setup_complete) {
            throw ImportError(
                "The submodule() helper for submodule '" + FullName +
                "' can only be called after `__setup__()`."
            );
        }
        if (PyObject_HasAttr(
            reinterpret_cast<PyObject*>(this),
            impl::TemplateString<Name>::ptr
        )) {
            throw AttributeError(
                "Module '" + ModName + "' already has an attribute named '" + Name + "'."
            );
        }

        // __import__() calls PyModuleDef_Init, which casts the module def to
        // PyObject*.  We cast it back here to obtain the original PyModuleDef*
        auto module_def = reinterpret_cast<PyModuleDef*>(
            Mod::__module__::__import__()
        );
        if (module_def == nullptr) {
            Exception::from_python();
        }

        // we have to build a PEP 451 ModuleSpec for the submodule in order to begin
        // multi-phase initialization
        PyObject* spec = get_spec<FullName>();
        if (spec == nullptr) {
            Exception::from_python();
        }

        // phase 1 initialization runs submodule's __setup__ and creates a unique type
        PyObject* mod = PyModule_FromDefAndSpec(module_def, spec);
        Py_DECREF(spec);
        if (mod == nullptr) {
            Exception::from_python();
        }

        // phase 2 initialization runs submodule's __ready__ and completes the import
        if (PyModule_ExecDef(mod, module_def) < 0) {
            Py_DECREF(mod);
            Exception::from_python();
        }

        // attach the submodule to the this parent
        int rc = PyObject_SetAttr(
            reinterpret_cast<PyObject*>(this),
            impl::TemplateString<Name>::ptr,
            mod
        );
        Py_DECREF(mod);  // parent now holds the only reference to the submodule
        if (rc) {
            Exception::from_python();
        }
    }

    // TODO: I might be able to do something similar with functions, in which
    // case I would automatically generate an overload set for each function as
    // it is added, or append to it as needed.  Alternatively, I can use the same
    // attach() method that I'm exposing to the public.

    // TODO: maybe also a __type__ method, which would add a type directly to the
    // module if it is not generic, or generate a front-facing abstract type if it
    // is.  That should pretty much automate the process of handling generic types.

public:
    inline static const std::string __doc__ =
        "A Python wrapper around the '" + ModName +
        "' C++ module, generated by Bertrand.";

    /* Initialize the module's type object the first time this module is imported.

    Note that this effectively populates that module's `PyType_Spec` structure, which
    is stored with static duration and reused to initialize the module's type whenever
    it is imported within the same process.  As such, this method will only be called
    *once*, the first time the module is imported.  Sub interpreters will still create
    their own unique types using the final spec, but the spec itself is a singleton.

    The default implementation does nothing, but you can insert calls to the `var()`
    method here in order to add getset descriptors to the module's type.  This is the
    proper way of synchronizing state between Python and C++, and allows Bertrand to
    automatically generate the necessary boilerplate for you.  Typically, that's all
    this method will contain. */
    static void __setup__() {};

    /* Initialize the module after it has been created.

    Note that unlike `__setup__()`, which is only called once per process, this method
    is called *every* time the module is imported into a fresh interpreter.  It is
    equivalent to running the module's body in Python, and is the proper place to add
    functions, types, and any other objects that do not need to be efficiently
    represented in the module's `PyType_Spec`.

    The default implementation does nothing, but you can essentially treat this just
    like a Python-level constructor for the module itself.  That includes any internal
    fields within the module's CRTP representation, which can be initialized here.  Any
    objects added at this stage are guaranteed be unique for each module, including any
    descriptors that are added to its type.  It's still up to you to make sure that the
    data they reference does not leak into any other modules, but the modules
    themselves will not interfere with this in any way. */
    void __ready__() {};

    /* Place this in the body of the module initialization function in order to import
    the module from Python.  For instance:

        auto PyInit_MyModule() {
            return Module<"MyModule">::__module__::__import__();
        }

    Note that the module name is ultimately determined by the suffix in the function
    name (`MyModule` in this case). */
    static PyObject* __import__() {
        static PyModuleDef_Slot slots[] = {
            {Py_mod_create, reinterpret_cast<void*>(__create__)},
            {Py_mod_exec, reinterpret_cast<void*>(__exec__)},
            #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)
            {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
            #endif
            {0, nullptr}
        };
        static PyModuleDef def = {
            .m_base = PyModuleDef_HEAD_INIT,
            .m_name = ModName,
            .m_doc = CRTP::__doc__.c_str(),
            .m_size = 0,
            .m_slots = &slots,
            .m_traverse = (traverseproc) CRTP::__traverse__,
            .m_clear = (inquiry) CRTP::__clear__,
            .m_free = (freefunc) CRTP::__dealloc__,
        };
        return PyModuleDef_Init(&def);
    }

    // TODO: traverse, clear, dealloc

    /* Do a truth check to see if the module's fields are already initialized. This can
    be used to trigger proper deallocation logic before re-initializing a module, or
    for debugging purposes. */
    explicit operator bool() const {
        return initialized;
    }

private:
    PyModuleObject base;
    bool initialized;

    inline static bool setup_complete = false;
    inline static std::vector<PyMethodDef> tp_methods;
    inline static std::vector<PyGetSetDef> tp_getset;
    inline static std::vector<PyMemberDef> tp_members;

    /* Create the module during multi-phase initialization.  This also creates a unique
    heap type for the module, allowing for arbitrary members and computed properties,
    which allow for better synchronization of state between Python and C++. */
    static PyObject* __create__(PyObject* spec, PyModuleDef* def) {
        try {
            // __setup__ is only called once, and populates the statically-allocated
            // type spec with all of the necessary slots
            if (!setup_complete) {
                CRTP::__setup__();
                CRTP::tp_methods.push_back({nullptr, nullptr, 0, nullptr});
                CRTP::tp_getset.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
                CRTP::tp_members.push_back({nullptr, 0, 0, 0, nullptr});
                setup_complete = true;
            }

            // the type spec itself is stored statically and reused whenever this module is
            // imported in the same process
            static PyType_Slot type_slots[] = {
                {Py_tp_base, &PyModule_Type},
                {Py_tp_methods, CRTP::tp_methods.data()},
                {Py_tp_getset, CRTP::tp_getset.data()},
                {Py_tp_members, CRTP::tp_members.data()},
                {0, nullptr}
            };
            static PyType_Spec type_spec = {
                .name = typeid(CRTP).name(),
                .basicsize = sizeof(CRTP),
                .itemsize = 0,
                .flags =
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION,
                .slots = type_slots
            };

            // converting the spec into a type generates a unique instance for every import
            PyTypeObject* type = reinterpret_cast<PyTypeObject*>(PyType_FromSpec(&type_spec));
            if (type == nullptr) {
                return nullptr;
            }

            // we then instantiate the module object using the newly-created type
            PyObject* mod = type->tp_new(type, nullptr, nullptr);
            Py_DECREF(type);  // module now holds the only reference to its type
            if (mod == nullptr) {
                return nullptr;
            }
            reinterpret_cast<CRTP*>(mod)->initialized = false;

            // make sure that the module uses the loader's name to match Python's semantics
            PyObject* name = PyObject_GetAttr(spec, impl::TemplateString<"name">::ptr);
            if (name == nullptr) {
                Py_DECREF(mod);
                return nullptr;
            }
            int rc = PyObject_SetAttr(mod, impl::TemplateString<"__name__">::ptr, name);
            Py_DECREF(name);
            if (rc < 0) {
                Py_DECREF(mod);
                return nullptr;
            }
            return mod;

        } catch (...) {
            Exception::to_python();
            return nullptr;
        }
    }

    /* Execute the module during multi-phase initializiation, completing the import
    process. */
    static int __exec__(PyObject* module) {
        try {
            auto mod = reinterpret_cast<CRTP*>(module);
            mod->__ready__();
            mod->initialized = true;
            return 0;
        } catch (...) {
            Exception::to_python();
            return -1;
        }
    }

    /* Construct an importlib.machinery.ExtensionFileLoader for use when defining
    submodules. */
    template <StaticStr Name>
    static PyObject* get_loader() {
        PyObject* importlib_machinery = PyImport_ImportModule("importlib.machinery");
        if (importlib_machinery == nullptr) {
            return nullptr;
        }
        PyObject* loader_class = PyObject_GetAttr(
            importlib_machinery,
            impl::TemplateString<"ExtensionFileLoader">::ptr
        );
        Py_DECREF(importlib_machinery);
        if (loader_class == nullptr) {
            return nullptr;
        }

        PyObject* filename = PyUnicode_FromString(__FILE__);
        if (filename == nullptr) {
            Py_DECREF(loader_class);
            return nullptr;
        }
        PyObject* loader = PyObject_CallFunctionObjArgs(
            loader_class,
            impl::TemplateString<Name>::ptr,
            filename,
            nullptr
        );
        Py_DECREF(loader_class);
        Py_DECREF(filename);

        if (loader == nullptr) {
            return nullptr;
        }
        return loader;
    }

    /* Construct a PEP 451 ModuleSpec for use when defining submodules. */
    template <StaticStr Name>
    static PyObject* get_spec() {
        PyObject* importlib_util = PyImport_ImportModule("importlib.util");
        if (importlib_util == nullptr) {
            return nullptr;
        }
        PyObject* spec_from_loader = PyObject_GetAttr(
            importlib_util,
            impl::TemplateString<"spec_from_loader">::ptr
        );
        Py_DECREF(importlib_util);
        if (spec_from_loader == nullptr) {
            return nullptr;
        }

        PyObject* loader = get_loader<Name>();
        if (loader == nullptr) {
            Py_DECREF(spec_from_loader);
            return nullptr;
        }
        PyObject* spec = PyObject_CallFunctionObjArgs(
            spec_from_loader,
            impl::TemplateString<Name>::ptr,
            loader,
            nullptr
        );
        Py_DECREF(loader);
        Py_DECREF(spec_from_loader);
        if (spec == nullptr) {
            return nullptr;
        }
        return spec;
    }

};


/* A Python module with a unique type, which supports the addition of descriptors for
computed properties, overload sets, etc. */
template <StaticStr Name>
class Module : public Object {
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


}  // namespace py


#endif  // BERTRAND_PYTHON_COMMON_MODULE_H
