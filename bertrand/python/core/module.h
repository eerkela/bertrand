#ifndef BERTRAND_PYTHON_CORE_MODULE_H
#define BERTRAND_PYTHON_CORE_MODULE_H


#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"
#include "type.h"


// TODO: include func.h and expose a helper method that automatically creates an
// overload set.


// TODO: the global type map might not map to strings holding the demangled name, but
// the actual Python type object associated with that type.  I could then access the
// demangled name from instances of BertrandMeta, which would correspond better to the
// type maps present in template interfaces and modules.
// -> What about standard library types?  The global type map would probably have to
// store demangled names in addition to the Python type objects.  Or I can stick to
// what I have and only store the string.

// -> That would potentially allow me to merge it with the exception map, which would
// be kind of nice.



/// TODO: Module<"name">::def::Bindings should work the same way as for any other type,
/// with the addition of a `function()` method that aliases to `static_method()` and
/// `submodule()` which allows for nested namespaces.  Otherwise, it should work as
/// similarly as possible to all other types.


namespace py {


//////////////////////
////    MODULE    ////
//////////////////////


namespace impl {

    /* Get the Python type object corresponding to the templated C++ type, whcih has
    been exposed to Python.

    This function bypasses the Python interpreter and accesses the type object directly
    from the module's internal C++ type map, which is updated whenever bindings are
    generated for a new type.  This also allows the type to be searched using C++
    template syntax, which is substantially more convenient.

    This helper is meant to be used by the default constructor for `py::Type<T>()` when
    the corresponding Type has been exposed to Python. */
    template <std::derived_from<Object> T, static_str Name>
    auto get_type(const Module<Name>& mod) {
        return reinterpret_cast<typename Module<Name>::__python__*>(
            ptr(mod)
        )->template _get_type<T>();
    }

    struct ModuleTag : BertrandTag {
    protected:
        /* A CRTP base class that provides helpers for writing module initialization
        functions to provide entry points for the Python interpreter.  Every specialization
        of `py::Module` should provide a public `__python__` type that inherits from this
        class and implements the `__export__()` method using the provided helpers.
        These allow for:

            - Possibly templated types, which will be attached to the module under a
            public proxy type that can be indexed to resolve concrete instantiations.
            - Exceptions, which will be automatically discovered if they inherit from
            `py::Exception` and can be caught and thrown as Python exceptions.
            - Functions, which will be modeled as overload sets at the python level and
            can be redeclared to provide alternate implementations.
            - Variables, which will share state across the language boundary and can be
            modified from both C++ and Python.
            - Submodules, which are used to model nested namespaces at the C++ level.

        Using these helpers is recommended to make writing module initialization logic
        as easy as possible, although it is not strictly necessary.  If you introduce
        custom code, you should thoroughly study Python's C API and the Bertrand source
        code as a guide.  In particular, it's important that the module initialization
        logic be idempotent (without side effects), and NOT share state between calls.
        This means it cannot utilize static variables or any other form of mutable
        shared state that will be propagated to the resulting module.  It should
        instead create unique objects every time the module is imported.  The helper
        methods do this internally, so in most cases you don't have to worry about it.

        NOTE: the idempotence requirement is due to the fact that Bertrand modules are
        expected to support sub-interpreters, which requires that no potentially
        conflicting state be shared between them.  By making state unique to each
        module, the interpreters can work in parallel without race conditions.  This is
        part of ongoing parallelization work in CPython itself.  See PEP 554 for more
        details.

        Another good resource is the official Python HOWTO on this subject:
            https://docs.python.org/3/howto/isolating-extensions.html
        */
        template <typename CRTP, static_str ModName>
        struct def : BertrandTag {
        protected:
            /* A helper that is passed to Module<"name">::__python__::__export__() in
            order to simplify the creation and population of a module's type object and
            contents.  The `__export__()` method should always return the result of
            this object's `finalize()` method, which returns a PyObject* that marks the
            module for multi-phase initialization within the Python interpreter. */
            struct Bindings : BertrandTag {
            private:
                using Meta = Type<BertrandMeta>::__python__;
                inline static std::vector<PyMethodDef> tp_methods;
                inline static std::vector<PyGetSetDef> tp_getset;
                inline static std::vector<PyMemberDef> tp_members;

