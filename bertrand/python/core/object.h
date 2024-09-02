#ifndef BERTRAND_PYTHON_CORE_OBJECT_H
#define BERTRAND_PYTHON_CORE_OBJECT_H

#include "declarations.h"


namespace py {


/* Retrieve the raw pointer backing a Python object. */
template <impl::inherits<Object> T>
[[nodiscard]] PyObject* ptr(T& obj);


/* Cause a Python object to relinquish ownership over its backing pointer, and then
return the raw pointer. */
template <impl::inherits<Object> T> requires (!std::is_const_v<std::remove_reference_t<T>>)
[[nodiscard]] PyObject* release(T&& obj);


/* Steal a reference to a raw Python object. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(PyObject* obj);


/* Borrow a reference to a raw Python object. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(PyObject* obj);


/* Convert an arbitrary C++ value to an equivalent Python object if it isn't one
already. */
template <typename T> requires (__as_object__<std::remove_cvref_t<T>>::enable)
[[nodiscard]] decltype(auto) as_object(T&& value) {
    using AsObj = __as_object__<std::remove_cvref_t<T>>;
    static_assert(
        !std::same_as<typename AsObj::type, Object>,
        "C++ types cannot be converted to py::Object directly.  Check your "
        "specialization of __as_object__ for this type and ensure the Return type "
        "derives from py::Object, and is not py::Object itself."
    );
    if constexpr (impl::has_call_operator<AsObj>) {
        return AsObj{}(std::forward<T>(value));
    } else {
        return typename AsObj::type(std::forward<T>(value));
    }
}


/* Wrap a non-owning, mutable reference to a C++ object into a `py::Object` proxy that
exposes it to Python.  Note that this only works if a corresponding `py::Object`
subclass exists, which was declared using the `__python__` CRTP helper, and whose C++
type exactly matches the argument.

WARNING: This function is unsafe and should be used with caution.  It is the caller's
responsibility to make sure that the underlying object outlives the wrapper, otherwise
undefined behavior will occur.  It is mostly intended for internal use in order to
expose shared state to Python, for instance to model exported global variables. */
template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::has_cpp<typename __as_object__<T>::type> &&
        impl::is<T, impl::cpp_type<typename __as_object__<T>::type>>
    )
[[nodiscard]] auto wrap(T& obj) -> __as_object__<T>::type;  // defined in ops.h


/* Wrap a non-owning, immutable reference to a C++ object into a `py::Object` proxy
that exposes it to Python.  Note that this only works if a corresponding `py::Object`
subclass exists, which was declared using the `__python__` CRTP helper, and whose C++
type exactly matches the argument.

WARNING: This function is unsafe and should be used with caution.  It is the caller's
responsibility to make sure that the underlying object outlives the wrapper, otherwise
undefined behavior will occur.  It is mostly intended for internal use in order to
expose shared state to Python, for instance to model exported global variables. */
template <typename T>
    requires (
        __as_object__<T>::enable &&
        impl::has_cpp<typename __as_object__<T>::type> &&
        impl::is<T, impl::cpp_type<typename __as_object__<T>::type>>
    )
[[nodiscard]] auto wrap(const T& obj) -> __as_object__<T>::type;  // defined in ops.h


/* Retrieve a reference to the internal C++ object that backs a `py::Object` wrapper,
if such an object exists.  Does nothing if called on a pure Python or naked C++
object.  If the wrapper does not own the backing object, this method will follow the
internal pointer to resolve the reference. */
template <typename T>
[[nodiscard]] auto& unwrap(T& obj);  // defined in ops.h


/* Retrieve a reference to the internal C++ object that backs a `py::Object` wrapper,
if such an object exists.  Does nothing if called on a pure Python or naked C++
object.  If the wrapper does not own the backing object, this method will follow the
internal pointer to resolve the reference. */
template <typename T>
[[nodiscard]] const auto& unwrap(const T& obj);  // defined in ops.h


template <>
struct Interface<Object> {};


/* An owning reference to a dynamically-typed Python object.  More specialized types
can always be implicitly converted to this type, but doing the opposite incurs a
runtime `isinstance()` check, and raises a `TypeError` if the check fails. */
struct Object : Interface<Object> {
private:

