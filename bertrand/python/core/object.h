#ifndef BERTRAND_PYTHON_CORE_OBJECT_H
#define BERTRAND_PYTHON_CORE_OBJECT_H

#include "declarations.h"


namespace bertrand {


namespace impl {

    /* Construct a new instance of an inner `Wrapper::__python__` type using
    Python-based memory allocation and forwarding to the nested type's C++ constructor
    to complete initialization. */
    template <typename Wrapper, typename... Args>
        requires (
            meta::python<Wrapper> &&
            !meta::is_qualified<Wrapper> &&
            std::constructible_from<typename Wrapper::__python__, Args...>
        )
    Wrapper construct(Args&&... args) {
        using __python__ = Wrapper::__python__;
        Type<Wrapper> type;
        PyTypeObject* cls = reinterpret_cast<PyTypeObject*>(ptr(type));
        Wrapper self = steal<Wrapper>(cls->tp_alloc(cls, 0));
        if (self.is(nullptr)) {
            Exception::from_python();
        }
        /// TODO: do I need to explicitly free the object if the constructor fails?
        /// Can the normal destructor fail in this case?
        new (reinterpret_cast<__python__*>(ptr(self))) __python__(std::forward<Args>(args)...);
        if (cls->tp_flags & Py_TPFLAGS_HAVE_GC) {
            PyObject_GC_Track(ptr(self));
        }
        return self;
    }

    /* Wrap a non-owning, mutable reference to a C++ object into a `bertrand::Object`
    proxy that exposes it to Python.  Note that this only works if a corresponding
    `bertrand::Object` subclass exists, which was declared using the `__python__` CRTP
    helper, and whose C++ type exactly matches the argument. */
    template <meta::cpp T>
        requires (
            meta::has_python<T> &&
            meta::has_cpp<meta::python_type<T>> &&
            meta::is<T, meta::cpp_type<meta::python_type<T>>>
        )
    [[nodiscard]] auto wrap(T& obj) -> meta::python_type<T>;

    /* Wrap a non-owning, immutable reference to a C++ object into a `bertrand::Object`
    proxy that exposes it to Python.  Note that this only works if a corresponding
    `bertrand::Object` subclass exists, which was declared using the `__python__` CRTP
    helper, and whose C++ type exactly matches the argument. */
    template <meta::cpp T>
        requires (
            meta::has_python<T> &&
            meta::has_cpp<meta::python_type<T>> &&
            meta::is<T, meta::cpp_type<meta::python_type<T>>>
        )
    [[nodiscard]] auto wrap(const T& obj) -> meta::python_type<T>;

    /* Retrieve a reference to the internal C++ object that backs a `bertrand::Object`
    wrapper, if such an object exists.  Does nothing if called on a pure Python or
    naked C++ object.  If the wrapper does not own the backing object, this method will
    follow the internal pointer to resolve the reference. */
    template <meta::python T> requires (meta::has_cpp<T>)
    [[nodiscard]] auto& unwrap(T& obj);