                struct Context {
                    bool is_var = false;
                    bool is_type = false;
                    bool is_template_interface = false;
                    bool is_exception = false;
                    bool is_function = false;
                    bool is_submodule = false;
                    std::vector<std::function<void(Module<ModName>&)>> callbacks;
                };

                std::unordered_map<std::string, Context> context;

                /* Construct an importlib.machinery.ExtensionFileLoader for use when
                defining submodules. */
                template <static_str Name>
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
                template <static_str Name>
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

                /* Insert a Python type into the global `bertrand.python.type_map`
                dictionary, so that it can be mapped to its C++ equivalent and used
                when generating Python bindings. */
                template <typename Cls>
                static void register_type(Module<ModName>& mod, PyTypeObject* type);

                /* Insert a Python exception type into the global
                `bertrand.python.exception_map` dictionary, so that it can be caught
                using its C++ equivalent. */
                static void register_exception(
                    Module<ModName>& mod,
                    PyTypeObject* type,
                    std::function<void(PyObject*, PyObject*, PyObject*, size_t)> callback
                );

            public:
                Bindings() = default;
                Bindings(const Bindings&) = delete;
                Bindings(Bindings&&) = delete;

                /* Expose an immutable variable to Python in a way that shares memory and
                state. */
                template <static_str Name, typename T>
                void var(const T& value) {
                    if (context.contains(Name)) {
                        throw AttributeError(
                            "Module '" + ModName + "' already has an attribute named '" +
                            Name + "'."
                        );
                    }
                    context[Name] = {
                        .is_var = true,
                    };
                    static bool skip = false;
                    if (skip) {
                        return;
                    }
                    static auto get = [](PyObject* self, void* closure) -> PyObject* {
                        try {
                            const T* value = reinterpret_cast<const T*>(closure);
                            if constexpr (impl::python_like<T>) {
                                return Py_NewRef(ptr(*value));
                            } else {
                                return release(wrap(*value));
                            }
                        } catch (...) {
                            Exception::to_python();
                            return nullptr;
                        }
                    };
                    static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "variable '" + Name + "' of module '" + ModName +
                            "' is immutable."
                        );
                        return -1;
                    };
                    tp_getset.push_back({
                        Name,
                        +get,  // converts a stateless lambda to a function pointer
                        +set,
                        nullptr,
                        const_cast<void*>(reinterpret_cast<const void*>(&value))
                    });
                    skip = true;
                }

                /* Expose an immutable variable held in per-module state.  This is very
                similar to exposing a global variable, but ensures that the value is
                unique to each interpreter, rather than being shared between them. */
                template <static_str Name, typename T>
                void var(const T CRTP::*value) {
                    if (context.contains(Name)) {
                        throw AttributeError(
                            "Module '" + ModName + "' already has an attribute named '" +
                            Name + "'."
                        );
                    }
                    context[Name] = {
                        .is_var = true,
                    };
                    static bool skip = false;
                    if (skip) {
                        return;
                    }
                    static auto get = [](PyObject* self, void* closure) -> PyObject* {
                        try {
                            CRTP* obj = reinterpret_cast<CRTP*>(self);
                            auto value = reinterpret_cast<const T CRTP::*>(closure);
                            if constexpr (impl::python_like<T>) {
                                return Py_NewRef(ptr(obj->*value));
                            } else {
                                return release(wrap(obj->*value));
                            }
                        } catch (...) {
                            Exception::to_python();
                            return nullptr;
                        }
                    };
                    static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                        PyErr_SetString(
                            PyExc_TypeError,
                            "variable '" + Name + "' of module '" + ModName +
                            "' is immutable."
                        );
                        return -1;
                    };
                    tp_getset.push_back({
                        Name,
                        +get,
                        +set,
                        nullptr,
                        const_cast<void*>(reinterpret_cast<const void*>(value))
                    });
                    skip = true;
                }

