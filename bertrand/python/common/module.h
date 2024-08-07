#ifndef BERTRAND_PYTHON_COMMON_MODULE_H
#define BERTRAND_PYTHON_COMMON_MODULE_H


#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"
#include "pytypedefs.h"
#include "type.h"


// TODO: include func.h and expose a helper method that automatically creates an
// overload set.


namespace py {


//////////////////////
////    MODULE    ////
//////////////////////


namespace impl {

    /* Borrow a reference to a type object registered to a particular module.  This is
    the recommended way of accessing a global type object rather than using the
    `static` keyword, since it allows for better encapsulation of module state.  Static
    types and references interfere with multi-phase module initialization, and can
    result in a situation where the result of `py::Type<MyClass>()` *does not* match
    the equivalent type that would be accessed from Python, which can lead to subtle
    bugs and inconsistencies.  This function avoids that problem, and causes
    `py::Type<MyClass>()` to always yield the same result as the active interpreter. */
    template <std::derived_from<Object> T, StaticStr Name>
    auto get_type(const Module<Name>& mod) {
        return mod.template __get_type__<T>();
    }

    struct ModuleTag : public BertrandTag {
    protected:
        /* A CRTP base class that provides helpers for writing module initialization
        functions to provide entry points for the Python interpreter.  Every specialization
        of `py::Module` should provide a public `__module__` type that inherits from this
        class and implements the `__new__()` and `__init__()` methods using the provided
        helpers.  These allow for:

            - Possibly templated types, which will be attached to the module under a public
            proxy type that can be indexed to resolve individual instantiations.
            - Functions, which will be modeled as overload sets at the python level and
            can be redeclared to provide different implementations.
            - Variables, which will share state across the language boundary and can be
            modified from both C++ and Python.
            - Submodules, which can be used to model namespaces at the C++ level.

        Using these helpers is recommended to make writing module initialization logic as
        easy as possible, although it is not strictly necessary.  If you introduce custom
        logic, you should thoroughly study Python's C API and the Bertrand source code as a
        guide.  In particular, it's important that the module initialization logic be
        idempotent (without side effects), and NOT share state between calls.  This means
        it cannot utilize static variables or any other form of shared state that will be
        propagated to the resulting module.  It should instead create unique objects every
        time the module is imported.  The helper methods do this internally, so in most
        cases you don't have to worry about it.

        NOTE: the idempotence requirement is due to the fact that Bertrand modules are
        expected to support sub-interpreters, which requires that no state be shared
        between them.  By making state unique to each module, the interpreters can work in
        parallel without race conditions.  This is part of ongoing parallelization work in
        CPython itself - see PEP 554 for more details.
        
        Another good resource is the official Python HOWTO on the subject:
            https://docs.python.org/3/howto/isolating-extensions.html
        */
        template <typename CRTP, StaticStr ModName>
        struct def : public BertrandTag {
        protected:

            /* Expose an immutable variable to Python in a way that shares memory and
            state.  Note that this adds a getset descriptor to the module's type, and
            therefore can only be called during `__setup__()`. */
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
                tp_getset.push_back({
                    Name,
                    (getter) +get,  // converts a stateless lambda to a function pointer
                    (setter) +set,
                    nullptr,
                    &value
                });
            }

            /* Expose a mutable variable to Python in a way that shares memory and
            state.  Note that this adds a getset descriptor to the module's type, and
            therefore can only be called during `__setup__()`. */
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
                tp_getset.push_back({
                    Name,
                    (getter) +get,
                    (setter) +set,
                    nullptr,
                    &value
                });
            }