    /* A convenience struct implementing the overload pattern for visiting a
    std::variant. */
    template <typename... Ts>
    struct Visitor : Ts... {
        using Ts::operator()...;
    };

    /* A helper class that simplifies the creation of new Python types within a
    `def::__export__()` script, which exposes the type's interface to Python.  `Cls`
    refers to the type being exposed, and may refer to either an external C++ type or
    the CRTP type in the case of inline Python types.  All bound methods/variables must
    either originate from that class or use a non-member method which takes the class
    as its first argument, unless the method/variable is static and unassociated with
    any instance. */
    template <typename Cls, typename CRTP, typename Wrapper, StaticStr ModName>
    struct Bindings;

protected:
    PyObject* m_ptr;

    struct borrowed_t {};
    struct stolen_t {};

    template <impl::inherits<Object> T>
    friend PyObject* ptr(T&);
    template <impl::inherits<Object> T> requires (!std::is_const_v<std::remove_reference_t<T>>)
    friend PyObject* release(T&&);
    template <std::derived_from<Object> T>
    friend T reinterpret_borrow(PyObject*);
    template <std::derived_from<Object> T>
    friend T reinterpret_steal(PyObject*);
    template <typename T>
    friend auto& unwrap(T& obj);
    template <typename T>
    friend const auto& unwrap(const T& obj);

    template <typename T>
    struct implicit_ctor {
        template <typename... Args>
        static constexpr bool enable =
            __init__<T, Args...>::enable &&
            std::is_invocable_r_v<T, __init__<T, Args...>, Args...>;
    };

    template <typename T>
    struct explicit_ctor {
        template <typename... Args>
        static constexpr bool enable =
            !__init__<T, Args...>::enable &&
            __explicit_init__<T, Args...>::enable &&
            std::is_invocable_r_v<T, __explicit_init__<T, Args...>, Args...>;
    };

    template <std::derived_from<Object> T, typename... Args>
    Object(implicit_ctor<T>, Args&&... args) : m_ptr(release((
        Interpreter::init(),  // comma operator
        __init__<T, Args...>{}(std::forward<Args>(args)...)
    ))) {
        using Return = std::invoke_result_t<__init__<T, Args...>, Args...>;
        static_assert(
            std::same_as<Return, T>,
            "__init__<T, Args...> must return an instance of T."
        );
    }

    template <std::derived_from<Object> T, typename... Args>
    Object(explicit_ctor<T>, Args&&... args) : m_ptr(release((
        Interpreter::init(),  // comma operator
        __explicit_init__<T, Args...>{}(std::forward<Args>(args)...)
    ))) {
        using Return = std::invoke_result_t<__explicit_init__<T, Args...>, Args...>;
        static_assert(
            std::same_as<Return, T>,
            "__explicit_init__<T, Args...> must return an instance of T."
        );
    }

    /* A CRTP base class that generates bindings for a new Python type that wraps an
    external C++ type.

    This class stores a variant containing either a full instance of or a (possibly
    const) pointer to the C++ type being wrapped, and can contain no other data
    members.  It should only implement the `__export__()` script, which uses the
    binding helpers to expose the C++ type's interface to Python. */
    template <typename CRTP, typename Derived, typename CppType = void>
    struct def : PyObject, impl::PythonTag {
    protected:

        template <typename... Ts>
        using Visitor = Visitor<Ts...>;

        template <StaticStr ModName>
        using Bindings = Object::Bindings<CppType, CRTP, Derived, ModName>;

    public:
        using __object__ = Derived;
        using __cpp__ = CppType;
        std::variant<CppType, CppType*, const CppType*> m_cpp;

        template <typename... Args>
        def(Args&&... args) : m_cpp(std::forward<Args>(args)...) {}

        /* Generate a new instance of the type object and define its Python interface.
        Every type should implement this method, which will be called whenever an
        enclosing module is imported.

        This method must end with a call to `bind.finalize<Bases...>()`, which will
        instantiate a unique heap type for each module. */
        template <StaticStr ModName>
        static Type<Derived> __export__(Bindings<ModName> bind);  // defined in type.h {
        //     // bind.var<"foo">(&CppType::foo);
        //     // bind.method<"bar">("an example method", &CppType::bar);
        //     // bind.type<"Baz", CppType::Baz, Bases...>();
        //     // ...
        //     return bind.finalize();
        // }