                /* Expose a mutable variable to Python in a way that shares memory and
                state. */
                template <static_str Name, typename T>
                void var(T& value) {
                    if (context.contains(Name)) {
                        throw AttributeError(
                            "Module '" + ModName + "' already has an attribute named '" +
                            Name + "'."
                        );
                    }
                    context[Name] = {
                        .is_var = true,
                    };
                    static bool skip = false;
                    if (skip) {
                        return;
                    }
                    static auto get = [](PyObject* self, void* closure) -> PyObject* {
                        try {
                            T* value = reinterpret_cast<T*>(closure);
                            if constexpr (impl::python_like<T>) {
                                return Py_NewRef(ptr(*value));
                            } else {
                                return release(wrap(*value));
                            }
                        } catch (...) {
                            Exception::to_python();
                            return nullptr;
                        }
                    };
                    static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                        if (new_val == nullptr) {
                            PyErr_SetString(
                                PyExc_TypeError,
                                "variable '" + Name + "' of module '" + ModName +
                                "' cannot be deleted."
                            );
                            return -1;
                        }
                        try {
                            *reinterpret_cast<T*>(closure) = static_cast<T>(
                                reinterpret_borrow<Object>(new_val)
                            );
                            return 0;
                        } catch (...) {
                            Exception::to_python();
                            return -1;
                        }
                    };
                    tp_getset.push_back({
                        Name,
                        +get,
                        +set,
                        nullptr,
                        reinterpret_cast<void*>(&value)
                    });
                    skip = true;
                }