    /* Retrieve a reference to the internal C++ object that backs a `bertrand::Object`
    wrapper, if such an object exists.  Does nothing if called on a pure Python or
    naked C++ object.  If the wrapper does not own the backing object, this method will
    follow the internal pointer to resolve the reference. */
    template <meta::python T> requires (meta::has_cpp<T>)
    [[nodiscard]] const auto& unwrap(const T& obj);

}


/* Access a member of the underlying PyObject* pointer by casting it to its internal
`__python__` representation.  The Bertrand object model and type safety guarantees
should ensure that this is safe for general use, but it is possible to invoke undefined
behavior if the underlying pointer is improperly initialized (by viewing a moved-from
object, or by improper use of the `borrow()` and `steal()` functions). */
template <meta::python Self>
[[nodiscard]] auto view(Self&& self) {
    if constexpr (DEBUG) {
        assert_(ptr(self) != nullptr, "Cannot view a null object.");
    }
    return reinterpret_cast<typename std::remove_cvref_t<Self>::__python__*>(
        ptr(std::forward<Self>(self))
    );
}


/* Retrieve the raw pointer backing a Python object. */
template <meta::python T>
[[nodiscard]] PyObject* ptr(T&& obj) {
    return std::forward<T>(obj).m_ptr;
}


/* Cause a Python object to relinquish ownership over its backing pointer, and then
return the raw pointer. */
template <meta::python T> requires (!meta::is_const<T>)
[[nodiscard]] PyObject* release(T&& obj) {
    PyObject* temp = obj.m_ptr;
    obj.m_ptr = nullptr;
    return temp;
}


/* Steal a reference to a raw Python object. */
template <meta::python T> requires (!meta::is_qualified<T>)
[[nodiscard]] T steal(PyObject* obj);


/* Borrow a reference to a raw Python object. */
template <meta::python T> requires (!meta::is_qualified<T>)
[[nodiscard]] T borrow(PyObject* obj);


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
and C++ need to share state.  Always prefer calling `bertrand::obj<T>()` or an
equivalent Python constructor when the lifetime of the object is uncertain, especially
when the result is passed up to Python, and may therefore acquire outside references. */
template <meta::has_python T>
[[nodiscard]] decltype(auto) to_python(T&& value) {
    if constexpr (meta::python<T>) {
        return std::forward<T>(value);
    } else if constexpr (
        meta::is_lvalue<T> &&
        meta::is<T, meta::cpp_type<meta::python_type<T>>>
    ) {
        return impl::wrap(std::forward<T>(value));
    } else {
        return meta::python_type<T>(std::forward<T>(value));
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
    if constexpr (meta::python<T> && meta::has_cpp<T>) {
        return impl::unwrap(std::forward<T>(obj));
    } else {
        return std::forward<T>(obj);
    }
}


/// TODO: implement the extra overloads for Object, Union, Type, and Tuple of
/// any of the above for type checks.


/* Does a compile-time check to see if the derived type inherits from the base type.
Ordinarily, this is equivalent to a `std::derived_from<>` concept, except that custom
logic is allowed by defining a zero-argument call operator in a specialization of
`__issubclass__`, and `interface<T>` specializations are used to handle Python objects
in a way that allows for multiple inheritance. */
template <meta::python Derived, meta::python Base>
    requires (
        std::is_invocable_r_v<bool, __issubclass__<Derived, Base>> ||
        !std::is_invocable_v<__issubclass__<Derived, Base>>
    )
[[nodiscard]] constexpr bool issubclass() {
    if constexpr (std::is_invocable_v<__issubclass__<Derived, Base>>) {
        return __issubclass__<Derived, Base>{}();
    } else if constexpr (meta::has_interface<Base>) {
        return meta::inherits<Derived, interface<Base>>;
    } else {
        return meta::inherits<Derived, Base>;
    }
}


/* Devolves to a compile-time `issubclass<Derived, Base>()` check unless the object is
a dynamic object which may be narrowed to a single type, or a one-argument call
operator is defined in a specialization of `__issubclass__`. */
template <meta::python Base, meta::python Derived>
    requires (std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, Derived>)
[[nodiscard]] constexpr bool issubclass(Derived&& obj) {
    if constexpr (DEBUG) {
        assert_(
            !obj.is(nullptr),
            "issubclass() cannot be called on a null object."
        );
    }
    return __issubclass__<Derived, Base>{}(std::forward<Derived>(obj));
}


/* Equivalent to Python `issubclass(obj, base)`.  This overload must be explicitly
enabled by defining a two-argument call operator in a specialization of
`__issubclass__`.  The derived type must be a single type or a dynamic object which can
be narrowed to a single type, and the base must be type-like, a union of types, or a
dynamic object which can be narrowed to such. */
template <meta::python Derived, meta::python Base>
    requires (std::is_invocable_r_v<bool, __issubclass__<Derived, Base>, Derived, Base>)
[[nodiscard]] constexpr bool issubclass(Derived&& obj, Base&& base) {
    if constexpr (DEBUG) {
        assert_(
            !obj.is(nullptr),
            "left operand to issubclass() cannot be a null object."
        );
        assert_(
            !base.is(nullptr),
            "right operand to issubclass() cannot be a null object."
        );
    }
    return __issubclass__<Derived, Base>{}(
        std::forward<Derived>(obj),
        std::forward<Base>(base)
    );
}


/* Checks if the given object can be safely converted to the specified base type.  This
is automatically called whenever a Python object is narrowed from a parent type to one
of its subclasses. */
template <meta::python Base, meta::python Derived>
    requires (
        std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, Derived> ||
        !std::is_invocable_v<__isinstance__<Derived, Base>>
    )
[[nodiscard]] constexpr bool isinstance(Derived&& obj) {
    if constexpr (DEBUG) {
        assert_(
            !obj.is(nullptr),
            "isinstance() cannot be called on a null object."
        );
    }
    if constexpr (std::is_invocable_v<__isinstance__<Derived, Base>, Derived>) {
        return __isinstance__<Derived, Base>{}(std::forward<Derived>(obj));
    } else {
        return issubclass<std::remove_cvref_t<Derived>, Base>();
    }
}


/* Equivalent to Python `isinstance(obj, base)`.  This overload must be explicitly
enabled by defining a two-argument call operator in a specialization of
`__isinstance__`.  By default, this is only done for bases which are type-like, a union
of types, or a dynamic object which can be narrowed to either of the above. */
template <meta::python Derived, meta::python Base>
    requires (std::is_invocable_r_v<bool, __isinstance__<Derived, Base>, Derived, Base>)
[[nodiscard]] constexpr bool isinstance(Derived&& obj, Base&& base) {
    if constexpr (DEBUG) {
        assert_(
            !obj.is(nullptr),
            "left operand to isinstance() cannot be a null object."
        );
        assert_(
            !base.is(nullptr),
            "right operand to isinstance() cannot be a null object."
        );
    }
    return __isinstance__<Derived, Base>{}(
        std::forward<Derived>(obj),
        std::forward<Base>(base)
    );
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

    // /* A helper class that simplifies the creation of new Python types within a
    // `def::__export__()` script, which exposes the type's interface to Python.  `Cls`
    // refers to the type being exposed, and may refer to either an external C++ type or
    // the CRTP type in the case of inline Python types.  All bound methods/variables must
    // either originate from that class or use a non-member method which takes the class
    // as its first argument, unless the method/variable is static and unassociated with
    // any instance. */
    // template <typename Cls, typename CRTP, typename Wrapper, static_str ModName>
    // struct Bindings;

protected:
    struct borrowed_t {};
    struct stolen_t {};

    template <meta::python T>
    friend PyObject* ptr(T&&);
    template <meta::python T> requires (!meta::is_const<T>)
    friend PyObject* release(T&&);
    template <meta::python T> requires (!meta::is_qualified<T>)
    friend T borrow(PyObject*);
    template <meta::python T> requires (!meta::is_qualified<T>)
    friend T steal(PyObject*);
    template <meta::python T> requires (meta::has_cpp<T>)
    friend auto& impl::unwrap(T& obj);
    template <meta::python T> requires (meta::has_cpp<T>)
    friend const auto& impl::unwrap(const T& obj);

    /// TODO: I can probably pare down the number of `cls<>` specializations by moving
    /// the module-specific specialization into the `Module` class, or by using
    /// metaprogramming to detect specializations of Module<> for the derived type
    /// internally.

    /* A CRTP base class that generates bindings for a new Python type that wraps an
    external C++ type.

    This class stores a variant containing either a full instance of or a (possibly
    const) pointer to the C++ type being wrapped, and can contain no other data
    members.  It should only implement the `__export__()` script, which uses the
    binding helpers to expose the C++ type's interface to Python. */
    template <typename CRTP, typename Derived, typename CppType = void>
    struct cls : PyObject {
    protected:

        template <static_str ModName>
        struct Bindings;

    public:
        using __object__ = Derived;
        using __cpp__ = CppType;
        std::variant<CppType, CppType*, const CppType*> m_cpp;

        template <typename... Args>
        cls(Args&&... args) : m_cpp(std::forward<Args>(args)...) {}

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
    struct cls<CRTP, Derived, void> {
    protected:

        template <static_str ModName>
        struct Bindings;

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
        // static Type<Derived> __import__();  // {
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
    struct cls<CRTP, Module<Name>, void> {
    protected:

        template <static_str ModName>
        struct Bindings;

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

    template <meta::python T> requires (!meta::is_qualified<T>)
    struct implicit_ctor {
        template <typename... Fs>
        struct helper { static constexpr bool enable = false; };
        template <>
        struct helper<> {
            static constexpr bool enable = __init__<T>::enable;
            static auto operator()() { return __init__<T>{}(); }
        };
        template <typename F> requires (__cast__<F, T>::enable && !meta::python<F>)
        struct helper<F> {
            static constexpr bool enable =
                std::same_as<T, typename __cast__<F, T>::type>;
            static auto operator()(F from) {
                return __cast__<F, T>{}(std::forward<F>(from));
            }
        };
        template <typename... Args>
        static constexpr bool enable = helper<Args...>::enable;
        template <typename... Args>
        static auto operator()(Args&&... args) {
            return helper<Args...>{}(std::forward<Args>(args)...);
        }
    };

    template <meta::python T> requires (!meta::is_qualified<T>)
    struct explicit_ctor {
        template <typename...>
        static constexpr bool enable = false;
        template <typename F>
            requires (__init__<T, F>::enable && !__cast__<F, T>::enable)
        static constexpr bool enable<F> =
            std::same_as<T, typename __init__<T, F>::type>;
        template <typename... Args>
            requires (__init__<T, Args...>::enable && sizeof...(Args) > 1)
        static constexpr bool enable<Args...> =
            std::same_as<T, typename __init__<T, Args...>::type>;

        template <typename... Args>
        static auto operator()(Args&&... args) {
            return __init__<T, Args...>{}(std::forward<Args>(args)...);
        }
    };

    PyObject* m_ptr;

    template <typename T, typename... Args>
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

    template <typename T, typename... Args>
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

public:
    struct __python__ : cls<__python__, Object>, PyObject {
        static Type<Object> __import__();
    };

    /* Copy constructor.  Borrows a reference to an existing object. */
    Object(const Object& other) noexcept : m_ptr(Py_XNewRef(ptr(other))) {}

    /* Move constructor.  Steals a reference to a temporary object. */
    Object(Object&& other) noexcept : m_ptr(release(other)) {}

    /* borrow() constructor.  Borrows a reference to a raw Python pointer. */
    Object(PyObject* p, borrowed_t) noexcept : m_ptr(Py_XNewRef(p)) {}

    /* steal() constructor.  Steals a reference to a raw Python pointer. */
    Object(PyObject* p, stolen_t) noexcept : m_ptr(p) {}

    /* Initializer list constructor.  Implemented via the __initializer__ control
    struct. */
    template <typename Self = Object> requires (__initializer__<Self>::enable)
    Object(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    /* Universal implicit constructor.  Implemented via the __init__ control struct. */
    template <typename... Args>
        requires (implicit_ctor<Object>::template enable<Args...>)
    Object(Args&&... args) : Object(
        implicit_ctor<Object>{},
        std::forward<Args>(args)...
    ) {}

    /* Universal explicit constructor.  Implemented via the __explicit_init__ control
    struct. */
    template <typename... Args>
        requires (explicit_ctor<Object>::template enable<Args...>)
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
            assert_(
                ptr(other) != nullptr,
                "Cannot copy-assign from a null object."
            );
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
            assert_(
                ptr(other) != nullptr,
                "Cannot move-assign from a null object."
            );
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
    template <typename Self, meta::python T>
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
            std::convertible_to<typename __contains__<Self, Key>::type, bool> && (
                std::is_invocable_r_v<bool, __contains__<Self, Key>, Self, Key> || (
                    !std::is_invocable_v<__contains__<Self, Key>, Self, Key> &&
                    meta::has_cpp<Self> &&
                    meta::has_contains<meta::cpp_type<Self>, meta::cpp_type<Key>>
                ) || (
                    !std::is_invocable_v<__contains__<Self, Key>, Self, Key> &&
                    !meta::has_cpp<Self>
                )
            )
        )
    [[nodiscard]] bool in(this Self&&, Key&&);

    /* Universal implicit conversion operator.  Implemented via the __cast__ control
    struct. */
    template <typename Self, typename T>
        requires (
            __cast__<Self, T>::enable &&
            std::same_as<T, typename __cast__<Self, T>::type>
        )
    [[nodiscard]] operator T(this Self&& self) {
        return __cast__<Self, T>{}(std::forward<Self>(self));
    }

    /* Contextually convert a Python object to a boolean, allowing Python-style truth
    checks. */
    template <typename Self>
    [[nodiscard]] explicit operator bool(this Self&& self) {
        if constexpr (
            meta::has_cpp<Self> &&
            meta::has_operator_bool<meta::cpp_type<Self>>
        ) {
            return static_cast<bool>(from_python(std::forward<Self>(self)));
        } else {
            int result = PyObject_IsTrue(ptr(std::forward<Self>(self)));
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        }
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
                    meta::has_cpp<Self> &&
                    meta::lookup_yields<
                        meta::cpp_type<Self>&,
                        typename __getitem__<Self, Key...>::type,
                        Key...
                    >
                ) || (
                    !std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...> &&
                    !meta::has_cpp<Self> &&
                    std::derived_from<typename __getitem__<Self, Key...>::type, Object>
                )
            )
        )
    decltype(auto) operator[](this Self&& self, Key&&... key) {
        if constexpr (
            meta::has_cpp<Self> &&
            std::is_invocable_v<__getitem__<Self, Key...>, Self, Key...>
        ) {
            return __getitem__<Self, Key...>{}(
                std::forward<Self>(self),
                std::forward<Key>(key)...
            );

        } else {
            return impl::Item<Self, Key...>(
                std::forward<Self>(self),
                std::forward<Key>(key)...
            );
        }
    }

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
                    meta::has_cpp<Self> &&
                    std::is_invocable_r_v<
                        typename __call__<Self, Args...>::type,
                        meta::cpp_type<Self>,
                        Args...
                    >
                ) || (
                    !std::is_invocable_v<__call__<Self, Args...>, Self, Args...> &&
                    !meta::has_cpp<Self> &&
                    std::derived_from<typename __call__<Self, Args...>::type, Object> &&
                    __getattr__<Self, "__call__">::enable &&
                    meta::Function<typename __getattr__<Self, "__call__">::type>
                )
            )
        )
    decltype(auto) operator()(this Self&& self, Args&&... args) {
        if constexpr (std::is_invocable_v<__call__<Self, Args...>, Self, Args...>) {
            return __call__<Self, Args...>{}(
                std::forward<Self>(self),
                std::forward<Args>(args)...
            );

        } else if constexpr (meta::has_cpp<Self>) {
            return from_python(std::forward<Self>(self))(
                std::forward<Args>(args)...
            );
        } else {
            return getattr<"__call__">(std::forward<Self>(self))(
                std::forward<Args>(args)...
            );
        }
    }
};