        /* Return a new reference to the type object.  This method must be implemented
        by all Python types, and will be called by the default constructor for
        `Type<T>`.

        This method should import the parent module and unpack this specific type
        object, implicitly invoking the `__export__()` method if the module was not
        previously loaded. */
        static Type<Derived> __import__() {
            /// TODO: this can be defined by default using the global registry held
            /// within the bertrand.python module at the C++ level.
            // return Module<"bertrand.python">().as_object<Derived>();
            // return Module<"bertrand.python">().as_object(derived);  // python style
            // return bertrand.python.as_object(derived);  // in Python
        }

        /* Default `tp_dealloc` calls the Python type's C++ destructor. */
        static void __dealloc__(CRTP* self) {
            PyTypeObject* type = Py_TYPE(self);
            if (type->tp_flags & Py_TPFLAGS_HAVE_GC) {
                PyObject_GC_UnTrack(self);
            }
            self->~CRTP();  // cleans up all C++ resources
            type->tp_free(self);
            if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
                Py_DECREF(type);  // required for heap types
            }
        }

        /* Default `tp_traverse` does nothing by default.  Users can override this
        to implement a GC traversal function, but should always call the base
        class's implementation in their return statement. */
        static int __traverse__(CRTP* self, visitproc visit, void* arg) {
            if (Py_TYPE(self)->tp_flags & Py_TPFLAGS_HEAPTYPE) {
                Py_VISIT(Py_TYPE(self));  // required for heap types
            }
            return 0;
        }

