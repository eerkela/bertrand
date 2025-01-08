#ifndef BERTRAND_PYTHON_CORE_OBJECT_H
#define BERTRAND_PYTHON_CORE_OBJECT_H

#include "declarations.h"


namespace py {


namespace impl {

    /* Wrap a non-owning, mutable reference to a C++ object into a `py::Object` proxy that
    exposes it to Python.  Note that this only works if a corresponding `py::Object`
    subclass exists, which was declared using the `__python__` CRTP helper, and whose C++
    type exactly matches the argument. */
    template <cpp T>
        requires (
            has_python<T> &&
            has_cpp<python_type<T>> &&
            is<T, cpp_type<python_type<T>>>
        )
    [[nodiscard]] auto wrap(T& obj) -> python_type<T>;

    /* Wrap a non-owning, immutable reference to a C++ object into a `py::Object` proxy
    that exposes it to Python.  Note that this only works if a corresponding `py::Object`
    subclass exists, which was declared using the `__python__` CRTP helper, and whose C++
    type exactly matches the argument. */
    template <cpp T>
        requires (
            has_python<T> &&
            has_cpp<python_type<T>> &&
            is<T, cpp_type<python_type<T>>>
        )
    [[nodiscard]] auto wrap(const T& obj) -> python_type<T>;

    /* Retrieve a reference to the internal C++ object that backs a `py::Object` wrapper,
    if such an object exists.  Does nothing if called on a pure Python or naked C++
    object.  If the wrapper does not own the backing object, this method will follow the
    internal pointer to resolve the reference. */
    template <python T> requires (has_cpp<T>)
    [[nodiscard]] auto& unwrap(T& obj);