template <meta::python T> requires (!meta::is_qualified<T>)
[[nodiscard]] T borrow(PyObject* ptr) {
    return T(ptr, Object::borrowed_t{});
}


template <meta::python T> requires (!meta::is_qualified<T>)
[[nodiscard]] T steal(PyObject* ptr) {
    return T(ptr, Object::stolen_t{});
}


/* repr() calls on Python objects will always delegate to the equivalent Python
function. */
template <meta::python Self>
struct __repr__<Self> : returns<std::string> {
    static std::string operator()(Self obj) {
        Object str = steal<Object>(PyObject_Repr(ptr(obj)));
        if (str.is(nullptr)) {
            Exception::from_python();
        }
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(ptr(str), &size);
        if (data == nullptr) {
            Exception::from_python();
        }
        return std::string(data, size);
    }
};


/* Inserting a Python object into an output stream corresponds to a `str()` call at the
Python level. */
template <meta::inherits<std::ostream> Stream, meta::python Self>
struct __lshift__<Stream, Self>                             : returns<Stream&> {
    /// TODO: Str, Bytes, ByteArray should specialize this to write to the stream directly.
    static Stream& operator()(Stream stream, Self self) {
        Object str = steal<Object>(PyObject_Str(ptr(self)));
        if (str.is(nullptr)) {
            Exception::from_python();
        }
        Py_ssize_t len;
        const char* data = PyUnicode_AsUTF8AndSize(ptr(str), &len);
        if (data == nullptr) {
            Exception::from_python();
        }
        stream.write(data, len);
        return stream;
    }
};