        /* Default `tp_clear` does nothing by default.  Users can override this
        to implement a GC clear function, but should always call the base class's
        implementation in their return statement. */
        static int __clear__(CRTP* self) {
            return 0;
        }

    };

    /* A CRTP base class that generates bindings for a pure Python class, which has no
    C++ equivalent.  This is the default specialization if the C++ type is omitted
    during inheritance.

    This class expects the user to either implement a new CPython type directly inline
    or supply a reference to an externally-defined `PyObject` type in its `__import__()`
    script.  In both cases, the CRTP class must also derive from the Python type it is
    modeling, defaulting to `PyObject` if the type is implemented inline and has no
    bases.  This allows inheritance from built-in types like `PyListObject`,
    `PyTypeObject`, `PyModuleObject`, etc. as long as the derived type is
    binary-compatible with `PyObject`.  This means that the binding type MUST NOT:

        -   Be virtual, which would add a vtable to the structure that Python cannot
            read.
        -   Use multiple inheritance, unless the other base classes do not introduce
            any additional data members or virtual functions that would affect the
            binary layout.
        -   Attempt to initialize, destroy, or otherwise corrupt any of the internal
            Python fields, which are managed by the interpreter and must be left alone.

    Other than that, the user is free to implement any number of internal fields at the
    C++ level, define constructors and destructors, and override any of Python's dunder
    methods as they see fit.  Bertrand will ensure that these hooks are called whenever
    the Python object is created, destroyed, or otherwise manipulated from Python. */
    template <typename CRTP, typename Derived>
    struct def<CRTP, Derived, void> : impl::PythonTag {
    protected:

        template <StaticStr ModName>
        using Bindings = Object::Bindings<CRTP, CRTP, Derived, ModName>;

    public:
        using __object__ = Derived;
        using __cpp__ = void;

        /// TODO: each type should only implement one of either __export__ or
        /// __import__.  If they define __export__, then a constexpr branch will be
        /// taken in the default __import__ which imports the bertrand.python module
        /// and retrieves the type from the global registry, similar to C++ types.

        /* Return a new reference to the type object.  This method must be implemented
        by all Python types, and will be called by the default constructor for
        `Type<T>`.

        If the user is defining an inline Python type, this method should import the
        parent module and unpack this specific type, implicitly invoking the
        `__export__()` method if the module was not previously loaded.  If the user is
        exposing an existing Python type, this method should simply borrow a reference
        to the existing type object. */
        static Type<Derived> __import__();  // defined in except.h {
        //     throw NotImplementedError(
        //         "the __import__() method must be defined for all Python types: "
        //         + impl::demangle(typeid(CRTP).name())
        //     );
        // }

        /* Default `tp_dealloc` calls the Python type's C++ destructor. */
        static void __dealloc__(CRTP* self) {
            PyTypeObject* type = Py_TYPE(self);
            if (type->tp_flags & Py_TPFLAGS_HAVE_GC) {
                PyObject_GC_UnTrack(self);
            }
            self->~CRTP();  // cleans up all C++ resources
            type->tp_free(self);
            if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
                Py_DECREF(type);  // required for heap types
            }
        }

        /* Default `tp_traverse` does nothing by default.  Users can override this
        to implement a GC traversal function, but should always call the base
        class's implementation in their return statement. */
        static int __traverse__(CRTP* self, visitproc visit, void* arg) {
            if (Py_TYPE(self)->tp_flags & Py_TPFLAGS_HEAPTYPE) {
                Py_VISIT(Py_TYPE(self));  // required for heap types
            }
            return 0;
        }

        /* Default `tp_clear` does nothing by default.  Users can override this
        to implement a GC clear function, but should always call the base class's
        implementation in their return statement. */
        static int __clear__(CRTP* self) {
            return 0;
        }

    };

    /* A CRTP base class that generates bindings for an importable Python module, which
    can contain any number of types, functions, variables, and submodules.

    This class is the primary entry point for the Python interpreter, and will load the
    module according to multi-phase initialization rules.  That means each interpreter
    will hold its own unique module instance, with implications for cross-interpreter
    communication and parallelization, as part of ongoing work to bypass the GIL.

    Each module must minimally implement the `__export__()` script, which uses the
    binding helpers to expose the module's interface to Python.  This class then
    defines a corresponding `__import__()` script, which should be returned from the
    body of the module initialization function, as shown below:

        extern "C" PyObject* PyInit_name() {
            return Module<"name">::__python__::__import__();
        }

    The `extern "C"` linkage is required to prevent name mangling and allow the Python
    interpreter to load the module, which calls its `__export__()` script to recursively
    load the module's contents.  The module's name is set by the `name` portion of the
    `PyInit_name` signature, which must exactly match the final component of the
    module's dotted name.

    In order to facilitate state management between Python and C++, as well as to
    simplify access to internal members, each module has a unique type that is created
    as part of the import process.  As a result, users can implement module-level C++
    fields directly within this class as if it were a normal Python type, and bind them
    in exactly the same way.  Such fields are guaranteed to be unique for each module,
    and are therefore safe to access from multiple threads, provided that the user
    ensures that the data they reference is thread-safe. */
    template <typename CRTP, StaticStr Name>
    struct def<CRTP, Module<Name>, void> : impl::PythonTag {
    protected:

        template <StaticStr ModName>
        using Bindings = Object::Bindings<CRTP, CRTP, Module<ModName>, ModName>;

    public:
        using __object__ = Module<Name>;
        using __cpp__ = void;

        /// TODO: docs
        template <StaticStr ModName>
        static Module<Name> __export__(Bindings<ModName> bind);  // defined in type.h {
        //     return CRTP::__import__();
        // }

        /// TODO: docs
        static PyObject* __import__() {
            /// TODO: return multi-phase initialization object
        }

        /* Default `tp_dealloc` calls the Python type's C++ destructor. */
        static void __dealloc__(CRTP* self) {
            PyTypeObject* type = Py_TYPE(self);
            if (type->tp_flags & Py_TPFLAGS_HAVE_GC) {
                PyObject_GC_UnTrack(self);
            }
            self->~CRTP();  // cleans up all C++ resources
            type->tp_free(self);
            if (type->tp_flags & Py_TPFLAGS_HEAPTYPE) {
                Py_DECREF(type);  // required for heap types
            }
        }

        /* Default `tp_traverse` does nothing by default.  Users can override this
        to implement a GC traversal function, but should always call the base
        class's implementation in their return statement. */
        static int __traverse__(CRTP* self, visitproc visit, void* arg) {
            if (Py_TYPE(self)->tp_flags & Py_TPFLAGS_HEAPTYPE) {
                Py_VISIT(Py_TYPE(self));  // required for heap types
            }
            return 0;
        }

        /* Default `tp_clear` does nothing by default.  Users can override this
        to implement a GC clear function, but should always call the base class's
        implementation in their return statement. */
        static int __clear__(CRTP* self) {
            return 0;
        }

    };

