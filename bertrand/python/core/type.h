#ifndef BERTRAND_PYTHON_CORE_TYPE_H
#define BERTRAND_PYTHON_CORE_TYPE_H

#include "declarations.h"
#include "object.h"
#include "code.h"
#include "except.h"


namespace py {


namespace impl {


    template <typename Wrapper, typename... Args>
        requires (
            has_type<Wrapper> &&
            is_type<Type<Wrapper>> &&
            std::constructible_from<typename Type<Wrapper>::__python__, Args...> &&
            sizeof(Type<Wrapper>::__python__) != 0
        )
    static Wrapper construct(Args&&... args) {
        using Self = Type<Wrapper>::__python__;
        Type<Wrapper> type;
        PyTypeObject* cls = reinterpret_cast<PyTypeObject*>(ptr(type));
        Self* self = reinterpret_cast<Self*>(cls->tp_alloc(cls, 0));
        if (self == nullptr) {
            Exception::from_python();
        }
        try {
            new (self) Self(std::forward<Args>(args)...);
        } catch (...) {
            cls->tp_free(self);
            throw;
        }
        PyObject_GC_Track(self);
        return reinterpret_steal<Wrapper>(reinterpret_cast<PyObject*>(self));
    }

    /* Marks a py::Object subclass as a type object, and exposes several helpers to
    make writing custom types as easy as possible.  Every specialization of `Type<>`
    should inherit from this class and implement a public `__python__` struct that
    inherits from its CRTP helpers. */
    struct TypeTag : BertrandTag {
    protected:

        /* A convenience struct implementing the overload pattern for visiting a
        std::variant. */
        template <typename... Ts>
        struct Visitor : Ts... {
            using Ts::operator()...;
        };

        template <typename Begin, std::sentinel_for<Begin> End>
        struct Iterator {
            PyObject_HEAD
            Begin begin;
            End end;

            static void __dealloc__(Iterator* self) {
                PyObject_GC_UnTrack(self);
                self->begin.~Begin();
                self->end.~End();
                PyTypeObject* type = Py_TYPE(self);
                type->tp_free(self);
                Py_DECREF(type);
            }

            static int __traverse__(Iterator* self, visitproc visit, void* arg) {
                Py_VISIT(Py_TYPE(self));
                return 0;
            }

            static PyObject* __next__(Iterator* self) {
                try {
                    if (self->begin == self->end) {
                        return nullptr;
                    }
                    if constexpr (std::is_lvalue_reference_v<decltype(*(self->begin))>) {
                        auto result = wrap(*(self->begin));  // non-owning obj
                        ++(self->begin);
                        return release(result);
                    } else {
                        auto result = as_object(*(self->begin));  // owning obj
                        ++(self->begin);
                        return release(result);
                    }
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

        };

        /* Shared behavior for all Python types, be they wrappers around external C++
        classes or pure Python, possibly even implemented inline within the CRTP class
        itself.  Each type must minimally support these methods.  Everything else is an
        implementation detail. */
        template <typename CRTP, typename Wrapper, typename CppType>
        struct BaseDef : BertrandTag {
        protected:

            template <StaticStr ModName>
            struct Bindings;

        public:

            /* Generate a new instance of the type object to be attached to the given
            module.

            The `bindings` argument is a helper that includes a number of convenience
            methods for exposing the type to Python.  The `__export__` script must end
            with a call to `bindings.finalize<Bases...>()`, which will instantiate a
            unique heap type for each module.  This is the correct way to expose types
            using per-module state, which allows for multiple sub-interpreters to run
            in parallel without interfering with one other (possibly without a central
            GIL).  The bindings are automatically executed by the import system
            whenever the enclosing module is loaded into a fresh interpreter.

            Any attributes added at this stage are guaranteed be unique for each
            module.  It's still up to the user to make sure that any data they
            reference does not cause race conditions with other modules, but the
            modules themselves will not impede this.

            Note that this method does not actually attach the type to the module -
            that is done in the module's `__export__()` script, which is semantically
            identical to this method and provides much of the same syntax.

            The default implementation forwards to `__import__()`, which eases the
            binding of standard library types and external, C-style Python extensions.
            Types that reference a C++ class or are implemented inline should override
            this method to provide their own bindings. */
            template <StaticStr ModName>
            static Type<Wrapper> __export__(Bindings<ModName> bindings) {
                // bindings.var<"foo">(&CppType::foo);
                // bindings.method<"bar">("an example method", &CppType::bar);
                // bindings.type<"Baz", CppType::Baz, Bases...>();
                // ...
                // return bindings.template finalize<Bases...>();
                return CRTP::__import__();
            }

            /* Return a new reference to the type by importing its parent module and
            unpacking this specific type.

            This is called automatically by the default constructor for
            `Type<Wrapper>`, and is the proper way to handle per-module state, instead
            of using static type objects.  It will implicitly invoke the `__export__()`
            method if the module was not previously loaded, either via a
            `PyImport_Import()` API call or the simplified `Module` constructor. */
            static Type<Wrapper> __import__() {
                throw NotImplementedError(
                    "the __import__() method must be defined for all Python types: "
                    + demangle(typeid(CRTP).name())
                );
            }

            /* tp_dealloc calls the ordinary C++ destructor. */
            static void __dealloc__(CRTP* self) {
                PyObject_GC_UnTrack(self);  // required for heap types
                self->~CRTP();  // cleans up all C++ resources
                PyTypeObject* type = Py_TYPE(self);
                type->tp_free(self);
                Py_DECREF(type);  // required for heap types
            }

        };

        /* A CRTP base class that automatically generates boilerplate bindings for a
        new Python type which wraps around an external C++ type.  This is the preferred
        way of writing new Python types in C++.

        This class is only available to specializations of `py::Type<>`, each of which
        should define a public `__python__` struct that inherits from it via the CRTP
        pattern.  It uses template metaprogramming to automate the creation of Python
        types and their associated methods, slots, and other attributes in a way that
        conforms to Python best practices.  This includes:

            -   The use of heap types over static types, which are closer to native
                Python and can be mutated at runtime.
            -   Per-module (rather than per-process) state, which allows for multiple
                sub-interpreters to run in parallel.
            -   Cyclic GC support, which is necessary for types that contain Python
                objects, as well as the heap type itself.
            -   Direct access to the wrapped C++ object, which can be a non-owning,
                mutable or immutable reference, allowing for shared state across the
                language boundary.
            -   Default implementations of common slots like `__repr__`, `__hash__`,
                `__iter__`, and others, which can be detected via template
                metaprogramming and naturally overridden by the user if necessary.
            -   A shared metaclass, which demangles the C++ type name and delegates
                `isinstance()` and `issubclass()` checks to the `py::__isinstance__<>`
                and `py::__issubclass__<>` control structs, respectively.
            -   Support for C++ templates, which can be indexed in Python using the
                metaclass's `__getitem__` slot.
            -   Method overloading, wherein each method is represented as an overload
                set attached to the type object via the descriptor protocol.
            -   Modeling for member and static methods and variables, which are
                translated 1:1 into Python equivalents.

        Needless to say, these are all very complex and error-prone tasks that are best
        left to Bertrand's metaprogramming facilities.  By using this base class, the
        user can focus only on the public interface of the class they're trying to bind
        and not on any of the low-level details necessary to configure it for Python.
        Its protected status also strongly couples the type with the enclosing wrapper,
        producing a single unit of code that is easier to reason about and manage. */
        template <typename CRTP, typename Wrapper, typename CppType = void>
        struct def;

        /* A specialization of `def` for pure Python classes, which have no C++
        equivalent.  This is the default specialization if the C++ type is omitted
        during inheritance.  It expects the user to either implement a raw CPython type
        directly inline or supply a reference to an externally-defined `PyTypeObject`,
        which presumably exists somewhere in the CPython API or a third-party
        library.

        Users of this type must at least implement the `__import__()` hook, which
        returns a new reference to the type object, wherever it is defined.  If the
        type is defined inline within this class, then the user must also implement the
        `__export__()` hook, which is called to initialize the type and expose it to
        Python.  Everything else is an implementation detail. */
        template <typename CRTP, typename Wrapper>
        struct def<CRTP, Wrapper, void> : BaseDef<CRTP, Wrapper, void> {
            static constexpr Origin __origin__ = Origin::PYTHON;
        };

    };

}


////////////////////
////    TYPE    ////
////////////////////


template <>
struct Type<Object>;
template <>
struct Interface<Type<Object>> {};


/* `Type<Object>` (the default specialization) refers to a dynamic type, which can be
used polymorphically to represent the type of any Python object.  More specialized
types can always be implicitly converted to this type, but doing the opposite incurs a
runtime `issubclass()` check, and can potentially raise a `TypeError`, just like
`py::Object`. */
template <>
struct Type<Object> : Object, Interface<Type<Object>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(
                &PyBaseObject_Type
            ));
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

};


/* Allow py::Type<> to be templated on arbitrary C++ types by redirecting to its Python
equivalent (py::Type<int>() == py::Type<py::Int>()). */
template <typename T> requires (__as_object__<T>::enable)
struct Type<T> : Type<typename __as_object__<T>::type> {};


/* Explicitly call py::Type(obj) to deduce the Python type of an arbitrary object. */
template <typename T> requires (__as_object__<T>::enable)
explicit Type(const T&) -> Type<typename __as_object__<T>::type>;


/* All types are default-constructible by invoking the `__import__()` method, which
ensures that the result always matches the current state of the Python interpreter. */
template <typename T>
struct __init__<Type<T>> : Returns<Type<T>> {
    static auto operator()() {
        return Type<T>::__python__::__import__();
    }
};


/* Metaclasses are represented as types of types, and are constructible by recursively
calling `Py_TYPE(cls)` on the inner type until we reach a concrete implementation. */
template <typename T>
struct __init__<Type<Type<T>>> : Returns<Type<Type<T>>> {
    static auto operator()() {
        return reinterpret_borrow<Type<Type<T>>>(reinterpret_cast<PyObject*>(
            Py_TYPE(ptr(Type<T>()))
        ));
    }
};


/* Implement the CTAD guide by default-initializing the corresponding py::Type. */
template <typename T> requires (__as_object__<T>::enable)
struct __explicit_init__<Type<typename __as_object__<T>::type>, T> :
    Returns<Type<typename __as_object__<T>::type>>
{
    static auto operator()(const T& obj) {
        return Type<typename __as_object__<T>::type>();
    }
};


/// NOTE: additional metaclass constructors for py::Type are defined in common.h


/* Calling a py::Type is the same as invoking the inner type's constructor. */
template <typename T, typename... Args> requires (std::constructible_from<T, Args...>)
struct __call__<Type<T>, Args...> : Returns<T> {
    static auto operator()(const Type<T>& self, Args&&... args) {
        return T(std::forward<Args>(args)...);
    }
};


/* `isinstance()` is already implemented for all `py::Type` specializations by
recurring on the templated type. */
template <typename T, typename Cls>
struct __isinstance__<T, Type<Cls>> : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::python_like<T>) {
            if (!PyType_Check(ptr(obj))) {
                return false;
            }
            if constexpr (impl::dynamic_type<Cls>) {
                return true;
            } else {
                int result = PyObject_IsSubclass(
                    ptr(obj),
                    ptr(Type<Cls>())
                );
                if (result < 0) {
                    Exception::from_python();
                }
                return result;
            }
        } else {
            return false;
        }
    }
    static constexpr bool operator()(const T& obj, const Type<Cls>& cls) {
        if constexpr (impl::python_like<T> && impl::dynamic_type<Cls>) {
            int result = PyObject_IsSubclass(
                ptr(obj),
                ptr(cls)
            );
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            return isinstance<Cls>(obj);
        }
    }
};


/* `issubclass()` is already implemented for all `py::Type` specializations by
recurring on the templated type. */
template <typename T, typename Cls>
struct __issubclass__<T, Type<Cls>> : Returns<bool> {
    static consteval bool operator()() { return issubclass<T, Cls>(); }
    static constexpr bool operator()(const T& obj) { return issubclass<Cls>(obj); }
    static constexpr bool operator()(const T& obj, const Type<Cls>& cls) {
        if constexpr (impl::dynamic_type<Cls>) {
            int result = PyObject_IsSubclass(
                ptr(as_object(obj)),
                ptr(cls)
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;
        } else {
            return issubclass<Cls>(obj);
        }
    }
};


/* Implicitly convert the py::Type of a subclass into one of its parent classes or
a type of the same category. */
template <typename From, typename To> requires (
    std::derived_from<
        typename __as_object__<From>::type,
        typename __as_object__<To>::type
    >
)
struct __cast__<Type<From>, Type<To>> : Returns<Type<To>> {
    static auto operator()(const Type<From>& from) {
        return reinterpret_borrow<Type<To>>(ptr(from));
    }
    static auto operator()(Type<From>&& from) {
        return reinterpret_steal<Type<To>>(release(from));
    }
};


/* Implicitly convert the py::Type of a parent class into one of its subclasses by
applying a runtime `issubclass()` check. */
template <typename From, typename To> requires (
    !std::same_as<
        typename __as_object__<To>::type,
        typename __as_object__<From>::type
    > &&
    std::derived_from<
        typename __as_object__<To>::type,
        typename __as_object__<From>::type
    >
)
struct __cast__<Type<From>, Type<To>> : Returns<Type<To>> {
    static auto operator()(const Type<From>& from) {
        if (issubclass<
            typename __as_object__<To>::type,
            typename __as_object__<From>::type
        >()) {
            return reinterpret_borrow<Type<To>>(ptr(from));
        } else {
            throw TypeError(
                "cannot convert Python type from '" + repr(Type<From>()) +
                 "' to '" + repr(Type<To>()) + "'"
            );
        }
    }
    static auto operator()(Type<From>&& from) {
        if (issubclass<
            typename __as_object__<To>::type,
            typename __as_object__<From>::type
        >()) {
            return reinterpret_steal<Type<To>>(release(from));
        } else {
            throw TypeError(
                "cannot convert Python type from '" + repr(Type<From>()) +
                 "' to '" + repr(Type<To>()) + "'"
            );
        }
    }
};


/// NOTE: forward the BertrandMeta interface below if the type originates from C++.


////////////////////
////    META    ////
////////////////////


template <>
struct Type<BertrandMeta>;


template <>
struct Interface<BertrandMeta> {};
template <>
struct Interface<Type<BertrandMeta>> {};


/* An instance of Bertrand's metatype, which is used to represent all C++ types that
have been exposed to Python. */
struct BertrandMeta : Object, Interface<BertrandMeta> {

    BertrandMeta(PyObject* p, borrowed_t t) : Object(p, t) {}
    BertrandMeta(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<BertrandMeta>::enable<Args...>)
    BertrandMeta(Args&&... args) : Object(
        implicit_ctor<BertrandMeta>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<BertrandMeta>::enable<Args...>)
    explicit BertrandMeta(Args&&... args) : Object(
        explicit_ctor<BertrandMeta>{},
        std::forward<Args>(args)...
    ) {}

};


/* Bertrand's metatype, which is used to expose C++ classes to Python and simplify the
binding process. */
template <>
struct Type<BertrandMeta> : Object, Interface<Type<BertrandMeta>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, BertrandMeta> {
        static constexpr StaticStr __doc__ =
R"doc(A shared metaclass for all Bertrand extension types.

Notes
-----
This metaclass identifies all types that originate from C++.  It is used to
automatically demangle the type name, provide accurate `isinstance()` and
`issubclass()` checks based on Bertrand's C++ control structures, allow C++ template
subscription via `__getitem__`, and provide a common interface for class-level property
descriptors, method overloading (including for built-in slots), and other common
behaviors.

Performing an `isinstance()` check against this type will return `True` if and only if
the candidate type is implemented in C++.)doc";

        PyTypeObject base;

        /// TODO: instancecheck, subclasscheck may need updates to avoid conflicts
        /// with overload sets, etc.

        /* The `instancecheck` and `subclasscheck` function pointers are used to back
        the metaclass's Python-level `__instancecheck__()` and `__subclasscheck()__`
        methods, and are automatically set during construction for each type. */
        bool(*instancecheck)(__python__*, PyObject*);
        bool(*subclasscheck)(__python__*, PyObject*);

        /* C++ needs a few extra hooks to accurately model class-level variables in a
        way that shares state with Python.  The dictionaries below are searched during
        the metaclass's Python-level `__getattr__`, `__setattr__`, and `__delattr__`
        methods, which are invoked whenever an unrecognized class-level attribute
        access is attempted. */
        struct ClassGet {
            PyObject*(*func)(PyObject* /* self */, void* /* closure */);
            void* closure;  // holds a pointer to a class-level C++ variable
        };
        struct ClassSet {
            /// NOTE: value might be null, which should delete the attribute
            int(*func)(PyObject* /* self */, PyObject* /* value */, void* /* closure */);
            void* closure;  // holds a pointer to a class-level C++ variable
        };
        using ClassGetters = std::unordered_map<std::string_view, ClassGet>;
        using ClassSetters = std::unordered_map<std::string_view, ClassSet>;
        ClassGetters class_getters;
        ClassSetters class_setters;

        /* The metaclass will demangle the C++ class name and track templates via an
        internal dictionary accessible through `__getitem__()`. */
        PyObject* demangled;
        PyObject* templates;
        __python__* parent;  // backreference to the public template interface, or null
        /// NOTE: by following the parent pointer, we can look up an instantiation's
        /// exact type in the global C++ registry, then check against its parent and
        /// again for the class itself in order to do an efficient type check.  That
        /// would involve a single import and 2 PyType_IsSubtype() calls, which is
        /// as good as this gets.  Built-in classes would be faster thanks to specific
        /// API functions, and types which need more specific checks would be slower.

        /* Python typically uses C slots to implement its object model, which are
        usually defined statically and are therefore unable to support advanced C++
        features, like method overloading, templates, etc.  In order to solve this,
        Bertrand's metaclass reserves an internal `py::OverloadSet` for each slot,
        which defaults to null and can be populated from either side of the language
        divide.

        The way this is implemented is rather complicated, and deserves explanation:

        When the `bindings.type<"name", Cls, Bases...>()` helper is invoked from an
        `__export__()` script, it corresponds to the creation of a new heap type, which
        is an instance of this class.  The type initially does not specify any slots,
        relying on Python to inherit them from the bases like normal.  Once the type is
        created, these inherited slots are recorded in the metaclass's `parent_*`
        pointers, and then overridden with thin wrappers that check for a matching
        overload set.  If one is found, then the slot forwards its arguments to the set
        using an optimized vectorcall protocol, which accounts for `self` if
        applicable.  If no overload set is found, then the slot will default to the
        inherited implementation.  If no such implementation exists, then the slot will
        raise an `AttributeError` instead.

        The overload sets are stored as opaque pointers listed below, which initialize
        to null.  They are populated by the `bindings.method<"name">()` helper in the
        type's `__export__()` script just like all other methods, but via a slightly
        different code path due to the presence of getset descriptors on the metaclass
        itself.  When the helper is called with a reserved slot name (like `__init__`,
        `__getitem__`, etc.), then a property setter is invoked, which will enforce a
        minimal signature check and then populate the corresponding overload set with
        the given method.  If the helper is invoked multiple times with the same name,
        then the set will be appended to, allowing for C++-style method overloading of
        internal Python slots.

        One of the chief benefits of this system is that it allows overloads to be
        accessed symmetrically from both sides of the language divide without any
        special syntax.  As such, users can define a type in C++ using standard method
        overloading and then expose it to Python, which will see the overloaded methods
        as distinct and dispatch to the correct one based on the arguments.  Even
        better, new overloads can be registered dynamically from either side, without
        regard to whether the method represents a built-in slot or not.

        This approach also limits the performance overhead of dynamic Python lookups as
        much as possible by encoding everything in Python's slot system, which bypasses
        much of the interpreter's normal attribute lookup machinery.  Besides searching
        for an overload, the only additional work that needs to be done is a single
        null pointer check and possible (stack) allocation for the vectorcall
        arguments.  The result should thus have similar overall performance to a native
        Python type, with the added benefit of C++-style method overloading.

        See the Python data model for a complete list of Python dunder methods:
            https://docs.python.org/3/reference/datamodel.html

        See the Python C API for a complete list of C slots:
            https://docs.python.org/3/c-api/typeobj.html
        */
        /// TODO: I might be able to delete the overload sets that aren't directly used
        /// in the type's slots, which could save a bunch of memory.
        /// -> The upside to keeping them is that I can enforce a strict signature
        /// match, which is useful for debugging and type checking.  However, I could
        /// maybe do the same by specializing method<"name">() to only be enabled if
        /// the signature matches at compile time.  That wouldn't prevent you from
        /// messing things up on the python side, but the memory savings would probably
        /// make up for it.
        /// -> Perhaps I can keep the descriptor, but lose the overload sets?  That way,
        /// I still get the same Python-level signature enforcement, but also the
        /// corresponding memory savings.
        PyObject* __new__;
        PyObject* __init__;
        PyObject* __del__;  // delete
        PyObject* __repr__;
        PyObject* __str__;
        PyObject* __bytes__;  // delete
        PyObject* __format__;  // delete
        PyObject* __lt__;
        PyObject* __le__;
        PyObject* __eq__;
        PyObject* __ne__;
        PyObject* __ge__;
        PyObject* __gt__;
        PyObject* __hash__;
        PyObject* __bool__;
        PyObject* __getattr__;
        PyObject* __getattribute__;  // delete
        PyObject* __setattr__;
        PyObject* __delattr__;
        PyObject* __dir__;  // delete
        PyObject* __get__;
        PyObject* __set__;
        PyObject* __delete__;
        PyObject* __init_subclass__;  // delete
        PyObject* __set_name__;  // delete
        PyObject* __mro_entries__;  // delete
        PyObject* __instancecheck__;  // delete
        PyObject* __subclasscheck__;   // delete
        PyObject* __class_getitem__;  // delete, metaclass's __getitem__ always takes priority
        PyObject* __call__;
        PyObject* __len__;
        PyObject* __length_hint__;  // delete
        PyObject* __getitem__;
        PyObject* __setitem__;
        PyObject* __delitem__;
        PyObject* __missing__;  // delete
        PyObject* __iter__;
        PyObject* __next__;
        PyObject* __reversed__;  // delete
        PyObject* __contains__;
        PyObject* __add__;
        PyObject* __sub__;
        PyObject* __mul__;
        PyObject* __matmul__;
        PyObject* __truediv__;
        PyObject* __floordiv__;
        PyObject* __mod__;
        PyObject* __divmod__;
        PyObject* __pow__;
        PyObject* __lshift__;
        PyObject* __rshift__;
        PyObject* __and__;
        PyObject* __xor__;
        PyObject* __or__;
        PyObject* __radd__;
        PyObject* __rsub__;
        PyObject* __rmul__;
        PyObject* __rmatmul__;
        PyObject* __rtruediv__;
        PyObject* __rfloordiv__;
        PyObject* __rmod__;
        PyObject* __rdivmod__;
        PyObject* __rpow__;
        PyObject* __rlshift__;
        PyObject* __rrshift__;
        PyObject* __rand__;
        PyObject* __rxor__;
        PyObject* __ror__;
        PyObject* __iadd__;
        PyObject* __isub__;
        PyObject* __imul__;
        PyObject* __imatmul__;
        PyObject* __itruediv__;
        PyObject* __ifloordiv__;
        PyObject* __imod__;
        PyObject* __ipow__;
        PyObject* __ilshift__;
        PyObject* __irshift__;
        PyObject* __iand__;
        PyObject* __ixor__;
        PyObject* __ior__;
        PyObject* __neg__;
        PyObject* __pos__;
        PyObject* __abs__;
        PyObject* __invert__;
        PyObject* __complex_py__;  // delete
        PyObject* __int__;
        PyObject* __float__;
        PyObject* __index__;
        PyObject* __round__;  // delete
        PyObject* __trunc__;  // delete
        PyObject* __floor__;  // delete
        PyObject* __ceil__;  // delete
        PyObject* __enter__;
        PyObject* __exit__;
        PyObject* __buffer__;
        PyObject* __release_buffer__;
        PyObject* __await__;
        PyObject* __aiter__;
        PyObject* __anext__;
        PyObject* __aenter__;
        PyObject* __aexit__;