            /* Expose a py::Object type to Python.  Can only be called during
            `__ready__()`. */
            template <StaticStr Name, typename Cls, typename... Bases>
                requires (
                    std::derived_from<Cls, Object> &&
                    (std::derived_from<Bases, Object> && ...)
                )
            void type() {
                using PyMeta = Type<BertrandMeta>::__python__;
                Type<BertrandMeta> metaclass;
                Module<ModName> mod = reinterpret_borrow<Module<ModName>>(
                    reinterpret_cast<PyObject*>(this)
                );

                // ensure type name is unique or corresponds to a template interface
                BertrandMeta existing = reinterpret_steal<BertrandMeta>(nullptr);
                if (PyObject_HasAttr(ptr(mod), impl::TemplateString<Name>::ptr)) {
                    if constexpr (impl::is_generic<Cls>) {
                        existing = reinterpret_steal<BertrandMeta>(PyObject_GetAttr(
                            ptr(mod),
                            impl::TemplateString<Name>::ptr
                        ));
                        if (ptr(existing) == nullptr) {
                            Exception::from_python();
                        }
                        if (!(
                            PyType_Check(ptr(existing)) &&
                            PyType_IsSubtype(
                                reinterpret_cast<PyTypeObject*>(ptr(existing)),
                                reinterpret_cast<PyTypeObject*>(ptr(metaclass))
                            ) &&
                            reinterpret_cast<PyMeta*>(
                                ptr(existing)
                            )->template_instantiations != nullptr
                        )) {
                            throw AttributeError(
                                "Module '" + ModName + "' already has an attribute named '" +
                                Name + "'"
                            );
                        }
                    } else {
                        throw AttributeError(
                            "Module '" + ModName + "' already has an attribute named '" +
                            Name + "'"
                        );
                    }
                }

                // call the type's __ready__() method to populate the type object
                Type<Cls> cls = Type<Cls>::__python__::template __ready_impl__<Bases...>(mod);

                // if the wrapper type is generic, create or update the template
                // interface's __getitem__ dict with the new instantiation
                if constexpr (impl::is_generic<Cls>) {
                    if (ptr(existing) == nullptr) {
                        existing = PyMeta::stub_type<Name, Cls, Bases...>(mod);
                        if (PyModule_AddObjectRef(
                            ptr(mod),
                            Name,
                            ptr(existing)
                        )) {
                            Exception::from_python();
                        }
                    }
                    PyObject* key = PyTuple_Pack(
                        sizeof...(Bases),
                        ptr(Type<Bases>())...
                    );
                    if (key == nullptr) {
                        Exception::from_python();
                    }
                    int rc = PyDict_SetItem(
                        reinterpret_cast<PyMeta*>(
                            ptr(existing)
                        )->template_instantiations,
                        key,
                        ptr(cls)
                    );
                    Py_DECREF(key);
                    if (rc) {
                        Exception::from_python();
                    }

                // otherwise, insert the type object directly into the module
                } else {
                    if (PyModule_AddObjectRef(
                        ptr(mod),
                        Name,
                        ptr(cls)
                    )) {
                        Exception::from_python();
                    }
                }

                // TODO: insert into the module's `types` map, and maybe also into the
                // "bertrand" module's `types` map, which would allow the type to be
                // retrieved from both languages

                types[typeid(Cls)] = reinterpret_cast<PyTypeObject*>(ptr(cls));
            }

            // TODO: I might be able to do something similar with functions, in which
            // case I would automatically generate an overload set for each function as
            // it is added, or append to it as needed.  Alternatively, I can use the same
            // attach() method that I'm exposing to the public.

            /* Add a submodule to this module in order to model nested namespaces at
            the C++ level.  Note that a `py::Module` specialization with the
            corresponding name (appended to the current name) must exist.  Its module
            initialization logic will be invoked in a recursive fashion. */
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

                // phase 2 initialization runs submodule's __init__ and completes the import
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

        public:
            static constexpr StaticStr __doc__ =
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
            void __init__() {};

            /* Import the module in the body of a module initialization function to provide
            an entry point for the Python interpreter.  For instance:

                extern "C" auto PyInit_MyModule() {
                    return Module<"MyModule">::__module__::__import__();
                }

            Note that the module name is ultimately determined by the suffix in the
            function (`MyModule` in this case), and the `extern "C"` portion is required
            to make it visible to the CPython interpreter. */
            static PyObject* __import__() {
                static PyModuleDef_Slot slots[] = {
                    {Py_mod_create, reinterpret_cast<void*>(__create__)},
                    {Py_mod_exec, reinterpret_cast<void*>(__exec__)},
                    {
                        Py_mod_multiple_interpreters,
                        Py_MOD_PER_INTERPRETER_GIL_SUPPORTED
                    },
                    {0, nullptr}
                };
                static PyModuleDef def = {
                    .m_base = PyModuleDef_HEAD_INIT,
                    .m_name = ModName,
                    .m_doc = CRTP::__doc__,
                    .m_size = 0,
                    .m_slots = &slots,
                    .m_traverse = (traverseproc) CRTP::__traverse__,
                    .m_clear = (inquiry) CRTP::__clear__,
                    .m_free = (freefunc) CRTP::__dealloc__,
                };
                return PyModuleDef_Init(&def);
            }

