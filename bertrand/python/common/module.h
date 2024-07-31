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



template <typename CRTP, StaticStr Name>
struct Object::__module__ : public BertrandTag {
    PyModuleObject base;
    bool initialized;  // indicates whether the module's fields are valid

    inline static std::string __doc__ =
        "A Python wrapper around the '" + Name + "' C++ module, generated by Bertrand.";

    inline static std::vector<PyMethodDef> tp_methods;
    inline static std::vector<PyGetSetDef> tp_getset;
    inline static std::vector<PyMemberDef> tp_members;

    template <StaticStr SubName>
    static Module<Name + "." + SubName> submodule(
        Module<Name>& parent,
        const Str& doc
    ) {
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

    // TODO: perhaps add a var() method for adding a global variable to the
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

    // TODO: there's going to have to be some way of recursively instantiating new types
    // for each submodule as well, since namespaces may have global variables that need
    // to be modeled as property descriptors.  Perhaps what I need to do is just provide
    // a separate py::Module<> specialization for each submodule, and then handle it in
    // all of their __init__/__new__ methods.
    // -> This requires reimplementing the __create__/__exec__ logic in a recursive
    // fashion.  Perhaps this can be done automatically in submodule()?

    // TODO: I also need to consider what happens on a reload, especially as it relates
    // to the module's C++ state.  I could maybe add an operator bool() to the module
    // to indicate whether it is already initialized.  This would always be set to false
    // when the create() method is done, and set to true after exec() is done.


    /* Initialize the module's type object the first time this module is imported.

    Note that this effectively populates that module's `PyType_Spec` structure, which
    is stored with static duration and reused to initialize the module's type whenever
    it is imported within the same process.  As such, this method will only be called
    *once*, the first time the module is imported.  Sub interpreters will still create
    their own unique types using the resulting spec, but the spec itself is only
    generated once.

    The default implementation does nothing, but you can insert calls to the `var()`
    method here in order to add getset descriptors to the module's type.  This is the
    proper way of synchronizing state between Python and C++, and allows Bertrand to
    automatically generate the necessary boilerplate for you.  Typically, that's all
    this method will contain. */
    static void __new__() {};

    /* Initialize the module after it has been created.

    Note that unlike `__new__()`, which is only called once per process, this method is
    called *every* time the module is imported into a fresh interpreter.  It is
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
    static void __init__(CRTP* self) {};

    /* Place this in the body of the module initialization function in order to import
    the module from Python.  For instance:

        auto PyInit_MyModule() {
            return Module<"MyModule">::__module__::__ready__();
        }

    Note that the module name is ultimately determined by the suffix in the function
    name (`MyModule` in this case). */
    static PyObject* __ready__() {
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
            .m_name = Name,
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

    /* Create the module during multi-phase initialization.  This also creates a unique
    heap type for the module, allowing for arbitrary members and computed properties,
    which allow for better synchronization of state between Python and C++. */
    static PyObject* __create__(PyObject* spec, PyModuleDef* def) {
        try {
            // __new__ is only called once, and populates the statically-allocated type
            // spec with all of the necessary slots
            static bool new_called = false;
            if (!new_called) {
                CRTP::__new__();
                CRTP::tp_methods.push_back({nullptr, nullptr, 0, nullptr});
                CRTP::tp_getset.push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
                CRTP::tp_members.push_back({nullptr, 0, 0, 0, nullptr});
                new_called = true;
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
            CRTP::__init__(mod);
            mod->initialized = true;
            return 0;
        } catch (...) {
            Exception::to_python();
            return -1;
        }
    }

};


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


}  // namespace py


#endif  // BERTRAND_PYTHON_COMMON_MODULE_H