        /* The original Python slots are recorded here for posterity. */
        reprfunc tp_repr;
        hashfunc tp_hash;
        ternaryfunc tp_call;
        reprfunc tp_str;
        getattrofunc tp_getattro;
        setattrofunc tp_setattro;
        richcmpfunc tp_richcompare;
        getiterfunc tp_iter;
        iternextfunc tp_iternext;
        descrgetfunc tp_descr_get;
        descrsetfunc tp_descr_set;
        initproc tp_init;
        newfunc tp_new;
        lenfunc mp_length;
        binaryfunc mp_subscript;
        objobjargproc mp_ass_subscript;
        objobjargproc sq_contains;
        unaryfunc am_await;
        unaryfunc am_aiter;
        unaryfunc am_anext;
        getbufferproc bf_getbuffer;
        releasebufferproc bf_releasebuffer;
        binaryfunc nb_add;
        binaryfunc nb_inplace_add;
        binaryfunc nb_subtract;
        binaryfunc nb_inplace_subtract;
        binaryfunc nb_multiply;
        binaryfunc nb_inplace_multiply;
        binaryfunc nb_remainder;
        binaryfunc nb_inplace_remainder;
        binaryfunc nb_divmod;
        ternaryfunc nb_power;
        ternaryfunc nb_inplace_power;
        unaryfunc nb_negative;
        unaryfunc nb_positive;
        unaryfunc nb_absolute;
        inquiry nb_bool;
        unaryfunc nb_invert;
        binaryfunc nb_lshift;
        binaryfunc nb_inplace_lshift;
        binaryfunc nb_rshift;
        binaryfunc nb_inplace_rshift;
        binaryfunc nb_and;
        binaryfunc nb_inplace_and;
        binaryfunc nb_xor;
        binaryfunc nb_inplace_xor;
        binaryfunc nb_or;
        binaryfunc nb_inplace_or;
        unaryfunc nb_int;
        unaryfunc nb_float;
        binaryfunc nb_floor_divide;
        binaryfunc nb_inplace_floor_divide;
        binaryfunc nb_true_divide;
        binaryfunc nb_inplace_true_divide;
        unaryfunc nb_index;
        binaryfunc nb_matrix_multiply;
        binaryfunc nb_inplace_matrix_multiply;

        /* Get a new reference to the metatype from the global `bertrand.python`
        module. */
        static Type __import__();  // TODO: defined in __init__.h alongside "bertrand.python" module {
        //     return impl::get_type<BertrandMeta>(Module<"bertrand.python">());
        // }