/* Default initialize `bertrand::Object` to `None`. */
template <>
struct __init__<Object>                                     : returns<Object> {
    static auto operator()() { return borrow<Object>(Py_None); }
};


template <meta::python T>
struct __cast__<T>                                          : returns<T> {
    static T operator()(T value) { return std::forward<T>(value); }
};


/* Implicitly convert a Python object into one of its superclasses. */
template <meta::python From, meta::python To>
    requires (
        !meta::is_qualified<To> &&
        !meta::is<From, To> &&
        issubclass<std::remove_cvref_t<From>, To>()
    )
struct __cast__<From, To>                                   : returns<To> {
    static To operator()(From from) {
        if constexpr (std::is_lvalue_reference_v<From>) {
            return borrow<To>(ptr(from));
        } else {
            return steal<To>(release(from));
        }
    }
};


/* Implicitly convert a Python object into one of its subclasses by applying an
`isinstance<Derived>()` check. */
template <meta::python From, meta::python To>
    requires (
        !meta::is_qualified<To> &&
        !meta::is<From, To> &&
        issubclass<To, std::remove_cvref_t<From>>()
    )
struct __cast__<From, To>                                   : returns<To> {
    static auto operator()(From from) {
        if (isinstance<To>(from)) {
            if constexpr (std::is_lvalue_reference_v<From>) {
                return borrow<To>(ptr(from));
            } else {
                return steal<To>(release(from));
            }
        } else {
            throw TypeError(
                "cannot convert Python object from type '" + repr(Type<From>()) +
                "' to type '" + repr(Type<To>()) + "'"
            );
        }
    }
};