public:
    struct __python__ : def<__python__, Object>, PyObject {
        static Type<Object> __import__();  // defined in type.h
    };

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const Object& other) : m_ptr(Py_XNewRef(ptr(other))) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(Object&& other) : m_ptr(release(other)) {}

    /* reinterpret_borrow() constructor.  Borrows a reference to a raw Python pointer. */
    Object(PyObject* p, borrowed_t) : m_ptr(Py_XNewRef(p)) {}

    /* reinterpret_steal() constructor.  Steals a reference to a raw Python pointer. */
    Object(PyObject* p, stolen_t) : m_ptr(p) {}

    /* Universal implicit constructor.  Implemented via the __init__ control struct. */
    template <typename... Args> requires (implicit_ctor<Object>::enable<Args...>)
    Object(Args&&... args) : Object(
        implicit_ctor<Object>{},
        std::forward<Args>(args)...
    ) {}

    /* Universal explicit constructor.  Implemented via the __explicit_init__ control
    struct. */
    template <typename... Args> requires (explicit_ctor<Object>::enable<Args...>)
    explicit Object(Args&&... args) : Object(
        explicit_ctor<Object>{},
        std::forward<Args>(args)...
    ) {}

    /* Destructor.  Allows any object to be stored with static duration. */
    ~Object() noexcept {
        if (Py_IsInitialized()) {
            Py_XDECREF(m_ptr);
        }
    }

    /* Copy assignment operator. */
    Object& operator=(const Object& other) {
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = Py_XNewRef(ptr(other));
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    Object& operator=(Object&& other) {
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = ptr(other);
            other.m_ptr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Access an internal member of the underlying PyObject* pointer. */
    template <impl::has_python Self>
    [[nodiscard]] std::remove_cvref_t<Self>::__python__* operator->(this Self&& self) {
        using Ptr = std::remove_cvref_t<Self>::__python__;
        return reinterpret_cast<Ptr*>(ptr(self));
    }

    /* Check for exact pointer identity. */
    template <typename Self, impl::inherits<Object> T>
    [[nodiscard]] bool is(this Self&& self, T&& other) {
        return ptr(self) == ptr(other);
    }

    /* Check for exact pointer identity. */
    template <typename Self>
    [[nodiscard]] bool is(this Self&& self, PyObject* other) {
        return ptr(self) == other;
    }

    /* Contains operator.  Equivalent to Python's `in` keyword, but with reversed
    operands (i.e. `x in y` -> `y.contains(x)`).  This is consistent with other STL
    container types, and the allowable key types can be specified via the __contains__
    control struct. */
    template <typename Self, typename Key> requires (__contains__<Self, Key>::enable)
    [[nodiscard]] bool contains(this Self&& self, Key&& key);

    /* Contextually convert an Object into a boolean value for use in if/else 
    statements, with the same semantics as in Python. */
    template <typename Self>
    [[nodiscard]] explicit operator bool(this Self&& self);

    /* Universal implicit conversion operator.  Implemented via the __cast__ control
    struct. */
    template <typename Self, typename T>
        requires (
            __cast__<Self, T>::enable &&
            std::is_invocable_r_v<T, __cast__<Self, T>, Self>
        )
    [[nodiscard]] operator T(this Self&& self) {
        return __cast__<Self, T>{}(std::forward<Self>(self));
    }

    /* Universal explicit conversion operator.  Implemented via the __explicit_cast__
    control struct. */
    template <typename Self, typename T>
        requires (
            !__cast__<Self, T>::enable &&
            __explicit_cast__<Self, T>::enable &&
            std::is_invocable_r_v<T, __explicit_cast__<Self, T>, Self>
        )
    [[nodiscard]] explicit operator T(this Self&& self) {
        return __explicit_cast__<Self, T>{}(std::forward<Self>(self));
    }

    /* Call operator.  This can be enabled for specific argument signatures and return
    types via the __call__ control struct, enabling static type safety for callable
    Python objects in C++. */
    template <typename Self, typename... Args> requires (__call__<Self, Args...>::enable)
    decltype(auto) operator()(this Self&& self, Args&&... args) {
        using call = __call__<Self, Args...>;
        using Return = typename call::type;
        static_assert(
            std::is_void_v<Return> || std::derived_from<Return, Object>,
            "Call operator must return either void or a py::Object subclass.  "
            "Check your specialization of __call__ for the given arguments and "
            "ensure that it is derived from py::Object."
        );
        if constexpr (impl::has_call_operator<call>) {
            return call{}(std::forward<Self>(self), std::forward<Args>(args)...);

        /// TODO: maybe all of these special cases can be implemented as specializations
        /// of their respective control structures in type.h???
        } else if constexpr (impl::has_cpp<Self>) {
            static_assert(
                std::is_invocable_r_v<Return, impl::cpp_type<Self>, Args...>,
                "__call__<Self, Args...> is enabled for operands whose C++ "
                "representations have no viable overload for `Self(Args...)`"
            );
            /// TODO: this needs to do argument translation similar to below for
            /// Python functions.  This will necessarily involve some kind of
            /// unpacking.  Or will it if the C++ call operator only accepts
            /// positional arguments?  I'll still have to account for variadic
            /// unpacking one way or another.
            if constexpr (std::is_void_v<Return>) {
                unwrap(self)(unwrap(std::forward<Args>(args))...);
            } else {
                return unwrap(self)(unwrap(std::forward<Args>(args))...);
            }

        } else {
            if constexpr (std::is_void_v<Return>) {
                Function<Return(*)(Args...)>::template invoke_py<Return>(
                    ptr(self),
                    std::forward<Args>(args)...
                );
            } else {
                return Function<Return(*)(Args...)>::template invoke_py<Return>(
                    ptr(self),
                    std::forward<Args>(args)...
                );
            }
        }
    }

    /* Index operator.  Specific key and element types can be controlled via the
    __getitem__, __setitem__, and __delitem__ control structs. */
    template <typename Self, typename... Key>
        requires (__getitem__<Self, Key...>::enable)
    decltype(auto) operator[](this Self&& self, Key&&... key);

};


template <>
struct Interface<Type<Object>> {};


template <impl::inherits<Object> T>
[[nodiscard]] PyObject* ptr(T& obj) {
    return obj.m_ptr;
}


template <impl::inherits<Object> T> requires (!std::is_const_v<std::remove_reference_t<T>>)
[[nodiscard]] PyObject* release(T&& obj) {
    PyObject* temp = obj.m_ptr;
    obj.m_ptr = nullptr;
    return temp;
}


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(PyObject* ptr) {
    return T(ptr, Object::borrowed_t{});
}


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(PyObject* ptr) {
    return T(ptr, Object::stolen_t{});
}


template <typename T, impl::is<Object> Base>
struct __isinstance__<T, Base>                              : Returns<bool> {
    static constexpr bool operator()(T&& obj) {
        return std::derived_from<std::remove_cvref_t<T>, Object>;
    }
    static constexpr bool operator()(T&& obj, Base&& cls);  // defined in ops.h
};


template <typename T, impl::is<Object> Base>
struct __issubclass__<T, Base>                              : Returns<bool> {
    using U = std::remove_cvref_t<T>;
    static constexpr bool operator()() {
        return std::derived_from<U, Object>;
    }
    static constexpr bool operator()(T&& obj) {
        if constexpr (impl::dynamic_type<U>) {
            return PyType_Check(ptr(obj));
        } else {
            return impl::type_like<U>;
        }
    }
    static bool operator()(T&& obj, Base&& cls);  // defined in ops.h
};


/* Default initialize py::Object to None. */
template <>
struct __init__<Object>                                     : Returns<Object> {
    static auto operator()() {
        return reinterpret_steal<Object>(Py_NewRef(Py_None));
    }
};


/* Implicitly convert any C++ value into a py::Object by invoking as_object(). */
template <impl::cpp_like T> requires (__as_object__<T>::enable)
struct __init__<Object, T>                                  : Returns<Object> {
    static auto operator()(T&& value) {
        return reinterpret_steal<Object>(
            release(as_object(std::forward<T>(value)))
        );
    }
};


/* Implicitly convert a lazily-evaluated attribute or item wrapper into a normalized
Object instance. */
template <impl::inherits<Object> Self, impl::lazily_evaluated T>
    requires (std::convertible_to<impl::lazy_type<T>, Self>)
struct __init__<Self, T>                                    : Returns<Self> {
    static Self operator()(T&& value) {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return reinterpret_borrow<impl::lazy_type<T>>(ptr(value));
        } else {
            return reinterpret_steal<impl::lazy_type<T>>(release(value));
        }
    }
};