        /* The metaclass can't use Bindings, since they depend on the metaclass to
        function.  The logic is fundamentally the same, however. */
        template <StaticStr ModName>
        static Type __export__(Module<ModName> module) {
            static PyType_Slot slots[] = {
                {Py_tp_doc, const_cast<char*>(__doc__.buffer)},
                {Py_tp_traverse, reinterpret_cast<void*>(__traverse__)},
                {Py_tp_clear, reinterpret_cast<void*>(__clear__)},
                {Py_tp_dealloc, reinterpret_cast<void*>(__dealloc__)},
                {Py_tp_repr, reinterpret_cast<void*>(class_repr)},
                {Py_tp_getattro, reinterpret_cast<void*>(class_getattr)},
                {Py_tp_setattro, reinterpret_cast<void*>(class_setattr)},
                {Py_tp_iter, reinterpret_cast<void*>(class_iter)},
                {Py_mp_length, reinterpret_cast<void*>(class_len)},
                {Py_mp_subscript, reinterpret_cast<void*>(class_getitem)},
                {Py_tp_methods, methods},
                {Py_tp_getset, getset},
                {0, nullptr}
            };
            static PyType_Spec spec = {
                .name = ModName + ".Meta",
                .basicsize = sizeof(BertrandMeta),
                .itemsize = 0,
                .flags =
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE | Py_TPFLAGS_HAVE_GC |
                    Py_TPFLAGS_MANAGED_WEAKREF | Py_TPFLAGS_MANAGED_DICT |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_TYPE_SUBCLASS,
                .slots = slots
            };
            PyObject* cls = PyType_FromModuleAndSpec(
                ptr(module),
                &spec,
                &PyType_Type
            );
            if (cls == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Type>(cls);
        }

        /* Register the metaclass's fields with Python's cyclic garbage collector. */
        static int __traverse__(__python__* cls, visitproc visit, void* arg) {
            Py_VISIT(cls->demangled);
            Py_VISIT(cls->templates);
            Py_VISIT(cls->__new__);
            Py_VISIT(cls->__init__);
            Py_VISIT(cls->__del__);
            Py_VISIT(cls->__repr__);
            Py_VISIT(cls->__str__);
            Py_VISIT(cls->__bytes__);
            Py_VISIT(cls->__format__);
            Py_VISIT(cls->__lt__);
            Py_VISIT(cls->__le__);
            Py_VISIT(cls->__eq__);
            Py_VISIT(cls->__ne__);
            Py_VISIT(cls->__ge__);
            Py_VISIT(cls->__gt__);
            Py_VISIT(cls->__hash__);
            Py_VISIT(cls->__bool__);
            Py_VISIT(cls->__getattr__);
            Py_VISIT(cls->__getattribute__);
            Py_VISIT(cls->__setattr__);
            Py_VISIT(cls->__delattr__);
            Py_VISIT(cls->__dir__);
            Py_VISIT(cls->__get__);
            Py_VISIT(cls->__set__);
            Py_VISIT(cls->__delete__);
            Py_VISIT(cls->__init_subclass__);
            Py_VISIT(cls->__set_name__);
            Py_VISIT(cls->__mro_entries__);
            Py_VISIT(cls->__instancecheck__);
            Py_VISIT(cls->__subclasscheck__);
            Py_VISIT(cls->__class_getitem__);
            Py_VISIT(cls->__call__);
            Py_VISIT(cls->__len__);
            Py_VISIT(cls->__length_hint__);
            Py_VISIT(cls->__getitem__);
            Py_VISIT(cls->__setitem__);
            Py_VISIT(cls->__delitem__);
            Py_VISIT(cls->__missing__);
            Py_VISIT(cls->__iter__);
            Py_VISIT(cls->__next__);
            Py_VISIT(cls->__reversed__);
            Py_VISIT(cls->__contains__);
            Py_VISIT(cls->__add__);
            Py_VISIT(cls->__sub__);
            Py_VISIT(cls->__mul__);
            Py_VISIT(cls->__matmul__);
            Py_VISIT(cls->__truediv__);
            Py_VISIT(cls->__floordiv__);
            Py_VISIT(cls->__mod__);
            Py_VISIT(cls->__divmod__);
            Py_VISIT(cls->__pow__);
            Py_VISIT(cls->__lshift__);
            Py_VISIT(cls->__rshift__);
            Py_VISIT(cls->__and__);
            Py_VISIT(cls->__xor__);
            Py_VISIT(cls->__or__);
            Py_VISIT(cls->__radd__);
            Py_VISIT(cls->__rsub__);
            Py_VISIT(cls->__rmul__);
            Py_VISIT(cls->__rmatmul__);
            Py_VISIT(cls->__rtruediv__);
            Py_VISIT(cls->__rfloordiv__);
            Py_VISIT(cls->__rmod__);
            Py_VISIT(cls->__rdivmod__);
            Py_VISIT(cls->__rpow__);
            Py_VISIT(cls->__rlshift__);
            Py_VISIT(cls->__rrshift__);
            Py_VISIT(cls->__rand__);
            Py_VISIT(cls->__rxor__);
            Py_VISIT(cls->__ror__);
            Py_VISIT(cls->__iadd__);
            Py_VISIT(cls->__isub__);
            Py_VISIT(cls->__imul__);
            Py_VISIT(cls->__imatmul__);
            Py_VISIT(cls->__itruediv__);
            Py_VISIT(cls->__ifloordiv__);
            Py_VISIT(cls->__imod__);
            Py_VISIT(cls->__ipow__);
            Py_VISIT(cls->__ilshift__);
            Py_VISIT(cls->__irshift__);
            Py_VISIT(cls->__iand__);
            Py_VISIT(cls->__ixor__);
            Py_VISIT(cls->__ior__);
            Py_VISIT(cls->__neg__);
            Py_VISIT(cls->__pos__);
            Py_VISIT(cls->__abs__);
            Py_VISIT(cls->__invert__);
            Py_VISIT(cls->__complex_py__);
            Py_VISIT(cls->__int__);
            Py_VISIT(cls->__float__);
            Py_VISIT(cls->__index__);
            Py_VISIT(cls->__round__);
            Py_VISIT(cls->__trunc__);
            Py_VISIT(cls->__floor__);
            Py_VISIT(cls->__ceil__);
            Py_VISIT(cls->__enter__);
            Py_VISIT(cls->__exit__);
            Py_VISIT(cls->__buffer__);
            Py_VISIT(cls->__release_buffer__);
            Py_VISIT(cls->__await__);
            Py_VISIT(cls->__aiter__);
            Py_VISIT(cls->__anext__);
            Py_VISIT(cls->__aenter__);
            Py_VISIT(cls->__aexit__);
            Py_VISIT(Py_TYPE(cls));  // required for heap types
            return 0;
        }

        /* Break reference cycles if they exist. */
        static int __clear__(__python__* cls) {
            Py_CLEAR(cls->demangled);
            Py_CLEAR(cls->templates);
            Py_CLEAR(cls->__new__);
            Py_CLEAR(cls->__init__);
            Py_CLEAR(cls->__del__);
            Py_CLEAR(cls->__repr__);
            Py_CLEAR(cls->__str__);
            Py_CLEAR(cls->__bytes__);
            Py_CLEAR(cls->__format__);
            Py_CLEAR(cls->__lt__);
            Py_CLEAR(cls->__le__);
            Py_CLEAR(cls->__eq__);
            Py_CLEAR(cls->__ne__);
            Py_CLEAR(cls->__ge__);
            Py_CLEAR(cls->__gt__);
            Py_CLEAR(cls->__hash__);
            Py_CLEAR(cls->__bool__);
            Py_CLEAR(cls->__getattr__);
            Py_CLEAR(cls->__getattribute__);
            Py_CLEAR(cls->__setattr__);
            Py_CLEAR(cls->__delattr__);
            Py_CLEAR(cls->__dir__);
            Py_CLEAR(cls->__get__);
            Py_CLEAR(cls->__set__);
            Py_CLEAR(cls->__delete__);
            Py_CLEAR(cls->__init_subclass__);
            Py_CLEAR(cls->__set_name__);
            Py_CLEAR(cls->__mro_entries__);
            Py_CLEAR(cls->__instancecheck__);
            Py_CLEAR(cls->__subclasscheck__);
            Py_CLEAR(cls->__class_getitem__);
            Py_CLEAR(cls->__call__);
            Py_CLEAR(cls->__len__);
            Py_CLEAR(cls->__length_hint__);
            Py_CLEAR(cls->__getitem__);
            Py_CLEAR(cls->__setitem__);
            Py_CLEAR(cls->__delitem__);
            Py_CLEAR(cls->__missing__);
            Py_CLEAR(cls->__iter__);
            Py_CLEAR(cls->__next__);
            Py_CLEAR(cls->__reversed__);
            Py_CLEAR(cls->__contains__);
            Py_CLEAR(cls->__add__);
            Py_CLEAR(cls->__sub__);
            Py_CLEAR(cls->__mul__);
            Py_CLEAR(cls->__matmul__);
            Py_CLEAR(cls->__truediv__);
            Py_CLEAR(cls->__floordiv__);
            Py_CLEAR(cls->__mod__);
            Py_CLEAR(cls->__divmod__);
            Py_CLEAR(cls->__pow__);
            Py_CLEAR(cls->__lshift__);
            Py_CLEAR(cls->__rshift__);
            Py_CLEAR(cls->__and__);
            Py_CLEAR(cls->__xor__);
            Py_CLEAR(cls->__or__);
            Py_CLEAR(cls->__radd__);
            Py_CLEAR(cls->__rsub__);
            Py_CLEAR(cls->__rmul__);
            Py_CLEAR(cls->__rmatmul__);
            Py_CLEAR(cls->__rtruediv__);
            Py_CLEAR(cls->__rfloordiv__);
            Py_CLEAR(cls->__rmod__);
            Py_CLEAR(cls->__rdivmod__);
            Py_CLEAR(cls->__rpow__);
            Py_CLEAR(cls->__rlshift__);
            Py_CLEAR(cls->__rrshift__);
            Py_CLEAR(cls->__rand__);
            Py_CLEAR(cls->__rxor__);
            Py_CLEAR(cls->__ror__);
            Py_CLEAR(cls->__iadd__);
            Py_CLEAR(cls->__isub__);
            Py_CLEAR(cls->__imul__);
            Py_CLEAR(cls->__imatmul__);
            Py_CLEAR(cls->__itruediv__);
            Py_CLEAR(cls->__ifloordiv__);
            Py_CLEAR(cls->__imod__);
            Py_CLEAR(cls->__ipow__);
            Py_CLEAR(cls->__ilshift__);
            Py_CLEAR(cls->__irshift__);
            Py_CLEAR(cls->__iand__);
            Py_CLEAR(cls->__ixor__);
            Py_CLEAR(cls->__ior__);
            Py_CLEAR(cls->__neg__);
            Py_CLEAR(cls->__pos__);
            Py_CLEAR(cls->__abs__);
            Py_CLEAR(cls->__invert__);
            Py_CLEAR(cls->__complex_py__);
            Py_CLEAR(cls->__int__);
            Py_CLEAR(cls->__float__);
            Py_CLEAR(cls->__index__);
            Py_CLEAR(cls->__round__);
            Py_CLEAR(cls->__trunc__);
            Py_CLEAR(cls->__floor__);
            Py_CLEAR(cls->__ceil__);
            Py_CLEAR(cls->__enter__);
            Py_CLEAR(cls->__exit__);
            Py_CLEAR(cls->__buffer__);
            Py_CLEAR(cls->__release_buffer__);
            Py_CLEAR(cls->__await__);
            Py_CLEAR(cls->__aiter__);
            Py_CLEAR(cls->__anext__);
            Py_CLEAR(cls->__aenter__);
            Py_CLEAR(cls->__aexit__);
            return 0;
        }

        /* Deallocate overload sets and the heap type when it falls out of scope. */
        static void __dealloc__(__python__* cls) {
            PyObject_GC_UnTrack(cls);
            cls->class_getters.~ClassGetters();
            cls->class_setters.~ClassSetters();
            Py_XDECREF(cls->demangled);
            Py_XDECREF(cls->templates);
            Py_XDECREF(cls->__new__);
            Py_XDECREF(cls->__init__);
            Py_XDECREF(cls->__del__);
            Py_XDECREF(cls->__repr__);
            Py_XDECREF(cls->__str__);
            Py_XDECREF(cls->__bytes__);
            Py_XDECREF(cls->__format__);
            Py_XDECREF(cls->__lt__);
            Py_XDECREF(cls->__le__);
            Py_XDECREF(cls->__eq__);
            Py_XDECREF(cls->__ne__);
            Py_XDECREF(cls->__ge__);
            Py_XDECREF(cls->__gt__);
            Py_XDECREF(cls->__hash__);
            Py_XDECREF(cls->__bool__);
            Py_XDECREF(cls->__getattr__);
            Py_XDECREF(cls->__getattribute__);
            Py_XDECREF(cls->__setattr__);
            Py_XDECREF(cls->__delattr__);
            Py_XDECREF(cls->__dir__);
            Py_XDECREF(cls->__get__);
            Py_XDECREF(cls->__set__);
            Py_XDECREF(cls->__delete__);
            Py_XDECREF(cls->__init_subclass__);
            Py_XDECREF(cls->__set_name__);
            Py_XDECREF(cls->__mro_entries__);
            Py_XDECREF(cls->__instancecheck__);
            Py_XDECREF(cls->__subclasscheck__);
            Py_XDECREF(cls->__class_getitem__);
            Py_XDECREF(cls->__call__);
            Py_XDECREF(cls->__len__);
            Py_XDECREF(cls->__length_hint__);
            Py_XDECREF(cls->__getitem__);
            Py_XDECREF(cls->__setitem__);
            Py_XDECREF(cls->__delitem__);
            Py_XDECREF(cls->__missing__);
            Py_XDECREF(cls->__iter__);
            Py_XDECREF(cls->__next__);
            Py_XDECREF(cls->__reversed__);
            Py_XDECREF(cls->__contains__);
            Py_XDECREF(cls->__add__);
            Py_XDECREF(cls->__sub__);
            Py_XDECREF(cls->__mul__);
            Py_XDECREF(cls->__matmul__);
            Py_XDECREF(cls->__truediv__);
            Py_XDECREF(cls->__floordiv__);
            Py_XDECREF(cls->__mod__);
            Py_XDECREF(cls->__divmod__);
            Py_XDECREF(cls->__pow__);
            Py_XDECREF(cls->__lshift__);
            Py_XDECREF(cls->__rshift__);
            Py_XDECREF(cls->__and__);
            Py_XDECREF(cls->__xor__);
            Py_XDECREF(cls->__or__);
            Py_XDECREF(cls->__radd__);
            Py_XDECREF(cls->__rsub__);
            Py_XDECREF(cls->__rmul__);
            Py_XDECREF(cls->__rmatmul__);
            Py_XDECREF(cls->__rtruediv__);
            Py_XDECREF(cls->__rfloordiv__);
            Py_XDECREF(cls->__rmod__);
            Py_XDECREF(cls->__rdivmod__);
            Py_XDECREF(cls->__rpow__);
            Py_XDECREF(cls->__rlshift__);
            Py_XDECREF(cls->__rrshift__);
            Py_XDECREF(cls->__rand__);
            Py_XDECREF(cls->__rxor__);
            Py_XDECREF(cls->__ror__);
            Py_XDECREF(cls->__iadd__);
            Py_XDECREF(cls->__isub__);
            Py_XDECREF(cls->__imul__);
            Py_XDECREF(cls->__imatmul__);
            Py_XDECREF(cls->__itruediv__);
            Py_XDECREF(cls->__ifloordiv__);
            Py_XDECREF(cls->__imod__);
            Py_XDECREF(cls->__ipow__);
            Py_XDECREF(cls->__ilshift__);
            Py_XDECREF(cls->__irshift__);
            Py_XDECREF(cls->__iand__);
            Py_XDECREF(cls->__ixor__);
            Py_XDECREF(cls->__ior__);
            Py_XDECREF(cls->__neg__);
            Py_XDECREF(cls->__pos__);
            Py_XDECREF(cls->__abs__);
            Py_XDECREF(cls->__invert__);
            Py_XDECREF(cls->__complex_py__);
            Py_XDECREF(cls->__int__);
            Py_XDECREF(cls->__float__);
            Py_XDECREF(cls->__index__);
            Py_XDECREF(cls->__round__);
            Py_XDECREF(cls->__trunc__);
            Py_XDECREF(cls->__floor__);
            Py_XDECREF(cls->__ceil__);
            Py_XDECREF(cls->__enter__);
            Py_XDECREF(cls->__exit__);
            Py_XDECREF(cls->__buffer__);
            Py_XDECREF(cls->__release_buffer__);
            Py_XDECREF(cls->__await__);
            Py_XDECREF(cls->__aiter__);
            Py_XDECREF(cls->__anext__);
            Py_XDECREF(cls->__aenter__);
            Py_XDECREF(cls->__aexit__);
            PyTypeObject* type = Py_TYPE(cls);
            type->tp_free(cls);
            Py_DECREF(type);  // required for heap types
        }

        /* Initialize the metaclass's internal fields within the `bindings.finalize()`
        helper.  Without this, the pointers are left uninitialized, which leads to
        undefined behavior when accessed. */
        void __construct__() {
            instancecheck = nullptr;
            subclasscheck = nullptr;
            new (&class_getters) ClassGetters();
            new (&class_setters) ClassSetters();
            std::string s = "<class '" + impl::demangle(base.tp_name) + "'>";
            demangled = PyUnicode_FromStringAndSize(s.c_str(), s.size());
            if (demangled == nullptr) {
                Exception::from_python();
            }
            templates = nullptr;
            __new__ = nullptr;
            __init__ = nullptr;
            __del__ = nullptr;
            __repr__ = nullptr;
            __str__ = nullptr;
            __bytes__ = nullptr;
            __format__ = nullptr;
            __lt__ = nullptr;
            __le__ = nullptr;
            __eq__ = nullptr;
            __ne__ = nullptr;
            __ge__ = nullptr;
            __gt__ = nullptr;
            __hash__ = nullptr;
            __bool__ = nullptr;
            __getattr__ = nullptr;
            __getattribute__ = nullptr;
            __setattr__ = nullptr;
            __delattr__ = nullptr;
            __dir__ = nullptr;
            __get__ = nullptr;
            __set__ = nullptr;
            __delete__ = nullptr;
            __init_subclass__ = nullptr;
            __set_name__ = nullptr;
            __mro_entries__ = nullptr;
            __instancecheck__ = nullptr;
            __subclasscheck__ = nullptr;
            __class_getitem__ = nullptr;
            __call__ = nullptr;
            __len__ = nullptr;
            __length_hint__ = nullptr;
            __getitem__ = nullptr;
            __setitem__ = nullptr;
            __delitem__ = nullptr;
            __missing__ = nullptr;
            __iter__ = nullptr;
            __next__ = nullptr;
            __reversed__ = nullptr;
            __contains__ = nullptr;
            __add__ = nullptr;
            __sub__ = nullptr;
            __mul__ = nullptr;
            __matmul__ = nullptr;
            __truediv__ = nullptr;
            __floordiv__ = nullptr;
            __mod__ = nullptr;
            __divmod__ = nullptr;
            __pow__ = nullptr;
            __lshift__ = nullptr;
            __rshift__ = nullptr;
            __and__ = nullptr;
            __xor__ = nullptr;
            __or__ = nullptr;
            __radd__ = nullptr;
            __rsub__ = nullptr;
            __rmul__ = nullptr;
            __rmatmul__ = nullptr;
            __rtruediv__ = nullptr;
            __rfloordiv__ = nullptr;
            __rmod__ = nullptr;
            __rdivmod__ = nullptr;
            __rpow__ = nullptr;
            __rlshift__ = nullptr;
            __rrshift__ = nullptr;
            __rand__ = nullptr;
            __rxor__ = nullptr;
            __ror__ = nullptr;
            __iadd__ = nullptr;
            __isub__ = nullptr;
            __imul__ = nullptr;
            __imatmul__ = nullptr;
            __itruediv__ = nullptr;
            __ifloordiv__ = nullptr;
            __imod__ = nullptr;
            __ipow__ = nullptr;
            __ilshift__ = nullptr;
            __irshift__ = nullptr;
            __iand__ = nullptr;
            __ixor__ = nullptr;
            __ior__ = nullptr;
            __neg__ = nullptr;
            __pos__ = nullptr;
            __abs__ = nullptr;
            __invert__ = nullptr;
            __complex_py__ = nullptr;
            __int__ = nullptr;
            __float__ = nullptr;
            __index__ = nullptr;
            __round__ = nullptr;
            __trunc__ = nullptr;
            __floor__ = nullptr;
            __ceil__ = nullptr;
            __enter__ = nullptr;
            __exit__ = nullptr;
            __buffer__ = nullptr;
            __release_buffer__ = nullptr;
            __await__ = nullptr;
            __aiter__ = nullptr;
            __anext__ = nullptr;
            __aenter__ = nullptr;
            __aexit__ = nullptr;
            tp_init = nullptr;
            tp_new = nullptr;
            tp_getattro = nullptr;
            tp_setattro = nullptr;
            tp_repr = nullptr;
            tp_hash = nullptr;
            tp_call = nullptr;
            tp_str = nullptr;
            tp_richcompare = nullptr;
            tp_iter = nullptr;
            tp_iternext = nullptr;
            tp_descr_get = nullptr;
            tp_descr_set = nullptr;
            mp_length = nullptr;
            mp_subscript = nullptr;
            mp_ass_subscript = nullptr;
            sq_contains = nullptr;
            am_await = nullptr;
            am_aiter = nullptr;
            am_anext = nullptr;
            bf_getbuffer = nullptr;
            bf_releasebuffer = nullptr;
            nb_add = nullptr;
            nb_inplace_add = nullptr;
            nb_subtract = nullptr;
            nb_inplace_subtract = nullptr;
            nb_multiply = nullptr;
            nb_inplace_multiply = nullptr;
            nb_remainder = nullptr;
            nb_inplace_remainder = nullptr;
            nb_divmod = nullptr;
            nb_power = nullptr;
            nb_inplace_power = nullptr;
            nb_negative = nullptr;
            nb_positive = nullptr;
            nb_absolute = nullptr;
            nb_bool = nullptr;
            nb_invert = nullptr;
            nb_lshift = nullptr;
            nb_inplace_lshift = nullptr;
            nb_rshift = nullptr;
            nb_inplace_rshift = nullptr;
            nb_and = nullptr;
            nb_inplace_and = nullptr;
            nb_xor = nullptr;
            nb_inplace_xor = nullptr;
            nb_or = nullptr;
            nb_inplace_or = nullptr;
            nb_int = nullptr;
            nb_float = nullptr;
            nb_floor_divide = nullptr;
            nb_inplace_floor_divide = nullptr;
            nb_true_divide = nullptr;
            nb_inplace_true_divide = nullptr;
            nb_index = nullptr;
            nb_matrix_multiply = nullptr;
            nb_inplace_matrix_multiply = nullptr;
        }

        /* Create a trivial instance of the metaclass to serve as a Python entry point
        for a C++ template hierarchy.  The interface type is not usable on its own
        except to provide access to its C++ instantiations, as well as efficient type
        checks against them and a central point for documentation. */
        template <StaticStr Name, typename Cls, StaticStr ModName>
        static BertrandMeta stub_type(Module<ModName>& module) {
            static std::string docstring;
            static bool doc_initialized = false;
            if (!doc_initialized) {
                docstring = "A public interface for the '" + ModName + "." + Name;
                docstring += "' template hierarchy.\n\n";
                docstring +=
R"doc(This type cannot be used directly, but indexing it with one or more Python
types allows access to individual instantiations with the same syntax as C++.
Note that due to its dynamic nature, Python cannot create any new instantiations
of a C++ template - this class merely navigates the existing instantiations at
the C++ level and retrieves their corresponding Python types.)doc";
                doc_initialized = true;
            }
            static PyType_Slot slots[] = {
                {Py_tp_doc, const_cast<char*>(docstring.c_str())},
                {0, nullptr}
            };
            static PyType_Spec spec = {
                .name = Name,
                .basicsize = sizeof(__python__),
                .itemsize = 0,
                .flags =
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE |
                    Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_DISALLOW_INSTANTIATION |
                    Py_TPFLAGS_IMMUTABLETYPE,
                .slots = slots
            };

            __python__* cls = reinterpret_cast<__python__*>(PyType_FromMetaclass(
                reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>())),
                ptr(module),
                &spec,
                nullptr
            ));
            if (cls == nullptr) {
                Exception::from_python();
            }
            try {
                cls->__construct__();
                cls->instancecheck = template_instancecheck;
                cls->subclasscheck = template_subclasscheck;
                cls->templates = PyDict_New();
                if (cls->templates == nullptr) {
                    Exception::from_python();
                }
                return reinterpret_steal<BertrandMeta>(reinterpret_cast<PyObject*>(cls));
            } catch (...) {
                Py_DECREF(cls);
                throw;
            }
        }

        /* `repr(cls)` displays the type's demangled C++ name.  */
        static PyObject* class_repr(__python__* cls) {
            return Py_XNewRef(cls->demangled);
        }

        /* `len(cls)` yields the number of template instantiations registered to this
        type.  This will always succeed: testing `if cls` in Python is sufficient to
        determine whether it is a template interface that requires instantiation. */
        static Py_ssize_t class_len(__python__* cls) {
            return cls->templates ? PyDict_Size(cls->templates) : 0;
        }

        /* `iter(cls)` yields individual template instantiations in the order in which
        they were defined. */
        static PyObject* class_iter(__python__* cls) {
            if (cls->templates == nullptr) {
                PyErr_Format(
                    PyExc_TypeError,
                    "class '%U' has no template instantiations",
                    cls->demangled
                );
                return nullptr;
            }
            return PyObject_GetIter(cls->templates);
        }

        /* `cls[]` allows navigation of the C++ template hierarchy. */
        static PyObject* class_getitem(__python__* cls, PyObject* key) {
            if (cls->templates == nullptr) {
                /// TODO: maybe this can more efficiently check whether the
                /// __class_getitem__ overload set exists?
                if (PyObject_HasAttr(
                    reinterpret_cast<PyObject*>(cls),
                    impl::TemplateString<"__class_getitem__">::ptr
                )) {
                    return PyObject_CallMethodOneArg(
                        reinterpret_cast<PyObject*>(cls),
                        impl::TemplateString<"__class_getitem__">::ptr,
                        key
                    );
                } else {
                    PyErr_Format(
                        PyExc_TypeError,
                        "class '%U' has no template instantiations",
                        cls->demangled
                    );
                    return nullptr;
                }
            }

            if (PyTuple_Check(key)) {
                Py_INCREF(key);
            } else {
                key = PyTuple_Pack(1, key);
                if (key == nullptr) {
                    return nullptr;
                }
            }

            try {
                PyObject* value = PyDict_GetItem(cls->templates, key);
                if (value == nullptr) {
                    const char* demangled = PyUnicode_AsUTF8(cls->demangled);
                    if (demangled == nullptr) {
                        Py_DECREF(key);
                        return nullptr;
                    }
                    std::string message = "class template has not been instantiated: ";
                    message += demangled;
                    PyObject* py_repr = PyObject_Repr(key);
                    if (py_repr == nullptr) {
                        Py_DECREF(key);
                        return nullptr;
                    }
                    Py_DECREF(key);
                    Py_ssize_t py_len;
                    const char* py_str = PyUnicode_AsUTF8AndSize(
                        py_repr,
                        &py_len
                    );
                    if (py_str == nullptr) {
                        Py_DECREF(py_repr);
                        return nullptr;
                    }
                    std::string key_repr(py_str, py_len);
                    Py_DECREF(py_repr);
                    message += "[";
                    message += key_repr.substr(1, key_repr.size() - 2);
                    message += "]";
                    PyErr_SetString(PyExc_TypeError, message.c_str());
                    return nullptr;
                }
                return value;

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* The metaclass's __doc__ string defaults to a getset descriptor that appends
        the type's template instantiations to the normal `tp_doc` slot. */
        static PyObject* class_doc(__python__* cls, void*) {
            std::string doc = cls->base.tp_doc;
            if (cls->templates) {
                if (!doc.ends_with("\n")) {
                    doc += "\n";
                }
                doc += "\n";
                std::string header = "Instantations (" + std::to_string(PyDict_Size(
                    cls->templates
                )) + ")";
                doc += header + "\n" + std::string(header.size() - 1, '-') + "\n";

                std::string prefix = "    " + std::string(cls->base.tp_name) + "[";
                PyObject* key;
                PyObject* value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(
                    cls->templates,
                    &pos,
                    &key,
                    &value
                )) {
                    std::string key_string = prefix;
                    size_t i = 0;
                    size_t size = PyTuple_GET_SIZE(key);
                    while (i < size) {
                        PyObject* item = PyTuple_GET_ITEM(key, i);
                        int rc = PyObject_IsInstance(
                            item,
                            ptr(Type<BertrandMeta>())
                        );
                        if (rc < 0) {
                            Exception::from_python();
                        } else if (rc) {
                            const char* demangled_key = PyUnicode_AsUTF8(
                                reinterpret_cast<__python__*>(item)->demangled
                            );
                            if (demangled_key == nullptr) {
                                Exception::from_python();
                            }
                            doc += demangled_key;
                        } else {
                            doc += reinterpret_cast<PyTypeObject*>(item)->tp_name;
                        }
                        if (++i < size) {
                            doc += ", ";
                        }
                    }
                    key_string += "]";

                    const char* demangled_value = PyUnicode_AsUTF8(
                        reinterpret_cast<__python__*>(value)->demangled
                    );
                    if (demangled_value == nullptr) {
                        Exception::from_python();
                    }

                    doc += key_string + " -> " + demangled_value + "\n";
                }
            }
            return PyUnicode_FromStringAndSize(doc.c_str(), doc.size());
        }

        /// TODO: reserving getattr/setattr for class variables interferes with the
        /// type creation process.  If the attribute name doesn't appear in the
        /// class variable dictionary, it should forward to the standard Python
        /// behavior of inserting into and retrieving from the class dictionary.
        /// -> perhaps not for template interfaces, however, which should be immutable.

        /* `cls.` allows access to class-level variables with shared state. */
        static PyObject* class_getattr(__python__* cls, PyObject* attr) {
            Py_ssize_t size;
            const char* name = PyUnicode_AsUTF8AndSize(attr, &size);
            if (name == nullptr) {
                return nullptr;
            }
            std::string_view view {name, static_cast<size_t>(size)};
            auto it = cls->class_getters.find(view);
            if (it != cls->class_getters.end()) {
                return it->second.func(
                    reinterpret_cast<PyObject*>(cls),
                    it->second.closure
                );
            }
            PyErr_Format(
                PyExc_AttributeError,
                "type object '%U' has no attribute '%s'",
                cls->demangled,
                name
            );
            return nullptr;
        }

        /* `cls. = ...` allows assignment to class-level variables with shared state. */
        static int class_setattr(__python__* cls, PyObject* attr, PyObject* value) {
            Py_ssize_t size;
            const char* name = PyUnicode_AsUTF8AndSize(attr, &size);
            if (name == nullptr) {
                return -1;
            }
            std::string_view view {name, static_cast<size_t>(size)};
            auto it = cls->class_setters.find(view);
            if (it != cls->class_setters.end()) {
                return it->second.func(
                    reinterpret_cast<PyObject*>(cls),
                    value,
                    it->second.closure
                );
            }
            PyErr_Format(
                PyExc_AttributeError,
                "type object '%U' has no attribute '%s'",
                cls->demangled,
                name
            );
            return -1;
        }

        /* isinstance(obj, cls) forwards the behavior of the __isinstance__ control
        struct and accounts for possible template instantiations. */
        static PyObject* class_instancecheck(__python__* cls, PyObject* instance) {
            try {
                return PyBool_FromLong(cls->instancecheck(cls, instance));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* issubclass(sub, cls) forwards the behavior of the __issubclass__ control
        struct and accounts for possible template instantiations. */
        static PyObject* class_subclasscheck(__python__* cls, PyObject* subclass) {
            try {
                return PyBool_FromLong(cls->subclasscheck(cls, subclass));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /* A candidate for the `instancecheck` function pointer to be used for stub
        types. */
        static bool template_instancecheck(__python__* cls, PyObject* instance) {
            if (PyType_IsSubtype(
                Py_TYPE(instance),
                reinterpret_cast<PyTypeObject*>(cls)
            )) {
                return true;
            }
            PyObject* key;
            PyObject* value;
            Py_ssize_t pos = 0;
            while (PyDict_Next(cls->templates, &pos, &key, &value)) {
                __python__* instantiation = reinterpret_cast<__python__*>(value);
                if (instantiation->instancecheck(cls, instance)) {
                    return true;
                }
            }
            return false;
        }

        /* A candidate for the `subclasscheck` function pointer to be used for stub
        types. */
        static bool template_subclasscheck(__python__* cls, PyObject* subclass) {
            if (PyType_Check(subclass)) {
                if (PyType_IsSubtype(
                    reinterpret_cast<PyTypeObject*>(subclass),
                    reinterpret_cast<PyTypeObject*>(cls)
                )) {
                    return true;
                }
                PyObject* key;
                PyObject* value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(cls->templates, &pos, &key, &value)) {
                    __python__* instantiation = reinterpret_cast<__python__*>(value);
                    if (instantiation->subclasscheck(cls, subclass)) {
                        return true;
                    }
                }
                return false;
            } else {
                throw TypeError("issubclass() arg 1 must be a class");
            }
        }

        /* Allocate a vectorcall argument array with an implicit self argument. */
        static std::tuple<PyObject* const*, Py_ssize_t, PyObject*> vectorcall_args(
            PyObject* self,
            PyObject* args,
            PyObject* kwargs
        ) {
            // allocate dynamic array with `self` as first argument
            Py_ssize_t args_size = 0;
            Py_ssize_t kwargs_size = 0;
            if (args) {
                args_size = PyTuple_GET_SIZE(args);
            }
            if (kwargs) {
                kwargs_size = PyDict_Size(kwargs);
            }
            PyObject** forward = new PyObject*[1 + args_size + kwargs_size];
            forward[0] = self;

            // insert positional args
            Py_ssize_t i = 1;
            Py_ssize_t stop = args_size + 1;
            for (; i < stop; ++i) {
                forward[i] = PyTuple_GET_ITEM(args, i - 1);
            }

            // insert keyword args
            PyObject* kwnames = nullptr;
            if (kwargs) {
                kwnames = PyTuple_New(kwargs_size);
                if (kwnames == nullptr) {
                    delete[] forward;
                    return {nullptr, 0, nullptr};
                }
                PyObject* key;
                PyObject* value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(kwargs, &pos, &key, &value)) {
                    PyTuple_SET_ITEM(kwnames, pos, key);
                    forward[i++] = value;
                }
            }

            return {forward, i, kwnames};
        }

        /* Allocate a vectorcall argument array *without* a self argument. */
        static std::tuple<PyObject* const*, Py_ssize_t, PyObject*> vectorcall_args(
            PyObject* args,
            PyObject* kwargs
        ) {
            // allocate dynamic array
            Py_ssize_t args_size = 0;
            Py_ssize_t kwargs_size = 0;
            if (args) {
                args_size = PyTuple_GET_SIZE(args);
            }
            if (kwargs) {
                kwargs_size = PyDict_Size(kwargs);
            }
            PyObject** forward = new PyObject*[args_size + kwargs_size];

            // insert positional args
            Py_ssize_t i = 0;
            for (; i < args_size; ++i) {
                forward[i] = PyTuple_GET_ITEM(args, i);
            }

            // insert keyword args
            PyObject* kwnames = nullptr;
            if (kwargs) {
                kwnames = PyTuple_New(kwargs_size);
                if (kwnames == nullptr) {
                    delete[] forward;
                    return {nullptr, 0, nullptr};
                }
                PyObject* key;
                PyObject* value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(kwargs, &pos, &key, &value)) {
                    PyTuple_SET_ITEM(kwnames, pos, key);
                    forward[i++] = value;
                }
            }

            return {forward, i, kwnames};
        }

        /* All of the above slots are implemented as getset descriptors on the
        metaclass so as to intercept the standard assignment operator and update the
        internal C++ function pointers. */
        struct Descr {

            /// TODO: getters should return the default behavior if the slot is not
            /// overloaded?
            /// -> first, check if an overload set is present, and if so, return it.
            /// Then, check if the default slot is present, and if so, return a
            /// Function or PyCFunction that wraps it.  Otherwise, raise an
            /// AttributeError.

            /// TODO: setters should enforce a signature match to conform with the
            /// slot's expected function signature.  Several of these will likely be
            /// variadic.

            /// TODO: setters always generate a new overload set, and replace the
            /// corresponding slot with a new function pointer that delegates to the
            /// overload set.  Deleting the overload set should revert the slot back to
            /// its original behavior.

            struct __new__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __init__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __del__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __repr__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __str__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __bytes__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __format__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __lt__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __le__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __eq__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ne__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ge__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __gt__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __hash__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __bool__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __getattr__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __getattribute__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __setattr__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __delattr__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __dir__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __get__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __set__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __delete__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __init_subclass__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __set_name__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __mro_entries__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __instancecheck__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __subclasscheck__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __class_getitem__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __call__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __len__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __length_hint__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __getitem__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __setitem__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __delitem__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __missing__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __iter__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __next__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __reversed__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __contains__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __add__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __sub__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __mul__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __matmul__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __truediv__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __floordiv__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __mod__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __divmod__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __pow__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __lshift__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };
    
            struct __rshift__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __and__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __xor__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __or__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __radd__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rsub__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rmul__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rmatmul__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rtruediv__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rfloordiv__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rmod__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rdivmod__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rpow__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rlshift__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };
    
            struct __rrshift__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rand__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rxor__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ror__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __iadd__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __isub__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __imul__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __imatmul__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __itruediv__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ifloordiv__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __imod__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __idivmod__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ipow__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ilshift__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };
    
            struct __irshift__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __iand__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ixor__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ior__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __neg__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __pos__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __abs__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __invert__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __complex_py__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __int__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __float__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __index__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __round__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __trunc__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __floor__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ceil__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __enter__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __exit__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __buffer__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __release_buffer__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __await__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __aiter__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __anext__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __aenter__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __aexit__ {
                static PyObject* get(__python__* cls, void*);
                static int set(__python__* cls, PyObject* value, void*);
            };

        };

        /// TODO: this requires some support when implementing overload sets and
        /// functions in general.  First of all, a function should be able to be
        /// templated with a member function pointer, which indicates the requirement
        /// of a `self` parameter when it is invoked.  This `self` parameter will
        /// either be stored as the first argument if the descriptor protocol was not
        /// invoked, or as an implicit `args[-1]` index with
        /// `Py_VECTORCALL_ARGUMENTS_OFFSET` otherwise.  All functions can check for a
        /// member function pointer at compile time and insert this logic into its base
        /// call operator to make this symmetrical.  CTAD can then be used to deduce
        /// it in the method<"name">(&Class::method) call.  That gives the best of all
        /// worlds.

        /// TODO: there could be performance benefits to keeping the metaclass static.
        /// That would prevent the binary operators from needing to import the metaclass
        /// every time they are called.

        /* Wrappers for all of the Python slots that can delegate to a corresponding
        overload set when called.  The getset descriptors will replace the type's
        base slots with these implementations as long as a corresponding overload set
        is present.  When the overload set is deleted, the slot will be reset to its
        original value. */
        struct Slots {

            static PyObject* tp_repr(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__repr__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static Py_hash_t tp_hash(PyObject* self) {
                PyObject* result = PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__hash__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
                if (result == nullptr) {
                    return -1;
                }
                long long hash = PyLong_AsLongLong(result);
                Py_DECREF(result);
                return hash;
            }

            static PyObject* tp_call(PyObject* self, PyObject* args, PyObject* kwargs) {
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(self));
                if (meta->__call__) {
                    auto [forward, size, kwnames] =
                        vectorcall_args(self, args, kwargs);
                    PyObject* result = PyObject_Vectorcall(
                        meta->__call__,
                        forward,
                        size | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    );
                    delete[] forward;
                    Py_XDECREF(kwnames);
                    return result;
                } else if (meta->tp_call) {
                    return meta->tp_call(self, args, kwargs);
                } else {
                    PyErr_Format(
                        PyExc_TypeError,
                        "'%U' object is not callable",
                        meta->demangled
                    );
                    return nullptr;
                }
            }

            static PyObject* tp_str(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__str__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* tp_getattro(PyObject* self, PyObject* attr) {
                PyObject* const forward[] = {self, attr};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__getattribute__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static int tp_setattro(PyObject* self, PyObject* attr, PyObject* value) {
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(self));
                if (value == nullptr) {
                    if (meta->__delattr__) {
                        PyObject* const forward[] = {self, attr};
                        PyObject* result = PyObject_Vectorcall(
                            meta->__delattr__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                        if (result == nullptr) {
                            return -1;
                        }
                        Py_DECREF(result);
                        return 0;
                    } else if (meta->tp_setattro) {
                        return meta->tp_setattro(self, attr, value);
                    } else {
                        PyErr_Format(
                            PyExc_AttributeError,
                            "cannot delete attribute '%U' from object of type "
                            "'%U'",
                            attr,
                            meta->demangled
                        );
                        return -1;
                    }
                } else {
                    if (meta->__setattr__) {
                        PyObject* const forward[] = {self, attr, value};
                        PyObject* result = PyObject_Vectorcall(
                            meta->__setattr__,
                            forward,
                            2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                        if (result == nullptr) {
                            return -1;
                        }
                        Py_DECREF(result);
                        return 0;
                    } else if (meta->tp_setattro) {
                        return meta->tp_setattro(self, attr, value);
                    } else {
                        PyErr_Format(
                            PyExc_AttributeError,
                            "cannot set attribute '%U' on object of type '%U'",
                            attr,
                            meta->demangled
                        );
                        return -1;
                    }
                }
            }

            static PyObject* tp_richcompare(PyObject* self, PyObject* other, int op) {
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(self));
                switch (op) {
                    case Py_LT:
                        if (meta->__lt__) {
                            PyObject* const forward[] = {self, other};
                            return PyObject_Vectorcall(
                                meta->__lt__,
                                forward,
                                1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                nullptr
                            );
                        } else if (meta->tp_richcompare) {
                            return meta->tp_richcompare(self, other, op);
                        } else {
                            PyErr_Format(
                                PyExc_TypeError,
                                "'<' not supported between instances of '%U' "
                                "and '%s'",
                                meta->demangled,
                                Py_TYPE(other)->tp_name
                            );
                            return nullptr;
                        }
                    case Py_LE:
                        if (meta->__le__) {
                            PyObject* const forward[] = {self, other};
                            return PyObject_Vectorcall(
                                meta->__le__,
                                forward,
                                1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                nullptr
                            );
                        } else if (meta->tp_richcompare) {
                            return meta->tp_richcompare(self, other, op);
                        } else {
                            PyErr_Format(
                                PyExc_TypeError,
                                "'<=' not supported between instances of '%U' "
                                "and '%s'",
                                meta->demangled,
                                Py_TYPE(other)->tp_name
                            );
                            return nullptr;
                        }
                    case Py_EQ:
                        if (meta->__eq__) {
                            PyObject* const forward[] = {self, other};
                            return PyObject_Vectorcall(
                                meta->__eq__,
                                forward,
                                1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                nullptr
                            );
                        } else if (meta->tp_richcompare) {
                            return meta->tp_richcompare(self, other, op);
                        } else {
                            PyErr_Format(
                                PyExc_TypeError,
                                "'==' not supported between instances of '%U' "
                                "and '%s'",
                                meta->demangled,
                                Py_TYPE(other)->tp_name
                            );
                            return nullptr;
                        }
                    case Py_NE:
                        if (meta->__ne__) {
                            PyObject* const forward[] = {self, other};
                            return PyObject_Vectorcall(
                                meta->__ne__,
                                forward,
                                1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                nullptr
                            );
                        } else if (meta->tp_richcompare) {
                            return meta->tp_richcompare(self, other, op);
                        } else {
                            PyErr_Format(
                                PyExc_TypeError,
                                "'!=' not supported between instances of '%U' "
                                "and '%s'",
                                meta->demangled,
                                Py_TYPE(other)->tp_name
                            );
                            return nullptr;
                        }
                    case Py_GE:
                        if (meta->__ge__) {
                            PyObject* const forward[] = {self, other};
                            return PyObject_Vectorcall(
                                meta->__ge__,
                                forward,
                                1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                nullptr
                            );
                        } else if (meta->tp_richcompare) {
                            return meta->tp_richcompare(self, other, op);
                        } else {
                            PyErr_Format(
                                PyExc_TypeError,
                                "'>=' not supported between instances of '%U' "
                                "and '%s'",
                                meta->demangled,
                                Py_TYPE(other)->tp_name
                            );
                            return nullptr;
                        }
                    case Py_GT:
                        if (meta->__gt__) {
                            PyObject* const forward[] = {self, other};
                            return PyObject_Vectorcall(
                                meta->__gt__,
                                forward,
                                1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                nullptr
                            );
                        } else if (meta->tp_richcompare) {
                            return meta->tp_richcompare(self, other, op);
                        } else {
                            PyErr_Format(
                                PyExc_TypeError,
                                "'>' not supported between instances of '%U' "
                                "and '%s'",
                                meta->demangled,
                                Py_TYPE(other)->tp_name
                            );
                            return nullptr;
                        }
                    default:
                        PyErr_Format(
                            PyExc_SystemError,
                            "invalid rich comparison operator"
                        );
                        return nullptr;
                }
            }

            static PyObject* tp_iter(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__iter__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* tp_iternext(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__next__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* tp_descr_get(PyObject* self, PyObject* obj, PyObject* type) {
                PyObject* const forward[] = {self, obj, type};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__get__,
                    forward,
                    2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static int tp_descr_set(PyObject* self, PyObject* obj, PyObject* value) {
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(self));
                if (value == nullptr) {
                    if (meta->__delete__) {
                        PyObject* const forward[] = {self, obj};
                        PyObject* result = PyObject_Vectorcall(
                            meta->__delete__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                        if (result == nullptr) {
                            return -1;
                        }
                        Py_DECREF(result);
                        return 0;
                    } else if (meta->tp_descr_set) {
                        return meta->tp_descr_set(self, obj, value);
                    } else {
                        PyErr_Format(
                            PyExc_AttributeError,
                            "cannot delete attribute '%U' from object of type "
                            "'%U'",
                            meta->demangled,
                            Py_TYPE(obj)->tp_name
                        );
                        return -1;
                    }
                } else {
                    if (meta->__set__) {
                        PyObject* const forward[] = {self, obj, value};
                        PyObject* result = PyObject_Vectorcall(
                            meta->__set__,
                            forward,
                            2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                        if (result == nullptr) {
                            return -1;
                        }
                        Py_DECREF(result);
                        return 0;
                    } else if (meta->tp_descr_set) {
                        return meta->tp_descr_set(self, obj, value);
                    } else {
                        PyErr_Format(
                            PyExc_AttributeError,
                            "cannot set attribute '%U' on object of type '%U'",
                            meta->demangled,
                            Py_TYPE(obj)->tp_name
                        );
                        return -1;
                    }
                }
            }

            static int tp_init(PyObject* self, PyObject* args, PyObject* kwargs) {
                auto [forward, size, kwnames] =
                    vectorcall_args(self, args, kwargs);
                PyObject* result = PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__init__,
                    forward,
                    size | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    kwnames
                );
                delete[] forward;
                Py_XDECREF(kwnames);
                if (result == nullptr) {
                    return -1;
                }
                Py_DECREF(result);
                return 0;
            }

            static PyObject* tp_new(PyObject* cls, PyObject* args, PyObject* kwargs) {
                auto [forward, size, kwnames] =
                    vectorcall_args(args, kwargs);
                PyObject* result = PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(cls)->__new__,
                    forward,
                    size,  // no self argument
                    kwnames
                );
                delete[] forward;
                Py_XDECREF(kwnames);
                return result;
            }

            static Py_ssize_t mp_length(PyObject* self) {
                PyObject* result = PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__len__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
                if (result == nullptr) {
                    return -1;
                }
                Py_ssize_t length = PyLong_AsSsize_t(result);
                Py_DECREF(result);
                return length;
            }

            static PyObject* mp_subscript(PyObject* self, PyObject* key) {
                PyObject* const forward[] = {self, key};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__getitem__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static int mp_ass_subscript(PyObject* self, PyObject* key, PyObject* value) {
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(self));
                if (value == nullptr) {
                    if (meta->__delitem__) {
                        PyObject* const forward[] = {self, key};
                        PyObject* result = PyObject_Vectorcall(
                            meta->__delitem__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                        if (result == nullptr) {
                            return -1;
                        }
                        Py_DECREF(result);
                        return 0;
                    } else if (meta->mp_ass_subscript) {
                        return meta->mp_ass_subscript(self, key, value);
                    } else {
                        PyErr_Format(
                            PyExc_TypeError,
                            "'%U' object does not support item assignment",
                            meta->demangled
                        );
                        return -1;
                    }
                } else {
                    if (meta->__setitem__) {
                        PyObject* const forward[] = {self, key, value};
                        PyObject* result = PyObject_Vectorcall(
                            meta->__setitem__,
                            forward,
                            2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                        if (result == nullptr) {
                            return -1;
                        }
                        Py_DECREF(result);
                        return 0;
                    } else if (meta->mp_ass_subscript) {
                        return meta->mp_ass_subscript(self, key, value);
                    } else {
                        PyErr_Format(
                            PyExc_TypeError,
                            "'%U' object does not support item assignment",
                            meta->demangled
                        );
                        return -1;
                    }
                }
            }

            static int sq_contains(PyObject* self, PyObject* key) {
                PyObject* const forward[] = {self, key};
                PyObject* result = PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__contains__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
                if (result == nullptr) {
                    return -1;
                }
                int cond = PyObject_IsTrue(result);
                Py_DECREF(result);
                return cond;
            }

            static PyObject* am_await(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__await__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* am_aiter(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__aiter__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* am_anext(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__anext__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static int bf_getbuffer(PyObject* self, Py_buffer* view, int flags) {
                /// TODO: this gets really complicated, and will require a deeper
                /// dive to implement according to Python semantics
            }

            static int bf_releasebuffer(PyObject* self, Py_buffer* view) {
                /// TODO: this gets really complicated, and will require a deeper
                /// dive to implement according to Python semantics
            }

            static PyObject* nb_add(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__add__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__add__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_add) {
                        result = meta->nb_add(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__radd__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__radd__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_add) {
                    return meta->nb_add(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_add(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__iadd__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_subtract(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__sub__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__sub__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_subtract) {
                        result = meta->nb_subtract(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rsub__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rsub__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_subtract) {
                    return meta->nb_subtract(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_subtract(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__isub__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_multiply(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__mul__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__mul__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_multiply) {
                        result = meta->nb_multiply(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rmul__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rmul__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_multiply) {
                    return meta->nb_multiply(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_multiply(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__imul__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_remainder(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__mod__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__mod__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_remainder) {
                        result = meta->nb_remainder(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rmod__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rmod__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_remainder) {
                    return meta->nb_remainder(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_remainder(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__imod__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_divmod(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__divmod__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__divmod__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_divmod) {
                        result = meta->nb_divmod(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rdivmod__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rdivmod__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_divmod) {
                    return meta->nb_divmod(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_power(PyObject* base, PyObject* exp, PyObject* mod) {
                Type<BertrandMeta> meta_type;
                PyTypeObject* base_type = Py_TYPE(base);
                if (PyType_IsSubtype(
                    Py_TYPE(base_type),
                    reinterpret_cast<PyTypeObject*>(ptr(meta_type))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(base_type);
                    if (meta->__pow__) {
                        PyObject* const forward[] = {base, exp, mod};
                        result = PyObject_Vectorcall(
                            meta->__pow__,
                            forward,
                            2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_power) {
                        result = meta->nb_power(base, exp, mod);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                PyTypeObject* exp_type = Py_TYPE(exp);
                if (PyType_IsSubtype(
                    Py_TYPE(exp_type),
                    reinterpret_cast<PyTypeObject*>(ptr(meta_type))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(exp_type);
                    if (meta->__rpow__) {
                        PyObject* const forward[] = {exp, base, mod};
                        result = PyObject_Vectorcall(
                            meta->__rpow__,
                            forward,
                            2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_power) {
                        result = meta->nb_power(exp, base, mod);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(mod));
                if (meta->__pow__) {
                    PyObject* const forward[] = {mod, base, exp};
                    return PyObject_Vectorcall(
                        meta->__pow__,
                        forward,
                        2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_power) {
                    return meta->nb_power(mod, base, exp);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_power(PyObject* self, PyObject* exp, PyObject* mod) {
                PyObject* const forward[] = {self, exp, mod};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__ipow__,
                    forward,
                    2 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_negative(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__neg__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_positive(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__pos__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_absolute(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__abs__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static int nb_bool(PyObject* self) {
                PyObject* result = PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__bool__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
                if (result == nullptr) {
                    return -1;
                }
                int cond = PyObject_IsTrue(result);
                Py_DECREF(result);
                return cond;
            }

            static PyObject* nb_invert(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__invert__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_lshift(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__lshift__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__lshift__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_lshift) {
                        result = meta->nb_lshift(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rlshift__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rlshift__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_lshift) {
                    return meta->nb_lshift(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_lshift(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__ilshift__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_rshift(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__rshift__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__rshift__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_rshift) {
                        result = meta->nb_rshift(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rrshift__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rrshift__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_rshift) {
                    return meta->nb_rshift(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_rshift(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__irshift__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_and(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__and__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__and__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_and) {
                        result = meta->nb_and(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rand__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rand__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_and) {
                    return meta->nb_and(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_and(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__iand__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_xor(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__xor__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__xor__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_xor) {
                        result = meta->nb_xor(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rxor__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rxor__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_xor) {
                    return meta->nb_xor(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_xor(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__ixor__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_or(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__or__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__or__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_or) {
                        result = meta->nb_or(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__ror__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__ror__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_or) {
                    return meta->nb_or(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_or(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__ior__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_int(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__int__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_float(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__float__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_floor_divide(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__floordiv__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__floordiv__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_floor_divide) {
                        result = meta->nb_floor_divide(lhs, rhs);
                    }
                    Py_RETURN_NOTIMPLEMENTED;
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rfloordiv__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rfloordiv__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_floor_divide) {
                    return meta->nb_floor_divide(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_floor_divide(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__ifloordiv__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_true_divide(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__truediv__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__truediv__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_true_divide) {
                        result = meta->nb_true_divide(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rtruediv__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rtruediv__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_true_divide) {
                    return meta->nb_true_divide(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_true_divide(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__itruediv__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_index(PyObject* self) {
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__index__,
                    &self,
                    PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_matrix_multiply(PyObject* lhs, PyObject* rhs) {
                PyTypeObject* lhs_type = Py_TYPE(lhs);
                if (PyType_IsSubtype(
                    Py_TYPE(lhs_type),
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>()))
                )) {
                    PyObject* result;
                    __python__* meta = reinterpret_cast<__python__*>(lhs_type);
                    if (meta->__matmul__) {
                        PyObject* const forward[] = {lhs, rhs};
                        result = PyObject_Vectorcall(
                            meta->__matmul__,
                            forward,
                            1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                    } else if (meta->nb_matrix_multiply) {
                        result = meta->nb_matrix_multiply(lhs, rhs);
                    }
                    if (result != Py_NotImplemented) {
                        return result;
                    }
                }
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(rhs));
                if (meta->__rmatmul__) {
                    PyObject* const forward[] = {rhs, lhs};
                    return PyObject_Vectorcall(
                        meta->__rmatmul__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                } else if (meta->nb_matrix_multiply) {
                    return meta->nb_matrix_multiply(rhs, lhs);
                }
                Py_RETURN_NOTIMPLEMENTED;
            }

            static PyObject* nb_inplace_matrix_multiply(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__imatmul__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

        };

        inline static PyMethodDef methods[] = {
            {
                .ml_name = "__instancecheck__",
                .ml_meth = reinterpret_cast<PyCFunction>(class_instancecheck),
                .ml_flags = METH_O,
                .ml_doc =
R"doc(Determine if an object is an instance of the class or its subclasses.

Parameters
----------
instance : object
    The object to check against this type.

Returns
-------
bool
    Forwards the result of the __isinstance__ control struct at the C++ level.

Notes
-----
This method is called automatically by Python when using the `isinstance()` function to
test against this type.  It is equivalent to a C++ `py::isinstance()` call, which can
be customize by specializing the `__isinstance__` control struct.)doc"
            },
            {
                .ml_name = "__subclasscheck__",
                .ml_meth = reinterpret_cast<PyCFunction>(class_subclasscheck),
                .ml_flags = METH_O,
                .ml_doc =
R"doc(Determine if a class is a subclass of this class.

Parameters
----------
subclass : type
    The class to check against this type.

Returns
-------
bool
    Forwards the result of the __issubclass__ control struct at the C++ level.

Notes
-----
This method is called automatically by Python when using the `issubclass()` function
to test against this type.  It is equivalent to a C++ `py::issubclass()` call, which
can be customize by specializing the `__issubclass__` control struct.)doc"
            },
            {nullptr, nullptr, 0, nullptr}
        };

        /// TODO: document all the descriptors

        inline static PyGetSetDef getset[] = {
            {
                .name = "__doc__",
                .get = reinterpret_cast<getter>(class_doc),
                .set = nullptr,
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__new__",
                .get = reinterpret_cast<getter>(Descr::__new__::get),
                .set = reinterpret_cast<setter>(Descr::__new__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__init__",
                .get = reinterpret_cast<getter>(Descr::__init__::get),
                .set = reinterpret_cast<setter>(Descr::__init__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__del__",
                .get = reinterpret_cast<getter>(Descr::__del__::get),
                .set = reinterpret_cast<setter>(Descr::__del__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__repr__",
                .get = reinterpret_cast<getter>(Descr::__repr__::get),
                .set = reinterpret_cast<setter>(Descr::__repr__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__str__",
                .get = reinterpret_cast<getter>(Descr::__str__::get),
                .set = reinterpret_cast<setter>(Descr::__str__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__bytes__",
                .get = reinterpret_cast<getter>(Descr::__bytes__::get),
                .set = reinterpret_cast<setter>(Descr::__bytes__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__format__",
                .get = reinterpret_cast<getter>(Descr::__format__::get),
                .set = reinterpret_cast<setter>(Descr::__format__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__lt__",
                .get = reinterpret_cast<getter>(Descr::__lt__::get),
                .set = reinterpret_cast<setter>(Descr::__lt__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__le__",
                .get = reinterpret_cast<getter>(Descr::__le__::get),
                .set = reinterpret_cast<setter>(Descr::__le__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__eq__",
                .get = reinterpret_cast<getter>(Descr::__eq__::get),
                .set = reinterpret_cast<setter>(Descr::__eq__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ne__",
                .get = reinterpret_cast<getter>(Descr::__ne__::get),
                .set = reinterpret_cast<setter>(Descr::__ne__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ge__",
                .get = reinterpret_cast<getter>(Descr::__ge__::get),
                .set = reinterpret_cast<setter>(Descr::__ge__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__gt__",
                .get = reinterpret_cast<getter>(Descr::__gt__::get),
                .set = reinterpret_cast<setter>(Descr::__gt__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__hash__",
                .get = reinterpret_cast<getter>(Descr::__hash__::get),
                .set = reinterpret_cast<setter>(Descr::__hash__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__bool__",
                .get = reinterpret_cast<getter>(Descr::__bool__::get),
                .set = reinterpret_cast<setter>(Descr::__bool__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__getattr__",
                .get = reinterpret_cast<getter>(Descr::__getattr__::get),
                .set = reinterpret_cast<setter>(Descr::__getattr__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__getattribute__",
                .get = reinterpret_cast<getter>(Descr::__getattribute__::get),
                .set = reinterpret_cast<setter>(Descr::__getattribute__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__setattr__",
                .get = reinterpret_cast<getter>(Descr::__setattr__::get),
                .set = reinterpret_cast<setter>(Descr::__setattr__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__delattr__",
                .get = reinterpret_cast<getter>(Descr::__delattr__::get),
                .set = reinterpret_cast<setter>(Descr::__delattr__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__dir__",
                .get = reinterpret_cast<getter>(Descr::__dir__::get),
                .set = reinterpret_cast<setter>(Descr::__dir__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__get__",
                .get = reinterpret_cast<getter>(Descr::__get__::get),
                .set = reinterpret_cast<setter>(Descr::__get__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__set__",
                .get = reinterpret_cast<getter>(Descr::__set__::get),
                .set = reinterpret_cast<setter>(Descr::__set__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__delete__",
                .get = reinterpret_cast<getter>(Descr::__delete__::get),
                .set = reinterpret_cast<setter>(Descr::__delete__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__init_subclass__",
                .get = reinterpret_cast<getter>(Descr::__init_subclass__::get),
                .set = reinterpret_cast<setter>(Descr::__init_subclass__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__set_name__",
                .get = reinterpret_cast<getter>(Descr::__set_name__::get),
                .set = reinterpret_cast<setter>(Descr::__set_name__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__mro_entries__",
                .get = reinterpret_cast<getter>(Descr::__mro_entries__::get),
                .set = reinterpret_cast<setter>(Descr::__mro_entries__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__instancecheck__",
                .get = reinterpret_cast<getter>(Descr::__instancecheck__::get),
                .set = reinterpret_cast<setter>(Descr::__instancecheck__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__subclasscheck__",
                .get = reinterpret_cast<getter>(Descr::__subclasscheck__::get),
                .set = reinterpret_cast<setter>(Descr::__subclasscheck__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__class_getitem__",
                .get = reinterpret_cast<getter>(Descr::__class_getitem__::get),
                .set = reinterpret_cast<setter>(Descr::__class_getitem__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__call__",
                .get = reinterpret_cast<getter>(Descr::__call__::get),
                .set = reinterpret_cast<setter>(Descr::__call__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__len__",
                .get = reinterpret_cast<getter>(Descr::__len__::get),
                .set = reinterpret_cast<setter>(Descr::__len__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__length_hint__",
                .get = reinterpret_cast<getter>(Descr::__length_hint__::get),
                .set = reinterpret_cast<setter>(Descr::__length_hint__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__getitem__",
                .get = reinterpret_cast<getter>(Descr::__getitem__::get),
                .set = reinterpret_cast<setter>(Descr::__getitem__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__setitem__",
                .get = reinterpret_cast<getter>(Descr::__setitem__::get),
                .set = reinterpret_cast<setter>(Descr::__setitem__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__delitem__",
                .get = reinterpret_cast<getter>(Descr::__delitem__::get),
                .set = reinterpret_cast<setter>(Descr::__delitem__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__missing__",
                .get = reinterpret_cast<getter>(Descr::__missing__::get),
                .set = reinterpret_cast<setter>(Descr::__missing__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__iter__",
                .get = reinterpret_cast<getter>(Descr::__iter__::get),
                .set = reinterpret_cast<setter>(Descr::__iter__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__next__",
                .get = reinterpret_cast<getter>(Descr::__next__::get),
                .set = reinterpret_cast<setter>(Descr::__next__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__reversed__",
                .get = reinterpret_cast<getter>(Descr::__reversed__::get),
                .set = reinterpret_cast<setter>(Descr::__reversed__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__contains__",
                .get = reinterpret_cast<getter>(Descr::__contains__::get),
                .set = reinterpret_cast<setter>(Descr::__contains__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__add__",
                .get = reinterpret_cast<getter>(Descr::__add__::get),
                .set = reinterpret_cast<setter>(Descr::__add__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__sub__",
                .get = reinterpret_cast<getter>(Descr::__sub__::get),
                .set = reinterpret_cast<setter>(Descr::__sub__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__mul__",
                .get = reinterpret_cast<getter>(Descr::__mul__::get),
                .set = reinterpret_cast<setter>(Descr::__mul__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__matmul__",
                .get = reinterpret_cast<getter>(Descr::__matmul__::get),
                .set = reinterpret_cast<setter>(Descr::__matmul__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__truediv__",
                .get = reinterpret_cast<getter>(Descr::__truediv__::get),
                .set = reinterpret_cast<setter>(Descr::__truediv__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__floordiv__",
                .get = reinterpret_cast<getter>(Descr::__floordiv__::get),
                .set = reinterpret_cast<setter>(Descr::__floordiv__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__mod__",
                .get = reinterpret_cast<getter>(Descr::__mod__::get),
                .set = reinterpret_cast<setter>(Descr::__mod__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__divmod__",
                .get = reinterpret_cast<getter>(Descr::__divmod__::get),
                .set = reinterpret_cast<setter>(Descr::__divmod__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__pow__",
                .get = reinterpret_cast<getter>(Descr::__pow__::get),
                .set = reinterpret_cast<setter>(Descr::__pow__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__lshift__",
                .get = reinterpret_cast<getter>(Descr::__lshift__::get),
                .set = reinterpret_cast<setter>(Descr::__lshift__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rshift__",
                .get = reinterpret_cast<getter>(Descr::__rshift__::get),
                .set = reinterpret_cast<setter>(Descr::__rshift__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__and__",
                .get = reinterpret_cast<getter>(Descr::__and__::get),
                .set = reinterpret_cast<setter>(Descr::__and__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__xor__",
                .get = reinterpret_cast<getter>(Descr::__xor__::get),
                .set = reinterpret_cast<setter>(Descr::__xor__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__or__",
                .get = reinterpret_cast<getter>(Descr::__or__::get),
                .set = reinterpret_cast<setter>(Descr::__or__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__radd__",
                .get = reinterpret_cast<getter>(Descr::__radd__::get),
                .set = reinterpret_cast<setter>(Descr::__radd__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rsub__",
                .get = reinterpret_cast<getter>(Descr::__rsub__::get),
                .set = reinterpret_cast<setter>(Descr::__rsub__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rmul__",
                .get = reinterpret_cast<getter>(Descr::__rmul__::get),
                .set = reinterpret_cast<setter>(Descr::__rmul__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rmatmul__",
                .get = reinterpret_cast<getter>(Descr::__rmatmul__::get),
                .set = reinterpret_cast<setter>(Descr::__rmatmul__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rtruediv__",
                .get = reinterpret_cast<getter>(Descr::__rtruediv__::get),
                .set = reinterpret_cast<setter>(Descr::__rtruediv__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rfloordiv__",
                .get = reinterpret_cast<getter>(Descr::__rfloordiv__::get),
                .set = reinterpret_cast<setter>(Descr::__rfloordiv__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rmod__",
                .get = reinterpret_cast<getter>(Descr::__rmod__::get),
                .set = reinterpret_cast<setter>(Descr::__rmod__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rdivmod__",
                .get = reinterpret_cast<getter>(Descr::__rdivmod__::get),
                .set = reinterpret_cast<setter>(Descr::__rdivmod__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rpow__",
                .get = reinterpret_cast<getter>(Descr::__rpow__::get),
                .set = reinterpret_cast<setter>(Descr::__rpow__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rlshift__",
                .get = reinterpret_cast<getter>(Descr::__rlshift__::get),
                .set = reinterpret_cast<setter>(Descr::__rlshift__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rrshift__",
                .get = reinterpret_cast<getter>(Descr::__rrshift__::get),
                .set = reinterpret_cast<setter>(Descr::__rrshift__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rand__",
                .get = reinterpret_cast<getter>(Descr::__rand__::get),
                .set = reinterpret_cast<setter>(Descr::__rand__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__rxor__",
                .get = reinterpret_cast<getter>(Descr::__rxor__::get),
                .set = reinterpret_cast<setter>(Descr::__rxor__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ror__",
                .get = reinterpret_cast<getter>(Descr::__ror__::get),
                .set = reinterpret_cast<setter>(Descr::__ror__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__iadd__",
                .get = reinterpret_cast<getter>(Descr::__iadd__::get),
                .set = reinterpret_cast<setter>(Descr::__iadd__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__isub__",
                .get = reinterpret_cast<getter>(Descr::__isub__::get),
                .set = reinterpret_cast<setter>(Descr::__isub__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__imul__",
                .get = reinterpret_cast<getter>(Descr::__imul__::get),
                .set = reinterpret_cast<setter>(Descr::__imul__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__imatmul__",
                .get = reinterpret_cast<getter>(Descr::__imatmul__::get),
                .set = reinterpret_cast<setter>(Descr::__imatmul__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__itruediv__",
                .get = reinterpret_cast<getter>(Descr::__itruediv__::get),
                .set = reinterpret_cast<setter>(Descr::__itruediv__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ifloordiv__",
                .get = reinterpret_cast<getter>(Descr::__ifloordiv__::get),
                .set = reinterpret_cast<setter>(Descr::__ifloordiv__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__imod__",
                .get = reinterpret_cast<getter>(Descr::__imod__::get),
                .set = reinterpret_cast<setter>(Descr::__imod__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__idivmod__",
                .get = reinterpret_cast<getter>(Descr::__idivmod__::get),
                .set = reinterpret_cast<setter>(Descr::__idivmod__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ipow__",
                .get = reinterpret_cast<getter>(Descr::__ipow__::get),
                .set = reinterpret_cast<setter>(Descr::__ipow__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ilshift__",
                .get = reinterpret_cast<getter>(Descr::__ilshift__::get),
                .set = reinterpret_cast<setter>(Descr::__ilshift__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__irshift__",
                .get = reinterpret_cast<getter>(Descr::__irshift__::get),
                .set = reinterpret_cast<setter>(Descr::__irshift__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__iand__",
                .get = reinterpret_cast<getter>(Descr::__iand__::get),
                .set = reinterpret_cast<setter>(Descr::__iand__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ixor__",
                .get = reinterpret_cast<getter>(Descr::__ixor__::get),
                .set = reinterpret_cast<setter>(Descr::__ixor__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ior__",
                .get = reinterpret_cast<getter>(Descr::__ior__::get),
                .set = reinterpret_cast<setter>(Descr::__ior__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__neg__",
                .get = reinterpret_cast<getter>(Descr::__neg__::get),
                .set = reinterpret_cast<setter>(Descr::__neg__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__pos__",
                .get = reinterpret_cast<getter>(Descr::__pos__::get),
                .set = reinterpret_cast<setter>(Descr::__pos__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__abs__",
                .get = reinterpret_cast<getter>(Descr::__abs__::get),
                .set = reinterpret_cast<setter>(Descr::__abs__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__invert__",
                .get = reinterpret_cast<getter>(Descr::__invert__::get),
                .set = reinterpret_cast<setter>(Descr::__invert__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__complex__",
                .get = reinterpret_cast<getter>(Descr::__complex_py__::get),
                .set = reinterpret_cast<setter>(Descr::__complex_py__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__int__",
                .get = reinterpret_cast<getter>(Descr::__int__::get),
                .set = reinterpret_cast<setter>(Descr::__int__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__float__",
                .get = reinterpret_cast<getter>(Descr::__float__::get),
                .set = reinterpret_cast<setter>(Descr::__float__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__index__",
                .get = reinterpret_cast<getter>(Descr::__index__::get),
                .set = reinterpret_cast<setter>(Descr::__index__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__round__",
                .get = reinterpret_cast<getter>(Descr::__round__::get),
                .set = reinterpret_cast<setter>(Descr::__round__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__trunc__",
                .get = reinterpret_cast<getter>(Descr::__trunc__::get),
                .set = reinterpret_cast<setter>(Descr::__trunc__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__floor__",
                .get = reinterpret_cast<getter>(Descr::__floor__::get),
                .set = reinterpret_cast<setter>(Descr::__floor__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__ceil__",
                .get = reinterpret_cast<getter>(Descr::__ceil__::get),
                .set = reinterpret_cast<setter>(Descr::__ceil__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__enter__",
                .get = reinterpret_cast<getter>(Descr::__enter__::get),
                .set = reinterpret_cast<setter>(Descr::__enter__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__exit__",
                .get = reinterpret_cast<getter>(Descr::__exit__::get),
                .set = reinterpret_cast<setter>(Descr::__exit__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__buffer__",
                .get = reinterpret_cast<getter>(Descr::__buffer__::get),
                .set = reinterpret_cast<setter>(Descr::__buffer__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__release_buffer__",
                .get = reinterpret_cast<getter>(Descr::__release_buffer__::get),
                .set = reinterpret_cast<setter>(Descr::__release_buffer__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__await__",
                .get = reinterpret_cast<getter>(Descr::__await__::get),
                .set = reinterpret_cast<setter>(Descr::__await__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__aiter__",
                .get = reinterpret_cast<getter>(Descr::__aiter__::get),
                .set = reinterpret_cast<setter>(Descr::__aiter__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__anext__",
                .get = reinterpret_cast<getter>(Descr::__anext__::get),
                .set = reinterpret_cast<setter>(Descr::__anext__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__aenter__",
                .get = reinterpret_cast<getter>(Descr::__aenter__::get),
                .set = reinterpret_cast<setter>(Descr::__aenter__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {
                .name = "__aexit__",
                .get = reinterpret_cast<getter>(Descr::__aexit__::get),
                .set = reinterpret_cast<setter>(Descr::__aexit__::set),
                .doc = nullptr,
                .closure = nullptr
            },
            {nullptr, nullptr, nullptr, nullptr, nullptr}
        };

    };

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


template <typename T>
struct __isinstance__<T, BertrandMeta> : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::python_like<T>) {
            int result = PyObject_IsInstance(
                ptr(obj),
                ptr(Type<BertrandMeta>())
            );
            if (result < 0) {
                Exception::from_python();
            }
            return result;
        } else {
            return false;
        }
    }
    static constexpr bool operator()(const T& obj, const BertrandMeta& cls) {
        if constexpr (impl::python_like<T>) {
            using Meta = Type<BertrandMeta>::__python__;
            Meta* meta = reinterpret_cast<Meta*>(ptr(cls));
            return meta->instancecheck(meta, ptr(obj));
        } else {
            return false;
        }
    }
};


template <typename T>
struct __issubclass__<T, BertrandMeta> : Returns<bool> {
    static consteval bool operator()() {
        return impl::originates_from_cpp<T>;
    }
    static bool operator()(const T& obj) {
        int result = PyObject_IsSubclass(
            ptr(obj),
            ptr(Type<BertrandMeta>())
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;
    }
    static bool operator()(const T& obj, const BertrandMeta& cls) {
        if constexpr (impl::python_like<T>) {
            using Meta = Type<BertrandMeta>::__python__;
            Meta* meta = reinterpret_cast<Meta*>(ptr(cls));
            return meta->subclasscheck(meta, ptr(obj));
        } else {
            return false;
        }
    }
};


template <>
struct __len__<BertrandMeta>                                : Returns<size_t> {};
template <>
struct __iter__<BertrandMeta>                               : Returns<BertrandMeta> {};
template <typename... T>
struct __getitem__<BertrandMeta, Type<T>...>                : Returns<BertrandMeta> {
    static auto operator()(const BertrandMeta& cls, const Type<T>&... key) {
        using Meta = Type<BertrandMeta>::__python__;
        Meta* meta = reinterpret_cast<Meta*>(ptr(cls));
        if (meta->templates == nullptr) {
            throw TypeError("class has no template instantiations");
        }
        PyObject* tuple = PyTuple_Pack(sizeof...(T), ptr(key)...);
        if (tuple == nullptr) {
            Exception::from_python();
        }
        PyObject* value = PyDict_GetItem(meta->templates, tuple);
        Py_DECREF(tuple);
        if (value == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<BertrandMeta>(value);
    }
};


template <impl::originates_from_cpp T>
struct __len__<Type<T>>                                     : Returns<size_t> {};
template <impl::originates_from_cpp T>
struct __iter__<Type<T>>                                    : Returns<BertrandMeta> {};
template <impl::originates_from_cpp T, typename... U>
struct __getitem__<Type<T>, Type<U>...>                     : Returns<BertrandMeta> {
    static auto operator()(const Type<T>& cls, const Type<U>&... key) {
        using Meta = Type<BertrandMeta>::__python__;
        Meta* meta = reinterpret_cast<Meta*>(ptr(cls));
        if (meta->templates == nullptr) {
            throw TypeError("class has no template instantiations");
        }
        PyObject* tuple = PyTuple_Pack(sizeof...(U), ptr(key)...);
        if (tuple == nullptr) {
            Exception::from_python();
        }
        PyObject* value = PyDict_GetItem(meta->templates, tuple);
        Py_DECREF(tuple);
        if (value == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<BertrandMeta>(value);
    }
};


////////////////////////
////    BINDINGS    ////
////////////////////////


namespace impl {

    /// TODO: all bindings can be centralized in BaseDef.  There's no need for any
    /// special handling, and all types use the exact same system, without any extra
    /// template magic.

    template <typename CRTP, typename Wrapper, typename CppType>
    template <StaticStr ModName>
    struct TypeTag::BaseDef<CRTP, Wrapper, CppType>::Bindings {
    private:
        using Meta = Type<BertrandMeta>::__python__;
        inline static std::vector<PyMethodDef> tp_methods;
        inline static std::vector<PyGetSetDef> tp_getset;
        inline static std::vector<PyMemberDef> tp_members;

        /* This function is stored as a pointer in the metaclass's C++ members and
        called whenever a Python-level `isinstance()` check is made against this class.
        It will delegate to an `__isinstance__<Object, Wrapper>` specialization if one
        exists.  Otherwise it will default to standard Python behavior. */
        static bool instancecheck(Meta* cls, PyObject* instance) {
            if constexpr (std::is_invocable_v<
                __isinstance__<Object, Wrapper>,
                const Object&
            >) {
                return __isinstance__<Object, Wrapper>{}(
                    reinterpret_borrow<Object>(instance)
                );
            } else {
                return PyType_IsSubtype(
                    Py_TYPE(instance),
                    reinterpret_cast<PyTypeObject*>(cls)
                );
            }
        }

        /* This function is stored as a pointer in the metaclass's C++ members and
        called whenever a Python-level `issubclass()` check is made against this calss.
        It will delegate to an `__issubclass__<Object, Wrapper>` specialization if one
        exists.  Otherwise it will default to standard Python behavior. */
        static bool subclasscheck(Meta* cls, PyObject* subclass) {
            if constexpr (std::is_invocable_v<
                __issubclass__<Object, Wrapper>,
                const Object&
            >) {
                return __issubclass__<Object, Wrapper>{}(
                    reinterpret_borrow<Object>(subclass)
                );
            } else {
                if (PyType_Check(subclass)) {
                    return PyType_IsSubtype(
                        reinterpret_cast<PyTypeObject*>(subclass),
                        reinterpret_cast<PyTypeObject*>(cls)
                    );
                } else {
                    throw TypeError(
                        "issubclass() arg 1 must be a class"
                    );
                }
            }
        }

        /// TODO: Perhaps the way to implement register_type is to use a C++ unordered
        /// map that maps std::type_index to a context object that contains all the
        /// information needed to generate bindings for that type.  In fact, that
        /// might directly reference the type's template instantiations, which
        /// can be dynamically inserted into during the module's export process.

        /* Insert a Python type into the global `bertrand.python.types` registry, so
        that it can be mapped to its C++ equivalent and used when generating Python
        bindings. */
        template <typename Cls>
        static void register_type(Module<ModName>& mod, PyTypeObject* type);

        struct Context {
            bool is_member_var = false;
            bool is_class_var = false;
            bool is_property = false;
            bool is_class_property = false;
            bool is_type = false;
            bool is_template_interface = false;
            bool is_method = false;
            bool is_classmethod = false;
            bool is_staticmethod = false;
            std::vector<std::function<void(Type<Wrapper>&)>> callbacks;
        };

        template <typename T>
        static void register_iterator(Type<Wrapper>& type) {
            static PyType_Slot slots[] = {
                {
                    Py_tp_iter,
                    reinterpret_cast<void*>(PyObject_SelfIter)
                },
                {
                    Py_tp_iternext,
                    reinterpret_cast<void*>(T::__next__)
                },
                {0, nullptr}
            };
            static PyType_Spec spec = {
                .name = typeid(T).name(),
                .basicsize = sizeof(T),
                .itemsize = 0,
                .flags =
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE |
                    Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION,
                .slots = slots
            };
            PyObject* cls = PyType_FromModuleAndSpec(
                ptr(Base::module),
                &spec,
                nullptr
            );
            if (cls == nullptr) {
                Exception::from_python();
            }
            int rc = PyObject_SetAttrString(ptr(type), typeid(T).name(), cls);
            Py_DECREF(cls);
            if (rc) {
                Exception::from_python();
            }
        }

        // auto result = Base::finalize(tp_flags);
        // if constexpr (impl::iterable<CppType>) {
        //     register_iterator<Iterator<
        //         decltype(std::ranges::begin(std::declval<CppType>())),
        //         decltype(std::ranges::end(std::declval<CppType>()))
        //     >>(result);
        // }
        // if constexpr (impl::iterable<const CppType>) {
        //     register_iterator<Iterator<
        //         decltype(std::ranges::begin(std::declval<const CppType>())),
        //         decltype(std::ranges::end(std::declval<const CppType>()))
        //     >>(result);
        // }
        // if constexpr (impl::reverse_iterable<CppType>) {
        //     register_iterator<Iterator<
        //         decltype(std::ranges::rbegin(std::declval<CppType>())),
        //         decltype(std::ranges::rend(std::declval<CppType>()))
        //     >>(result);
        // }
        // if constexpr (impl::reverse_iterable<const CppType>) {
        //     register_iterator<Iterator<
        //         decltype(std::ranges::rbegin(std::declval<const CppType>())),
        //         decltype(std::ranges::rend(std::declval<const CppType>()))
        //     >>(result);
        // }
        // return result;

    public:
        using t_cpp = CppType;

        Module<ModName> module;
        std::unordered_map<std::string, Context> context;
        Meta::ClassGetters class_getters;
        Meta::ClassSetters class_setters;

        Bindings(const Module<ModName>& mod) : module(mod) {}
        Bindings(const Bindings&) = delete;
        Bindings(Bindings&&) = delete;

        /* Expose an immutable member variable to Python as a getset descriptor,
        which synchronizes its state. */
        template <StaticStr Name, typename T>
        void var(const T CRTP::*value) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_member_var = true,
            };
            static bool skip = false;
            if (skip) {
                return;
            }
            static auto get = [](PyObject* self, void* closure) -> PyObject* {
                try {
                    CRTP* obj = reinterpret_cast<CRTP*>(self);
                    auto member = reinterpret_cast<const T CRTP::*>(closure);
                    if constexpr (impl::python_like<T>) {
                        return Py_NewRef(ptr(obj->*member));
                    } else {
                        return release(wrap(obj->*member));
                    }
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            };
            static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                std::string msg = "variable '" + Name + "' of type '" +
                    impl::demangle(typeid(Wrapper).name()) + "' is immutable.";
                PyErr_SetString(PyExc_TypeError, msg.c_str());
                return -1;
            };
            tp_getset.push_back({
                Name,
                +get,  // converts a stateless lambda to a function pointer
                +set,
                nullptr,
                const_cast<void*>(reinterpret_cast<const void*>(value))
            });
            skip = true;
        }

        /* Expose a mutable member variable to Python as a getset descriptor, which
        synchronizes its state. */
        template <StaticStr Name, typename T>
        void var(T CRTP::*value) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_member_var = true,
            };
            static bool skip = false;
            if (skip) {
                return;
            }
            static auto get = [](PyObject* self, void* closure) -> PyObject* {
                try {
                    CRTP* obj = reinterpret_cast<CRTP*>(self);
                    auto member = reinterpret_cast<T CRTP::*>(closure);
                    if constexpr (impl::python_like<T>) {
                        return Py_NewRef(ptr(obj->*member));
                    } else {
                        return release(wrap(obj->*member));
                    }
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            };
            static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                if (new_val == nullptr) {
                    std::string msg = "variable '" + Name + "' of type '" +
                        impl::demangle(typeid(Wrapper).name()) +
                        "' cannot be deleted.";
                    PyErr_SetString(PyExc_TypeError, msg.c_str());
                    return -1;
                }
                try {
                    CRTP* obj = reinterpret_cast<CRTP*>(self);
                    auto member = reinterpret_cast<T CRTP::*>(closure);
                    obj->*member = static_cast<T>(
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
                +get,  // converts a stateless lambda to a function pointer
                +set,
                nullptr,  // doc
                reinterpret_cast<void*>(value)
            });
            skip = true;
        }

        /* Expose an immutable member variable to Python as a getset descriptor,
        which synchronizes its state. */
        template <StaticStr Name, typename T>
        void var(const T CppType::*value) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_member_var = true,
            };
            static bool skip = false;
            if (skip) {
                return;
            }
            static auto get = [](PyObject* self, void* closure) -> PyObject* {
                try {
                    auto member = reinterpret_cast<const T CppType::*>(closure);
                    return std::visit(
                        Visitor{
                            [member](const CppType& obj) {
                                if constexpr (impl::python_like<T>) {
                                    return Py_NewRef(ptr(obj.*member));
                                } else {
                                    return release(wrap(obj.*member));
                                }
                            },
                            [member](const CppType* obj) {
                                if constexpr (impl::python_like<T>) {
                                    return Py_NewRef(ptr(obj->*member));
                                } else {
                                    return release(wrap(obj->*member));
                                }
                            }
                        },
                        reinterpret_cast<CRTP*>(self)->m_cpp
                    );
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            };
            static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                std::string msg = "variable '" + Name + "' of type '" +
                    impl::demangle(typeid(Wrapper).name()) + "' is immutable.";
                PyErr_SetString(PyExc_TypeError, msg.c_str());
                return -1;
            };
            tp_getset.push_back({
                Name,
                +get,  // converts a stateless lambda to a function pointer
                +set,
                nullptr,
                const_cast<void*>(reinterpret_cast<const void*>(value))
            });
            skip = true;
        }

        /* Expose a mutable member variable to Python as a getset descriptor, which
        synchronizes its state. */
        template <StaticStr Name, typename T>
        void var(T CppType::*value) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_member_var = true,
            };
            static bool skip = false;
            if (skip) {
                return;
            }
            static auto get = [](PyObject* self, void* closure) -> PyObject* {
                try {
                    auto member = reinterpret_cast<T CppType::*>(closure);
                    return std::visit(
                        Visitor{
                            [member](CppType& obj) {
                                if constexpr (impl::python_like<T>) {
                                    return Py_NewRef(ptr(obj.*member));
                                } else {
                                    return release(wrap(obj.*member));
                                }
                            },
                            [member](CppType* obj) {
                                if constexpr (impl::python_like<T>) {
                                    return Py_NewRef(ptr(obj->*member));
                                } else {
                                    return release(wrap(obj->*member));
                                }
                            },
                            [member](const CppType* obj) {
                                if constexpr (impl::python_like<T>) {
                                    return Py_NewRef(ptr(obj->*member));
                                } else {
                                    return release(wrap(obj->*member));
                                }
                            }
                        },
                        reinterpret_cast<CRTP*>(self)->m_cpp
                    );
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            };
            static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                if (new_val == nullptr) {
                    std::string msg = "variable '" + Name + "' of type '" +
                        impl::demangle(typeid(Wrapper).name()) +
                        "' cannot be deleted.";
                    PyErr_SetString(PyExc_TypeError, msg.c_str());
                    return -1;
                }
                try {
                    auto member = reinterpret_cast<T CppType::*>(closure);
                    std::visit(
                        Visitor{
                            [member, new_val](CppType& obj) {
                                obj.*member = static_cast<T>(
                                    reinterpret_borrow<Object>(new_val)
                                );
                            },
                            [member, new_val](CppType* obj) {
                                obj->*member = static_cast<T>(
                                    reinterpret_borrow<Object>(new_val)
                                );
                            },
                            [new_val](const CppType* obj) {
                                throw TypeError(
                                    "variable '" + Name + "' of type '" +
                                    impl::demangle(typeid(Wrapper).name()) +
                                    "' is immutable."
                                );
                            }
                        },
                        reinterpret_cast<CRTP*>(self)->m_cpp
                    );
                    return 0;
                } catch (...) {
                    Exception::to_python();
                    return -1;
                }
            };
            tp_getset.push_back({
                Name,
                +get,  // converts a stateless lambda to a function pointer
                +set,
                nullptr,  // doc
                reinterpret_cast<void*>(value)
            });
            skip = true;
        }

        /* Expose an immutable static variable to Python using the `__getattr__()`
        slot of the metatype, which synchronizes its state. */
        template <StaticStr Name, typename T>
        void classvar(const T& value) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_class_var = true,
            };
            static auto get = [](PyObject* cls, void* closure) -> PyObject* {
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
                std::string msg = "variable '" + Name + "' of type '" +
                    impl::demangle(typeid(Wrapper).name()) + "' is immutable.";
                PyErr_SetString(PyExc_TypeError, msg.c_str());
                return -1;
            };
            class_getters[Name] = {
                +get,
                const_cast<void*>(reinterpret_cast<const void*>(&value))
            };
            class_setters[Name] = {
                +set,
                const_cast<void*>(reinterpret_cast<const void*>(&value))
            };
        }

        /* Expose a mutable static variable to Python using the `__getattr__()` and
        `__setattr__()` slots of the metatype, which synchronizes its state.  */
        template <StaticStr Name, typename T>
        void classvar(T& value) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_class_var = true,
            };
            static auto get = [](PyObject* self, void* value) -> PyObject* {
                try {
                    T* value = reinterpret_cast<T*>(value);
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
            static auto set = [](PyObject* self, PyObject* new_val, void* value) -> int {
                if (new_val == nullptr) {
                    std::string msg = "variable '" + Name + "' of type '" +
                        impl::demangle(typeid(Wrapper).name()) +
                        "' cannot be deleted.";
                    PyErr_SetString(PyExc_TypeError, msg.c_str());
                    return -1;
                }
                try {
                    *reinterpret_cast<T*>(value) = static_cast<T>(
                        reinterpret_borrow<Object>(new_val)
                    );
                    return 0;
                } catch (...) {
                    Exception::to_python();
                    return -1;
                }
            };
            class_getters[Name] = {+get, reinterpret_cast<void*>(&value)};
            class_setters[Name] = {+set, reinterpret_cast<void*>(&value)};
        }

        /* Expose a C++-style const getter to Python as a getset descriptor. */
        template <StaticStr Name, typename Return>
        void property(Return(CRTP::*getter)() const) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_property = true,
            };
            static bool skip = false;
            if (skip) {
                return;
            }
            static auto get = [](PyObject* self, void* closure) -> PyObject* {
                try {
                    CRTP* obj = reinterpret_cast<CRTP*>(self);
                    auto member = reinterpret_cast<Return(CRTP::*)() const>(closure);
                    if constexpr (std::is_lvalue_reference_v<Return>) {
                        if constexpr (impl::python_like<Return>) {
                            return Py_NewRef(ptr((obj->*member)()));
                        } else {
                            return release(wrap((obj->*member)()));
                        }
                    } else {
                        return release(as_object((obj->*member)()));
                    }
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            };
            tp_getset.push_back({
                Name,
                +get,  // converts a stateless lambda to a function pointer
                nullptr,  // setter
                nullptr,  // doc
                const_cast<void*>(reinterpret_cast<const void*>(getter))
            });
            skip = true;
        }

        /* Expose a C++-style getter/setter pair to Python as a getset
        descriptor. */
        template <StaticStr Name, typename Return, typename Value>
        void property(
            Return(CRTP::*getter)() const,
            void(CRTP::*setter)(Value&&)
        ) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_property = true,
            };
            static bool skip = false;
            if (skip) {
                return;
            }
            static struct Closure {
                Return(CRTP::*getter)() const;
                void(CRTP::*setter)(Value&&);
            } closure = {getter, setter};
            static auto get = [](PyObject* self, void* closure) -> PyObject* {
                try {
                    CRTP* obj = reinterpret_cast<CRTP*>(self);
                    Closure* ctx = reinterpret_cast<Closure*>(closure);
                    if constexpr (std::is_lvalue_reference_v<Return>) {
                        if constexpr (impl::python_like<Return>) {
                            return Py_NewRef(ptr((obj->*(ctx->getter))()));
                        } else {
                            return release(wrap((obj->*(ctx->getter))()));
                        }
                    } else {
                        return release(as_object((obj->*(ctx->getter))()));
                    }
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            };
            static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                if (new_val == nullptr) {
                    std::string msg = "variable '" + Name + "' of type '" +
                        impl::demangle(typeid(Wrapper).name()) +
                        "' cannot be deleted.";
                    PyErr_SetString(PyExc_TypeError, msg.c_str());
                    return -1;
                }
                try {
                    CRTP* obj = reinterpret_cast<CRTP*>(self);
                    Closure* ctx = reinterpret_cast<Closure*>(closure);
                    (obj->*(ctx->setter))(static_cast<Value>(
                        reinterpret_borrow<Object>(new_val)
                    ));
                    return 0;
                } catch (...) {
                    Exception::to_python();
                    return -1;
                }
            };
            tp_getset.push_back({
                Name,
                +get,  // converts a stateless lambda to a function pointer
                +set,
                nullptr,  // doc
                &closure
            });
            skip = true;
        }

        /* Expose a C++-style const getter to Python as a getset descriptor. */
        template <StaticStr Name, typename Return>
        void property(Return(CppType::*getter)() const) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_property = true,
            };
            static bool skip = false;
            if (skip) {
                return;
            }
            static auto get = [](PyObject* self, void* closure) -> PyObject* {
                try {
                    auto member = reinterpret_cast<Return(CppType::*)() const>(closure);
                    return release(wrap(std::visit(
                        Visitor{
                            [member](const CppType& obj) {
                                if constexpr (std::is_lvalue_reference_v<Return>) {
                                    if constexpr (impl::python_like<Return>) {
                                        return Py_NewRef(ptr((obj.*member)()));
                                    } else {
                                        return release(wrap((obj.*member)()));
                                    }
                                } else {
                                    return release(as_object((obj.*member)()));
                                }
                            },
                            [member](const CppType* obj) {
                                if constexpr (std::is_lvalue_reference_v<Return>) {
                                    if constexpr (impl::python_like<Return>) {
                                        return Py_NewRef(ptr((obj->*member)()));
                                    } else {
                                        return release(wrap((obj->*member)()));
                                    }
                                } else {
                                    return release(as_object((obj->*member)()));
                                }
                            }
                        },
                        reinterpret_cast<CRTP*>(self)->m_cpp
                    )));
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            };
            tp_getset.push_back({
                Name,
                +get,  // converts a stateless lambda to a function pointer
                nullptr,  // setter
                nullptr,  // doc
                const_cast<void*>(reinterpret_cast<const void*>(getter))
            });
            skip = true;
        }

        /* Expose a C++-style getter/setter pair to Python as a getset
        descriptor. */
        template <StaticStr Name, typename Return, typename Value>
        void property(
            Return(CppType::*getter)() const,
            void(CppType::*setter)(Value&&)
        ) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_property = true,
            };
            static bool skip = false;
            if (skip) {
                return;
            }
            static struct Closure {
                Return(CppType::*getter)() const;
                void(CppType::*setter)(Value&&);
            } closure = {getter, setter};
            static auto get = [](PyObject* self, void* closure) -> PyObject* {
                try {
                    Closure* ctx = reinterpret_cast<Closure*>(closure);
                    return release(wrap(std::visit(
                        Visitor{
                            [ctx](CppType& obj) {
                                if constexpr (std::is_lvalue_reference_v<Return>) {
                                    if constexpr (impl::python_like<Return>) {
                                        return Py_NewRef(ptr((obj.*(ctx->getter))()));
                                    } else {
                                        return release(wrap((obj.*(ctx->getter))()));
                                    }
                                } else {
                                    return release(as_object((obj.*(ctx->getter))()));
                                }
                            },
                            [ctx](CppType* obj) {
                                if constexpr (std::is_lvalue_reference_v<Return>) {
                                    if constexpr (impl::python_like<Return>) {
                                        return Py_NewRef(ptr((obj->*(ctx->getter))()));
                                    } else {
                                        return release(wrap((obj->*(ctx->getter))()));
                                    }
                                } else {
                                    return release(as_object((obj->*(ctx->getter))()));
                                }
                            },
                            [ctx](const CppType* obj) {
                                if constexpr (std::is_lvalue_reference_v<Return>) {
                                    if constexpr (impl::python_like<Return>) {
                                        return Py_NewRef(ptr((obj->*(ctx->getter))()));
                                    } else {
                                        return release(wrap((obj->*(ctx->getter))()));
                                    }
                                } else {
                                    return release(as_object((obj->*(ctx->getter))()));
                                }
                            }
                        },
                        reinterpret_cast<CRTP*>(self)->m_cpp
                    )));
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            };
            static auto set = [](PyObject* self, PyObject* new_val, void* closure) -> int {
                if (new_val == nullptr) {
                    std::string msg = "variable '" + Name + "' of type '" +
                        impl::demangle(typeid(Wrapper).name()) +
                        "' cannot be deleted.";
                    PyErr_SetString(PyExc_TypeError, msg.c_str());
                    return -1;
                }
                try {
                    Closure* ctx = reinterpret_cast<Closure*>(closure);
                    std::visit(
                        Visitor{
                            [ctx, new_val](CppType& obj) {
                                obj.*(ctx->setter)(static_cast<Value>(
                                    reinterpret_borrow<Object>(new_val)
                                ));
                            },
                            [ctx, new_val](CppType* obj) {
                                obj->*(ctx->setter)(static_cast<Value>(
                                    reinterpret_borrow<Object>(new_val)
                                ));
                            },
                            [new_val](const CppType* obj) {
                                throw TypeError(
                                    "variable '" + Name + "' of type '" +
                                    impl::demangle(typeid(Wrapper).name()) +
                                    "' is immutable."
                                );
                            }
                        },
                        reinterpret_cast<CRTP*>(self)->m_cpp
                    );
                    return 0;
                } catch (...) {
                    Exception::to_python();
                    return -1;
                }
            };
            tp_getset.push_back({
                Name,
                +get,  // converts a stateless lambda to a function pointer
                +set,
                nullptr,  // doc
                &closure
            });
            skip = true;
        }

        /* Expose a C++-style static getter to Python using the `__getattr__()`
        slot of the metatype. */
        template <StaticStr Name, typename Return>
        void classproperty(Return(*getter)()) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_class_property = true,
            };
            static auto get = [](PyObject* cls, void* closure) -> PyObject* {
                try {
                    Return(*func)() = reinterpret_cast<Return(*)()>(closure);
                    if constexpr (std::is_lvalue_reference_v<Return>) {
                        if constexpr (impl::python_like<Return>) {
                            return Py_NewRef(ptr(func()));
                        } else {
                            return release(wrap(func()));
                        }
                    } else {
                        return release(as_object(func()));
                    }
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            };
            class_getters[Name] = {+get, reinterpret_cast<void*>(getter)};
        }

        /* Expose a C++-style static getter/setter pair to Python using the
        `__getattr__()` and `__setattr__()` slots of the metatype. */
        template <StaticStr Name, typename Return, typename Value>
        void classproperty(Return(*getter)(), void(*setter)(Value&&)) {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_class_property = true,
            };
            static struct Closure {
                Return(*getter)();
                void(*setter)(Value&&);
            } closure = {getter, setter};
            static auto get = [](PyObject* cls, void* closure) -> PyObject* {
                try {
                    Closure* ctx = reinterpret_cast<Closure*>(closure);
                    if constexpr (std::is_lvalue_reference_v<Return>) {
                        if constexpr (impl::python_like<Return>) {
                            return Py_NewRef(ptr(ctx->getter()));
                        } else {
                            return release(wrap(ctx->getter()));
                        }
                    } else {
                        return release(as_object(ctx->getter()));
                    }
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            };
            static auto set = [](PyObject* cls, PyObject* new_val, void* closure) -> int {
                if (new_val == nullptr) {
                    std::string msg = "variable '" + Name + "' of type '" +
                        impl::demangle(typeid(Wrapper).name()) +
                        "' cannot be deleted.";
                    PyErr_SetString(PyExc_TypeError, msg.c_str());
                    return -1;
                }
                try {
                    reinterpret_cast<Closure*>(closure)->setter(static_cast<Value>(
                        reinterpret_borrow<Object>(new_val)
                    ));
                    return 0;
                } catch (...) {
                    Exception::to_python();
                    return -1;
                }
            };
            class_getters[Name] = {+get, reinterpret_cast<void*>(&closure)};
            class_setters[Name] = {+set, reinterpret_cast<void*>(&closure)};
        }

        /* Expose a nested py::Object type to Python. */
        template <
            StaticStr Name,
            std::derived_from<Object> Cls,
            std::derived_from<Object>... Bases
        > requires (
            !impl::is_generic<Cls> &&
            (impl::has_type<Cls> && impl::is_type<Type<Cls>>) &&
            ((impl::has_type<Bases> && impl::is_type<Type<Bases>>) && ...)
        )
        void type(std::string&& doc = "") {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            static std::string docstring = std::move(doc);
            context[Name] = {
                .is_type = true,
                .callbacks = {
                    [](Type<Wrapper>& type) {
                        Module<ModName> mod = reinterpret_steal<Module<ModName>>(
                            PyType_GetModule(ptr(type))
                        );
                        if (ptr(mod) == nullptr) {
                            Exception::from_python();
                        }
                        Type<Cls> cls = Type<Cls>::__python__::__export__(mod);

                        if (PyObject_SetAttr(ptr(type), Name, ptr(cls))) {
                            Exception::from_python();
                        }

                        // insert into the module's C++ type map for fast lookup
                        // using C++ template syntax
                        using Mod = Module<ModName>::__python__;
                        reinterpret_cast<Mod*>(ptr(mod))->type_map[typeid(Cls)] =
                            reinterpret_cast<PyTypeObject*>(ptr(cls));

                        // insert into the global type map for use when generating
                        // C++ bindings from Python type hints
                        register_type<Cls>(
                            mod,
                            reinterpret_cast<PyTypeObject*>(ptr(cls))
                        );
                    }
                }
            };
        }

        /* Expose a nested and templated py::Object type to Python. */
        template <
            StaticStr Name,
            template <typename...> typename Cls
        >
        void type() {
            if (context.contains(Name)) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) +
                    "' already has an attribute named '" + Name + "'."
                );
            }
            context[Name] = {
                .is_type = true,
                .is_template_interface = true,
                .callbacks = {
                    [](Type<Wrapper>& type) {
                        Module<ModName> mod = reinterpret_steal<Module<ModName>>(
                            PyType_GetModule(ptr(type))
                        );
                        if (ptr(mod) == nullptr) {
                            Exception::from_python();
                        }
                        BertrandMeta stub = Meta::stub_type<Name, Cls>(mod);
                        if (PyObject_SetAttr(ptr(type), Name, ptr(stub))) {
                            Exception::from_python();
                        }
                    }
                }
            };
        }

        /* Expose a template instantiation of a nested py::Object type to Python. */
        template <
            StaticStr Name,
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
                    "No template interface found for type '" + Name + "' in "
                    "class '" + impl::demangle(typeid(Wrapper).name()) + "' "
                    "with specialization '" +
                    impl::demangle(typeid(Cls).name()) + "'.  Did you "
                    "forget to register the unspecialized template first?"
                );
            } else if (!it->second.is_template_interface) {
                throw AttributeError(
                    "Class '" + impl::demangle(typeid(Wrapper).name()) + "' "
                    "already has an attribute named '" + Name + "'."
                );
            }
            it->second.callbacks.push_back([](Type<Wrapper>& type) {
                // get the template interface with the same name
                BertrandMeta existing = reinterpret_steal<BertrandMeta>(
                    PyObject_GetAttr(
                        ptr(type),
                        impl::TemplateString<Name>::ptr
                    )
                );
                if (ptr(existing) == nullptr) {
                    Exception::from_python();
                }

                // call the type's __export__() method
                Module<ModName> mod = reinterpret_steal<Module<ModName>>(
                    PyType_GetModule(ptr(type))
                );
                if (ptr(mod) == nullptr) {
                    Exception::from_python();
                }
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
                    sizeof...(Bases) + 1,
                    ptr(Type<Bases>())...
                );
                if (key == nullptr) {
                    Exception::from_python();
                }
                if (PyDict_SetItem(
                    reinterpret_cast<typename Type<BertrandMeta>::__python__*>(
                        ptr(existing)
                    )->templates,
                    key,
                    ptr(cls)
                )) {
                    Py_DECREF(key);
                    Exception::from_python();
                }
                Py_DECREF(key);

                // insert into the module's C++ type map for fast lookup
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

        /// TODO: implement a template constraint that enforces that dunder methods
        /// always have the correct signature at compile time.

        /* Expose a C++ instance method to Python as an instance method, which can
        be overloaded from either side of the language boundary.  */
        template <StaticStr Name, typename Return, typename... Target>
        void method(Return(CRTP::*func)(Target...)) {
            // TODO: check for an existing overload set with the same name and
            // insert into it, or create a new one if none exists.  Then, call
            // PyObject_SetAttr() to insert the overload set into the class dict
            // or invoke a metaclass descriptor to handle internal slots.
        }

        /* Expose a C++ instance method to Python as an instance method, which can
        be overloaded from either side of the language boundary.  */
        template <StaticStr Name, typename Return, typename... Target>
        void method(Return(CppType::*func)(Target...)) {
            // TODO: check for an existing overload set with the same name and
            // insert into it, or create a new one if none exists.  Then, call
            // PyObject_SetAttr() to insert the overload set into the class dict
            // or invoke a metaclass descriptor to handle internal slots.

            // func is a pointer to a member function of CppType, which can be
            // called like this:
            //      obj.*func(std::forward<Args>(args)...);
        }

        /* Expose a C++ static method to Python as a class method, which can be
        overloaded from either side of the language boundary.  */
        template <StaticStr Name, typename Return, typename... Target>
        void classmethod(Return(*func)(Target...)) {

        }

        /* Expose a C++ static method to Python as a static method, which can be
        overloaded from either side of the language boundary.  */
        template <StaticStr Name, typename Return, typename... Target>
        void staticmethod(Return(*func)(Target...)) {

        }

        /* Finalize a type definition and produce a corresponding type object.  This
        method should always be called in the return statement of an `__export__()`
        script, which automates the import process.

        The argument is a bitmask of Python flags that control various aspects of the
        type's behavior.  By default, the final type supports inheritance, weak
        references, and possesses an instance dictionary, just like normal Python
        classes.  Note that the `Py_TPFLAGS_DEFAULT`, `Py_TPFLAGS_HEAPTYPE`, and
        `Py_TPFLAGS_HAVE_GC` flags are always set and cannot be overridden.  These are
        required for Bertrand types to function correctly with respect to the Python
        interpreter. */
        template <std::derived_from<Object>... Bases>
        Type<Wrapper> finalize(unsigned int tp_flags =
            Py_TPFLAGS_BASETYPE | Py_TPFLAGS_MANAGED_WEAKREF | Py_TPFLAGS_MANAGED_DICT
        ) {
            /// TODO: initialize the type object without any slots, then record them
            /// in the metaclass and replace with the slots defined above, and then
            /// call PyType_Modified().
            static unsigned int flags =
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE | Py_TPFLAGS_HAVE_GC;
            static std::vector<PyType_Slot> slots;
            static bool initialized = false;
            if (!initialized) {
                flags |= tp_flags;
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
                .name = typeid(Wrapper).name(),
                .basicsize = sizeof(CRTP),
                .itemsize = 0,
                .flags = flags,
                .slots = slots.data()
            };

            // instantiate the type object
            PyObject* bases = nullptr;
            if constexpr (sizeof...(Bases)) {
                bases = PyTuple_Pack(sizeof...(Bases), ptr(Type<Bases>())...);
                if (bases == nullptr) {
                    Exception::from_python();
                }
            }
            Type<Wrapper> cls = reinterpret_steal<Type<Wrapper>>(
                PyType_FromMetaclass(
                    reinterpret_cast<PyTypeObject*>(ptr(Type<BertrandMeta>())),
                    ptr(module),
                    &spec,
                    bases
                )
            );
            Py_XDECREF(bases);
            if (ptr(cls) == nullptr) {
                Exception::from_python();
            }

            // initialize the metaclass fields
            Meta* meta = reinterpret_cast<Meta*>(ptr(cls));
            meta->__construct__();
            meta->instancecheck = instancecheck;
            meta->subclasscheck = subclasscheck;
            meta->class_getters = std::move(class_getters);
            meta->class_setters = std::move(class_setters);

            // execute the callbacks to populate the type object
            for (auto&& [name, ctx] : context) {
                for (auto&& callback : ctx.callbacks) {
                    callback(cls);
                }
            }

            /// TODO: insert the type into the global type registry.

            return cls;
        }

    };

    template <typename CRTP, typename Wrapper, typename CppType>
    struct TypeTag::def : BaseDef<CRTP, Wrapper, CppType> {
        using Base = BaseDef<CRTP, Wrapper, CppType>;

    protected:
        using Variant = std::variant<CppType, CppType*, const CppType*>;

    public:
        using t_cpp = CppType;

        // TODO: the variant and visitor have to be defined separately

        static constexpr Origin __origin__ = Origin::CPP;
        static constexpr StaticStr __doc__ = [] {
            return (
                "A Bertrand-generated Python wrapper for the '" +
                impl::demangle(typeid(CppType).name()) + "' C++ type."
            );
        }();
        PyObject_HEAD
        Variant m_cpp;

        template <typename Begin, std::sentinel_for<Begin> End>
        using Iterator = TypeTag::Iterator<Begin, End>;

        /* tp_hash delegates to `std::hash` if it exists. */
        static Py_hash_t __hash__(CRTP* self) {
            try {
                return std::visit(Visitor{
                    [](const CppType& cpp) {
                        return std::hash<CppType>{}(cpp);
                    },
                    [](const CppType* cpp) {
                        return std::hash<CppType>{}(*cpp);
                    }
                }, self->m_cpp);
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /* tp_iter delegates to the type's begin() and end() iterators if they exist,
        and provides a thin Python wrapper around them that respects reference
        semantics and const-ness. */
        static PyObject* __iter__(CRTP* self) {
            try {
                PyObject* iter = std::visit(Visitor{
                    [self](CppType& cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::begin(std::declval<CppType>()));
                        using End = decltype(std::ranges::end(std::declval<CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::begin(cpp));
                        new (&iter->end) Iterator::End(std::ranges::end(cpp));
                        return iter;
                    },
                    [self](CppType* cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::begin(std::declval<CppType>()));
                        using End = decltype(std::ranges::end(std::declval<CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::begin(*cpp));
                        new (&iter->end) Iterator::End(std::ranges::end(*cpp));
                        return iter;
                    },
                    [self](const CppType* cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::begin(std::declval<const CppType>()));
                        using End = decltype(std::ranges::end(std::declval<const CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::begin(*cpp));
                        new (&iter->end) Iterator::End(std::ranges::end(*cpp));
                        return iter;
                    }
                }, self->m_cpp);
                PyObject_GC_Track(iter);
                return iter;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* __reversed__ delegates to the type's rbegin() and rend() iterators if they
        exist, and provides a thin Python wrapper around them that respects reference
        semantics and const-ness. */
        static PyObject* __reversed__(CRTP* self) {
            try {
                PyObject* iter = std::visit(Visitor{
                    [self](CppType& cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::rbegin(std::declval<CppType>()));
                        using End = decltype(std::ranges::rend(std::declval<CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::rbegin(cpp));
                        new (&iter->end) Iterator::End(std::ranges::rend(cpp));
                        return iter;
                    },
                    [self](CppType* cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::rbegin(std::declval<CppType>()));
                        using End = decltype(std::ranges::rend(std::declval<CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::rbegin(*cpp));
                        new (&iter->end) Iterator::End(std::ranges::rend(*cpp));
                        return iter;
                    },
                    [self](const CppType* cpp) -> PyObject* {
                        using Begin = decltype(std::ranges::rbegin(std::declval<const CppType>()));
                        using End = decltype(std::ranges::rend(std::declval<const CppType>()));
                        using Iterator = Iterator<Begin, End>;
                        PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
                            PyObject_GetAttrString(
                                reinterpret_cast<PyObject*>(Py_TYPE(self)),
                                typeid(Iterator).name()
                            )
                        );
                        if (iter_type == nullptr) {
                            Exception::from_python();
                        }
                        Iterator* iter = iter_type->tp_alloc(iter_type, 0);
                        Py_DECREF(iter_type);
                        if (iter == nullptr) {
                            Exception::from_python();
                        }
                        new (&iter->begin) Iterator::Begin(std::ranges::rbegin(*cpp));
                        new (&iter->end) Iterator::End(std::ranges::rend(*cpp));
                        return iter;
                    }
                }, self->m_cpp);
                PyObject_GC_Track(iter);
                return iter;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }


        /// TODO: __create__ should be a void member function that initializes the C++
        /// fields given arbitrary C++ arguments.  Separating it from the tp_alloc and
        /// reinterpret_steal components allows for much more flexibility.  I might
        /// also be able to merge this with the metaclass's initialize() method.
        /// BaseDef should provide a default implementation that does nothing, and the
        /// user should always call the parent class's method first in their own
        /// implementation.  There should also be a corresponding __destroy__ method
        /// that should again call the parent class's method first, and then clean up
        /// any C++ resources that were allocated in __create__.  This allows the
        /// dealloc logic to be separated from GC tracking and heap type boilerplate.
        /// -> What to do with traverse and clear?  These would have to be implemented
        /// by hand, and should always call the parent class's method first.

        /* C++ constructor that forwards to the wrapped object's constructor.  This is
        called from the Wrapper's C++ constructors to convert a C++ value into a Python
        object outside the Python-level __new__/__init__ sequence.  It can be overridden
        if the object type includes more members than just the wrapped C++ object, but
        this will generally never occur.  Any required fields should be contained
        within the C++ object itself. */
        template <typename... Args>
        static Wrapper __construct__(Args&&... args) {
            PyTypeObject* type = reinterpret_cast<PyTypeObject*>(ptr(Type<Wrapper>()));
            Object self = reinterpret_steal<Object>(type->tp_alloc(type, 0));
            if (ptr(self) == nullptr) {
                Exception::from_python();
            }
            CRTP* obj = reinterpret_cast<CRTP*>(ptr(self));
            new (&obj->m_cpp) CppType(std::forward<Args>(args)...);
            return reinterpret_steal<Wrapper>(release(self));
        }

        /* tp_repr demangles the exact C++ type name and includes memory address
        similar to Python. */
        static PyObject* __repr__(CRTP* self) {
            try {
                std::string str = std::visit(Visitor{
                    [](const CppType& cpp) {
                        return repr(cpp);
                    },
                    [](const CppType* cpp) {
                        return repr(*cpp);
                    }
                }, self->m_cpp);
                PyObject* result = PyUnicode_FromStringAndSize(
                    str.c_str(),
                    str.size()
                );
                if (result == nullptr) {
                    return nullptr;
                }
                return result;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* tp_dealloc calls the ordinary C++ destructor. */
        static void __dealloc__(CRTP* self) {
            PyObject_GC_UnTrack(self);  // required for heap types
            self->~CRTP();  // cleans up all C++ resources
            PyTypeObject* type = Py_TYPE(self);
            type->tp_free(self);
            Py_DECREF(type);  // required for heap types
        }

        // TODO: maybe there needs to be some handling to make sure that the container
        // yields specifically lvalue references to Python objects?  I might also need
        // to const_cast them in the __clear__ method in order to properly release
        // everything.

        /* tp_traverse registers any Python objects that are owned by the C++ object
        with Python's cyclic garbage collector. */
        static int __traverse__(CRTP* self, visitproc visit, void* arg) {
            if constexpr (
                impl::iterable<CppType> &&
                std::derived_from<std::decay_t<impl::iter_type<CppType>>, Object>
            ) {
                if (std::holds_alternative<CppType>(self->m_cpp)) {
                    for (auto&& item : std::get<CppType>(self->m_cpp)) {
                        Py_VISIT(ptr(item));
                    }
                }
            }
            Py_VISIT(Py_TYPE(self));  // required for heap types
            return 0;
        }

        /* tp_clear breaks possible reference cycles for Python objects that are owned
        by the C++ object.  Note that special care has to be taken to avoid
        double-freeing any Python objects between this method and the `__dealloc__()`
        destructor. */
        static int __clear__(CRTP* self) {
            if constexpr (
                impl::iterable<CppType> &&
                std::derived_from<std::decay_t<impl::iter_type<CppType>>, Object>
            ) {
                if (std::holds_alternative<CppType>(self->m_cpp)) {
                    /// NOTE: In order to avoid a double free, we release() all of the
                    /// container's items here, so that when their destructors are called
                    /// normally in __dealloc__(), they see a null pointer and do nothing.
                    for (auto&& item : std::get<CppType>(self->m_cpp)) {
                        Py_XDECREF(release(item));
                    }
                }
            }
            return 0;
        }

    private:
        inline static std::vector<PyMethodDef> tp_methods;
        inline static std::vector<PyGetSetDef> tp_getset;
        inline static std::vector<PyMemberDef> tp_members;

        template <typename T>
            requires (
                __as_object__<T>::enable &&
                impl::originates_from_cpp<typename __as_object__<T>::type> &&
                std::same_as<T, typename __as_object__<T>::type::__python__::CppType>
            )
        friend auto wrap(const T& obj) -> __as_object__<T>::type;
        template <typename T>
            requires (
                __as_object__<T>::enable &&
                impl::originates_from_cpp<typename __as_object__<T>::type> &&
                std::same_as<T, typename __as_object__<T>::type::__python__::CppType>
            )
        friend auto wrap(T& obj) -> __as_object__<T>::type;
        template <typename T> requires (impl::cpp_or_originates_from_cpp<T>)
        friend auto& unwrap(T& obj);
        template <typename T> requires (impl::cpp_or_originates_from_cpp<T>)
        friend auto& unwrap(const T& obj);

        /* Implements py::wrap() for immutable references. */
        static Wrapper _wrap(const CppType& cpp) {
            Type<Wrapper> type;
            PyTypeObject* type_ptr = reinterpret_cast<PyTypeObject*>(ptr(type));
            PyObject* self = type_ptr->tp_alloc(type_ptr, 0);
            if (self == nullptr) {
                Exception::from_python();
            }
            new (&reinterpret_cast<CRTP*>(self)->m_cpp) Variant(&cpp);
            return reinterpret_steal<Wrapper>(self);
        }

        /* Implements py::wrap() for mutable references. */
        static Wrapper _wrap(CppType& cpp) {
            Type<Wrapper> type;
            PyTypeObject* type_ptr = reinterpret_cast<PyTypeObject*>(ptr(type));
            PyObject* self = type_ptr->tp_alloc(type_ptr, 0);
            if (self == nullptr) {
                Exception::from_python();
            }
            new (&reinterpret_cast<CRTP*>(self)->m_cpp) Variant(&cpp);
            return reinterpret_steal<Wrapper>(self);
        }

        /* Implements py::unwrap() for immutable wrappers. */
        static const CppType& _unwrap(const Wrapper& obj) {
            return std::visit(Visitor{
                [](const CppType& cpp) {
                    return cpp;
                },
                [](const CppType* cpp) {
                    return *cpp;
                }
            }, reinterpret_cast<CRTP*>(ptr(obj))->m_cpp);
        }

        /* Implements py::unwrap() for mutable wrappers. */
        static CppType& _unwrap(Wrapper& obj) {
            return std::visit(Visitor{
                [](CppType& cpp) {
                    return cpp;
                },
                [](CppType* cpp) {
                    return *cpp;
                },
                [&obj](const CppType* cpp) {
                    throw TypeError(
                        "requested a mutable reference to const object: " +
                        repr(obj)
                    );
                }
            }, reinterpret_cast<CRTP*>(ptr(obj))->m_cpp);
        }

    };

}


/// NOTE: all types must manually define __as_object__ as they are generated, since it
/// can't be deduced any other way.


/// TODO: what about isinstance/issubclass?


template <typename T, impl::originates_from_cpp Self>
struct __isinstance__<T, Self>                              : Returns<bool> {
    /// TODO: is this default behavior?
    static bool operator()(const T& obj) {
        return PyType_IsSubtype(Py_TYPE(ptr(obj)), ptr(Type<Self>()));
    }
};


template <impl::originates_from_cpp Self, typename T>
    requires (std::convertible_to<T, impl::cpp_type<Self>>)
struct __init__<Self, T>                                    : Returns<Self> {
    static Self operator()(T&& value) {
        return Type<Self>::__python__::__construct__(std::forward<T>(value));
    }
};


template <impl::originates_from_cpp Self, typename... Args>
    requires (std::constructible_from<impl::cpp_type<Self>, Args...>)
struct __explicit_init__<Self, Args...>                     : Returns<Self> {
    static Self operator()(Args&&... args) {
        return Type<Self>::__python__::__construct__(std::forward<Args>(args)...);
    }
};


template <impl::originates_from_cpp Self, typename T>
    requires (std::convertible_to<impl::cpp_type<Self>, T>)
struct __cast__<Self, T>                                    : Returns<T> {
    static T operator()(const Self& self) {
        return unwrap(self);
    }
};


template <impl::originates_from_cpp Self, typename T>
    requires (impl::explicitly_convertible_to<impl::cpp_type<Self>, T>)
struct __explicit_cast__<Self, T>                           : Returns<T> {
    static T operator()(const Self& self) {
        return static_cast<T>(unwrap(self));
    }
};


template <impl::originates_from_cpp Self, typename... Args>
    requires (std::is_invocable_v<impl::cpp_type<Self>, Args...>)
struct __call__<Self, Args...> : Returns<std::invoke_result_t<impl::cpp_type<Self>, Args...>> {};


/// TODO: __getattr__/__setattr__/__delattr__ must be defined for each type, and cannot
/// be deduced here.


template <impl::originates_from_cpp Self, typename... Key>
    requires (impl::supports_lookup<impl::cpp_type<Self>, Key...>)
struct __getitem__<Self, Key...> : Returns<impl::lookup_type<impl::cpp_type<Self>, Key...>> {
    template <typename... Ks>
    static decltype(auto) operator()(const Self& self, Ks&&... key) {
        return unwrap(self)[std::forward<Ks>(key)...];
    }
};


template <impl::originates_from_cpp Self, typename Value, typename... Key>
    requires (impl::supports_item_assignment<impl::cpp_type<Self>, Value, Key...>)
struct __setitem__<Self, Value, Key...> : Returns<void> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_size<impl::cpp_type<Self>>)
struct __len__<Self> : Returns<size_t> {};


template <impl::originates_from_cpp Self>
    requires (impl::iterable<impl::cpp_type<Self>>)
struct __iter__<Self> : Returns<impl::iter_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self>
    requires (impl::reverse_iterable<impl::cpp_type<Self>>)
struct __reversed__<Self> : Returns<impl::reverse_iter_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self, typename Key>
    requires (impl::has_contains<impl::cpp_type<Self>, Key>)
struct __contains__<Self, Key> : Returns<bool> {};


template <impl::originates_from_cpp Self>
    requires (impl::hashable<impl::cpp_type<Self>>)
struct __hash__<Self> : Returns<size_t> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_abs<impl::cpp_type<Self>>)
struct __abs__<Self> : Returns<impl::abs_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_pos<impl::cpp_type<Self>>)
struct __pos__<Self> : Returns<impl::pos_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_neg<impl::cpp_type<Self>>)
struct __neg__<Self> : Returns<impl::neg_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_preincrement<impl::cpp_type<Self>>)
struct __increment__<Self> : Returns<impl::preincrement_type<impl::cpp_type<Self>>> {};


template <impl::originates_from_cpp Self>
    requires (impl::has_predecrement<impl::cpp_type<Self>>)
struct __decrement__<Self> : Returns<impl::predecrement_type<impl::cpp_type<Self>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_lt<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __lt__<L, R> : Returns<impl::lt_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_le<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __le__<L, R> : Returns<impl::le_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_eq<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __eq__<L, R> : Returns<impl::eq_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_ne<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ne__<L, R> : Returns<impl::ne_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_ge<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ge__<L, R> : Returns<impl::ge_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_gt<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __gt__<L, R> : Returns<impl::gt_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_add<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __add__<L, R> : Returns<impl::add_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_iadd<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __iadd__<L, R> : Returns<impl::iadd_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_sub<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __sub__<L, R> : Returns<impl::sub_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_isub<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __isub__<L, R> : Returns<impl::isub_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_mul<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __mul__<L, R> : Returns<impl::mul_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_imul<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __imul__<L, R> : Returns<impl::imul_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_truediv<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __truediv__<L, R> : Returns<impl::truediv_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_itruediv<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __itruediv__<L, R> : Returns<impl::itruediv_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_mod<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __mod__<L, R> : Returns<impl::mod_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_imod<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __imod__<L, R> : Returns<impl::imod_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_pow<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __pow__<L, R> : Returns<impl::pow_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_lshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __lshift__<L, R> : Returns<impl::lshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_ilshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ilshift__<L, R> : Returns<impl::ilshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_rshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __rshift__<L, R> : Returns<impl::rshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_irshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __irshift__<L, R> : Returns<impl::irshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_and<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __and__<L, R> : Returns<impl::and_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_iand<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __iand__<L, R> : Returns<impl::iand_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_or<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __or__<L, R> : Returns<impl::or_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_ior<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ior__<L, R> : Returns<impl::ior_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_xor<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __xor__<L, R> : Returns<impl::xor_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::originates_from_cpp<L> || impl::originates_from_cpp<R>) &&
        impl::has_ixor<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ixor__<L, R> : Returns<impl::ixor_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


////////////////////
////    CODE    ////
////////////////////


template <>
struct Type<Code>;


template <>
struct Interface<Type<Code>> {
    [[nodiscard]] static Code compile(const std::string& source);
    [[nodiscard]] static Py_ssize_t line_number(const auto& self) noexcept;
    [[nodiscard]] static Py_ssize_t argcount(const auto& self) noexcept;
    [[nodiscard]] static Py_ssize_t posonlyargcount(const auto& self) noexcept;
    [[nodiscard]] static Py_ssize_t kwonlyargcount(const auto& self) noexcept;
    [[nodiscard]] static Py_ssize_t nlocals(const auto& self) noexcept;
    [[nodiscard]] static Py_ssize_t stacksize(const auto& self) noexcept;
    [[nodiscard]] static int flags(const auto& self) noexcept;

    /// NOTE: these are defined in __init__.h
    [[nodiscard]] static Str filename(const auto& self);
    [[nodiscard]] static Str name(const auto& self);
    [[nodiscard]] static Str qualname(const auto& self);
    [[nodiscard]] static Tuple<Str> varnames(const auto& self);
    [[nodiscard]] static Tuple<Str> cellvars(const auto& self);
    [[nodiscard]] static Tuple<Str> freevars(const auto& self);
    [[nodiscard]] static Bytes bytecode(const auto& self);
    [[nodiscard]] static Tuple<Object> consts(const auto& self);
    [[nodiscard]] static Tuple<Str> names(const auto& self);
};


template <>
struct Type<Code> : Object, Interface<Type<Code>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(
                reinterpret_cast<PyObject*>(&PyCode_Type)
            );
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline Code Interface<Type<Code>>::compile(const std::string& source) {
    return Code::compile(source);
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::line_number(const auto& self) noexcept {
    return self.line_number;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::argcount(const auto& self) noexcept {
    return self.argcount;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::posonlyargcount(const auto& self) noexcept {
    return self.posonlyargcount;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::kwonlyargcount(const auto& self) noexcept {
    return self.kwonlyargcount;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::nlocals(const auto& self) noexcept {
    return self.nlocals;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::stacksize(const auto& self) noexcept {
    return self.stacksize;
}
[[nodiscard]] inline int Interface<Type<Code>>::flags(const auto& self) noexcept {
    return self.flags;
}


/////////////////////
////    FRAME    ////
/////////////////////


template <>
struct Type<Frame>;


template <>
struct Interface<Type<Frame>> {
    [[nodiscard]] static std::string to_string(const auto& self);
    [[nodiscard]] static std::optional<Code> code(const auto& self);
    [[nodiscard]] static std::optional<Frame> back(const auto& self);
    [[nodiscard]] static size_t line_number(const auto& self);
    [[nodiscard]] static size_t last_instruction(const auto& self);
    [[nodiscard]] static std::optional<Object> generator(const auto& self);

    /// NOTE: these are defined in __init__.h
    [[nodiscard]] static Object get(const auto& self, const Str& name);
    [[nodiscard]] static Dict<Str, Object> builtins(const auto& self);
    [[nodiscard]] static Dict<Str, Object> globals(const auto& self);
    [[nodiscard]] static Dict<Str, Object> locals(const auto& self);
};


template <>
struct Type<Frame> : Object, Interface<Type<Frame>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(
                reinterpret_cast<PyObject*>(&PyFrame_Type)
            );
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline std::string Interface<Type<Frame>>::to_string(const auto& self) {
    return self.to_string();
}
[[nodiscard]] inline std::optional<Code> Interface<Type<Frame>>::code(const auto& self) {
    return self.code;
}
[[nodiscard]] inline std::optional<Frame> Interface<Type<Frame>>::back(const auto& self) {
    return self.back;
}
[[nodiscard]] inline size_t Interface<Type<Frame>>::line_number(const auto& self) {
    return self.line_number;
}
[[nodiscard]] inline size_t Interface<Type<Frame>>::last_instruction(const auto& self) {
    return self.last_instruction;
}
[[nodiscard]] inline std::optional<Object> Interface<Type<Frame>>::generator(const auto& self) {
    return self.generator;
}


/////////////////////////
////    TRACEBACK    ////
/////////////////////////


template <>
struct Type<Traceback>;


template <>
struct Interface<Type<Traceback>> {
    [[nodiscard]] static std::string to_string(const auto& self);
};


template <>
struct Type<Traceback> : Object, Interface<Type<Traceback>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(
                reinterpret_cast<PyObject*>(&PyTraceBack_Type)
            );
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline std::string Interface<Type<Traceback>>::to_string(const auto& self) {
    return self.to_string();
}


///////////////////////////////
////    EXCEPTION TYPES    ////
///////////////////////////////


template <>
struct Type<Exception>;


template <>
struct Interface<Type<Exception>> {
    [[noreturn, clang::noinline]] static void from_python();  // defined in __init__.h
    static void to_python();
};


template <>
struct Type<Exception> : Object, Interface<Type<Exception>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(PyExc_Exception);
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


inline void Interface<Type<Exception>>::to_python() {
    Exception::to_python();
}


#define BUILTIN_EXCEPTION_TYPE(CLS, BASE, PYTYPE)                                       \
    template <>                                                                         \
    struct Type<CLS>;                                                                   \
                                                                                        \
    template <>                                                                         \
    struct Interface<Type<CLS>> : Interface<Type<BASE>> {};                             \
                                                                                        \
    template <>                                                                         \
    struct Type<CLS> : Object, Interface<Type<CLS>>, impl::TypeTag {                    \
        struct __python__ : TypeTag::def<__python__, CLS> {                             \
            static Type __import__() {                                                  \
                return reinterpret_borrow<Type>(PYTYPE);                                \
            }                                                                           \
        };                                                                              \
                                                                                        \
        Type(PyObject* p, borrowed_t t) : Object(p, t) {}                               \
        Type(PyObject* p, stolen_t t) : Object(p, t) {}                                 \
                                                                                        \
        template <typename... Args>                                                     \
            requires (implicit_ctor<Type>::template enable<Args...>)                    \
        Type(Args&&... args) : Object(                                                  \
            implicit_ctor<Type>{},                                                      \
            std::forward<Args>(args)...                                                 \
        ) {}                                                                            \
                                                                                        \
        template <typename... Args>                                                     \
            requires (explicit_ctor<Type>::template enable<Args...>)                    \
        explicit Type(Args&&... args) : Object(                                         \
            explicit_ctor<Type>{},                                                      \
            std::forward<Args>(args)...                                                 \
        ) {}                                                                            \
    };


BUILTIN_EXCEPTION_TYPE(ArithmeticError, Exception, PyExc_ArithmeticError)
    BUILTIN_EXCEPTION_TYPE(FloatingPointError, ArithmeticError, PyExc_FloatingPointError)
    BUILTIN_EXCEPTION_TYPE(OverflowError, ArithmeticError, PyExc_OverflowError)
    BUILTIN_EXCEPTION_TYPE(ZeroDivisionError, ArithmeticError, PyExc_ZeroDivisionError)
BUILTIN_EXCEPTION_TYPE(AssertionError, Exception, PyExc_AssertionError)
BUILTIN_EXCEPTION_TYPE(AttributeError, Exception, PyExc_AttributeError)
BUILTIN_EXCEPTION_TYPE(BufferError, Exception, PyExc_BufferError)
BUILTIN_EXCEPTION_TYPE(EOFError, Exception, PyExc_EOFError)
BUILTIN_EXCEPTION_TYPE(ImportError, Exception, PyExc_ImportError)
    BUILTIN_EXCEPTION_TYPE(ModuleNotFoundError, ImportError, PyExc_ModuleNotFoundError)
BUILTIN_EXCEPTION_TYPE(LookupError, Exception, PyExc_LookupError)
    BUILTIN_EXCEPTION_TYPE(IndexError, LookupError, PyExc_IndexError)
    BUILTIN_EXCEPTION_TYPE(KeyError, LookupError, PyExc_KeyError)
BUILTIN_EXCEPTION_TYPE(MemoryError, Exception, PyExc_MemoryError)
BUILTIN_EXCEPTION_TYPE(NameError, Exception, PyExc_NameError)
    BUILTIN_EXCEPTION_TYPE(UnboundLocalError, NameError, PyExc_UnboundLocalError)
BUILTIN_EXCEPTION_TYPE(OSError, Exception, PyExc_OSError)
    BUILTIN_EXCEPTION_TYPE(BlockingIOError, OSError, PyExc_BlockingIOError)
    BUILTIN_EXCEPTION_TYPE(ChildProcessError, OSError, PyExc_ChildProcessError)
    BUILTIN_EXCEPTION_TYPE(ConnectionError, OSError, PyExc_ConnectionError)
        BUILTIN_EXCEPTION_TYPE(BrokenPipeError, ConnectionError, PyExc_BrokenPipeError)
        BUILTIN_EXCEPTION_TYPE(ConnectionAbortedError, ConnectionError, PyExc_ConnectionAbortedError)
        BUILTIN_EXCEPTION_TYPE(ConnectionRefusedError, ConnectionError, PyExc_ConnectionRefusedError)
        BUILTIN_EXCEPTION_TYPE(ConnectionResetError, ConnectionError, PyExc_ConnectionResetError)
    BUILTIN_EXCEPTION_TYPE(FileExistsError, OSError, PyExc_FileExistsError)
    BUILTIN_EXCEPTION_TYPE(FileNotFoundError, OSError, PyExc_FileNotFoundError)
    BUILTIN_EXCEPTION_TYPE(InterruptedError, OSError, PyExc_InterruptedError)
    BUILTIN_EXCEPTION_TYPE(IsADirectoryError, OSError, PyExc_IsADirectoryError)
    BUILTIN_EXCEPTION_TYPE(NotADirectoryError, OSError, PyExc_NotADirectoryError)
    BUILTIN_EXCEPTION_TYPE(PermissionError, OSError, PyExc_PermissionError)
    BUILTIN_EXCEPTION_TYPE(ProcessLookupError, OSError, PyExc_ProcessLookupError)
    BUILTIN_EXCEPTION_TYPE(TimeoutError, OSError, PyExc_TimeoutError)
BUILTIN_EXCEPTION_TYPE(ReferenceError, Exception, PyExc_ReferenceError)
BUILTIN_EXCEPTION_TYPE(RuntimeError, Exception, PyExc_RuntimeError)
    BUILTIN_EXCEPTION_TYPE(NotImplementedError, RuntimeError, PyExc_NotImplementedError)
    BUILTIN_EXCEPTION_TYPE(RecursionError, RuntimeError, PyExc_RecursionError)
BUILTIN_EXCEPTION_TYPE(StopAsyncIteration, Exception, PyExc_StopAsyncIteration)
BUILTIN_EXCEPTION_TYPE(StopIteration, Exception, PyExc_StopIteration)
BUILTIN_EXCEPTION_TYPE(SyntaxError, Exception, PyExc_SyntaxError)
    BUILTIN_EXCEPTION_TYPE(IndentationError, SyntaxError, PyExc_IndentationError)
        BUILTIN_EXCEPTION_TYPE(TabError, IndentationError, PyExc_TabError)
BUILTIN_EXCEPTION_TYPE(SystemError, Exception, PyExc_SystemError)
BUILTIN_EXCEPTION_TYPE(TypeError, Exception, PyExc_TypeError)
BUILTIN_EXCEPTION_TYPE(ValueError, Exception, PyExc_ValueError)
    BUILTIN_EXCEPTION_TYPE(UnicodeError, ValueError, PyExc_UnicodeError)
        // BUILTIN_EXCEPTION_TYPE(UnicodeDecodeError, PyExc_UnicodeDecodeError)
        // BUILTIN_EXCEPTION_TYPE(UnicodeEncodeError, PyExc_UnicodeEncodeError)
        // BUILTIN_EXCEPTION_TYPE(UnicodeTranslateError, PyExc_UnicodeTranslateError)


#undef BUILTIN_EXCEPTION_TYPE


template <>
struct Type<UnicodeDecodeError>;


template <>
struct Interface<Type<UnicodeDecodeError>> : Interface<Type<UnicodeError>> {
    [[nodiscard]] static std::string encoding(const auto& self);
    [[nodiscard]] static std::string object(const auto& self);
    [[nodiscard]] static Py_ssize_t start(const auto& self);
    [[nodiscard]] static Py_ssize_t end(const auto& self);
    [[nodiscard]] static std::string reason(const auto& self);
};


template <>
struct Type<UnicodeDecodeError> : Object, Interface<Type<UnicodeDecodeError>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(PyExc_UnicodeDecodeError);
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline std::string Interface<Type<UnicodeDecodeError>>::encoding(
    const auto& self
) {
    return self.encoding;
}
[[nodiscard]] inline std::string Interface<Type<UnicodeDecodeError>>::object(
    const auto& self
) {
    return self.object;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeDecodeError>>::start(
    const auto& self
) {
    return self.start;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeDecodeError>>::end(
    const auto& self
) {
    return self.end;
}
[[nodiscard]] inline std::string Interface<Type<UnicodeDecodeError>>::reason(
    const auto& self
) {
    return self.reason;
}


template <>
struct Type<UnicodeEncodeError>;


template <>
struct Interface<Type<UnicodeEncodeError>> : Interface<Type<UnicodeError>> {
    [[nodiscard]] static std::string encoding(const auto& self);
    [[nodiscard]] static std::string object(const auto& self);
    [[nodiscard]] static Py_ssize_t start(const auto& self);
    [[nodiscard]] static Py_ssize_t end(const auto& self);
    [[nodiscard]] static std::string reason(const auto& self);
};


template <>
struct Type<UnicodeEncodeError> : Object, Interface<Type<UnicodeEncodeError>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(PyExc_UnicodeEncodeError);
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline std::string Interface<Type<UnicodeEncodeError>>::encoding(
    const auto& self
) {
    return self.encoding;
}
[[nodiscard]] inline std::string Interface<Type<UnicodeEncodeError>>::object(
    const auto& self
) {
    return self.object;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeEncodeError>>::start(
    const auto& self
) {
    return self.start;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeEncodeError>>::end(
    const auto& self
) {
    return self.end;
}
[[nodiscard]] inline std::string Interface<Type<UnicodeEncodeError>>::reason(
    const auto& self
) {
    return self.reason;
}


template <>
struct Type<UnicodeTranslateError>;


template <>
struct Interface<Type<UnicodeTranslateError>> : Interface<Type<UnicodeError>> {
    [[nodiscard]] static std::string object(const auto& self);
    [[nodiscard]] static Py_ssize_t start(const auto& self);
    [[nodiscard]] static Py_ssize_t end(const auto& self);
    [[nodiscard]] static std::string reason(const auto& self);
};


template <>
struct Type<UnicodeTranslateError> : Object, Interface<Type<UnicodeTranslateError>>, impl::TypeTag {
    struct __python__ : TypeTag::def<__python__, Type> {
        static Type __import__() {
            return reinterpret_borrow<Type>(PyExc_UnicodeTranslateError);
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


[[nodiscard]] inline std::string Interface<Type<UnicodeTranslateError>>::object(
    const auto& self
) {
    return self.object;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeTranslateError>>::start(
    const auto& self
) {
    return self.start;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<UnicodeTranslateError>>::end(
    const auto& self
) {
    return self.end;
}
[[nodiscard]] inline std::string Interface<Type<UnicodeTranslateError>>::reason(
    const auto& self
) {
    return self.reason;
}


}  // namespace py


#endif