            // TODO: traverse, clear, dealloc - maybe handled by the module's type?

            /* Do a truth check to see if the module's fields are already initialized.
            This can be used to trigger proper deallocation logic before
            re-initializing a module, or for debugging purposes. */
            explicit operator bool() const {
                return import_complete;
            }

        private:
            inline static bool setup_complete = false;
            inline static std::vector<PyMethodDef> tp_methods;
            inline static std::vector<PyGetSetDef> tp_getset;
            inline static std::vector<PyMemberDef> tp_members;

            PyModuleObject base;
            bool import_complete;
            std::unordered_map<std::type_index, PyTypeObject*> types;

            /* Create the module during multi-phase initialization.  This also creates
            a unique heap type for the module, allowing for arbitrary members and
            computed properties, which allow for better synchronization of state
            between Python and C++. */
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

                    // the type spec itself is stored statically and reused whenever this
                    // module is imported in the same process
                    static PyType_Slot type_slots[] = {
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
                    PyTypeObject* type = reinterpret_cast<PyTypeObject*>(
                        PyType_FromSpecWithBases(
                            &type_spec,
                            reinterpret_cast<PyObject*>(&PyModule_Type)
                        )
                    );
                    if (type == nullptr) {
                        return nullptr;
                    }

                    // we then instantiate the module object using the newly-created type
                    PyObject* mod = type->tp_new(type, nullptr, nullptr);
                    Py_DECREF(type);  // module now holds the only reference to its type
                    if (mod == nullptr) {
                        return nullptr;
                    }
                    reinterpret_cast<CRTP*>(mod)->import_complete = false;

                    // make sure that the module uses the loader's name to match Python
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

            /* Execute the module during multi-phase initializiation, completing the
            import process. */
            static int __exec__(PyObject* module) {
                try {
                    auto mod = reinterpret_cast<CRTP*>(module);
                    mod->__init__();
                    mod->import_complete = true;
                    return 0;
                } catch (...) {
                    Exception::to_python();
                    return -1;
                }
            }

            /* Implements py::impl::get_type<T>(mod). */
            template <std::derived_from<Object> T, StaticStr Name>
            friend auto get_type(const Module<Name>& mod);
            template <std::derived_from<Object> T>
            auto __get_type__() {
                try {
                    return reinterpret_borrow<Type<T>>(types.at(typeid(T)));
                } catch (const std::out_of_range&) {
                    throw KeyError(
                        "Type '" + impl::demangle(typeid(T).name()) +
                        "' not found in module '" + ModName + "'."
                    );
                }
            }

            /* Construct an importlib.machinery.ExtensionFileLoader for use when
            defining submodules. */
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

    };

}


/* A Python module with a unique type, which supports the addition of descriptors for
computed properties, overload sets, etc. */
template <StaticStr Name>
class Module : public Object {
    using Base = Object;

public:

    Module(Handle h, borrowed_t t) : Base(h, t) {}
    Module(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args> requires (implicit_ctor<Module>::template enable<Args...>)
    Module(Args&&... args) : Base(
        implicit_ctor<Module>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Module>::template enable<Args...>)
    explicit Module(Args&&... args) : Base(
        explicit_ctor<Module>{},
        std::forward<Args>(args)...
    ) {}

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

    template <typename... Args> requires (implicit_ctor<Type>::template enable<Args...>)
    Type(Args&&... args) : Base(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::template enable<Args...>)
    explicit Type(Args&&... args) : Base(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

};


/* Default-initializing a module type retrieves the unique type of the named module. */
template <std::derived_from<impl::ModuleTag> Mod>
struct __init__<Type<Mod>>                                  : Returns<Type<Mod>> {
    static auto operator()() {
        return reintepret_steal<Type<Mod>>(
            reinterpret_cast<PyObject*>(Py_TYPE(ptr(Mod())))
        );
    }
};


}  // namespace py


#endif  // BERTRAND_PYTHON_COMMON_MODULE_H