/* Explicitly convert a lazily-evaluated attribute or item wrapper into a normalized
Object instance. */
template <impl::inherits<Object> Self, impl::lazily_evaluated T>
    requires (std::constructible_from<Self, impl::lazy_type<T>>)
struct __explicit_init__<Self, T>                           : Returns<Self> {
    static auto operator()(T&& value) {
        if constexpr (std::is_lvalue_reference_v<T>) {
            return Self(reinterpret_borrow<impl::lazy_type<T>>(ptr(value)));
        } else {
            return Self(reinterpret_steal<impl::lazy_type<T>>(release(value)));
        }
    }
};


/// NOTE: additional delegating constructors for objects using Python-level
/// `__new__()`/`__init__()` are defined in core.h


/* Implicitly convert a Python object into one of its subclasses by applying a runtime
`isinstance()` check. */
template <impl::inherits<Object> From, impl::inherits<From> To>
struct __cast__<From, To>                                   : Returns<To> {
    static auto operator()(From&& from);  // defined in ops.h
};


/* Implicitly convert a Python object into any recognized C++ type by checking for an
equivalent Python type via __as_object__, implicitly converting to that type, and then
implicitly converting the result to the C++ type in a 2-step process. */
template <impl::is<Object> From, impl::cpp_like To> requires (__as_object__<To>::enable)
struct __cast__<From, To>                                   : Returns<To> {
    static auto operator()(From&& self) {
        using Intermediate = __as_object__<To>::type;
        return impl::implicit_cast<To>(
            impl::implicit_cast<Intermediate>(std::forward<From>(self))
        );
    }
};