    /* Retrieve a reference to the internal C++ object that backs a `py::Object` wrapper,
    if such an object exists.  Does nothing if called on a pure Python or naked C++
    object.  If the wrapper does not own the backing object, this method will follow the
    internal pointer to resolve the reference. */
    template <python T> requires (has_cpp<T>)
    [[nodiscard]] const auto& unwrap(const T& obj);

}


/* Retrieve the raw pointer backing a Python object. */
template <impl::inherits<Object> T>
[[nodiscard]] PyObject* ptr(T&& obj) {
    return std::forward<T>(obj).m_ptr;
}


/* Access an internal member of the underlying PyObject* pointer by casting it to its
internal `__python__` representation.  The Bertrand object model and type safety
guarantees should ensure that this is safe for general use, but it is possible to
invoke undefined behavior if the underlying pointer is improperly initialized (by
accessing an internal field of a moved-from object, or through improper use of
`release()` or the `reinterpret()` family of functions). */
template <impl::has_python Self>
[[nodiscard]] auto reinterpret(Self&& self) {
    if constexpr (DEBUG) {
        assert_(ptr(self) != nullptr, "Cannot reinterpret a null object.");
    }
    using Ptr = std::remove_cvref_t<Self>::__python__;
    return reinterpret_cast<Ptr*>(
        ptr(std::forward<Self>(self))
    );
}


/* Cause a Python object to relinquish ownership over its backing pointer, and then
return the raw pointer. */
template <impl::inherits<Object> T> requires (!std::is_const_v<std::remove_reference_t<T>>)
[[nodiscard]] PyObject* release(T&& obj) {
    PyObject* temp = obj.m_ptr;
    obj.m_ptr = nullptr;
    return temp;
}


/* Steal a reference to a raw Python object. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(PyObject* obj);


/* Borrow a reference to a raw Python object. */
template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(PyObject* obj);


/* Convert an arbitrary value into a Python object if it is not one already.

Note that this method respects reference semantics as closely as possible, meaning that
if you provide an lvalue that can be directly represented in Python without requiring
any conversions (i.e. the C++ type and Python type are 1:1 correspondant), then the
resulting Python object will hold a pointer to the value, without copying.  As a
result, the caller is responsible for ensuring that the underlying object outlives the
Python object, otherwise undefined behavior will occur.  This can be prevented either
by explicitly moving the C++ object when this function is called, or by manually
generating a copy and passing it as a temporary, both of which will insert the value
into the Python object directly.

This function is considered to be lower-level than calling a Python constructor
directly, and is mostly intended for internal use when converting C++ return types to
Python in a way that respects reference semantics, and in other cases where both Python
and C++ need to share state.  Always prefer calling `py::obj<T>()` or an equivalent
Python constructor when the lifetime of the object is uncertain, especially when the
result is passed up to Python, and may therefore acquire outside references. */
template <impl::has_python T> requires (!std::same_as<impl::python_type<T>, Object>)
[[nodiscard]] decltype(auto) to_python(T&& value) {
    if constexpr (impl::python<T>) {
        return std::forward<T>(value);
    } else if constexpr (
        std::is_lvalue_reference_v<T> &&
        impl::is<T, impl::cpp_type<impl::python_type<T>>>
    ) {
        return impl::wrap(std::forward<T>(value));
    } else {
        return impl::python_type<T>(std::forward<T>(value));
    }
}


/* If the argument is a Python object that wraps around a C++ object, then return a
reference to that object, otherwise return the argument as-is.

This function is the inverse of `to_python()`, and is used to extract an lvalue
reference to the underlying C++ value of a Python object when passed as a parameter to
a C++ function that expects a matching type.  By unwrapping the C++ value directly, we
can bypass any additional copies or conversions, and allow mutable lvalues to bind to
Python objects appropriately, without requiring any additional steps.  If the argument
is already a C++ object or a pure Python type that has no C++ equivalent, then this
function will return it as-is, in which case conversion operators will be considered
like normal.

Note that the same lifetime considerations apply to this function as for `to_python()`,
meaning that if a Python object is unwrapped into a C++ reference, the Python object
must outlive the reference, otherwise undefined behavior will occur.  Additionally, if
a mutable reference is returned, it is generally unsafe to move from it, as the Python
wrapper will be left in an invalid state.  It is safe to modify such an object
in-place, however, in which case the changes will also be reflected in Python, as long
as doing so does not violate any immutability guarantees on the Python side.

If the lifetime of the Python object is uncertain, or if the result of this function
is subsequently stored elsewhere, then it is always preferable to rely on the object's
implicit conversion operators, which always create copies of the underlying value,
rather than referencing it directly. */
template <typename T>
[[nodiscard]] decltype(auto) from_python(T&& obj) {
    if constexpr (impl::python<T> && impl::has_cpp<T>) {
        return impl::unwrap(std::forward<T>(obj));
    } else {
        return std::forward<T>(obj);
    }
}


template <>
struct interface<Object> {};
template <>
struct interface<Type<Object>> {};


/* An owning reference to a dynamically-typed Python object.  More specialized types
can always be implicitly converted to this type, but doing the opposite incurs a
runtime `isinstance()` check, and raises a `TypeError` if the check fails. */
struct Object : interface<Object> {
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
    template <typename Cls, typename CRTP, typename Wrapper, static_str ModName>
    struct Bindings;

protected:
    PyObject* m_ptr;

    struct borrowed_t {};
    struct stolen_t {};

    template <impl::inherits<Object> T>
    friend PyObject* ptr(T&&);
    template <impl::inherits<Object> T> requires (!std::is_const_v<std::remove_reference_t<T>>)
    friend PyObject* release(T&&);
    template <std::derived_from<Object> T>
    friend T reinterpret_borrow(PyObject*);
    template <std::derived_from<Object> T>
    friend T reinterpret_steal(PyObject*);
    template <impl::python T> requires (impl::has_cpp<T>)
    friend auto& impl::unwrap(T& obj);
    template <impl::python T> requires (impl::has_cpp<T>)
    friend const auto& impl::unwrap(const T& obj);

    /// TODO: lift the static assertion here into a template constraint applied to/by
    /// the ctor classes