                /* Expose a mutable variable held in per-module state.  This is very
                similar to exposing a global variable, but ensures that the value is
                unique to each interpreter, rather than being shared between them. */
                template <static_str Name, typename T>
                void var(T CRTP::*value) {
                    if (context.contains(Name)) {
                        throw AttributeError(
                            "Module '" + ModName + "' already has an attribute named '" +
                            Name + "'."
                        );
                    }
                    context[Name] = {
                        .is_var = true,
                    };
                    static bool skip = false;
                    if (skip) {
                        return;
                    }
                    static auto get = [](PyObject* self, void* closure) -> PyObject* {
                        try {
                            CRTP* obj = reinterpret_cast<CRTP*>(self);
                            auto value = reinterpret_cast<T CRTP::*>(closure);
                            if constexpr (impl::python_like<T>) {
                                return Py_NewRef(ptr(obj->*value));
                            } else {
                                return release(wrap(obj->*value));
                            }
                        } catch (...) {
                            Exception::to_python();
                            return nullptr;
                        }
                    };
                    static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                        if (new_val == nullptr) {
                            PyErr_SetString(
                                PyExc_TypeError,
                                "variable '" + Name + "' of module '" + ModName +
                                "' cannot be deleted."
                            );
                            return -1;
                        }
                        try {
                            CRTP* obj = reinterpret_cast<CRTP*>(self);
                            auto value = reinterpret_cast<T CRTP::*>(closure);
                            obj->*value = static_cast<T>(
                                reinterpret_borrow<Object>(new_val)
                            );
                            return 0;
                        } catch (...) {
                            Exception::to_python();
                            return -1;
                        }
                    };
                    tp_getset.push_back({
                        Name,
                        +get,
                        +set,
                        nullptr,
                        reinterpret_cast<void*>(value)
                    });
                    skip = true;
                }

                /* Expose a concrete py::Object type to Python. */
                template <
                    static_str Name,
                    std::derived_from<Object> Cls,
                    std::derived_from<Object>... Bases
                > requires (
                    !impl::is_generic<Cls> &&
                    (impl::has_type<Cls> && impl::is_type<Type<Cls>>) &&
                    ((impl::has_type<Bases> && impl::is_type<Type<Bases>>) && ...)
                )
                void type() {
                    if (context.contains(Name)) {
                        throw AttributeError(
                            "Module '" + ModName + "' already has an attribute named "
                            "'" + Name + "'."
                        );
                    }
                    context[Name] = {
                        .is_type = true,
                        .callbacks = {
                            [](Module<ModName>& mod) {
                                Type<Cls> cls = Type<Cls>::__python__::__export__(mod);

                                if (PyModule_AddObjectRef(
                                    ptr(mod),
                                    Name,
                                    ptr(cls)
                                )) {
                                    Exception::from_python();
                                }

                                // insert into this module's C++ type map for fast
                                // lookup using C++ template syntax
                                reinterpret_cast<CRTP*>(ptr(mod))->type_map[typeid(Cls)] =
                                    reinterpret_cast<PyTypeObject*>(ptr(cls));

                                // insert into the global type map for use when 
                                // generating C++ bindings from Python type hints
                                register_type<Cls>(
                                    mod,
                                    reinterpret_cast<PyTypeObject*>(ptr(cls))
                                );
                            }
                        }
                    };
                }

                /* Expose a templated py::Object type to Python. */
                template <static_str Name, template <typename...> typename Cls>
                void type() {
                    if (context.contains(Name)) {
                        throw AttributeError(
                            "Module '" + ModName + "' already has an attribute named '"
                            + Name + "'."
                        );
                    }
                    context[Name] = {
                        .is_type = true,
                        .is_template_interface = true,
                        .callbacks = {
                            [](Module<ModName>& mod) {
                                BertrandMeta stub = Meta::stub_type<Name, Cls>(mod);

                                if (PyModule_AddObjectRef(
                                    ptr(mod),
                                    Name,
                                    ptr(stub)
                                )) {
                                    Exception::from_python();
                                }
                            }
                        }
                    };
                }

                /* Expose an instantiation of a templated py::Object type to Python. */
                template <
                    static_str Name,
                    std::derived_from<Object> Cls,
                    std::derived_from<Object>... Bases
                > requires (
                    impl::is_generic<Cls> &&
                    (impl::has_type<Cls> && impl::is_type<Type<Cls>>) &&
                    ((impl::has_type<Bases> && impl::is_type<Type<Bases>>) && ...)
                )
                void type() {
                    auto it = context.find(Name);
                    if (it == context.end()) {
                        throw TypeError(
                            "No template interface found for type '" + Name +
                            "' in module '" + ModName + "' with specialization '" +
                            impl::demangle(typeid(Cls).name()) + "'.  Did "
                            "you forget to register the unspecialized template "
                            "first?"
                        );
                    } else if (!it->second.is_template_interface) {
                        throw AttributeError(
                            "Module '" + ModName + "' already has an attribute named "
                            "'" + Name + "'."
                        );
                    }
                    it->second.callbacks.push_back([](Module<ModName>& mod) {
                        // get the template interface with the same name
                        BertrandMeta existing = reinterpret_steal<BertrandMeta>(
                            PyObject_GetAttr(
                                ptr(mod),
                                impl::TemplateString<Name>::ptr
                            )
                        );
                        if (ptr(existing) == nullptr) {
                            Exception::from_python();
                        }

                        // call the type's __export__() method
                        Type<Cls> cls = Type<Cls>::__python__::__export__(mod);

                        // Insert the template interface into the type's __bases__
                        PyTypeObject* pycls = reinterpret_cast<PyTypeObject*>(ptr(cls));
                        PyObject* curr_bases = pycls->tp_bases;
                        PyObject* add_base = PyTuple_Pack(1, ptr(existing));
                        if (add_base == nullptr) {
                            Exception::from_python();
                        }
                        PyObject* new_bases = PySequence_Concat(curr_bases, add_base);
                        Py_DECREF(add_base);
                        if (new_bases == nullptr) {
                            Exception::from_python();
                        }
                        pycls->tp_bases = new_bases;
                        Py_DECREF(curr_bases);
                        PyType_Modified(pycls);

                        // insert into the template interface's __getitem__ dict
                        PyObject* key = PyTuple_Pack(
                            sizeof...(Bases),
                            ptr(Type<Bases>())...
                        );
                        if (key == nullptr) {
                            Exception::from_python();
                        }
                        if (PyDict_SetItem(
                            reinterpret_cast<typename Type<BertrandMeta>::__python__*>(
                                ptr(existing)
                            )->template_instantiations,
                            key,
                            ptr(cls)
                        )) {
                            Py_DECREF(key);
                            Exception::from_python();
                        }
                        Py_DECREF(key);

                        // insert into this module's C++ type map for fast lookup
                        // using C++ template syntax
                        reinterpret_cast<CRTP*>(ptr(mod))->type_map[typeid(Cls)] =
                            reinterpret_cast<PyTypeObject*>(ptr(cls));

                        // insert into the global type map for use when generating C++
                        // bindings from Python type hints
                        register_type<Cls>(
                            mod,
                            reinterpret_cast<PyTypeObject*>(ptr(cls))
                        );
                    });
                }

                /* Expose a py::Exception type to Python. */
                template <
                    static_str Name,
                    std::derived_from<Exception> Cls,
                    std::derived_from<Exception>... Bases
                > requires (impl::has_type<Cls> && (impl::has_type<Bases> && ...))
                void type() {
                    if (context.contains(Name)) {
                        throw AttributeError(
                            "Module '" + ModName + "' already has an attribute named '"
                            + Name + "'."
                        );
                    }
                    context[Name] = {
                        .is_exception = true,
                        .callbacks = {
                            [](Module<ModName>& mod) {
                                Type<Cls> cls = Type<Cls>::__python__::__export__(mod);

                                if (PyModule_AddObjectRef(
                                    ptr(mod),
                                    Name,
                                    ptr(cls)
                                )) {
                                    Exception::from_python();
                                }

                                // insert into this module's C++ type map for fast
                                // lookup using C++ template syntax
                                reinterpret_cast<CRTP*>(ptr(mod))->type_map[typeid(Cls)] =
                                    reinterpret_cast<PyTypeObject*>(ptr(cls));

                                // insert into the global exception map for use when
                                // catching Python exceptions in C++
                                register_exception(
                                    mod,
                                    reinterpret_cast<PyTypeObject*>(ptr(cls)),
                                    [](PyObject* value) {
                                        throw reinterpret_steal<Cls>(value);
                                    }
                                );
                            }
                        }
                    };
                }

                // TODO: I might be able to do something similar with functions, in which
                // case I would automatically generate an overload set for each function as
                // it is added, or append to it as needed.  Alternatively, I can use the same
                // attach() method that I'm exposing to the public.

                // TODO: submodule will recursively execute another module's __export__
                // method and attach the resulting module to the current one.  No need
                // for any nested logic.

                /* Add a submodule to this module in order to model nested namespaces at
                the C++ level.  Note that a `py::Module` specialization with the
                corresponding name (appended to the current name) must exist.  Its module
                initialization logic will be invoked in a recursive fashion. */
                template <static_str Name>
                void submodule() {
                    if (context.contains(Name)) {
                        throw AttributeError(
                            "Module '" + ModName + "' already has an attribute named "
                            "'" + Name + "'."
                        );
                    }

                    // TODO: revisit all of this when I try to implement namespaces
                    // properly.

                    static constexpr static_str FullName = ModName + "." + Name;
                    using Mod = Module<FullName>;
                    if constexpr (!impl::is_module<Mod>) {
                        throw TypeError(
                            "No module specialization found for submodule '" + FullName +
                            "'.  Please define a specialization of py::Module with this name and "
                            "fill in its `__python__` initialization struct."
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
                    if (PyObject_SetAttr(
                        reinterpret_cast<PyObject*>(this),
                        impl::TemplateString<Name>::ptr,
                        mod
                    )) {
                        Py_DECREF(mod);
                        Exception::from_python();
                    }
                    Py_DECREF(mod);  // parent now holds the only reference to the submodule
                }

                /* Convert the bindings into a full Module object with a unique type. */
                Module<ModName> finalize() {
                    // configure module's PyType_Spec
                    static std::vector<PyType_Slot> slots = {
                        {
                            Py_tp_doc,
                            const_cast<void*>(
                                reinterpret_cast<const void*>(CRTP::__doc__.buffer)
                            )
                        },
                        {
                            Py_tp_dealloc,
                            reinterpret_cast<void*>(CRTP::__dealloc__)
                        },
                        {
                            Py_tp_traverse,
                            reinterpret_cast<void*>(CRTP::__traverse__)
                        },
                        {
                            Py_tp_clear,
                            reinterpret_cast<void*>(CRTP::__clear__)
                        },
                    };
                    static bool initialized = false;
                    if (!initialized) {
                        if (tp_methods.size()) {
                            tp_methods.push_back({
                                nullptr,
                                nullptr,
                                0,
                                nullptr
                            });
                            slots.push_back({
                                Py_tp_methods,
                                tp_methods.data()
                            });
                        }
                        if (tp_members.size()) {
                            tp_members.push_back({
                                nullptr,
                                0,
                                0,
                                0,
                                nullptr
                            });
                            slots.push_back({
                                Py_tp_members,
                                tp_members.data()
                            });
                        }
                        if (tp_getset.size()) {
                            tp_getset.push_back({
                                nullptr,
                                nullptr,
                                nullptr,
                                nullptr,
                                nullptr
                            });
                            slots.push_back({
                                Py_tp_getset,
                                tp_getset.data()
                            });
                        }
                        slots.push_back({0, nullptr});
                        initialized = true;
                    }
                    static PyType_Spec spec = {
                        .name = typeid(CRTP).name(),
                        .basicsize = sizeof(CRTP),
                        .itemsize = 0,
                        .flags =
                            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE |
                            Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_DISALLOW_INSTANTIATION,
                        .slots = slots.data()
                    };

                    // instantiate the type object
                    PyTypeObject* type = reinterpret_cast<PyTypeObject*>(
                        PyType_FromSpecWithBases(
                            &spec,
                            reinterpret_cast<PyObject*>(&PyModule_Type)
                        )
                    );
                    if (type == nullptr) {
                        Exception::from_python();
                    }

                    // instantiate a module from the type
                    Module<ModName> mod = reinterpret_steal<Module<ModName>>(
                        type->tp_alloc(type, 0)
                    );
                    Py_DECREF(type);  // module now holds the only reference to its type
                    if (ptr(mod) == nullptr) {
                        Exception::from_python();
                    }

                    // execute the callbacks to populate the module
                    for (auto&& [name, ctx] : context) {
                        for (auto&& callback : ctx.callbacks) {
                            callback(mod);
                        }
                    }

                    return mod;
                }

            };

        public:
            static constexpr static_str __doc__ =
                "A Python wrapper around the '" + ModName +
                "' C++ module, generated by Bertrand.";

            /* Expose the module's contents to Python.

            This method will be called during the module's multi-phase initialization
            process, and is where you should add functions, types, and any other
            objects that will be available from its Python namespace.  The `bindings`
            argument is a helper that includes a number of convenience methods for
            exposing various module-level attributes to Python, including variables,
            functions, types, and submodules.

            The `__export__()` method should always return `bindings.finalize()`, which
            will construct the module itself. */
            static Module<ModName> __export__(Bindings bindings) {
                // bindings.var<"x">(MyModule::x);
                // bindings.type<"MyClass", MyModule::MyClass>("docstring for MyClass");
                // bindings.function<"foo">(MyModule::foo, "docstring for foo");
                // ...
                return bindings.finalize();
            };

            /* Import the module in the body of a module initialization function to provide
            an entry point for the Python interpreter.  For instance:

                extern "C" PyObject* PyInit_MyModule() {
                    return Module<"MyModule">::__module__::__import__();
                }

            Note that the module name is ultimately determined by the suffix in the
            function (`MyModule` in this case), and the `extern "C"` portion is required
            to make it visible to the CPython interpreter.  The return type must be
            exactly `PyObject*`. */
            static PyObject* __import__() {
                static PyModuleDef_Slot slots[] = {
                    {
                        Py_mod_create,
                        reinterpret_cast<void*>(__create__)
                    },
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
                };
                return PyModuleDef_Init(&def);
            }

            /* tp_dealloc destroys internal C++ fields */
            static void __dealloc__(CRTP* self) {
                PyObject_GC_UnTrack(self);  // required for heap types
                self->~CRTP();
                PyTypeObject* type = Py_TYPE(self);
                type->tp_free(self);
                Py_DECREF(type);  // required for heap types
            }

            /* tp_traverse registers any Python objects that are owned by the module
            with the cyclic garbage collector. */
            static int __traverse__(CRTP* self, visitproc visit, void* arg) {
                // call Py_VISIT() on any raw PyObject* pointers, or
                // Py_VISIT(ptr(obj)) on any py::Object instances that may contain
                // circular references
                Py_VISIT(Py_TYPE(self));  // required for heap types
                return 0;
            }

            /* tp_clear breaks possible reference cycles for Python objects that are
            owned by the C++ object.  Note that special care has to be taken to avoid
            double-freeing any Python objects between this method and the
            `__dealloc__()` destructor. */
            static int __clear__(CRTP* self) {
                // call Py_CLEAR() on any raw PyObject* pointers, or
                // Py_XDECREF(release(obj)) on any py::Object instances that may
                // contain circular references
                return 0;
            }

        private:
            PyModuleObject base;
            std::unordered_map<std::type_index, PyTypeObject*> type_map;

            /* Create the module during multi-phase initialization.  This also creates
            a unique heap type for the module, allowing for arbitrary members and
            computed properties, which allow for better synchronization of state
            between Python and C++. */
            static PyObject* __create__(PyObject* spec, PyModuleDef* def) {
                try {
                    Module<ModName> mod = CRTP::__export__(Bindings{});

                    // module must use the loader's name to match Python implementation
                    PyObject* loader_name = PyObject_GetAttr(
                        spec,
                        impl::TemplateString<"name">::ptr
                    );
                    if (loader_name == nullptr) {
                        return nullptr;
                    }
                    int rc = PyObject_SetAttr(
                        mod,
                        impl::TemplateString<"__name__">::ptr,
                        loader_name
                    );
                    Py_DECREF(loader_name);
                    if (rc < 0) {
                        return nullptr;
                    }

                    return release(mod);
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

            /* Implements py::impl::get_type<T>(mod). */
            template <std::derived_from<Object> T, static_str Name>
            friend auto get_type(const Module<Name>& mod);
            template <std::derived_from<Object> T>
            auto _get_type() {
                auto it = type_map.find(typeid(T));
                if (it == type_map.end()) {
                    throw KeyError(
                        "Type '" + impl::demangle(typeid(T).name()) + "' not "
                        "found in module '" + ModName + "'."
                    );
                }
                return reinterpret_steal<Type<T>>(it->second);
            }

        };

    };

}