/* Explicitly convert a Python object into any C++ type by checking for an equivalent
Python type via __as_object__, explicitly converting to that type, and then explicitly
converting to the C++ type in a 2-step process. */
template <impl::inherits<Object> From, impl::cpp_like To>
    requires (__as_object__<To>::enable)
struct __explicit_cast__<From, To>                          : Returns<To> {
    static auto operator()(From&& from) {
        using Intermediate = __as_object__<To>::type;
        return static_cast<To>(
            static_cast<Intermediate>(std::forward<From>(from))
        );
    }
};


/* Explicitly convert a Python object into a C++ integer by calling `int(obj)` at the
Python level. */
template <impl::inherits<Object> From, impl::cpp_like To>
    requires (__as_object__<To>::enable && std::integral<To>)
struct __explicit_cast__<From, To>                          : Returns<To> {
    static To operator()(From&& from);  // defined in ops.h
};


/* Explicitly convert a Python object into a C++ floating-point number by calling
`float(obj)` at the Python level. */
template <impl::inherits<Object> From, impl::cpp_like To>
    requires (__as_object__<To>::enable && std::floating_point<To>)
struct __explicit_cast__<From, To>                          : Returns<To> {
    static To operator()(From&& from);  // defined in ops.h
};


/* Explicitly convert a Python object into a C++ complex number by calling
`complex(obj)` at the Python level. */
template <impl::inherits<Object> From, typename Float>
struct __explicit_cast__<From, std::complex<Float>> : Returns<std::complex<Float>> {
    static auto operator()(From&& from);  // defined in ops.h
};


/* Explicitly convert a Python object into a C++ sd::string representation by calling
`str(obj)` at the Python level. */
template <impl::inherits<Object> From, typename Char>
struct __explicit_cast__<From, std::basic_string<Char>> : Returns<std::basic_string<Char>> {
    static auto operator()(From&& from);  // defined in ops.h
};