    template <typename T>
    struct implicit_ctor {
        template <typename... Fs>
        struct helper { static constexpr bool enable = false; };
        template <>
        struct helper<> {
            static constexpr bool enable = __init__<T>::enable;
            template <typename... Args>
            static auto operator()(Args... args) {
                return __init__<T>{}(std::forward<Args>(args)...);
            }
        };
        template <typename F>
        struct helper<F> {
            static constexpr bool enable =
                !impl::inherits<F, Object> &&
                __cast__<F, T>::enable &&
                std::is_invocable_r_v<T, __cast__<F, T>, F>;
            template <typename... Args>
            static auto operator()(Args... args) {
                return __cast__<F, T>{}(std::forward<Args>(args)...);
            }
        };
        template <typename... Args>
        static constexpr bool enable = helper<Args...>::enable;
        template <typename... Args>
        static auto operator()(Args... args) {
            return helper<Args...>{}(std::forward<Args>(args)...);
        }
    };

    template <typename T>
    struct explicit_ctor {
        template <typename... Fs>
        struct helper {
            template <typename... Args>
            static constexpr bool enable =
                __init__<T, Args...>::enable &&
                std::is_invocable_r_v<T, __init__<T, Args...>, Args...>;
            template <typename... Args>
            static auto operator()(Args... args) {
                return __init__<T, Args...>{}(std::forward<Args>(args)...);
            }
        };
        template <typename F>
            requires (!__init__<T, F>::enable && __explicit_cast__<F, T>::enable)
        struct helper<F> {
            static constexpr bool enable =
                !impl::inherits<F, Object> &&
                std::is_invocable_r_v<T, __explicit_cast__<F, T>, F>;
            template <typename... Args>
            static auto operator()(Args... args) {
                return __explicit_cast__<F, T>{}(std::forward<Args>(args)...);
            }
        };
        template <typename... Args>
        static constexpr bool enable =
            !implicit_ctor<T>::template enable<Args...> &&
            helper<Args...>::template enable<Args...>;
        template <typename... Args>
        static auto operator()(Args... args) {
            return helper<Args...>{}(std::forward<Args>(args)...);
        }
    };

    template <std::derived_from<Object> T, typename... Args>
    explicit Object(implicit_ctor<T>, Args&&... args) : m_ptr(release((
        impl::Interpreter::init(),  // comma operator
        implicit_ctor<T>{}(std::forward<Args>(args)...)
    ))) {
        using Return = std::invoke_result_t<__init__<T, Args...>, Args...>;
        static_assert(
            std::is_same_v<Return, T>,
            "Implicit constructor must return a new instance of type T."
        );
        if constexpr (DEBUG) {
            assert_(
                m_ptr != nullptr,
                "Implicit constructor cannot return a null object."
            );
        }
    }

    template <std::derived_from<Object> T, typename... Args>
    explicit Object(explicit_ctor<T>, Args&&... args) : m_ptr(release((
        impl::Interpreter::init(),  // comma operator
        explicit_ctor<T>{}(std::forward<Args>(args)...)
    ))) {
        using Return = std::invoke_result_t<__init__<T, Args...>, Args...>;
        static_assert(
            std::is_same_v<Return, T>,
            "Explicit constructor must return a new instance of type T."
        );
        if constexpr (DEBUG) {
            assert_(
                m_ptr != nullptr,
                "Explicit constructor cannot return a null object."
            );
        }
    }

    /// TODO: rather than def<>, use type<> or cls<> to disambiguate from py::def,
    /// which is the function definition keyword.

    /* A CRTP base class that generates bindings for a new Python type that wraps an
    external C++ type.

    This class stores a variant containing either a full instance of or a (possibly
    const) pointer to the C++ type being wrapped, and can contain no other data
    members.  It should only implement the `__export__()` script, which uses the
    binding helpers to expose the C++ type's interface to Python. */
    template <typename CRTP, typename Derived, typename CppType = void>
    struct def : PyObject, impl::BertrandTag {
    protected:

        template <typename... Ts>
        using Visitor = Visitor<Ts...>;

        template <static_str ModName>
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
        template <static_str ModName>
        static Type<Derived> __export__(Bindings<ModName> bind);  // {
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
            // return Module<"bertrand.python">().to_python<Derived>();
            // return Module<"bertrand.python">().to_python(derived);  // python style
            // return bertrand.python.to_python(derived);  // in Python
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
    struct def<CRTP, Derived, void> : impl::BertrandTag {
    protected:

        template <static_str ModName>
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
        static Type<Derived> __import__();  // {
        //     throw NotImplementedError(
        //         "the __import__() method must be defined for all Python types: "
        //         + type_name<CRTP>
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
    template <typename CRTP, static_str Name>
    struct def<CRTP, Module<Name>, void> : impl::BertrandTag {
    protected:

        template <static_str ModName>
        using Bindings = Object::Bindings<CRTP, CRTP, Module<ModName>, ModName>;

    public:
        using __object__ = Module<Name>;
        using __cpp__ = void;

        /// TODO: docs
        template <static_str ModName>
        static Module<Name> __export__(Bindings<ModName> bind);  // {
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
        static Type<Object> __import__();
    };

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const Object& other) noexcept : m_ptr(Py_XNewRef(ptr(other))) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(Object&& other) noexcept : m_ptr(release(other)) {}

    /* reinterpret_borrow() constructor.  Borrows a reference to a raw Python pointer. */
    Object(PyObject* p, borrowed_t) noexcept : m_ptr(Py_XNewRef(p)) {}

    /* reinterpret_steal() constructor.  Steals a reference to a raw Python pointer. */
    Object(PyObject* p, stolen_t) noexcept : m_ptr(p) {}

    /* Initializer list constructor.  Implemented via the __initializer__ control
    struct. */
    template <typename Self = Object> requires (__initializer__<Self>::enable)
    Object(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

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
        /// NOTE: interpreter initialization check is necessary for the case where an
        /// extension module with global objects is imported into an active Python
        /// interpreter.  Due to Python's finalization logic, global destructors will
        /// be run *after* the interpreter has been finalized, meaning that any attempt
        /// to decrement a reference count will result in a segfault.  This check
        /// simply avoids the decrement in that case, which technically leaks a
        /// reference at shutdown, but is otherwise safe.  Either Python or the OS
        /// should clean up memory normally after the process exits, so this is not a
        /// huge concern, but is technically undefined behavior.  A real solution would
        /// call dlclose() on each extension module before the interpreter is
        /// invalidated, but that is not currently possible, and would likely
        /// necessitate changes to Python itself.  Note that due to the
        /// `impl::initialize_python()` and `impl::finalize_python()` functions, this
        /// is not a concern for dedicated C++ programs that are run under their own
        /// `main()` entry points, since the interpreter is guaranteed to outlive any
        /// global objects.
        if (m_ptr && Py_IsInitialized()) {
            Py_DECREF(m_ptr);
        }
    }