/* Implicitly convert any C++ value into a `bertrand::Object` by invoking the unary
cast type, and then reinterpreting as a dynamic object. */
template <meta::cpp T> requires (meta::has_python<T>)
struct __cast__<T, Object>                                  : returns<Object> {
    static auto operator()(T value) {
        return steal<Object>(release(
            meta::python_type<T>(std::forward<T>(value))
        ));
    }
};


/* Implicitly convert a Python object into any recognized C++ type by checking for an
equivalent Python type via __cast__, implicitly converting to that type, and then
implicitly converting the result to the C++ type in a 2-step process. */
template <typename From, meta::cpp To>
    requires (
        !meta::is_void<To> &&
        !meta::is_qualified<To> &&
         __cast__<To>::enable &&
         meta::Object<From>
    )
struct __cast__<From, To>                                   : returns<To> {
    static auto operator()(From self) {
        using Intermediate = std::remove_cvref_t<typename __cast__<To>::type>;
        return meta::implicit_cast<To>(
            meta::implicit_cast<Intermediate>(std::forward<From>(self))
        );
    }
};


/* Implicitly convert a Python object that wraps around a C++ type into an lvalue
reference to that type, which directly references the internal data. */
template <meta::python From, typename To>
    requires (
        meta::has_cpp<From> &&
        std::same_as<meta::cpp_type<std::remove_cvref_t<From>>, std::remove_cv_t<To>>
    )
struct __cast__<From, To&>                                  : returns<To&> {
    static To& operator()(From from) {
        return from_python(std::forward<From>(from));
    }
};


/* Implicitly convert a Python object that wraps around a C++ type into a pointer to
that type, which directly references the internal data.  This allows C++ functions
accepting pointer inputs to be called seamlessly from Python as long as the underlying
C++ type exactly matches. */
template <meta::python From, typename To>
    requires (
        meta::has_cpp<From> &&
        std::same_as<meta::cpp_type<std::remove_cvref_t<From>>, std::remove_cv_t<To>>
    )
struct __cast__<From, To*>                                  : returns<To*> {
    static To* operator()(From from) {
        return &from_python(std::forward<From>(from));
    }
};


/// TODO: perhaps I should also allow Object to be converted to `std::variant` along
/// the same lines?  I'm also not sure if these are really the best way to do this,
/// since you could just use a helper method to convert to whatever type you need,
/// or accept the optional/variant by value, in which case conversions should work as
/// expected?


/* Implicitly convert a Python object that wraps around a C++ type into an optional of
that type, which allows C++ functions expecting optional inputs to be called seamlessly
from Python. */
template <meta::python From, typename To> requires (std::convertible_to<From, To>)
struct __cast__<From, std::optional<To>>                    : returns<std::optional<To>> {
    static std::optional<To> operator()(From from) {
        return {meta::implicit_cast<To>(std::forward<From>(from))};
    }
};


/* Implicitly convert a Python object into a shared pointer as long as the underlying
types are convertible. */
template <meta::python From, typename To> requires (std::convertible_to<From, To>)
struct __cast__<From, std::shared_ptr<To>>                  : returns<std::shared_ptr<To>> {
    static std::shared_ptr<To> operator()(From value) {
        return std::make_shared<To>(
            meta::implicit_cast<To>(std::forward<From>(value))
        );
    }
};


/* Implicitly convert a Python object into a unique pointer as long as the underlying
types are convertible. */
template <meta::python From, typename To> requires (std::convertible_to<From, To>)
struct __cast__<From, std::unique_ptr<To>>                  : returns<std::unique_ptr<To>> {
    static std::unique_ptr<To> operator()(From value) {
        return std::make_unique<To>(
            meta::implicit_cast<To>(std::forward<From>(value))
        );
    }
};


/// NOTE: most control structures are enabled through the default specializations,
/// which attempt to introspect various dunder methods on the object type.  Because
/// these are fully generic for the dynamic `Object` type, those introspections will
/// always succeed, and the control structures will be enabled and delegate to Python,
/// which is the same behavior we'd be implementing here.  Doing it this way also
/// prevents template conflicts in downstream code, reducing the chance of ambiguity.


/// TODO: I might be unable to instantiate bertrand::signature for Python types until
/// func.h is included, since that's where Python signatures are defined.  In general,
/// it is probably not a bad idea to hold off defining any of the types that are here
/// (None, Slice, etc.) until core.h, since that's when all the core types and
/// specializations are defined.  Stuff like the print() function can also be defined
/// there, since all the python machinery for keyword arguments, etc. will be in place.


template <meta::Object Self, static_str Name>
struct __getattr__<Self, Name>                              : returns<Object> {};
template <meta::Object Self, static_str Name, std::convertible_to<Object> Value>
    requires (!meta::is_const<Self>)
struct __setattr__<Self, Name, Value>                       : returns<void> {};
template <meta::Object Self, static_str Name> requires (!meta::is_const<Self>)
struct __delattr__<Self, Name>                              : returns<void> {};
template <meta::Object Self, typename... Args> requires (meta::callable<
    Object(Arg<"*args", Object>, Arg<"**kwargs", Object>),
    Args...
>)
struct __call__<Self, Args...>                              : returns<Object> {
    static Object operator()(auto&&, auto&&...);  // TODO: this must be defined in func.h, after Python signatures are defined
};
template <meta::Object Self>
struct __iter__<Self>                                       : returns<Object> {};
template <meta::Object Self>
struct __reversed__<Self>                                   : returns<Object> {};