/* A Python module with a unique type, which supports the addition of descriptors for
computed properties, overload sets, etc. */
template <static_str Name>
struct Module : Object {

    Module(PyObject* p, borrowed_t t) : Object(p, t) {}
    Module(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Module>::template enable<Args...>)
    Module(Args&&... args) : Object(
        implicit_ctor<Module>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Module>::template enable<Args...>)
    explicit Module(Args&&... args) : Object(
        explicit_ctor<Module>{},
        std::forward<Args>(args)...
    ) {}

};


/* Default-initializing a py::Module is functionally equivalent to an import
statement. */
template <static_str Name>
struct __init__<Module<Name>>                               : returns<Module<Name>> {
    static auto operator()() {
        PyObject* mod = PyImport_Import(impl::TemplateString<Name>::ptr);
        if (mod == nullptr) {
            throw Exception();
        }
        return reinterpret_steal<Module<Name>>(mod);
    }
};


/* Accessing an attribute of an unspecialized py::Module defaults to returning a
dynamic object. */
template <static_str Name, static_str Attr>
struct __getattr__<Module<Name>, Attr>                       : returns<Object> {};
template <static_str Name, static_str Attr, typename Value> requires (__object__<Value>::enable)
struct __setattr__<Module<Name>, Attr, Value>                : returns<void> {};
template <static_str Name, static_str Attr>
struct __delattr__<Module<Name>, Attr>                       : returns<void> {};