    /* Copy assignment operator. */
    Object& operator=(const Object& other) noexcept {
        if constexpr (DEBUG) {
            assert_(ptr(other) != nullptr, "Cannot copy-assign from a null object.");
        }
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = Py_XNewRef(ptr(other));
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    Object& operator=(Object&& other) noexcept {
        if constexpr (DEBUG) {
            assert_(ptr(other) != nullptr, "Cannot move-assign from a null object.");
        }
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = ptr(other);
            other.m_ptr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
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

    /* Python-style contains operator.  Equivalent to Python's `in` keyword, but
    expects the argument to have a `.contains()` method that can be called with this
    type, order to support membership testing in both directions. */
    template <typename Self, typename Key>
        requires (
            __contains__<Self, Key>::enable &&
            std::same_as<typename __contains__<Self, Key>::type, bool> && (
                std::is_invocable_r_v<bool, __contains__<Self, Key>, Self, Key> || (
                    !std::is_invocable_v<__contains__<Self, Key>, Self, Key> &&
                    impl::has_cpp<Self> &&
                    impl::has_contains<impl::cpp_type<Self>, impl::cpp_type<Key>>
                ) || (
                    !std::is_invocable_v<__contains__<Self, Key>, Self, Key> &&
                    !impl::has_cpp<Self>
                )
            )
        )
    [[nodiscard]] bool in(this Self&& self, Key&& other);

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

    /* Index operator.  Specific key and element types can be controlled via the
    __getitem__, __setitem__, and __delitem__ control structs. */
    template <typename Self, typename... Key>
        requires (
            __getitem__<Self, Key...>::enable &&
            std::convertible_to<typename __getitem__<Self, Key...>::type, Object> && (
                std::is_invocable_r_v<
                    typename __getitem__<Self, Key...>::type,
                    __getitem__<Self, Key...>,
                    Self,
                    Key...
                > || (
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    impl::has_cpp<Self> &&
                    impl::lookup_yields<
                        impl::cpp_type<Self>&,
                        typename __getitem__<Self, Key...>::type,
                        Key...
                    >
                ) || (
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    !impl::has_cpp<Self> &&
                    std::derived_from<typename __getitem__<Self, Key...>::type, Object>
                )
            )
        )
    decltype(auto) operator[](this Self&& self, Key&&... key);

    /* Call operator.  This can be enabled for specific argument signatures and return
    types via the __call__ control struct, enabling static type safety for callable
    Python objects in C++. */
    template <typename Self, typename... Args>
        requires (
            __call__<Self, Args...>::enable &&
            std::convertible_to<typename __call__<Self, Args...>::type, Object> && (
                std::is_invocable_r_v<
                    typename __call__<Self, Args...>::type,
                    __call__<Self, Args...>,
                    Self,
                    Args...
                > || (
                    !std::is_invocable_v<__call__<Self, Args...>, Self, Args...> &&
                    impl::has_cpp<Self> &&
                    std::is_invocable_r_v<
                        typename __call__<Self, Args...>::type,
                        impl::cpp_type<Self>,
                        Args...
                    >
                ) || (
                    !std::is_invocable_v<__call__<Self, Args...>, Self, Args...> &&
                    !impl::has_cpp<Self> &&
                    std::derived_from<typename __call__<Self, Args...>::type, Object> &&
                    __getattr__<Self, "__call__">::enable &&
                    impl::inherits<typename __getattr__<Self, "__call__">::type, impl::FunctionTag>
                )
            )
        )
    decltype(auto) operator()(this Self&& self, Args&&... args);

};


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_borrow(PyObject* ptr) {
    return T(ptr, Object::borrowed_t{});
}


template <std::derived_from<Object> T>
[[nodiscard]] T reinterpret_steal(PyObject* ptr) {
    return T(ptr, Object::stolen_t{});
}


template <impl::inherits<Object> T>
struct __cast__<T>                                          : returns<T> {
    static T operator()(T value) { return std::forward<T>(value); }
};


/* Default initialize py::Object to None. */
template <>
struct __init__<Object>                                     : returns<Object> {
    static auto operator()() {
        return reinterpret_steal<Object>(Py_NewRef(Py_None));
    }
};


/* Implicitly convert any C++ value into a py::Object by invoking the unary cast type,
and then reinterpreting as a dynamic object. */
template <impl::cpp T> requires (impl::has_python<T>)
struct __cast__<T, Object>                                  : returns<Object> {
    static auto operator()(T value) {
        return reinterpret_steal<Object>(
            release(impl::python_type<T>(std::forward<T>(value)))
        );
    }
};


/* Implicitly convert a Python object into any recognized C++ type by checking for an
equivalent Python type via __cast__, implicitly converting to that type, and then
implicitly converting the result to the C++ type in a 2-step process. */
template <impl::is<Object> From, impl::cpp To>
    requires (
        !std::is_reference_v<To> &&
        !std::is_const_v<To> &&
        !std::is_volatile_v<To> &&
        __cast__<To>::enable
    )
struct __cast__<From, To>                                   : returns<To> {
    static auto operator()(From self) {
        using Intermediate = __cast__<To>::type;
        return impl::implicit_cast<To>(
            impl::implicit_cast<Intermediate>(std::forward<From>(self))
        );
    }
};


/* Implicitly convert a Python object that wraps around a C++ type into an lvalue
reference to that type, which directly references the internal data. */
template <impl::inherits<Object> From, typename To>
    requires (
        impl::has_cpp<From> &&
        std::same_as<impl::cpp_type<std::remove_cvref_t<From>>, std::remove_cv_t<To>>
    )
struct __cast__<From, To&>                                  : returns<To&> {
    static To& operator()(From from) {
        return from_python(std::forward<From>(from));
    }
};


/* Implicitly convert a Python object that wraps around a C++ type into a pointer to
that type, which directly references the internal data. */
template <impl::inherits<Object> From, typename To> requires (std::convertible_to<From, To>)
struct __cast__<From, std::optional<To>>                    : returns<std::optional<To>> {
    static std::optional<To> operator()(From from) {
        return {impl::implicit_cast<To>(std::forward<From>(from))};
    }
};


/* Implicitly convert a Python object that wraps around a C++ type into a pointer to
that type, which directly references the internal data. */
template <impl::inherits<Object> From, typename To>
    requires (
        impl::has_cpp<From> &&
        std::same_as<impl::cpp_type<std::remove_cvref_t<From>>, std::remove_cv_t<To>>
    )
struct __cast__<From, To*>                                  : returns<To*> {
    static To* operator()(From from) {
        return &from_python(std::forward<From>(from));
    }
};


/* Implicitly convert a Python object into a shared pointer as long as the underlying
types are convertible. */
template <impl::inherits<Object> From, typename To> requires (std::convertible_to<From, To>)
struct __cast__<From, std::shared_ptr<To>>                  : returns<std::shared_ptr<To>> {
    static std::shared_ptr<To> operator()(From value) {
        return std::make_shared<To>(
            impl::implicit_cast<To>(std::forward<From>(value))
        );
    }
};


/* Implicitly convert a Python object into a unique pointer as long as the underlying
types are convertible. */
template <impl::inherits<Object> From, typename To> requires (std::convertible_to<From, To>)
struct __cast__<From, std::unique_ptr<To>>                  : returns<std::unique_ptr<To>> {
    static std::unique_ptr<To> operator()(From value) {
        return std::make_unique<To>(
            impl::implicit_cast<To>(std::forward<From>(value))
        );
    }
};


/* Explicitly convert a Python object into any C++ type by checking for an equivalent
Python type via unary __cast__, explicitly converting to that type, and then to the C++
type in a 2-step process. */
template <impl::inherits<Object> From, impl::cpp To>
    requires (
        !std::is_reference_v<To> &&
        !std::is_const_v<To> &&
        !std::is_volatile_v<To> &&
        impl::has_python<To>
    )
struct __explicit_cast__<From, To>                          : returns<To> {
    static auto operator()(From from) {
        return static_cast<To>(
            static_cast<impl::python_type<To>>(std::forward<From>(from))
        );
    }
};


/* Contextually convert an Object into a boolean value for use in if/else statements,
with the same semantics as Python. */
template <impl::inherits<Object> From>
struct __explicit_cast__<From, bool>                         : returns<bool> {
    static bool operator()(From&& from);
};


/* Explicitly convert a Python object into a C++ integer by calling `int(obj)` at the
Python level. */
template <impl::inherits<Object> From, impl::cpp To>
    requires (impl::has_python<To> && std::integral<To>)
struct __explicit_cast__<From, To>                          : returns<To> {
    static To operator()(From&& from);
};


/* Explicitly convert a Python object into a C++ floating-point number by calling
`float(obj)` at the Python level. */
template <impl::inherits<Object> From, impl::cpp To>
    requires (impl::has_python<To> && std::floating_point<To>)
struct __explicit_cast__<From, To>                          : returns<To> {
    static To operator()(From&& from);
};


/* Explicitly convert a Python object into a C++ complex number by calling
`complex(obj)` at the Python level. */
template <impl::inherits<Object> From, typename Float>
struct __explicit_cast__<From, std::complex<Float>> : returns<std::complex<Float>> {
    static auto operator()(From&& from);
};


/* Explicitly convert a Python object into a C++ sd::string representation by calling
`str(obj)` at the Python level. */
template <impl::inherits<Object> From, typename Char>
struct __explicit_cast__<From, std::basic_string<Char>> : returns<std::basic_string<Char>> {
    static auto operator()(From&& from);
};


/// NOTE: most control structures are enabled through the default specializations,
/// which attempt to introspect various dunder methods on the object type.  Because
/// these are fully generic for the dynamic `Object` type, those introspections will
/// always succeed, and the control structures will be enabled and delegate to Python,
/// which is the same behavior we'd be implementing here.  Doing it this way also
/// prevents template conflicts in downstream code, reducing the chance of ambiguity.


template <impl::is<Object> Self, static_str Name>
struct __getattr__<Self, Name>                              : returns<Object> {};
template <impl::is<Object> Self, static_str Name, std::convertible_to<Object> Value>
    requires (!impl::is_const<Self>)
struct __setattr__<Self, Name, Value>                       : returns<void> {};
template <impl::is<Object> Self, static_str Name> requires (!impl::is_const<Self>)
struct __delattr__<Self, Name>                              : returns<void> {};
template <impl::is<Object> Self, std::convertible_to<Object>... Args>
struct __call__<Self, Args...>                              : returns<Object> {
    static Object operator()(Self, Args...);
};
template <impl::is<Object> Self>
struct __iter__<Self>                                       : returns<Object> {};
template <impl::is<Object> Self>
struct __reversed__<Self>                                   : returns<Object> {};


/// TODO: issubclass(obj, obj) doesn't work? 


/* Allow Objects to be used as left-hand arguments in `issubclass()` checks. */
template <impl::is<Object> Derived, typename Base>
struct __issubclass__<Derived, Base>                         : returns<bool> {
    static bool operator()(Derived derived);
};


/* Inserting a Python object into an output stream corresponds to a `str()` call at the
Python level. */
template <std::derived_from<std::ostream> Stream, impl::inherits<Object> Self>
struct __lshift__<Stream, Self>                             : returns<Stream&> {
    /// TODO: Str, Bytes, ByteArray should specialize this to write to the stream directly.
    static Stream& operator()(Stream& stream, Self self);
};


////////////////////
////    NONE    ////
////////////////////


struct NoneType;


template <>
struct interface<NoneType> : impl::BertrandTag {};
template <>
struct interface<Type<NoneType>> : impl::BertrandTag {};


/* Represents the type of Python's `None` singleton in C++. */
struct NoneType : Object, interface<NoneType> {
    struct __python__ : def<__python__, NoneType>, PyObject {
        static Type<NoneType> __import__();
    };

    NoneType(PyObject* p, borrowed_t t) : Object(p, t) {}
    NoneType(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = NoneType> requires (__initializer__<Self>::enable)
    NoneType(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args> requires (implicit_ctor<NoneType>::enable<Args...>)
    NoneType(Args&&... args) : Object(
        implicit_ctor<NoneType>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<NoneType>::enable<Args...>)
    explicit NoneType(Args&&... args) : Object(
        explicit_ctor<NoneType>{},
        std::forward<Args>(args)...
    ) {}

};


template <>
struct __init__<NoneType>                                   : returns<NoneType> {
    static auto operator()() {
        return reinterpret_borrow<NoneType>(Py_None);
    }
};


template <impl::none_like From>
struct __cast__<From, NoneType>                             : returns<NoneType> {
    static NoneType operator()(const From& value) {
        return {};
    }
};


template <impl::is<NoneType> From, impl::none_like To>
struct __cast__<From, To>                                   : returns<To> {
    static To operator()(From value) {
        return {};
    }
};


template <impl::is<NoneType> From, typename To> requires (!std::convertible_to<From, To>)
struct __cast__<From, std::optional<To>>                    : returns<std::optional<To>> {
    static std::optional<To> operator()(From value) {
        return std::nullopt;
    }
};


template <impl::is<NoneType> From, typename To>
struct __cast__<From, To*>                                  : returns<To*> {
    static To* operator()(From value) {
        return nullptr;
    }
};


template <impl::is<NoneType> From, typename To> requires (!std::convertible_to<From, To>)
struct __cast__<From, std::shared_ptr<To>>                  : returns<std::shared_ptr<To>> {
    static std::shared_ptr<To> operator()(From value) {
        return nullptr;
    }
};


template <impl::is<NoneType> From, typename To> requires (!std::convertible_to<From, To>)
struct __cast__<From, std::unique_ptr<To>>                  : returns<std::unique_ptr<To>> {
    static std::unique_ptr<To> operator()(From value) {
        return nullptr;
    }
};


template <impl::is<NoneType> Self>
struct __hash__<Self>                                       : returns<size_t> {};


inline const NoneType None;


//////////////////////////////
////    NOTIMPLEMENTED    ////
//////////////////////////////


struct NotImplementedType;


template <>
struct interface<NotImplementedType> : impl::BertrandTag {};
template <>
struct interface<Type<NotImplementedType>> : impl::BertrandTag {};


/* Represents the type of Python's `NotImplemented` singleton in C++. */
struct NotImplementedType : Object, interface<NotImplementedType> {
    struct __python__ : def<__python__, NotImplementedType>, PyObject {
        static Type<NotImplementedType> __import__();
    };

    NotImplementedType(PyObject* p, borrowed_t t) : Object(p, t) {}
    NotImplementedType(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = NotImplementedType> requires (__initializer__<Self>::enable)
    NotImplementedType(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args> requires (implicit_ctor<NotImplementedType>::enable<Args...>)
    NotImplementedType(Args&&... args) : Object(
        implicit_ctor<NotImplementedType>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<NotImplementedType>::enable<Args...>)
    explicit NotImplementedType(Args&&... args) : Object(
        explicit_ctor<NotImplementedType>{},
        std::forward<Args>(args)...
    ) {}

};


template <>
struct __init__<NotImplementedType>                         : returns<NotImplementedType> {
    static auto operator()() {
        return reinterpret_borrow<NotImplementedType>(Py_NotImplemented);
    }
};


template <impl::notimplemented_like From>
struct __cast__<From, NotImplementedType>                   : returns<NotImplementedType> {
    static NotImplementedType operator()(const From& value) {
        return {};
    }
};


template <impl::is<NotImplementedType> From, impl::notimplemented_like To>
struct __cast__<From, To>                                   : returns<To> {
    static To operator()(From value) {
        return {};
    }
};


template <impl::is<NotImplementedType> Self>
struct __hash__<Self>                                       : returns<size_t> {};


inline const NotImplementedType NotImplemented;


////////////////////////
////    ELLIPSIS    ////
////////////////////////


struct EllipsisType;


template <>
struct interface<EllipsisType> : impl::BertrandTag {};
template <>
struct interface<Type<EllipsisType>> : impl::BertrandTag {};


/* Represents the type of Python's `Ellipsis` singleton in C++. */
struct EllipsisType : Object, interface<EllipsisType> {
    struct __python__ : def<__python__, EllipsisType>, PyObject {
        static Type<EllipsisType> __import__();
    };

    EllipsisType(PyObject* p, borrowed_t t) : Object(p, t) {}
    EllipsisType(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = EllipsisType> requires (__initializer__<Self>::enable)
    EllipsisType(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args> requires (implicit_ctor<EllipsisType>::enable<Args...>)
    EllipsisType(Args&&... args) : Object(
        implicit_ctor<EllipsisType>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<EllipsisType>::enable<Args...>)
    explicit EllipsisType(Args&&... args) : Object(
        explicit_ctor<EllipsisType>{},
        std::forward<Args>(args)...
    ) {}

};


template <>
struct __init__<EllipsisType>                               : returns<EllipsisType> {
    static auto operator()() {
        return reinterpret_borrow<EllipsisType>(Py_Ellipsis);
    }
};


template <impl::ellipsis_like From>
struct __cast__<From, EllipsisType>                         : returns<EllipsisType> {
    static EllipsisType operator()(const From& value) {
        return {};
    }
};


template <impl::is<EllipsisType> From, impl::ellipsis_like To>
struct __cast__<From, To>                                   : returns<To> {
    static To operator()(From value) {
        return {};
    }
};


template <impl::is<EllipsisType> Self>
struct __hash__<Self>                                       : returns<size_t> {};


inline const EllipsisType Ellipsis;


}  // namespace py


#endif