template <impl::is<Object> Self, StaticStr Name>
struct __getattr__<Self, Name>                              : Returns<Object> {};
template <impl::is<Object> Self, StaticStr Name, std::convertible_to<Object> Value>
struct __setattr__<Self, Name, Value>                       : Returns<void> {};
template <impl::is<Object> Self, StaticStr Name>
struct __delattr__<Self, Name>                              : Returns<void> {};
template <impl::is<Object> Self, std::convertible_to<Object>... Key>
struct __getitem__<Self, Key...>                            : Returns<Object> {};
template <impl::is<Object> Self, std::convertible_to<Object> Value, std::convertible_to<Object>... Key>
struct __setitem__<Self, Value, Key...>                     : Returns<void> {};
template <impl::is<Object> Self, std::convertible_to<Object>... Key>
struct __delitem__<Self, Key...>                            : Returns<void> {};
template <impl::is<Object> Self, std::convertible_to<Object> Key>
struct __contains__<Self, Key>                              : Returns<bool> {};
template <impl::is<Object> Self>
struct __len__<Self>                                        : Returns<size_t> {};
template <impl::is<Object> Self>
struct __iter__<Self>                                       : Returns<Object> {};
template <impl::is<Object> Self>
struct __reversed__<Self>                                   : Returns<Object> {};
template <impl::is<Object> Self, typename ... Args>
struct __call__<Self, Args...>                              : Returns<Object> {};
template <impl::is<Object> Self>
struct __hash__<Self>                                       : Returns<size_t> {};
template <impl::is<Object> Self>
struct __abs__<Self>                                        : Returns<Object> {};
template <impl::is<Object> Self>
struct __invert__<Self>                                     : Returns<Object> {};
template <impl::is<Object> Self>
struct __pos__<Self>                                        : Returns<Object> {};
template <impl::is<Object> Self>
struct __neg__<Self>                                        : Returns<Object> {};
template <impl::is<Object> Self>
struct __increment__<Self>                                  : Returns<Object&> {};
template <impl::is<Object> Self>
struct __decrement__<Self>                                  : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __lt__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __lt__<L, R>                                         : Returns<bool> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __le__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __le__<L, R>                                         : Returns<bool> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __eq__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __eq__<L, R>                                         : Returns<bool> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __ne__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __ne__<L, R>                                         : Returns<bool> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __ge__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __ge__<L, R>                                         : Returns<bool> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __gt__<L, R>                                         : Returns<bool> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __gt__<L, R>                                         : Returns<bool> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __add__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __add__<L, R>                                        : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __sub__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __sub__<L, R>                                        : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __mul__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __mul__<L, R>                                        : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __truediv__<L, R>                                    : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __truediv__<L, R>                                    : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __floordiv__<L, R>                                   : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __floordiv__<L, R>                                   : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __mod__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __mod__<L, R>                                        : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __pow__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __pow__<L, R>                                        : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __lshift__<L, R>                                     : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __lshift__<L, R>                                     : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __rshift__<L, R>                                     : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __rshift__<L, R>                                     : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __and__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __and__<L, R>                                        : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __or__<L, R>                                         : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __or__<L, R>                                         : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __xor__<L, R>                                        : Returns<Object> {};
template <std::convertible_to<Object> L, impl::is<Object> R> requires (!impl::is<L, Object>)
struct __xor__<L, R>                                        : Returns<Object> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __iadd__<L, R>                                       : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __isub__<L, R>                                       : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __imul__<L, R>                                       : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __itruediv__<L, R>                                   : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __ifloordiv__<L, R>                                  : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __imod__<L, R>                                       : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __ipow__<L, R>                                       : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __ilshift__<L, R>                                    : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __irshift__<L, R>                                    : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __iand__<L, R>                                       : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __ior__<L, R>                                        : Returns<Object&> {};
template <impl::is<Object> L, std::convertible_to<Object> R>
struct __ixor__<L, R>                                       : Returns<Object&> {};


/* Inserting an object into an output stream corresponds to a `str()` call at the
Python level. */
template <std::derived_from<std::ostream> Stream, impl::inherits<Object> Self>
struct __lshift__<Stream, Self>                             : Returns<Stream&> {
    /// TODO: Str, Bytes, ByteArray should specialize this to write to the stream directly.
    static Stream& operator()(Stream& stream, Self&& self);  // defined in ops.h
};


template <impl::inherits<Object> T>
struct __as_object__<T>                                     : Returns<T> {
    static decltype(auto) operator()(T&& value) { return std::forward<T>(value); }
};


}  // namespace py


#endif