/* Allow 2-argument `issubclass()` checks between dynamic objects. */
template <meta::Object Derived, meta::Object Base>
struct __issubclass__<Derived, Base>                        : returns<bool> {
    static bool operator()(Derived derived) { return PyType_Check(ptr(derived)); }
    static bool operator()(Derived derived, Base base) {
        int result = PyObject_IsSubclass(
            ptr(derived),
            ptr(base)
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;
    }
};


/* Allow dynamic objects to be used as left-hand arguments in `issubclass()` checks. */
template <meta::Object Derived, typename Base>
struct __issubclass__<Derived, Base>                         : returns<bool> {
    static bool operator()(Derived derived) {
        int result = PyObject_IsSubclass(
            ptr(derived),
            ptr(Type<Base>())
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;
    }
};


/* Allow 2-argument `isinstance()` checks between dynamic objects. */
template <meta::Object Derived, meta::Object Base>
struct __isinstance__<Derived, Base>                        : returns<bool> {
    static bool operator()(Derived derived, Base base) {
        int result = PyObject_IsInstance(
            ptr(derived),
            ptr(base)
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;
    }
};


/* Allow dynamic objects to be used as left-hand arguments in `isinstance()` checks. */
template <meta::Object Derived, typename Base>
struct __isinstance__<Derived, Base>                         : returns<bool> {
    static bool operator()(Derived derived) {
        int result = PyObject_IsInstance(
            ptr(derived),
            ptr(Type<Base>())
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;
    }
};


////////////////////
////    TYPE    ////
////////////////////


/// NOTE: interface<> for Type<T> are usually defined alongside each wrapper type, so
/// there is no generic definition here.


/* `bertrand::Type<T>` refers to the Python type object associated with an unqualified
bertrand wrapper `T`.  Default-initializing the type will import it according to
proper, per-interpreter state. */
template <meta::has_python T>
    requires (
        meta::python<T> &&
        !meta::is_qualified<T> &&
        std::same_as<T, meta::python_type<T>>
    )
struct Type<T> : Object, interface<Type<T>> {
    struct __python__ : cls<__python__, Type<T>>, PyTypeObject {
        /// TODO: how the hell does this work?
        static Type<Type<T>> __import__();
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = Type> requires (__initializer__<Self>::enable)
    Type(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args>
        requires (implicit_ctor<Type>::template enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args>
        requires (explicit_ctor<Type>::template enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


/* `bertrand::Type<T>` can also accept qualified (possibly C++) types and redirect
them to their corresponding, normalized type. */
template <meta::has_python T>
struct Type : Type<std::remove_cvref_t<meta::python_type<T>>> {};


/* CTAD allows an object's type to be inferred from an initializer.  The corresponding
constructor simply forwards to the default constructor. */
template <meta::has_python T>
explicit Type(T&&) -> Type<std::remove_cvref_t<meta::python_type<T>>>;


/* Default constructor invokes the wrapper's inner `__import__()` method to return a
new reference to the type.  Such a method is always produced whenever bindings are
generated for a bertrand extension type, and the exact logic can be
implementation-defined to account for different standards within the CPython API. */
template <meta::has_import T>
struct __init__<Type<T>>                                   : returns<Type<T>> {
    static auto operator()() {
        return std::remove_cvref_t<T>::__python__::__import__();
    }
};


/* The single-argument constructor is only enabled for CTAD purposes.  It is identical
to calling the default constructor normally. */
template <meta::has_import T, meta::has_python U>
    requires (meta::is<T, meta::python_type<U>>)
struct __init__<Type<T>, U>                                : returns<Type<T>> {
    static auto operator()(U) { return Type<T>(); }
};


/// TODO: 3-argument form creates a new dynamic type that subclasses from the templated
/// type.  Maybe this is just a 2-argument form that limits the dynamic type to
/// single inheritance?  (e.g. `Type<Int>("my_int", {{"abc", 123}})`)


////////////////////
////    NONE    ////
////////////////////


template <>
struct interface<NoneType> : impl::none_tag {};
template <>
struct interface<Type<NoneType>> {};


/* Represents the type of Python's `None` singleton in C++. */
struct NoneType : Object, interface<NoneType> {
    struct __python__ : cls<__python__, NoneType>, PyObject {
        static Type<NoneType> __import__();
    };

    NoneType(PyObject* p, borrowed_t t) : Object(p, t) {}
    NoneType(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = NoneType> requires (__initializer__<Self>::enable)
    NoneType(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args> requires (implicit_ctor<NoneType>::template enable<Args...>)
    NoneType(Args&&... args) : Object(
        implicit_ctor<NoneType>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<NoneType>::template enable<Args...>)
    explicit NoneType(Args&&... args) : Object(
        explicit_ctor<NoneType>{},
        std::forward<Args>(args)...
    ) {}
};


template <meta::is_void Self>
struct __cast__<Self>                                       : returns<NoneType> {};


template <>
struct __init__<NoneType>                                   : returns<NoneType> {
    static auto operator()() { return borrow<NoneType>(Py_None); }
};


template <meta::NoneLike From>
struct __cast__<From, NoneType>                             : returns<NoneType> {
    static NoneType operator()(From value) { return {}; }
};


template <meta::None From, meta::NoneLike To>
struct __cast__<From, To>                                   : returns<To> {
    static To operator()(From value) { return {}; }
};


template <meta::None From, typename To> requires (!std::convertible_to<From, To>)
struct __cast__<From, std::optional<To>>                    : returns<std::optional<To>> {
    static std::optional<To> operator()(From value) { return std::nullopt; }
};


template <meta::None From, typename To>
struct __cast__<From, To*>                                  : returns<To*> {
    static To* operator()(From value) { return nullptr; }
};


template <meta::None From, typename To>
struct __cast__<From, std::shared_ptr<To>>                  : returns<std::shared_ptr<To>> {
    static std::shared_ptr<To> operator()(From value) { return nullptr; }
};


template <meta::None From, typename To>
struct __cast__<From, std::unique_ptr<To>>                  : returns<std::unique_ptr<To>> {
    static std::unique_ptr<To> operator()(From value) { return nullptr; }
};


template <meta::None Self>
struct __hash__<Self>                                       : returns<size_t> {};


inline const NoneType None;


//////////////////////////////
////    NOTIMPLEMENTED    ////
//////////////////////////////


template <>
struct interface<NotImplementedType> : impl::notimplemented_tag {};
template <>
struct interface<Type<NotImplementedType>> {};


/* Represents the type of Python's `NotImplemented` singleton in C++. */
struct NotImplementedType : Object, interface<NotImplementedType> {
    struct __python__ : cls<__python__, NotImplementedType>, PyObject {
        static Type<NotImplementedType> __import__();
    };

    NotImplementedType(PyObject* p, borrowed_t t) : Object(p, t) {}
    NotImplementedType(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = NotImplementedType> requires (__initializer__<Self>::enable)
    NotImplementedType(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args>
        requires (implicit_ctor<NotImplementedType>::template enable<Args...>)
    NotImplementedType(Args&&... args) : Object(
        implicit_ctor<NotImplementedType>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args>
        requires (explicit_ctor<NotImplementedType>::template enable<Args...>)
    explicit NotImplementedType(Args&&... args) : Object(
        explicit_ctor<NotImplementedType>{},
        std::forward<Args>(args)...
    ) {}
};


template <>
struct __init__<NotImplementedType>                         : returns<NotImplementedType> {
    static auto operator()() {
        return borrow<NotImplementedType>(Py_NotImplemented);
    }
};


template <meta::NotImplementedLike From>
struct __cast__<From, NotImplementedType>                   : returns<NotImplementedType> {
    static NotImplementedType operator()(const From& value) { return {}; }
};


template <meta::NotImplemented From, meta::NotImplementedLike To>
struct __cast__<From, To>                                   : returns<To> {
    static To operator()(From value) { return {}; }
};


template <meta::NotImplemented Self>
struct __hash__<Self>                                       : returns<size_t> {};


inline const NotImplementedType NotImplemented;


////////////////////////
////    ELLIPSIS    ////
////////////////////////


template <>
struct interface<EllipsisType> : impl::ellipsis_tag {};
template <>
struct interface<Type<EllipsisType>> {};


/* Represents the type of Python's `Ellipsis` singleton in C++. */
struct EllipsisType : Object, interface<EllipsisType> {
    struct __python__ : cls<__python__, EllipsisType>, PyObject {
        static Type<EllipsisType> __import__();
    };

    EllipsisType(PyObject* p, borrowed_t t) : Object(p, t) {}
    EllipsisType(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = EllipsisType> requires (__initializer__<Self>::enable)
    EllipsisType(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args>
        requires (implicit_ctor<EllipsisType>::template enable<Args...>)
    EllipsisType(Args&&... args) : Object(
        implicit_ctor<EllipsisType>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args>
        requires (explicit_ctor<EllipsisType>::template enable<Args...>)
    explicit EllipsisType(Args&&... args) : Object(
        explicit_ctor<EllipsisType>{},
        std::forward<Args>(args)...
    ) {}
};


template <>
struct __init__<EllipsisType>                               : returns<EllipsisType> {
    static auto operator()() {
        return borrow<EllipsisType>(Py_Ellipsis);
    }
};


template <meta::EllipsisLike From>
struct __cast__<From, EllipsisType>                         : returns<EllipsisType> {
    static EllipsisType operator()(const From& value) { return {}; }
};


template <meta::Ellipsis From, meta::EllipsisLike To>
struct __cast__<From, To>                                   : returns<To> {
    static To operator()(From value) { return {}; }
};


template <meta::Ellipsis Self>
struct __hash__<Self>                                       : returns<size_t> {};


inline const EllipsisType Ellipsis;


/////////////////////
////    SLICE    ////
/////////////////////


/// TODO: this must be declared after func.h
// template <meta::inherits<impl::slice_tag> Self>
// struct __getattr__<Self, "indices"> : returns<Function<
//     Tuple<Int>(Arg<"length", const Int&>)
// >> {};
template <meta::inherits<impl::slice_tag> Self>
struct __getattr__<Self, "start">                           : returns<Object> {
    static Object operator()(Self self) { return self.start; }
};
template <meta::inherits<impl::slice_tag> Self>
struct __getattr__<Self, "stop">                            : returns<Object> {
    static Object operator()(Self self) { return self.stop; }
};
template <meta::inherits<impl::slice_tag> Self>
struct __getattr__<Self, "step">                            : returns<Object> {
    static Object operator()(Self self) { return self.step; }
};


template <>
struct interface<Slice> : impl::slice_tag {
    /* Get the start object of the slice.  Note that this might not be an integer. */
    __declspec(property(get = _get_start)) Object start;
    [[nodiscard]] Object _get_start(this auto&& self) {
        return view(self)->start ?
            borrow<Object>(view(self)->start) :
            borrow<Object>(Py_None);
    }

    /* Get the stop object of the slice.  Note that this might not be an integer. */
    __declspec(property(get = _get_stop)) Object stop;
    [[nodiscard]] Object _get_stop(this auto&& self) {
        return view(self)->stop ?
            borrow<Object>(view(self)->stop) :
            borrow<Object>(Py_None);
    }

    /* Get the step object of the slice.  Note that this might not be an integer. */
    __declspec(property(get = _get_step)) Object step;
    [[nodiscard]] Object _get_step(this auto&& self) {
        return view(self)->step ?
            borrow<Object>(view(self)->step) :
            borrow<Object>(Py_None);
    }

    /* C++-style `get<I>()` accessor for slices, to enable structured bindings. */
    template <size_t I> requires (I < 3)
    [[nodiscard]] Object get(this auto&& self) {
        if constexpr (I == 0) {
            return std::forward<decltype(self)>(self).start;
        } else if constexpr (I == 1) {
            return std::forward<decltype(self)>(self).stop;
        } else {
            return std::forward<decltype(self)>(self).step;
        }
    }

    /* Normalize the indices of this slice against a container of the given length.
    This accounts for negative indices and clips those that are out of bounds.
    Returns a simple data struct with the following fields:

        * (Py_ssize_t) start: the normalized start index
        * (Py_ssize_t) stop: the normalized stop index
        * (Py_ssize_t) step: the normalized step size
        * (Py_ssize_t) length: the number of indices that are included in the slice

    It can be destructured using C++17 structured bindings:

        auto [start, stop, step, length] = slice.indices(size);
    */
    [[nodiscard]] auto indices(this auto&& self, size_t size) {
        struct Indices {
            Py_ssize_t start = 0;
            Py_ssize_t stop = 0;
            Py_ssize_t step = 0;
            Py_ssize_t length = 0;
        } result;
        if (PySlice_GetIndicesEx(
            ptr(self),
            size,
            &result.start,
            &result.stop,
            &result.step,
            &result.length
        )) {
            Exception::from_python();
        }
        return result;
    }
};
template <>
struct interface<Type<Slice>> {
    template <meta::Slice Self>
    [[nodiscard]] static Object start(Self&& self) { return self.start; }
    template <meta::Slice Self>
    [[nodiscard]] static Object stop(Self&& self) { return self.stop; }
    template <meta::Slice Self>
    [[nodiscard]] static Object step(Self&& self) { return self.step; }
    template <size_t I, meta::Slice Self> requires (I < 3)
    [[nodiscard]] static Object get(Self&& self) { return self.template get<I>(); }
    template <meta::Slice Self>
    [[nodiscard]] static auto indices(Self&& self, size_t size) { return self.indices(size); }
};


/* Represents a statically-typed Python `slice` object in C++.  Note that the start,
stop, and step values do not strictly need to be integers. */
struct Slice : Object, interface<Slice> {
    struct __python__ : cls<__python__, Slice>, PySliceObject {
        static Type<Slice> __import__();
    };

    Slice(PyObject* p, borrowed_t t) : Object(p, t) {}
    Slice(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename Self = Slice> requires (__initializer__<Self>::enable)
    Slice(const std::initializer_list<typename __initializer__<Self>::type>& init) :
        Object(__initializer__<Self>{}(init))
    {}

    template <typename... Args>
        requires (implicit_ctor<Slice>::template enable<Args...>)
    Slice(Args&&... args) : Object(
        implicit_ctor<Slice>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args>
        requires (explicit_ctor<Slice>::template enable<Args...>)
    explicit Slice(Args&&... args) : Object(
        explicit_ctor<Slice>{},
        std::forward<Args>(args)...
    ) {}
};


/// TODO: can't I separate __isinstance__/__issubclass__ into left and right-hand
/// versions?


template <meta::Object Derived, meta::Slice Base>
struct __isinstance__<Derived, Base>                        : returns<bool> {
    static constexpr bool operator()(Derived obj) { return PySlice_Check(ptr(obj)); }
};


template <typename... Args>
    requires (meta::callable<
        Slice(
            Arg<"start", Object>::pos::opt,
            Arg<"stop", Object>::pos::opt,
            Arg<"step", Object>::pos::opt
        ),
        Args...
    >)
struct __init__<Slice, Args...>                             : returns<Slice> {
    static auto operator()() {
        Slice result = steal<Slice>(
            PySlice_New(nullptr, nullptr, nullptr)
        );
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }

    static auto operator()(Object stop) {
        Slice result = steal<Slice>(PySlice_New(
            nullptr,
            ptr(stop),
            nullptr
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }

    static auto operator()(Object start, Object stop) {
        Slice result = steal<Slice>(PySlice_New(
            ptr(start),
            ptr(stop),
            nullptr
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }


    static auto operator()(Object start, Object stop, Object step) {
        Slice result = steal<Slice>(PySlice_New(
            ptr(start),
            ptr(stop),
            ptr(step)
        ));
        if (result.is(nullptr)) {
            Exception::from_python();
        }
        return result;
    }
};


template <meta::Slice L, meta::Slice R>
struct __lt__<L, R>                                         : returns<Bool> {};
template <meta::Slice L, meta::Slice R>
struct __le__<L, R>                                         : returns<Bool> {};
template <meta::Slice L, meta::Slice R>
struct __eq__<L, R>                                         : returns<Bool> {};
template <meta::Slice L, meta::Slice R>
struct __ne__<L, R>                                         : returns<Bool> {};
template <meta::Slice L, meta::Slice R>
struct __ge__<L, R>                                         : returns<Bool> {};
template <meta::Slice L, meta::Slice R>
struct __gt__<L, R>                                         : returns<Bool> {};


}  // namespace bertrand


namespace std {

    template <bertrand::meta::Slice T>
    struct tuple_size<T> : std::integral_constant<size_t, 3> {};

    template <size_t I, bertrand::meta::Slice T> requires (I < 3)
    struct tuple_element<I, T> { using type = bertrand::Object; };

    template <size_t I, bertrand::meta::Slice T> requires (I < 3)
    [[nodiscard]] auto get(T&& slice) {
        if constexpr (I == 0) {
            return std::forward<T>(slice).start;
        } else if constexpr (I == 1) {
            return std::forward<T>(slice).stop;
        } else {
            return std::forward<T>(slice).step;
        }
    }

}


#endif