////////////////////
////    TYPE    ////
////////////////////


template <static_str Name>
struct Type<Module<Name>> : Object {

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::template enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::template enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

};


/* Default-initializing a module type retrieves the unique type of the named module. */
template <std::derived_from<impl::ModuleTag> Mod>
struct __init__<Type<Mod>>                                  : returns<Type<Mod>> {
    static auto operator()() {
        return reintepret_steal<Type<Mod>>(
            reinterpret_cast<PyObject*>(Py_TYPE(ptr(Mod())))
        );
    }
};


//////////////////////////////////////
////    BERTRAND.PYTHON MODULE    ////
//////////////////////////////////////


/// TODO: perhaps the play is to have def<> be held in Object, but be restricted to
/// only accepting Type<> definitions.  That might allow me to eliminate the extra tags
/// and standardize everything in the same way.


template <>
struct Module<"bertrand.python">;
template <>
struct Type<Module<"bertrand.python">>;


template <>
struct interface<Module<"bertrand.python">> {

};


template <>
struct interface<Type<Module<"bertrand.python">>> {

};


/* The global `bertrand.python` module, which exposes the internal machinery of the
Python/C++ bindings to Python. */
template <>
struct Module<"bertrand.python"> :
    Object,
    interface<Module<"bertrand.python">>,
    impl::ModuleTag
{

};


/* The global `bertrand.python` module's unique type. */
template <>
struct Type<Module<"bertrand.python">> :
    Object,
    interface<Type<Module<"bertrand.python">>>,
    impl::TypeTag
{
    struct __python__ : def<__python__, Module<"bertrand.python">>, PyModuleObject {
        struct Context {
            std::string demangled_name;
            std::function<void(PyObject*)> exception_callback;
            Type<BertrandMeta>::__python__* template_interface;
        };

        std::unordered_map<std::type_index, Context> cpp_types;
        std::unordered_map<PyTypeObject*, Context> py_types;

    };
};


}  // namespace py


#endif
