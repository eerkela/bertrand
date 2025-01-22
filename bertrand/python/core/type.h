#ifndef BERTRAND_PYTHON_CORE_TYPE_H
#define BERTRAND_PYTHON_CORE_TYPE_H

#include "declarations.h"
#include "object.h"
#include "code.h"
#include "except.h"
#include "ops.h"


/// TODO: maybe what I need to do is split Type<>, BertrandMeta, Object::bindings into
/// separate headers.  type.h would be included just before ops.h, meta.h after
/// access.h and func.h, and then Object::bindings could be either placed into a bind.h
/// header included after module.h, or it can be defined later on in core.h as part
/// of the forward declarations.  That last option is probably the best, since it
/// could centralize all the forward declarations (at this level anyways) into a single
/// location.


namespace py {


////////////////////
////    TYPE    ////
////////////////////


/* `Type<Object>` (the default specialization) refers to a dynamic type, which can be
used polymorphically to represent the type of any Python object.  More specialized
types can always be implicitly converted to this type, but doing the opposite incurs a
runtime `issubclass()` check, and can potentially raise a `TypeError`, just like
`py::Object`. */
template <>
struct Type<Object> : Object, interface<Type<Object>> {
    struct __python__ : def<__python__, Type>, PyTypeObject {   
        static Type __import__() {
            return borrow<Type>(
                reinterpret_cast<PyObject*>(&PyBaseObject_Type)
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


/* Allow `Type<T>` to be templated on arbitrary C++ types by redirecting to their
Python equivalents (py::Type<int>() == py::Type<py::Int>()). */
template <typename T>
    requires (
        !std::derived_from<std::remove_cvref_t<T>, Object> &&
        __object__<std::remove_cvref_t<T>>::enable
    )
struct Type<T> : Type<typename __object__<std::remove_cvref_t<T>>::type> {};


/* Explicitly call `Type(obj)` to deduce the Python type of an arbitrary object. */
template <typename T> requires (__object__<T>::enable)
explicit Type(const T&) -> Type<typename __object__<T>::type>;


/* All types are default-constructible by invoking the `__import__()` hook. */
template <typename T>
struct __init__<Type<T>> : returns<Type<T>> {
    static auto operator()() {
        return Type<T>::__python__::__import__();
    }
};


/* Metaclasses are represented as Types of Types, and are constructible by recursively
calling `Py_TYPE(cls)` on the inner Type until we reach a concrete implementation. */
template <typename T>
struct __init__<Type<Type<T>>> : returns<Type<Type<T>>> {
    static auto operator()() {
        return borrow<Type<Type<T>>>(reinterpret_cast<PyObject*>(
            Py_TYPE(ptr(Type<T>()))
        ));
    }
};


/* Implement the CTAD guide by default-initializing the corresponding Type. */
template <typename T> requires (__object__<T>::enable)
struct __explicit_init__<Type<typename __object__<T>::type>, T> :
    returns<Type<typename __object__<T>::type>>
{
    static auto operator()(const T& obj) {
        return Type<typename __object__<T>::type>();
    }
};


/// NOTE: additional metaclass constructors for py::Type are defined in common.h


/* Calling a Type is the same as invoking the inner type's constructor. */
template <typename T, typename... Args> requires (std::constructible_from<T, Args...>)
struct __call__<Type<T>, Args...> : returns<T> {
    static auto operator()(const Type<T>& self, Args&&... args) {
        return T(std::forward<Args>(args)...);
    }
};


/* `isinstance()` is already implemented for all `Type<T>` specializations by
recurring on the templated type. */
template <typename T, typename Cls>
struct __isinstance__<T, Type<Cls>> : returns<bool> {
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


/* `issubclass()` is already implemented for all `Type<T>` specializations by
recurring on the templated type. */
template <typename T, typename Cls>
struct __issubclass__<T, Type<Cls>> : returns<bool> {
    static constexpr bool operator()() { return issubclass<T, Cls>(); }
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


/* Implicitly convert the Type of a subclass into one of its parent classes or an
equivalent C++ alias. */
template <typename From, typename To> requires (
    std::derived_from<
        interface<typename __object__<From>::type>,
        interface<typename __object__<To>::type>
    >
)
struct __cast__<Type<From>, Type<To>> : returns<Type<To>> {
    static auto operator()(const Type<From>& from) {
        return borrow<Type<To>>(ptr(from));
    }
    static auto operator()(Type<From>&& from) {
        return steal<Type<To>>(release(from));
    }
};


/* Implicitly convert the Type of a parent class into one of its subclasses by
applying a runtime `issubclass()` check. */
template <typename From, typename To> requires (
    !std::same_as<
        typename __object__<To>::type,
        typename __object__<From>::type
    > &&
    std::derived_from<
        interface<typename __object__<To>::type>,
        interface<typename __object__<From>::type>
    >
)
struct __cast__<Type<From>, Type<To>> : returns<Type<To>> {
    static auto operator()(const Type<From>& from) {
        if (issubclass<typename __object__<To>::type>(from)) {
            return borrow<Type<To>>(ptr(from));
        } else {
            throw TypeError(
                "cannot convert Python type from '" + repr(from) + "' to '" +
                repr(Type<To>()) + "'"
            );
        }
    }
    static auto operator()(Type<From>&& from) {
        if (issubclass<typename __object__<To>::type>(from)) {
            return steal<Type<To>>(release(from));
        } else {
            throw TypeError(
                "cannot convert Python type from '" + repr(from) + "' to '" +
                repr(Type<To>()) + "'"
            );
        }
    }
};


////////////////////
////    META    ////
////////////////////


template <>
struct Type<BertrandMeta>;


template <>
struct interface<BertrandMeta> {};


/// TODO: metaclass and/or bindings should insert a default tp_repr slot for all types,
/// which returns the demangled type name.  This is in addition to the __repr__ slot
/// for the type object itself, which does something similar.


/* Bertrand's metaclass, which is used to expose C++ classes to Python and simplify the
binding process.  Any type which includes an `__export__()` method will be exposed as
an instance of this class. */
struct BertrandMeta : Object, interface<BertrandMeta> {
    struct __python__ : def<__python__, BertrandMeta>, PyTypeObject {
        static constexpr static_str __doc__ =
R"doc(A shared metaclass for all Bertrand extension types.

Notes
-----
This metaclass effectively identifies all types that originate from C++, and is used to
expose a variety of C++ features to Python in a way that conforms to the idioms of both
languages.  This includes (but is not limited to):

    -   Method overloading for built-in Python slots like `__init__`, `__add__`, etc,
        which are implemented according to Python's dunder interface.
    -   Access to C++ template instantiations via Python type subscription.  This is
        done by indexing an instance of the metaclass, requesting its length, or
        iterating over it in a loop.
    -   Automatic demangling of the type's C++ name where possible, which improves the
        readability of error messages and other diagnostic output.
    -   A common interface for class-level variables and properties, which can share
        state between Python and C++.

Performing an `isinstance()` check against this type will return `True` if and only if
the candidate type was exposed to Python by way of Bertrand's binding interface.
Instances of this class cannot be created any other way.)doc";

        /// TODO: instancecheck and subclasscheck don't need to/shouldn't call the C++
        /// control structs, since doing so is asking for an infinite recursion.  The
        /// two should always remain separate, and you'd just overload
        /// __instancecheck__/__subclasscheck__ where necessary to make this work.
        /// -> iirc, the primary reason I wrote this was to account for template
        /// interfaces, but that's probably not necessary anymore since all types
        /// inherit from their interface like normal.

        /* The `instancecheck` and `subclasscheck` function pointers are used to back
        the metaclass's Python-level `__instancecheck__()` and `__subclasscheck()__`
        methods, and are automatically set during construction for each type. */
        bool(*_instancecheck)(__python__*, PyObject*) = nullptr;
        bool(*_subclasscheck)(__python__*, PyObject*) = nullptr;

        /* C++ needs a few extra hooks to accurately model class-level variables in a
        way that shares state with Python.  The dictionaries below are searched during
        the metaclass's Python-level `__getattr__`, `__setattr__`, and `__delattr__`
        methods, which are invoked whenever an unrecognized class-level attribute
        access is attempted. */
        struct ClassGet {
            PyObject*(*func)(__python__* /* cls */, void* /* closure */);
            void* closure;  // holds a pointer to a class-level C++ variable
        };
        struct ClassSet {
            /// NOTE: value might be null, which should delete the attribute
            int(*func)(__python__* /* cls */, PyObject* /* value */, void* /* closure */);
            void* closure;  // holds a pointer to a class-level C++ variable
        };
        using ClassGetters = std::unordered_map<std::string_view, ClassGet>;
        using ClassSetters = std::unordered_map<std::string_view, ClassSet>;
        ClassGetters class_getters;
        ClassSetters class_setters;

        /* The metaclass will demangle the C++ class name and track templates via an
        internal dictionary accessible through `__getitem__()`. */
        PyObject* demangled = nullptr;
        PyObject* templates = nullptr;
        __python__* parent = nullptr;  // backreference to the public template interface
        /// TODO: by following the parent pointer, we can look up an instantiation's
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

        When the `bind.type<"name", Cls, Bases...>()` helper is invoked from an
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
        to null.  They are populated by the `bind.method<"name">()` helper in the
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
        PyObject* __new__ = nullptr;
        PyObject* __init__ = nullptr;
        PyObject* __repr__ = nullptr;
        PyObject* __str__ = nullptr;
        PyObject* __lt__ = nullptr;
        PyObject* __le__ = nullptr;
        PyObject* __eq__ = nullptr;
        PyObject* __ne__ = nullptr;
        PyObject* __ge__ = nullptr;
        PyObject* __gt__ = nullptr;
        PyObject* __hash__ = nullptr;
        PyObject* __bool__ = nullptr;
        PyObject* __getattr__ = nullptr;
        PyObject* __getattribute__ = nullptr;
        PyObject* __setattr__ = nullptr;
        PyObject* __delattr__ = nullptr;
        PyObject* __get__ = nullptr;
        PyObject* __set__ = nullptr;
        PyObject* __delete__ = nullptr;
        PyObject* __call__ = nullptr;
        PyObject* __len__ = nullptr;
        PyObject* __getitem__ = nullptr;
        PyObject* __setitem__ = nullptr;
        PyObject* __delitem__ = nullptr;
        PyObject* __iter__ = nullptr;
        PyObject* __next__ = nullptr;
        PyObject* __contains__ = nullptr;
        PyObject* __add__ = nullptr;
        PyObject* __sub__ = nullptr;
        PyObject* __mul__ = nullptr;
        PyObject* __matmul__ = nullptr;
        PyObject* __truediv__ = nullptr;
        PyObject* __floordiv__ = nullptr;
        PyObject* __mod__ = nullptr;
        PyObject* __divmod__ = nullptr;
        PyObject* __pow__ = nullptr;
        PyObject* __lshift__ = nullptr;
        PyObject* __rshift__ = nullptr;
        PyObject* __and__ = nullptr;
        PyObject* __xor__ = nullptr;
        PyObject* __or__ = nullptr;
        PyObject* __radd__ = nullptr;
        PyObject* __rsub__ = nullptr;
        PyObject* __rmul__ = nullptr;
        PyObject* __rmatmul__ = nullptr;
        PyObject* __rtruediv__ = nullptr;
        PyObject* __rfloordiv__ = nullptr;
        PyObject* __rmod__ = nullptr;
        PyObject* __rdivmod__ = nullptr;
        PyObject* __rpow__ = nullptr;
        PyObject* __rlshift__ = nullptr;
        PyObject* __rrshift__ = nullptr;
        PyObject* __rand__ = nullptr;
        PyObject* __rxor__ = nullptr;
        PyObject* __ror__ = nullptr;
        PyObject* __iadd__ = nullptr;
        PyObject* __isub__ = nullptr;
        PyObject* __imul__ = nullptr;
        PyObject* __imatmul__ = nullptr;
        PyObject* __itruediv__ = nullptr;
        PyObject* __ifloordiv__ = nullptr;
        PyObject* __imod__ = nullptr;
        PyObject* __ipow__ = nullptr;
        PyObject* __ilshift__ = nullptr;
        PyObject* __irshift__ = nullptr;
        PyObject* __iand__ = nullptr;
        PyObject* __ixor__ = nullptr;
        PyObject* __ior__ = nullptr;
        PyObject* __neg__ = nullptr;
        PyObject* __pos__ = nullptr;
        PyObject* __abs__ = nullptr;
        PyObject* __invert__ = nullptr;
        PyObject* __int__ = nullptr;
        PyObject* __float__ = nullptr;
        PyObject* __index__ = nullptr;
        PyObject* __buffer__ = nullptr;
        PyObject* __release_buffer__ = nullptr;
        PyObject* __await__ = nullptr;
        PyObject* __aiter__ = nullptr;
        PyObject* __anext__ = nullptr;

        /* The original Python slots are recorded here for posterity. */
        reprfunc base_tp_repr = nullptr;
        hashfunc base_tp_hash = nullptr;
        ternaryfunc base_tp_call = nullptr;
        reprfunc base_tp_str = nullptr;
        getattrofunc base_tp_getattro = nullptr;
        setattrofunc base_tp_setattro = nullptr;
        richcmpfunc base_tp_richcompare = nullptr;
        getiterfunc base_tp_iter = nullptr;
        iternextfunc base_tp_iternext = nullptr;
        descrgetfunc base_tp_descr_get = nullptr;
        descrsetfunc base_tp_descr_set = nullptr;
        initproc base_tp_init = nullptr;
        newfunc base_tp_new = nullptr;
        lenfunc base_mp_length = nullptr;
        binaryfunc base_mp_subscript = nullptr;
        objobjargproc base_mp_ass_subscript = nullptr;
        objobjproc base_sq_contains = nullptr;
        unaryfunc base_am_await = nullptr;
        unaryfunc base_am_aiter = nullptr;
        unaryfunc base_am_anext = nullptr;
        getbufferproc base_bf_getbuffer = nullptr;
        releasebufferproc base_bf_releasebuffer = nullptr;
        binaryfunc base_nb_add = nullptr;
        binaryfunc base_nb_inplace_add = nullptr;
        binaryfunc base_nb_subtract = nullptr;
        binaryfunc base_nb_inplace_subtract = nullptr;
        binaryfunc base_nb_multiply = nullptr;
        binaryfunc base_nb_inplace_multiply = nullptr;
        binaryfunc base_nb_remainder = nullptr;
        binaryfunc base_nb_inplace_remainder = nullptr;
        binaryfunc base_nb_divmod = nullptr;
        ternaryfunc base_nb_power = nullptr;
        ternaryfunc base_nb_inplace_power = nullptr;
        unaryfunc base_nb_negative = nullptr;
        unaryfunc base_nb_positive = nullptr;
        unaryfunc base_nb_absolute = nullptr;
        inquiry base_nb_bool = nullptr;
        unaryfunc base_nb_invert = nullptr;
        binaryfunc base_nb_lshift = nullptr;
        binaryfunc base_nb_inplace_lshift = nullptr;
        binaryfunc base_nb_rshift = nullptr;
        binaryfunc base_nb_inplace_rshift = nullptr;
        binaryfunc base_nb_and = nullptr;
        binaryfunc base_nb_inplace_and = nullptr;
        binaryfunc base_nb_xor = nullptr;
        binaryfunc base_nb_inplace_xor = nullptr;
        binaryfunc base_nb_or = nullptr;
        binaryfunc base_nb_inplace_or = nullptr;
        unaryfunc base_nb_int = nullptr;
        unaryfunc base_nb_float = nullptr;
        binaryfunc base_nb_floor_divide = nullptr;
        binaryfunc base_nb_inplace_floor_divide = nullptr;
        binaryfunc base_nb_true_divide = nullptr;
        binaryfunc base_nb_inplace_true_divide = nullptr;
        unaryfunc base_nb_index = nullptr;
        binaryfunc base_nb_matrix_multiply = nullptr;
        binaryfunc base_nb_inplace_matrix_multiply = nullptr;

        __python__(const std::string& demangled) {
            this->demangled = PyUnicode_FromStringAndSize(
                demangled.c_str(),
                demangled.size()
            );
            if (this->demangled == nullptr) {
                Exception::from_python();
            }
            base_tp_repr                    = tp_repr;
            base_tp_hash                    = tp_hash;
            base_tp_call                    = tp_call;
            base_tp_str                     = tp_str;
            base_tp_getattro                = tp_getattro;
            base_tp_setattro                = tp_setattro;
            base_tp_richcompare             = tp_richcompare;
            base_tp_iter                    = tp_iter;
            base_tp_iternext                = tp_iternext;
            base_tp_descr_get               = tp_descr_get;
            base_tp_descr_set               = tp_descr_set;
            base_tp_init                    = tp_init;
            base_tp_new                     = tp_new;
            base_mp_length                  = tp_as_mapping->mp_length;
            base_mp_subscript               = tp_as_mapping->mp_subscript;
            base_mp_ass_subscript           = tp_as_mapping->mp_ass_subscript;
            base_sq_contains                = tp_as_sequence->sq_contains;
            base_am_await                   = tp_as_async->am_await;
            base_am_aiter                   = tp_as_async->am_aiter;
            base_am_anext                   = tp_as_async->am_anext;
            base_bf_getbuffer               = tp_as_buffer->bf_getbuffer;
            base_bf_releasebuffer           = tp_as_buffer->bf_releasebuffer;
            base_nb_add                     = tp_as_number->nb_add;
            base_nb_inplace_add             = tp_as_number->nb_inplace_add;
            base_nb_subtract                = tp_as_number->nb_subtract;
            base_nb_inplace_subtract        = tp_as_number->nb_inplace_subtract;
            base_nb_multiply                = tp_as_number->nb_multiply;
            base_nb_inplace_multiply        = tp_as_number->nb_inplace_multiply;
            base_nb_remainder               = tp_as_number->nb_remainder;
            base_nb_inplace_remainder       = tp_as_number->nb_inplace_remainder;
            base_nb_divmod                  = tp_as_number->nb_divmod;
            base_nb_power                   = tp_as_number->nb_power;
            base_nb_inplace_power           = tp_as_number->nb_inplace_power;
            base_nb_negative                = tp_as_number->nb_negative;
            base_nb_positive                = tp_as_number->nb_positive;
            base_nb_absolute                = tp_as_number->nb_absolute;
            base_nb_bool                    = tp_as_number->nb_bool;
            base_nb_invert                  = tp_as_number->nb_invert;
            base_nb_lshift                  = tp_as_number->nb_lshift;
            base_nb_inplace_lshift          = tp_as_number->nb_inplace_lshift;
            base_nb_rshift                  = tp_as_number->nb_rshift;
            base_nb_inplace_rshift          = tp_as_number->nb_inplace_rshift;
            base_nb_and                     = tp_as_number->nb_and;
            base_nb_inplace_and             = tp_as_number->nb_inplace_and;
            base_nb_xor                     = tp_as_number->nb_xor;
            base_nb_inplace_xor             = tp_as_number->nb_inplace_xor;
            base_nb_or                      = tp_as_number->nb_or;
            base_nb_inplace_or              = tp_as_number->nb_inplace_or;
            base_nb_int                     = tp_as_number->nb_int;
            base_nb_float                   = tp_as_number->nb_float;
            base_nb_floor_divide            = tp_as_number->nb_floor_divide;
            base_nb_inplace_floor_divide    = tp_as_number->nb_inplace_floor_divide;
            base_nb_true_divide             = tp_as_number->nb_true_divide;
            base_nb_inplace_true_divide     = tp_as_number->nb_inplace_true_divide;
            base_nb_index                   = tp_as_number->nb_index;
            base_nb_matrix_multiply         = tp_as_number->nb_matrix_multiply;
            base_nb_inplace_matrix_multiply = tp_as_number->nb_inplace_matrix_multiply;
        }

        ~__python__() noexcept {
            Py_XDECREF(demangled);
            Py_XDECREF(templates);
            Py_XDECREF(parent);
            Py_XDECREF(__new__);
            Py_XDECREF(__init__);
            Py_XDECREF(__repr__);
            Py_XDECREF(__str__);
            Py_XDECREF(__lt__);
            Py_XDECREF(__le__);
            Py_XDECREF(__eq__);
            Py_XDECREF(__ne__);
            Py_XDECREF(__ge__);
            Py_XDECREF(__gt__);
            Py_XDECREF(__hash__);
            Py_XDECREF(__bool__);
            Py_XDECREF(__getattr__);
            Py_XDECREF(__getattribute__);
            Py_XDECREF(__setattr__);
            Py_XDECREF(__delattr__);
            Py_XDECREF(__get__);
            Py_XDECREF(__set__);
            Py_XDECREF(__delete__);
            Py_XDECREF(__call__);
            Py_XDECREF(__len__);
            Py_XDECREF(__getitem__);
            Py_XDECREF(__setitem__);
            Py_XDECREF(__delitem__);
            Py_XDECREF(__iter__);
            Py_XDECREF(__next__);
            Py_XDECREF(__contains__);
            Py_XDECREF(__add__);
            Py_XDECREF(__sub__);
            Py_XDECREF(__mul__);
            Py_XDECREF(__matmul__);
            Py_XDECREF(__truediv__);
            Py_XDECREF(__floordiv__);
            Py_XDECREF(__mod__);
            Py_XDECREF(__divmod__);
            Py_XDECREF(__pow__);
            Py_XDECREF(__lshift__);
            Py_XDECREF(__rshift__);
            Py_XDECREF(__and__);
            Py_XDECREF(__xor__);
            Py_XDECREF(__or__);
            Py_XDECREF(__radd__);
            Py_XDECREF(__rsub__);
            Py_XDECREF(__rmul__);
            Py_XDECREF(__rmatmul__);
            Py_XDECREF(__rtruediv__);
            Py_XDECREF(__rfloordiv__);
            Py_XDECREF(__rmod__);
            Py_XDECREF(__rdivmod__);
            Py_XDECREF(__rpow__);
            Py_XDECREF(__rlshift__);
            Py_XDECREF(__rrshift__);
            Py_XDECREF(__rand__);
            Py_XDECREF(__rxor__);
            Py_XDECREF(__ror__);
            Py_XDECREF(__iadd__);
            Py_XDECREF(__isub__);
            Py_XDECREF(__imul__);
            Py_XDECREF(__imatmul__);
            Py_XDECREF(__itruediv__);
            Py_XDECREF(__ifloordiv__);
            Py_XDECREF(__imod__);
            Py_XDECREF(__ipow__);
            Py_XDECREF(__ilshift__);
            Py_XDECREF(__irshift__);
            Py_XDECREF(__iand__);
            Py_XDECREF(__ixor__);
            Py_XDECREF(__ior__);
            Py_XDECREF(__neg__);
            Py_XDECREF(__pos__);
            Py_XDECREF(__abs__);
            Py_XDECREF(__invert__);
            Py_XDECREF(__int__);
            Py_XDECREF(__float__);
            Py_XDECREF(__index__);
            Py_XDECREF(__buffer__);
            Py_XDECREF(__release_buffer__);
            Py_XDECREF(__await__);
            Py_XDECREF(__aiter__);
            Py_XDECREF(__anext__);
        }

        /* Metaclass's `tp_traverse` slot registers the metaclass's fields with
        Python's cyclic garbage collector. */
        static int __traverse__(__python__* cls, visitproc visit, void* arg) {
            Py_VISIT(cls->demangled);
            Py_VISIT(cls->templates);
            Py_VISIT(cls->parent);
            Py_VISIT(cls->__new__);
            Py_VISIT(cls->__init__);
            Py_VISIT(cls->__repr__);
            Py_VISIT(cls->__str__);
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
            Py_VISIT(cls->__get__);
            Py_VISIT(cls->__set__);
            Py_VISIT(cls->__delete__);
            Py_VISIT(cls->__call__);
            Py_VISIT(cls->__len__);
            Py_VISIT(cls->__getitem__);
            Py_VISIT(cls->__setitem__);
            Py_VISIT(cls->__delitem__);
            Py_VISIT(cls->__iter__);
            Py_VISIT(cls->__next__);
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
            Py_VISIT(cls->__int__);
            Py_VISIT(cls->__float__);
            Py_VISIT(cls->__index__);
            Py_VISIT(cls->__buffer__);
            Py_VISIT(cls->__release_buffer__);
            Py_VISIT(cls->__await__);
            Py_VISIT(cls->__aiter__);
            Py_VISIT(cls->__anext__);
            return def::__traverse__(cls, visit, arg);
        }

        /* Metaclass's `tp_clear` slot breaks reference cycles if they exist. */
        static int __clear__(__python__* cls) {
            Py_CLEAR(cls->demangled);
            Py_CLEAR(cls->templates);
            Py_CLEAR(cls->parent);
            Py_CLEAR(cls->__new__);
            Py_CLEAR(cls->__init__);
            Py_CLEAR(cls->__repr__);
            Py_CLEAR(cls->__str__);
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
            Py_CLEAR(cls->__get__);
            Py_CLEAR(cls->__set__);
            Py_CLEAR(cls->__delete__);
            Py_CLEAR(cls->__call__);
            Py_CLEAR(cls->__len__);
            Py_CLEAR(cls->__getitem__);
            Py_CLEAR(cls->__setitem__);
            Py_CLEAR(cls->__delitem__);
            Py_CLEAR(cls->__iter__);
            Py_CLEAR(cls->__next__);
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
            Py_CLEAR(cls->__int__);
            Py_CLEAR(cls->__float__);
            Py_CLEAR(cls->__index__);
            Py_CLEAR(cls->__buffer__);
            Py_CLEAR(cls->__release_buffer__);
            Py_CLEAR(cls->__await__);
            Py_CLEAR(cls->__aiter__);
            Py_CLEAR(cls->__anext__);
            return def::__clear__(cls);
        }

        /* `repr(cls)` displays the type's demangled C++ name.  */
        static PyObject* class_repr(__python__* cls) {
            Py_ssize_t size;
            const char* data = PyUnicode_AsUTF8AndSize(cls->demangled, &size);
            if (data == nullptr) {
                return nullptr;
            }
            std::string s = "<class '";
            s += std::string_view(data, size);
            s += "'>";
            return PyUnicode_FromStringAndSize(s.data(), s.size());
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
                    std::string message = "class template has not been instantiated: ";
                    Py_ssize_t len;
                    const char* data = PyUnicode_AsUTF8AndSize(
                        cls->demangled,
                        &len
                    );
                    if (data == nullptr) {
                        Py_DECREF(key);
                        return nullptr;
                    }
                    message += std::string_view(data, len);
                    message += "[";
                    PyObject* rep = PyObject_Repr(key);
                    Py_DECREF(key);
                    if (rep == nullptr) {
                        return nullptr;
                    }
                    data = PyUnicode_AsUTF8AndSize(rep, &len);
                    if (data == nullptr) {
                        Py_DECREF(rep);
                        return nullptr;
                    }
                    std::string_view filter(data, len);
                    message += filter.substr(1, filter.size() - 2);  // strip parens
                    message += "]";
                    Py_DECREF(rep);
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
        static PyObject* class_doc(__python__* cls, void*);

        /* `cls.` allows access to class-level variables with shared state, and
        otherwise falls back to ordinary Python attribute access. */
        static PyObject* class_getattro(__python__* cls, PyObject* attr) {
            Py_ssize_t size;
            const char* name = PyUnicode_AsUTF8AndSize(attr, &size);
            if (name == nullptr) {
                return nullptr;
            }
            auto it = cls->class_getters.find(
                std::string_view(name, static_cast<size_t>(size))
            );
            if (it != cls->class_getters.end()) {
                return it->second.func(cls, it->second.closure);
            }
            return PyObject_GenericGetAttr(reinterpret_cast<PyObject*>(cls), attr);
        }

        /* `cls. = ...` allows assignment to class-level variables with shared state. */
        static int class_setattro(__python__* cls, PyObject* attr, PyObject* value) {
            Py_ssize_t size;
            const char* name = PyUnicode_AsUTF8AndSize(attr, &size);
            if (name == nullptr) {
                return -1;
            }
            auto it = cls->class_setters.find(
                std::string_view(name, static_cast<size_t>(size))
            );
            if (it != cls->class_setters.end()) {
                return it->second.func(cls, value, it->second.closure);
            }
            return PyObject_GenericSetAttr(
                reinterpret_cast<PyObject*>(cls),
                attr,
                value
            );
        }

        /* isinstance(obj, cls) forwards the behavior of the __isinstance__ control
        struct and accounts for possible template instantiations. */
        static PyObject* class_instancecheck(__python__* cls, PyObject* instance) {
            try {
                return PyBool_FromLong(cls->_instancecheck(cls, instance));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* issubclass(sub, cls) forwards the behavior of the __issubclass__ control
        struct and accounts for possible template instantiations. */
        static PyObject* class_subclasscheck(__python__* cls, PyObject* subclass) {
            try {
                return PyBool_FromLong(cls->_subclasscheck(cls, subclass));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* The metaclass can't use Bindings, since they depend on the metaclass to
        function.  The logic is fundamentally the same, however. */
        template <static_str ModName>
        static Type<BertrandMeta> __export__(Module<ModName> module);

        /* Get a new reference to the metatype from the global `bertrand.python`
        module. */
        static Type<BertrandMeta> __import__();  // TODO: defined in __init__.h alongside "bertrand.python" module {
        //     return impl::get_type<BertrandMeta>(Module<"bertrand.python">());
        // }

        /* Create a trivial instance of the metaclass to serve as a Python entry point
        for a C++ template hierarchy.  The interface type is not usable on its own
        except to provide access to its C++ instantiations, as well as efficient type
        checks against them and a central point for documentation. */
        template <static_str Name, static_str ModName>
        static BertrandMeta stub_type(Module<ModName>& module, std::string&& doc);

        /* A candidate for the `_instancecheck` function pointer to be used for stub
        types. */
        static bool instancecheck(__python__* cls, PyObject* instance) {
            if (PyType_IsSubtype(Py_TYPE(instance), cls)) {
                return true;
            }
            PyObject* key;
            PyObject* value;
            Py_ssize_t pos = 0;
            while (PyDict_Next(cls->templates, &pos, &key, &value)) {
                __python__* instantiation = reinterpret_cast<__python__*>(value);
                if (instantiation->_instancecheck(cls, instance)) {
                    return true;
                }
            }
            return false;
        }

        /* A generic candidate for the `_instancecheck` function pointer to be used for
        C++ extension types. */
        template <typename Wrapper>
        static bool instancecheck(__python__* cls, PyObject* instance) {
            return PyType_IsSubtype(Py_TYPE(instance), cls);
        }

        /* A candidate for the `_subclasscheck` function pointer to be used for stub
        types. */
        static bool subclasscheck(__python__* cls, PyObject* subclass) {
            if (PyType_Check(subclass)) {
                if (PyType_IsSubtype(reinterpret_cast<PyTypeObject*>(subclass), cls)) {
                    return true;
                }
                PyObject* key;
                PyObject* value;
                Py_ssize_t pos = 0;
                while (PyDict_Next(cls->templates, &pos, &key, &value)) {
                    __python__* instantiation = reinterpret_cast<__python__*>(value);
                    if (instantiation->_subclasscheck(cls, subclass)) {
                        return true;
                    }
                }
                return false;
            } else {
                throw TypeError("issubclass() arg 1 must be a class");
            }
        }

        /* A generic candidate for the `_subclasscheck` function pointer to be used for
        C++ extension types. */
        template <typename Wrapper>
        static bool subclasscheck(__python__* cls, PyObject* subclass) {
            if (PyType_Check(subclass)) {
                return PyType_IsSubtype(reinterpret_cast<PyTypeObject*>(subclass), cls);
            } else {
                throw TypeError("issubclass() arg 1 must be a class");
            }
        }

    private:

        /* Allocate a vectorcall argument array with an implicit self argument. */
        static std::tuple<PyObject* const*, Py_ssize_t, PyObject*> vectorcall_args(
            PyObject* self,
            PyObject* args,
            PyObject* kwargs
        ) {
            Py_ssize_t args_size = args ? PyTuple_GET_SIZE(args) : 0;
            Py_ssize_t kwargs_size = kwargs ? PyDict_Size(kwargs) : 0;
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
                    PyTuple_SET_ITEM(kwnames, pos, Py_NewRef(key));
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
            Py_ssize_t args_size = args ? PyTuple_GET_SIZE(args) : 0;
            Py_ssize_t kwargs_size = kwargs ? PyDict_Size(kwargs) : 0;
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
                    PyTuple_SET_ITEM(kwnames, pos, Py_NewRef(key));
                    forward[i++] = value;
                }
            }

            return {forward, i, kwnames};
        }

        template <static_str Name>
        static PyObject* search_mro(__python__* cls) {
            // search instance dictionary first
            PyObject* dict = PyType_GetDict(cls);
            if (dict) {
                PyObject* value = PyDict_GetItem(
                    dict,
                    impl::TemplateString<Name>::ptr
                );
                if (value) {
                    return Py_NewRef(value);
                }
            }

            // traverse the MRO
            PyObject* mro = cls->tp_mro;
            if (mro != nullptr) {
                Py_ssize_t size = PyTuple_GET_SIZE(mro);
                for (Py_ssize_t i = 1; i < size; ++i) {
                    PyObject* type = PyTuple_GET_ITEM(mro, i);
                    if (PyObject_HasAttr(
                        type,
                        impl::TemplateString<Name>::ptr
                    )) {
                        return PyObject_GetAttr(
                            type,
                            impl::TemplateString<Name>::ptr
                        );
                    }
                }
            }

            PyErr_Format(
                PyExc_AttributeError,
                "type object '%U' has no attribute " + Name,
                cls->demangled
            );
            return nullptr;
        }

        /* A collection of getset descriptors on the metaclass that intercept the
        standard assignment operator and update the internal C++ function pointers. */
        struct Descr {

            /// TODO: descriptors include extra slots that aren't actually stored
            /// as overload sets in the metaclass itself.  These will need signature
            /// checks, but otherwise get inserted into the instance dict, rather than
            /// an internal overload set, since there's no corresponding slot.

            /// TODO: setters should enforce a signature match to conform with the
            /// slot's expected function signature.  Several of these will likely be
            /// variadic.

            /// TODO: setters always generate a new overload set, and replace the
            /// corresponding slot with a new function pointer that delegates to the
            /// overload set.  Deleting the overload set should revert the slot back to
            /// its original behavior.

            struct __new__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__new__ ?
                        Py_NewRef(cls->__new__) :
                        search_mro<"__new__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __init__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__init__ ?
                        Py_NewRef(cls->__init__) :
                        search_mro<"__init__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __del__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__del__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __repr__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__repr__ ?
                        Py_NewRef(cls->__repr__) :
                        search_mro<"__repr__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __str__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__str__ ?
                        Py_NewRef(cls->__str__) :
                        search_mro<"__str__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __bytes__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__bytes__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __format__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__format__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __lt__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__lt__ ?
                        Py_NewRef(cls->__lt__) :
                        search_mro<"__lt__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __le__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__le__ ?
                        Py_NewRef(cls->__le__) :
                        search_mro<"__le__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __eq__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__eq__ ?
                        Py_NewRef(cls->__eq__) :
                        search_mro<"__eq__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ne__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__ne__ ?
                        Py_NewRef(cls->__ne__) :
                        search_mro<"__ne__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ge__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__ge__ ?
                        Py_NewRef(cls->__ge__) :
                        search_mro<"__ge__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __gt__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__gt__ ?
                        Py_NewRef(cls->__gt__) :
                        search_mro<"__gt__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __hash__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__hash__ ?
                        Py_NewRef(cls->__hash__) :
                        search_mro<"__hash__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __bool__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__bool__ ?
                        Py_NewRef(cls->__bool__) :
                        search_mro<"__bool__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __getattr__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__getattr__ ?
                        Py_NewRef(cls->__getattr__) :
                        search_mro<"__getattr__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __getattribute__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__getattribute__ ?
                        Py_NewRef(cls->__getattribute__) :
                        search_mro<"__getattribute__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __setattr__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__setattr__ ?
                        Py_NewRef(cls->__setattr__) :
                        search_mro<"__setattr__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __delattr__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__delattr__ ?
                        Py_NewRef(cls->__delattr__) :
                        search_mro<"__delattr__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __dir__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__dir__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __get__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__get__ ?
                        Py_NewRef(cls->__get__) :
                        search_mro<"__get__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __set__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__set__ ?
                        Py_NewRef(cls->__set__) :
                        search_mro<"__set__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __delete__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__delete__ ?
                        Py_NewRef(cls->__delete__) :
                        search_mro<"__delete__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __init_subclass__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__init_subclass__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __set_name__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__set_name__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __mro_entries__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__mro_entries__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __instancecheck__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__instancecheck__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __subclasscheck__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__subclasscheck__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __class_getitem__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__class_getitem__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __call__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__call__ ?
                        Py_NewRef(cls->__call__) :
                        search_mro<"__call__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __len__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__len__ ?
                        Py_NewRef(cls->__len__) :
                        search_mro<"__len__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __length_hint__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__length_hint__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __getitem__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__getitem__ ?
                        Py_NewRef(cls->__getitem__) :
                        search_mro<"__getitem__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __setitem__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__setitem__ ?
                        Py_NewRef(cls->__setitem__) :
                        search_mro<"__setitem__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __delitem__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__delitem__ ?
                        Py_NewRef(cls->__delitem__) :
                        search_mro<"__delitem__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __missing__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__missing__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __iter__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__iter__ ?
                        Py_NewRef(cls->__iter__) :
                        search_mro<"__iter__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __next__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__next__ ?
                        Py_NewRef(cls->__next__) :
                        search_mro<"__next__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __reversed__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__reversed__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __contains__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__contains__ ?
                        Py_NewRef(cls->__contains__) :
                        search_mro<"__contains__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __add__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__add__ ?
                        Py_NewRef(cls->__add__) :
                        search_mro<"__add__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __sub__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__sub__ ?
                        Py_NewRef(cls->__sub__) :
                        search_mro<"__sub__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __mul__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__mul__ ?
                        Py_NewRef(cls->__mul__) :
                        search_mro<"__mul__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __matmul__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__matmul__ ?
                        Py_NewRef(cls->__matmul__) :
                        search_mro<"__matmul__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __truediv__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__truediv__ ?
                        Py_NewRef(cls->__truediv__) :
                        search_mro<"__truediv__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __floordiv__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__floordiv__ ?
                        Py_NewRef(cls->__floordiv__) :
                        search_mro<"__floordiv__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __mod__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__mod__ ?
                        Py_NewRef(cls->__mod__) :
                        search_mro<"__mod__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __divmod__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__divmod__ ?
                        Py_NewRef(cls->__divmod__) :
                        search_mro<"__divmod__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __pow__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__pow__ ?
                        Py_NewRef(cls->__pow__) :
                        search_mro<"__pow__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __lshift__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__lshift__ ?
                        Py_NewRef(cls->__lshift__) :
                        search_mro<"__lshift__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };
    
            struct __rshift__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rshift__ ?
                        Py_NewRef(cls->__rshift__) :
                        search_mro<"__rshift__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __and__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__and__ ?
                        Py_NewRef(cls->__and__) :
                        search_mro<"__and__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __xor__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__xor__ ?
                        Py_NewRef(cls->__xor__) :
                        search_mro<"__xor__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __or__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__or__ ?
                        Py_NewRef(cls->__or__) :
                        search_mro<"__or__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __radd__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__radd__ ?
                        Py_NewRef(cls->__radd__) :
                        search_mro<"__radd__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rsub__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rsub__ ?
                        Py_NewRef(cls->__rsub__) :
                        search_mro<"__rsub__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rmul__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rmul__ ?
                        Py_NewRef(cls->__rmul__) :
                        search_mro<"__rmul__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rmatmul__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rmatmul__ ?
                        Py_NewRef(cls->__rmatmul__) :
                        search_mro<"__rmatmul__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rtruediv__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rtruediv__ ?
                        Py_NewRef(cls->__rtruediv__) :
                        search_mro<"__rtruediv__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rfloordiv__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rfloordiv__ ?
                        Py_NewRef(cls->__rfloordiv__) :
                        search_mro<"__rfloordiv__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rmod__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rmod__ ?
                        Py_NewRef(cls->__rmod__) :
                        search_mro<"__rmod__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rdivmod__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rdivmod__ ?
                        Py_NewRef(cls->__rdivmod__) :
                        search_mro<"__rdivmod__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rpow__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rpow__ ?
                        Py_NewRef(cls->__rpow__) :
                        search_mro<"__rpow__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rlshift__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rlshift__ ?
                        Py_NewRef(cls->__rlshift__) :
                        search_mro<"__rlshift__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };
    
            struct __rrshift__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rrshift__ ?
                        Py_NewRef(cls->__rrshift__) :
                        search_mro<"__rrshift__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rand__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rand__ ?
                        Py_NewRef(cls->__rand__) :
                        search_mro<"__rand__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __rxor__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__rxor__ ?
                        Py_NewRef(cls->__rxor__) :
                        search_mro<"__rxor__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ror__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__ror__ ?
                        Py_NewRef(cls->__ror__) :
                        search_mro<"__ror__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __iadd__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__iadd__ ?
                        Py_NewRef(cls->__iadd__) :
                        search_mro<"__iadd__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __isub__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__isub__ ?
                        Py_NewRef(cls->__isub__) :
                        search_mro<"__isub__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __imul__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__imul__ ?
                        Py_NewRef(cls->__imul__) :
                        search_mro<"__imul__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __imatmul__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__imatmul__ ?
                        Py_NewRef(cls->__imatmul__) :
                        search_mro<"__imatmul__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __itruediv__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__itruediv__ ?
                        Py_NewRef(cls->__itruediv__) :
                        search_mro<"__itruediv__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ifloordiv__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__ifloordiv__ ?
                        Py_NewRef(cls->__ifloordiv__) :
                        search_mro<"__ifloordiv__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __imod__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__imod__ ?
                        Py_NewRef(cls->__imod__) :
                        search_mro<"__imod__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ipow__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__ipow__ ?
                        Py_NewRef(cls->__ipow__) :
                        search_mro<"__ipow__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ilshift__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__ilshift__ ?
                        Py_NewRef(cls->__ilshift__) :
                        search_mro<"__ilshift__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };
    
            struct __irshift__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__irshift__ ?
                        Py_NewRef(cls->__irshift__) :
                        search_mro<"__irshift__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __iand__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__iand__ ?
                        Py_NewRef(cls->__iand__) :
                        search_mro<"__iand__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ixor__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__ixor__ ?
                        Py_NewRef(cls->__ixor__) :
                        search_mro<"__ixor__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ior__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__ior__ ?
                        Py_NewRef(cls->__ior__) :
                        search_mro<"__ior__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __neg__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__neg__ ?
                        Py_NewRef(cls->__neg__) :
                        search_mro<"__neg__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __pos__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__pos__ ?
                        Py_NewRef(cls->__pos__) :
                        search_mro<"__pos__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __abs__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__abs__ ?
                        Py_NewRef(cls->__abs__) :
                        search_mro<"__abs__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __invert__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__invert__ ?
                        Py_NewRef(cls->__invert__) :
                        search_mro<"__invert__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __complex_py__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__complex__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __int__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__int__ ?
                        Py_NewRef(cls->__int__) :
                        search_mro<"__int__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __float__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__float__ ?
                        Py_NewRef(cls->__float__) :
                        search_mro<"__float__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __index__ {
                static PyObject* get(__python__* cls, void*) {
                    return cls->__index__ ?
                        Py_NewRef(cls->__index__) :
                        search_mro<"__index__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __round__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__round__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __trunc__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__trunc__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __floor__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__floor__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __ceil__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__ceil__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __enter__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__enter__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __exit__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__exit__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __buffer__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__buffer__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __release_buffer__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__release_buffer__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __await__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__await__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __aiter__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__aiter__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __anext__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__anext__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __aenter__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__aenter__">(cls);
                }
                static int set(__python__* cls, PyObject* value, void*);
            };

            struct __aexit__ {
                static PyObject* get(__python__* cls, void*) {
                    return search_mro<"__aexit__">(cls);
                }
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
        /// -> The Function call operator will check to see if the function is static
        /// and PY_VECTORCALL_ARGUMENTS_OFFSET is set.  If so, and the self argument
        /// is non-null then it will shift the argument array back by one in order
        /// to pass self as the first argument.  Since static methods are converted to
        /// static descriptors, Python will not ordinarily invoke this, but if I
        /// do the call from C++ and include `self`, then it will apply the
        /// correct ADL behavior.

        /// TODO: there could be performance benefits to keeping the metaclass static.
        /// That would prevent the binary operators from needing to import the metaclass
        /// every time they are called.

        /* A collection of wrappers for all of the Python slots that delegate to a
        corresponding overload set held within the metaclass.  These will be inserted
        into the corresponding slot on the type object whenever a dunder method is
        assigned to, and reset to its original value when the method is deleted. */
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
                } else if (meta->base_tp_call) {
                    return meta->base_tp_call(self, args, kwargs);
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
                __python__* meta = reinterpret_cast<__python__*>(Py_TYPE(self));
                if (meta->__getattribute__) {
                    PyObject* const forward[] = {self, attr};
                    return PyObject_Vectorcall(
                        meta->__getattribute__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                }
                PyObject* result = PyObject_GenericGetAttr(self, attr);
                if (
                    result == nullptr &&
                    meta->__getattr__ &&
                    PyErr_ExceptionMatches(PyExc_AttributeError)
                ) {
                    PyErr_Clear();
                    PyObject* const forward[] = {self, attr};
                    return PyObject_Vectorcall(
                        meta->__getattr__,
                        forward,
                        1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                }
                return result;
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
                    } else if (meta->base_tp_setattro) {
                        return meta->base_tp_setattro(self, attr, value);
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
                    } else if (meta->base_tp_setattro) {
                        return meta->base_tp_setattro(self, attr, value);
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
                        } else if (meta->base_tp_richcompare) {
                            return meta->base_tp_richcompare(self, other, op);
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
                        } else if (meta->base_tp_richcompare) {
                            return meta->base_tp_richcompare(self, other, op);
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
                        } else if (meta->base_tp_richcompare) {
                            return meta->base_tp_richcompare(self, other, op);
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
                        } else if (meta->base_tp_richcompare) {
                            return meta->base_tp_richcompare(self, other, op);
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
                        } else if (meta->base_tp_richcompare) {
                            return meta->base_tp_richcompare(self, other, op);
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
                        } else if (meta->base_tp_richcompare) {
                            return meta->base_tp_richcompare(self, other, op);
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
                    } else if (meta->base_tp_descr_set) {
                        return meta->base_tp_descr_set(self, obj, value);
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
                    } else if (meta->base_tp_descr_set) {
                        return meta->base_tp_descr_set(self, obj, value);
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
                    } else if (meta->base_mp_ass_subscript) {
                        return meta->base_mp_ass_subscript(self, key, value);
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
                    } else if (meta->base_mp_ass_subscript) {
                        return meta->base_mp_ass_subscript(self, key, value);
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
                PyObject* pyflags = PyLong_FromLong(flags);
                if (pyflags == nullptr) {
                    return -1;
                }
                PyObject* const forward[] = {self, pyflags};
                PyObject* result = PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__buffer__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
                Py_DECREF(pyflags);
                if (result == nullptr) {
                    return -1;
                }
                if (!PyMemoryView_Check(result)) {
                    PyErr_Format(
                        PyExc_TypeError,
                        "__buffer__ must return a memoryview object"
                    );
                    Py_DECREF(result);
                    return -1;
                }
                Py_buffer* buffer = PyMemoryView_GET_BUFFER(result);
                int rc = PyBuffer_FillInfo(
                    view,
                    self,
                    buffer->buf,
                    buffer->len,
                    buffer->readonly,
                    flags
                );
                view->itemsize = buffer->itemsize;
                view->format = buffer->format;
                view->ndim = buffer->ndim;
                view->shape = buffer->shape;
                view->strides = buffer->strides;
                view->suboffsets = buffer->suboffsets;
                view->internal = buffer->internal;
                Py_DECREF(result);
                return rc;
            }

            static int bf_releasebuffer(PyObject* self, Py_buffer* view) {
                PyObject* memoryview = PyMemoryView_FromBuffer(view);
                if (memoryview == nullptr) {
                    return -1;
                }
                PyObject* const forward[] = {self, memoryview};
                PyObject* result = PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__release_buffer__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
                Py_DECREF(memoryview);
                if (result == nullptr) {
                    return -1;
                }
                Py_DECREF(result);
                return 0;
            }

            static PyObject* nb_add(PyObject* lhs, PyObject* rhs);

            static PyObject* nb_inplace_add(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__iadd__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_subtract(PyObject* lhs, PyObject* rhs);

            static PyObject* nb_inplace_subtract(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__isub__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_multiply(PyObject* lhs, PyObject* rhs);

            static PyObject* nb_inplace_multiply(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__imul__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_remainder(PyObject* lhs, PyObject* rhs);

            static PyObject* nb_inplace_remainder(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__imod__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_divmod(PyObject* lhs, PyObject* rhs);

            static PyObject* nb_power(PyObject* base, PyObject* exp, PyObject* mod);

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

            static PyObject* nb_lshift(PyObject* lhs, PyObject* rhs);

            static PyObject* nb_inplace_lshift(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__ilshift__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_rshift(PyObject* lhs, PyObject* rhs);

            static PyObject* nb_inplace_rshift(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__irshift__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_and(PyObject* lhs, PyObject* rhs);

            static PyObject* nb_inplace_and(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__iand__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_xor(PyObject* lhs, PyObject* rhs);

            static PyObject* nb_inplace_xor(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__ixor__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_or(PyObject* lhs, PyObject* rhs);

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

            static PyObject* nb_floor_divide(PyObject* lhs, PyObject* rhs);

            static PyObject* nb_inplace_floor_divide(PyObject* self, PyObject* other) {
                PyObject* const forward[] = {self, other};
                return PyObject_Vectorcall(
                    reinterpret_cast<__python__*>(Py_TYPE(self))->__ifloordiv__,
                    forward,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
            }

            static PyObject* nb_true_divide(PyObject* lhs, PyObject* rhs);

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

            static PyObject* nb_matrix_multiply(PyObject* lhs, PyObject* rhs);

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
                .ml_doc = PyDoc_STR(
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
                )
            },
            {
                .ml_name = "__subclasscheck__",
                .ml_meth = reinterpret_cast<PyCFunction>(class_subclasscheck),
                .ml_flags = METH_O,
                .ml_doc = PyDoc_STR(
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
                )
            },
            {nullptr, nullptr, 0, nullptr}
        };

        inline static PyGetSetDef getset[] = {
            {
                .name = "__doc__",
                .get = reinterpret_cast<getter>(class_doc),
                .set = nullptr,
                .doc = PyDoc_STR(
R"doc(The class's documentation string.

See Also
--------
object.__doc__ : Python's built-in documentation string attribute.

Notes
-----
The metaclass adds a list of template instantiations to the end of the docstring, if
any are present.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__new__",
                .get = reinterpret_cast<getter>(Descr::__new__::get),
                .set = reinterpret_cast<setter>(Descr::__new__::set),
                .doc = PyDoc_STR(
R"doc(Instance creation operator.

See Also
--------
object.__new__ : Python's built-in instance creation operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__init__",
                .get = reinterpret_cast<getter>(Descr::__init__::get),
                .set = reinterpret_cast<setter>(Descr::__init__::set),
                .doc = PyDoc_STR(
R"doc(Instance initialization operator.

See Also
--------
object.__init__ : Python's built-in instance initialization operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__del__",
                .get = reinterpret_cast<getter>(Descr::__del__::get),
                .set = reinterpret_cast<setter>(Descr::__del__::set),
                .doc = PyDoc_STR(
R"doc(Instance deallocation operator.

See Also
--------
object.__del__ : Python's built-in instance deallocation operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.

This method should never need to be overridden, as the default `tp_dealloc` slot will
call the class's C++ destructor like normal.  Any custom deallocation logic should be
placed there instead, which is much safer and less error-prone.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__repr__",
                .get = reinterpret_cast<getter>(Descr::__repr__::get),
                .set = reinterpret_cast<setter>(Descr::__repr__::set),
                .doc = PyDoc_STR(
R"doc(String representation operator.

See Also
--------
object.__repr__ : Python's built-in string representation operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__str__",
                .get = reinterpret_cast<getter>(Descr::__str__::get),
                .set = reinterpret_cast<setter>(Descr::__str__::set),
                .doc = PyDoc_STR(
R"doc(String conversion operator.

See Also
--------
object.__str__ : Python's built-in string conversion operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__bytes__",
                .get = reinterpret_cast<getter>(Descr::__bytes__::get),
                .set = reinterpret_cast<setter>(Descr::__bytes__::set),
                .doc = PyDoc_STR(
R"doc(Bytes conversion operator.

See Also
--------
object.__bytes__ : Python's built-in bytes conversion operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__format__",
                .get = reinterpret_cast<getter>(Descr::__format__::get),
                .set = reinterpret_cast<setter>(Descr::__format__::set),
                .doc = PyDoc_STR(
R"doc(f-string formatting operator.

See Also
--------
object.__format__ : Python's built-in f-string formatting operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__lt__",
                .get = reinterpret_cast<getter>(Descr::__lt__::get),
                .set = reinterpret_cast<setter>(Descr::__lt__::set),
                .doc = PyDoc_STR(
R"doc(Less-than comparison operator.

See Also
--------
object.__lt__ : Python's built-in less-than comparison operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__le__",
                .get = reinterpret_cast<getter>(Descr::__le__::get),
                .set = reinterpret_cast<setter>(Descr::__le__::set),
                .doc = PyDoc_STR(
R"doc(Less-than-or-equal comparison operator.

See Also
--------
object.__le__ : Python's built-in less-than-or-equal comparison operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__eq__",
                .get = reinterpret_cast<getter>(Descr::__eq__::get),
                .set = reinterpret_cast<setter>(Descr::__eq__::set),
                .doc = PyDoc_STR(
R"doc(Equality operator.

See Also
--------
object.__eq__ : Python's built-in equality operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__ne__",
                .get = reinterpret_cast<getter>(Descr::__ne__::get),
                .set = reinterpret_cast<setter>(Descr::__ne__::set),
                .doc = PyDoc_STR(
R"doc(Inequality operator.

See Also
--------
object.__ne__ : Python's built-in inequality operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__ge__",
                .get = reinterpret_cast<getter>(Descr::__ge__::get),
                .set = reinterpret_cast<setter>(Descr::__ge__::set),
                .doc = PyDoc_STR(
R"doc(Greater-than-or-equal operator

See Also
--------
object.__ge__ : Python's built-in greater-than-or-equal operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__gt__",
                .get = reinterpret_cast<getter>(Descr::__gt__::get),
                .set = reinterpret_cast<setter>(Descr::__gt__::set),
                .doc = PyDoc_STR(
R"doc(Greater-than operator.

See Also
--------
object.__gt__ : Python's built-in greater-than operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__hash__",
                .get = reinterpret_cast<getter>(Descr::__hash__::get),
                .set = reinterpret_cast<setter>(Descr::__hash__::set),
                .doc = PyDoc_STR(
R"doc(Hash operator.

See Also
--------
object.__hash__ : Python's built-in hash operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__bool__",
                .get = reinterpret_cast<getter>(Descr::__bool__::get),
                .set = reinterpret_cast<setter>(Descr::__bool__::set),
                .doc = PyDoc_STR(
R"doc(Boolean conversion operator.

See Also
--------
object.__bool__ : Python's built-in boolean conversion operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__getattr__",
                .get = reinterpret_cast<getter>(Descr::__getattr__::get),
                .set = reinterpret_cast<setter>(Descr::__getattr__::set),
                .doc = PyDoc_STR(
R"doc(Attribute lookup operator.

See Also
--------
object.__getattr__ : Python's built-in attribute lookup operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.

This method is distinct from `__getattribute__` in that it will only be called if the
latter raises an `AttributeError` after exhausting all other options.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__getattribute__",
                .get = reinterpret_cast<getter>(Descr::__getattribute__::get),
                .set = reinterpret_cast<setter>(Descr::__getattribute__::set),
                .doc = PyDoc_STR(
R"doc(Attribute access operator.

See Also
--------
object.__getattribute__ : Python's built-in attribute access operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.

This method is distinct from `__getattr__` in that it will be called greedily on every
attribute access, before searching for descriptors or within the instance dictionary,
MRO, etc.  It's generally not recommended to override this method, as it can easily
lead to infinite recursion or unexpected behavior if not done carefully.  `__getattr__`
is much more stable in this regard.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__setattr__",
                .get = reinterpret_cast<getter>(Descr::__setattr__::get),
                .set = reinterpret_cast<setter>(Descr::__setattr__::set),
                .doc = PyDoc_STR(
R"doc(Attribute assignment operator.

See Also
--------
object.__setattr__ : Python's built-in attribute assignment operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__delattr__",
                .get = reinterpret_cast<getter>(Descr::__delattr__::get),
                .set = reinterpret_cast<setter>(Descr::__delattr__::set),
                .doc = PyDoc_STR(
R"doc(Attribute deletion operator.

See Also
--------
object.__delattr__ : Python's built-in attribute deletion operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__dir__",
                .get = reinterpret_cast<getter>(Descr::__dir__::get),
                .set = reinterpret_cast<setter>(Descr::__dir__::set),
                .doc = PyDoc_STR(
R"doc(Return a list of valid attributes for the object.

See Also
--------
object.__dir__ : Python's built-in attribute listing operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__get__",
                .get = reinterpret_cast<getter>(Descr::__get__::get),
                .set = reinterpret_cast<setter>(Descr::__get__::set),
                .doc = PyDoc_STR(
R"doc(Descriptor access operator.

See Also
--------
object.__get__ : Python's built-in descriptor access operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__set__",
                .get = reinterpret_cast<getter>(Descr::__set__::get),
                .set = reinterpret_cast<setter>(Descr::__set__::set),
                .doc = PyDoc_STR(
R"doc(Descriptor assignment operator.

See Also
--------
object.__set__ : Python's built-in descriptor assignment operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__delete__",
                .get = reinterpret_cast<getter>(Descr::__delete__::get),
                .set = reinterpret_cast<setter>(Descr::__delete__::set),
                .doc = PyDoc_STR(
R"doc(Descriptor deletion operator.

See Also
--------
object.__delete__ : Python's built-in descriptor deletion operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__init_subclass__",
                .get = reinterpret_cast<getter>(Descr::__init_subclass__::get),
                .set = reinterpret_cast<setter>(Descr::__init_subclass__::set),
                .doc = PyDoc_STR(
R"doc(Subclass initialization operator.

See Also
--------
object.__init_subclass__ : Python's built-in subclass initialization operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__set_name__",
                .get = reinterpret_cast<getter>(Descr::__set_name__::get),
                .set = reinterpret_cast<setter>(Descr::__set_name__::set),
                .doc = PyDoc_STR(
R"doc(Attribute naming operator.

See Also
--------
object.__set_name__ : Python's built-in attribute naming operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__mro_entries__",
                .get = reinterpret_cast<getter>(Descr::__mro_entries__::get),
                .set = reinterpret_cast<setter>(Descr::__mro_entries__::set),
                .doc = PyDoc_STR(
R"doc(Inheritable base substitution operator.

See Also
--------
object.__mro_entries__ : Python's built-in base substitution operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__instancecheck__",
                .get = reinterpret_cast<getter>(Descr::__instancecheck__::get),
                .set = reinterpret_cast<setter>(Descr::__instancecheck__::set),
                .doc = PyDoc_STR(
R"doc(Instance type check operator.

See Also
--------
object.__instancecheck__ : Python's built-in instance type check operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__subclasscheck__",
                .get = reinterpret_cast<getter>(Descr::__subclasscheck__::get),
                .set = reinterpret_cast<setter>(Descr::__subclasscheck__::set),
                .doc = PyDoc_STR(
R"doc(Subclass type check operator.

See Also
--------
object.__subclasscheck__ : Python's built-in subclass type check operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__class_getitem__",
                .get = reinterpret_cast<getter>(Descr::__class_getitem__::get),
                .set = reinterpret_cast<setter>(Descr::__class_getitem__::set),
                .doc = PyDoc_STR(
R"doc(Class subscript operator.

See Also
--------
object.__class_getitem__ : Python's built-in class subscript operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.

Note that this method has lower precedence than the `__getitem__` slot of the
metaclass itself, which is used to navigate template hierarchies from Python.  This
should not be a problem in practice, as that method is only called when indexing a
template interface type, which otherwise has no usable interface.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__call__",
                .get = reinterpret_cast<getter>(Descr::__call__::get),
                .set = reinterpret_cast<setter>(Descr::__call__::set),
                .doc = PyDoc_STR(
R"doc(Instance call operator.

See Also
--------
object.__call__ : Python's built-in instance call operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__len__",
                .get = reinterpret_cast<getter>(Descr::__len__::get),
                .set = reinterpret_cast<setter>(Descr::__len__::set),
                .doc = PyDoc_STR(
R"doc(Sequence length operator.

See Also
--------
object.__len__ : Python's built-in sequence length operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__length_hint__",
                .get = reinterpret_cast<getter>(Descr::__length_hint__::get),
                .set = reinterpret_cast<setter>(Descr::__length_hint__::set),
                .doc = PyDoc_STR(
R"doc(Iterator length hint operator.

See Also
--------
object.__length_hint__ : Python's built-in iterator length hint operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.

This method is distinct from `__len__` in that it will only be called if the latter is
unavailable, and only as an optimization when a container of indeterminate length is
being iterated over.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__getitem__",
                .get = reinterpret_cast<getter>(Descr::__getitem__::get),
                .set = reinterpret_cast<setter>(Descr::__getitem__::set),
                .doc = PyDoc_STR(
R"doc(Instance subscript operator.

See Also
--------
object.__getitem__ : Python's built-in instance subscript operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__setitem__",
                .get = reinterpret_cast<getter>(Descr::__setitem__::get),
                .set = reinterpret_cast<setter>(Descr::__setitem__::set),
                .doc = PyDoc_STR(
R"doc(Instance subscript assignment operator.

See Also
--------
object.__setitem__ : Python's built-in instance subscript assignment operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__delitem__",
                .get = reinterpret_cast<getter>(Descr::__delitem__::get),
                .set = reinterpret_cast<setter>(Descr::__delitem__::set),
                .doc = PyDoc_STR(
R"doc(Instance subscript deletion operator.

See Also
--------
object.__delitem__ : Python's built-in instance subscript deletion operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__missing__",
                .get = reinterpret_cast<getter>(Descr::__missing__::get),
                .set = reinterpret_cast<setter>(Descr::__missing__::set),
                .doc = PyDoc_STR(
R"doc(Fallback instance subscript operator.

See Also
--------
object.__missing__ : Python's built-in fallback instance subscript operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.

This method is called by `dict.__getitem__()` to implement `self[key]` when the key is
not found in the dictionary.  It is not called under any other circumstances.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__iter__",
                .get = reinterpret_cast<getter>(Descr::__iter__::get),
                .set = reinterpret_cast<setter>(Descr::__iter__::set),
                .doc = PyDoc_STR(
R"doc(Iteration operator.

See Also
--------
object.__iter__ : Python's built-in iteration operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__next__",
                .get = reinterpret_cast<getter>(Descr::__next__::get),
                .set = reinterpret_cast<setter>(Descr::__next__::set),
                .doc = PyDoc_STR(
R"doc(Iterator increment operator.

See Also
--------
object.__next__ : Python's built-in iterator increment operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__reversed__",
                .get = reinterpret_cast<getter>(Descr::__reversed__::get),
                .set = reinterpret_cast<setter>(Descr::__reversed__::set),
                .doc = PyDoc_STR(
R"doc(Reverse iteration operator.

See Also
--------
object.__reversed__ : Python's built-in reverse iteration operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__contains__",
                .get = reinterpret_cast<getter>(Descr::__contains__::get),
                .set = reinterpret_cast<setter>(Descr::__contains__::set),
                .doc = PyDoc_STR(
R"doc(Membership test operator.

See Also
--------
object.__contains__ : Python's built-in membership test operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__add__",
                .get = reinterpret_cast<getter>(Descr::__add__::get),
                .set = reinterpret_cast<setter>(Descr::__add__::set),
                .doc = PyDoc_STR(
R"doc(Addition operator.

See Also
--------
object.__add__ : Python's built-in addition operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__sub__",
                .get = reinterpret_cast<getter>(Descr::__sub__::get),
                .set = reinterpret_cast<setter>(Descr::__sub__::set),
                .doc = PyDoc_STR(
R"doc(Subtraction operator.

See Also
--------
object.__sub__ : Python's built-in subtraction operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__mul__",
                .get = reinterpret_cast<getter>(Descr::__mul__::get),
                .set = reinterpret_cast<setter>(Descr::__mul__::set),
                .doc = PyDoc_STR(
R"doc(Multiplication operator.

See Also
--------
object.__mul__ : Python's built-in multiplication operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__matmul__",
                .get = reinterpret_cast<getter>(Descr::__matmul__::get),
                .set = reinterpret_cast<setter>(Descr::__matmul__::set),
                .doc = PyDoc_STR(
R"doc(Matrix multiplication operator.

See Also
--------
object.__matmul__ : Python's built-in matrix multiplication operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__truediv__",
                .get = reinterpret_cast<getter>(Descr::__truediv__::get),
                .set = reinterpret_cast<setter>(Descr::__truediv__::set),
                .doc = PyDoc_STR(
R"doc(True division operator.

See Also
--------
object.__truediv__ : Python's built-in true division operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__floordiv__",
                .get = reinterpret_cast<getter>(Descr::__floordiv__::get),
                .set = reinterpret_cast<setter>(Descr::__floordiv__::set),
                .doc = PyDoc_STR(
R"doc(Floor division operator.

See Also
--------
object.__floordiv__ : Python's built-in floor division operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__mod__",
                .get = reinterpret_cast<getter>(Descr::__mod__::get),
                .set = reinterpret_cast<setter>(Descr::__mod__::set),
                .doc = PyDoc_STR(
R"doc(Modulus operator.

See Also
--------
object.__mod__ : Python's built-in modulus operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__divmod__",
                .get = reinterpret_cast<getter>(Descr::__divmod__::get),
                .set = reinterpret_cast<setter>(Descr::__divmod__::set),
                .doc = PyDoc_STR(
R"doc(Combined floor division and modulus operator.

See Also
--------
object.__divmod__ : Python's built-in combined floor division and modulus operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__pow__",
                .get = reinterpret_cast<getter>(Descr::__pow__::get),
                .set = reinterpret_cast<setter>(Descr::__pow__::set),
                .doc = PyDoc_STR(
R"doc(Exponentiation operator.

See Also
--------
object.__pow__ : Python's built-in exponentiation operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__lshift__",
                .get = reinterpret_cast<getter>(Descr::__lshift__::get),
                .set = reinterpret_cast<setter>(Descr::__lshift__::set),
                .doc = PyDoc_STR(
R"doc(Binary left-shift operator.

See Also
--------
object.__lshift__ : Python's built-in binary left-shift operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rshift__",
                .get = reinterpret_cast<getter>(Descr::__rshift__::get),
                .set = reinterpret_cast<setter>(Descr::__rshift__::set),
                .doc = PyDoc_STR(
R"doc(Binary right-shift operator.

See Also
--------
object.__rshift__ : Python's built-in binary right-shift operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__and__",
                .get = reinterpret_cast<getter>(Descr::__and__::get),
                .set = reinterpret_cast<setter>(Descr::__and__::set),
                .doc = PyDoc_STR(
R"doc(Bitwise AND operator.

See Also
--------
object.__and__ : Python's built-in bitwise AND operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__xor__",
                .get = reinterpret_cast<getter>(Descr::__xor__::get),
                .set = reinterpret_cast<setter>(Descr::__xor__::set),
                .doc = PyDoc_STR(
R"doc(Bitwise XOR operator.

See Also
--------
object.__xor__ : Python's built-in bitwise XOR operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__or__",
                .get = reinterpret_cast<getter>(Descr::__or__::get),
                .set = reinterpret_cast<setter>(Descr::__or__::set),
                .doc = PyDoc_STR(
R"doc(Bitwise OR operator.

See Also
--------
object.__or__ : Python's built-in bitwise OR operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__radd__",
                .get = reinterpret_cast<getter>(Descr::__radd__::get),
                .set = reinterpret_cast<setter>(Descr::__radd__::set),
                .doc = PyDoc_STR(
R"doc(Reverse addition operator.

See Also
--------
object.__radd__ : Python's built-in reverse addition operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rsub__",
                .get = reinterpret_cast<getter>(Descr::__rsub__::get),
                .set = reinterpret_cast<setter>(Descr::__rsub__::set),
                .doc = PyDoc_STR(
R"doc(Reverse subtraction operator.

See Also
--------
object.__rsub__ : Python's built-in reverse subtraction operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rmul__",
                .get = reinterpret_cast<getter>(Descr::__rmul__::get),
                .set = reinterpret_cast<setter>(Descr::__rmul__::set),
                .doc = PyDoc_STR(
R"doc(Reverse multiplication operator.

See Also
--------
object.__rmul__ : Python's built-in reverse multiplication operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rmatmul__",
                .get = reinterpret_cast<getter>(Descr::__rmatmul__::get),
                .set = reinterpret_cast<setter>(Descr::__rmatmul__::set),
                .doc = PyDoc_STR(
R"doc(Reverse matrix multiplication operator.

See Also
--------
object.__rmatmul__ : Python's built-in reverse matrix multiplication operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rtruediv__",
                .get = reinterpret_cast<getter>(Descr::__rtruediv__::get),
                .set = reinterpret_cast<setter>(Descr::__rtruediv__::set),
                .doc = PyDoc_STR(
R"doc(Reverse true division operator.

See Also
--------
object.__rtruediv__ : Python's built-in reverse true division operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rfloordiv__",
                .get = reinterpret_cast<getter>(Descr::__rfloordiv__::get),
                .set = reinterpret_cast<setter>(Descr::__rfloordiv__::set),
                .doc = PyDoc_STR(
R"doc(Reverse floor division operator.

See Also
--------
object.__rfloordiv__ : Python's built-in reverse floor division operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rmod__",
                .get = reinterpret_cast<getter>(Descr::__rmod__::get),
                .set = reinterpret_cast<setter>(Descr::__rmod__::set),
                .doc = PyDoc_STR(
R"doc(Reverse modulus operator.

See Also
--------
object.__rmod__ : Python's built-in reverse modulus operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rdivmod__",
                .get = reinterpret_cast<getter>(Descr::__rdivmod__::get),
                .set = reinterpret_cast<setter>(Descr::__rdivmod__::set),
                .doc = PyDoc_STR(
R"doc(Combined and reversed floor division and modulus operator.

See Also
--------
object.__rdivmod__ : Python's built-in reversed floor division and modulus operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rpow__",
                .get = reinterpret_cast<getter>(Descr::__rpow__::get),
                .set = reinterpret_cast<setter>(Descr::__rpow__::set),
                .doc = PyDoc_STR(
R"doc(Reverse exponentiation operator.

See Also
--------
object.__rpow__ : Python's built-in reverse exponentiation operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rlshift__",
                .get = reinterpret_cast<getter>(Descr::__rlshift__::get),
                .set = reinterpret_cast<setter>(Descr::__rlshift__::set),
                .doc = PyDoc_STR(
R"doc(Reverse binary left-shift operator.

See Also
--------
object.__rlshift__ : Python's built-in reverse binary left-shift operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rrshift__",
                .get = reinterpret_cast<getter>(Descr::__rrshift__::get),
                .set = reinterpret_cast<setter>(Descr::__rrshift__::set),
                .doc = PyDoc_STR(
R"doc(Reverse binary right-shift operator.

See Also
--------
object.__rrshift__ : Python's built-in reverse binary right-shift operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rand__",
                .get = reinterpret_cast<getter>(Descr::__rand__::get),
                .set = reinterpret_cast<setter>(Descr::__rand__::set),
                .doc = PyDoc_STR(
R"doc(Reverse bitwise AND operator.

See Also
--------
object.__rand__ : Python's built-in reverse bitwise AND operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__rxor__",
                .get = reinterpret_cast<getter>(Descr::__rxor__::get),
                .set = reinterpret_cast<setter>(Descr::__rxor__::set),
                .doc = PyDoc_STR(
R"doc(Reverse bitwise XOR operator.

See Also
--------
object.__rxor__ : Python's built-in reverse bitwise XOR operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__ror__",
                .get = reinterpret_cast<getter>(Descr::__ror__::get),
                .set = reinterpret_cast<setter>(Descr::__ror__::set),
                .doc = PyDoc_STR(
R"doc(Reverse bitwise OR operator.

See Also
--------
object.__ror__ : Python's built-in reverse bitwise OR operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__iadd__",
                .get = reinterpret_cast<getter>(Descr::__iadd__::get),
                .set = reinterpret_cast<setter>(Descr::__iadd__::set),
                .doc = PyDoc_STR(
R"doc(In-place addition operator.

See Also
--------
object.__iadd__ : Python's built-in in-place addition operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__isub__",
                .get = reinterpret_cast<getter>(Descr::__isub__::get),
                .set = reinterpret_cast<setter>(Descr::__isub__::set),
                .doc = PyDoc_STR(
R"doc(In-place subtraction operator.

See Also
--------
object.__isub__ : Python's built-in in-place subtraction operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__imul__",
                .get = reinterpret_cast<getter>(Descr::__imul__::get),
                .set = reinterpret_cast<setter>(Descr::__imul__::set),
                .doc = PyDoc_STR(
R"doc(In-place multiplication operator.

See Also
--------
object.__imul__ : Python's built-in in-place multiplication operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__imatmul__",
                .get = reinterpret_cast<getter>(Descr::__imatmul__::get),
                .set = reinterpret_cast<setter>(Descr::__imatmul__::set),
                .doc = PyDoc_STR(
R"doc(In-place matrix multiplication operator.

See Also
--------
object.__imatmul__ : Python's built-in in-place matrix multiplication operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__itruediv__",
                .get = reinterpret_cast<getter>(Descr::__itruediv__::get),
                .set = reinterpret_cast<setter>(Descr::__itruediv__::set),
                .doc = PyDoc_STR(
R"doc(In-place true division operator.

See Also
--------
object.__itruediv__ : Python's built-in in-place true division operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__ifloordiv__",
                .get = reinterpret_cast<getter>(Descr::__ifloordiv__::get),
                .set = reinterpret_cast<setter>(Descr::__ifloordiv__::set),
                .doc = PyDoc_STR(
R"doc(In-place floor division operator.

See Also
--------
object.__ifloordiv__ : Python's built-in in-place floor division operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__imod__",
                .get = reinterpret_cast<getter>(Descr::__imod__::get),
                .set = reinterpret_cast<setter>(Descr::__imod__::set),
                .doc = PyDoc_STR(
R"doc(In-place modulus operator.

See Also
--------
object.__imod__ : Python's built-in in-place modulus operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__ipow__",
                .get = reinterpret_cast<getter>(Descr::__ipow__::get),
                .set = reinterpret_cast<setter>(Descr::__ipow__::set),
                .doc = PyDoc_STR(
R"doc(In-place exponentiation operator.

See Also
--------
object.__ipow__ : Python's built-in in-place exponentiation operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__ilshift__",
                .get = reinterpret_cast<getter>(Descr::__ilshift__::get),
                .set = reinterpret_cast<setter>(Descr::__ilshift__::set),
                .doc = PyDoc_STR(
R"doc(In-place binary left-shift operator.

See Also
--------
object.__ilshift__ : Python's built-in in-place binary left-shift operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__irshift__",
                .get = reinterpret_cast<getter>(Descr::__irshift__::get),
                .set = reinterpret_cast<setter>(Descr::__irshift__::set),
                .doc = PyDoc_STR(
R"doc(In-place binary right-shift operator.

See Also
--------
object.__irshift__ : Python's built-in in-place binary right-shift operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__iand__",
                .get = reinterpret_cast<getter>(Descr::__iand__::get),
                .set = reinterpret_cast<setter>(Descr::__iand__::set),
                .doc = PyDoc_STR(
R"doc(In-place bitwise AND operator.

See Also
--------
object.__iand__ : Python's built-in in-place bitwise AND operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__ixor__",
                .get = reinterpret_cast<getter>(Descr::__ixor__::get),
                .set = reinterpret_cast<setter>(Descr::__ixor__::set),
                .doc = PyDoc_STR(
R"doc(In-place bitwise XOR operator.

See Also
--------
object.__ixor__ : Python's built-in in-place bitwise XOR operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__ior__",
                .get = reinterpret_cast<getter>(Descr::__ior__::get),
                .set = reinterpret_cast<setter>(Descr::__ior__::set),
                .doc = PyDoc_STR(
R"doc(In-place bitwise OR operator.

See Also
--------
object.__ior__ : Python's built-in in-place bitwise OR operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__neg__",
                .get = reinterpret_cast<getter>(Descr::__neg__::get),
                .set = reinterpret_cast<setter>(Descr::__neg__::set),
                .doc = PyDoc_STR(
R"doc(Unary negative operator.

See Also
--------
object.__neg__ : Python's built-in unary negative operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__pos__",
                .get = reinterpret_cast<getter>(Descr::__pos__::get),
                .set = reinterpret_cast<setter>(Descr::__pos__::set),
                .doc = PyDoc_STR(
R"doc(Unary positive operator.

See Also
--------
object.__pos__ : Python's built-in unary positive operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__abs__",
                .get = reinterpret_cast<getter>(Descr::__abs__::get),
                .set = reinterpret_cast<setter>(Descr::__abs__::set),
                .doc = PyDoc_STR(
R"doc(Absolute value operator.

See Also
--------
object.__abs__ : Python's built-in absolute value operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__invert__",
                .get = reinterpret_cast<getter>(Descr::__invert__::get),
                .set = reinterpret_cast<setter>(Descr::__invert__::set),
                .doc = PyDoc_STR(
R"doc(Unary bitwise NOT operator.

See Also
--------
object.__invert__ : Python's built-in unary bitwise NOT operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__complex__",
                .get = reinterpret_cast<getter>(Descr::__complex_py__::get),
                .set = reinterpret_cast<setter>(Descr::__complex_py__::set),
                .doc = PyDoc_STR(
R"doc(Complex conversion operator.

See Also
--------
object.__complex__ : Python's built-in complex conversion operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__int__",
                .get = reinterpret_cast<getter>(Descr::__int__::get),
                .set = reinterpret_cast<setter>(Descr::__int__::set),
                .doc = PyDoc_STR(
R"doc(Integer conversion operator.

See Also
--------
object.__int__ : Python's built-in integer conversion operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__float__",
                .get = reinterpret_cast<getter>(Descr::__float__::get),
                .set = reinterpret_cast<setter>(Descr::__float__::set),
                .doc = PyDoc_STR(
R"doc(Floating point conversion operator.

See Also
--------
object.__float__ : Python's built-in floating point conversion operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__index__",
                .get = reinterpret_cast<getter>(Descr::__index__::get),
                .set = reinterpret_cast<setter>(Descr::__index__::set),
                .doc = PyDoc_STR(
R"doc(Generalized integer conversion operator.

See Also
--------
object.__index__ : Python's built-in generalized integer conversion operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.

This method is distinct from `__int__()` in that it is generally used in the context of
indexing, such as in a slice or array subscript.  It is more permissive than the latter
method, however, and will be used in a variety of other contexts where an integer-like
object is expected.  See the Python docs for more detail.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__round__",
                .get = reinterpret_cast<getter>(Descr::__round__::get),
                .set = reinterpret_cast<setter>(Descr::__round__::set),
                .doc = PyDoc_STR(
R"doc(Numeric rounding operator.

See Also
--------
object.__round__ : Python's built-in rounding operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__trunc__",
                .get = reinterpret_cast<getter>(Descr::__trunc__::get),
                .set = reinterpret_cast<setter>(Descr::__trunc__::set),
                .doc = PyDoc_STR(
R"doc(Numeric truncation operator.

See Also
--------
object.__trunc__ : Python's built-in truncation operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__floor__",
                .get = reinterpret_cast<getter>(Descr::__floor__::get),
                .set = reinterpret_cast<setter>(Descr::__floor__::set),
                .doc = PyDoc_STR(
R"doc(Numeric floor operator.

See Also
--------
object.__floor__ : Python's built-in floor operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__ceil__",
                .get = reinterpret_cast<getter>(Descr::__ceil__::get),
                .set = reinterpret_cast<setter>(Descr::__ceil__::set),
                .doc = PyDoc_STR(
R"doc(Numeric ceiling operator.

See Also
--------
object.__ceil__ : Python's built-in ceiling operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__enter__",
                .get = reinterpret_cast<getter>(Descr::__enter__::get),
                .set = reinterpret_cast<setter>(Descr::__enter__::set),
                .doc = PyDoc_STR(
R"doc(Context manager entry operator.

See Also
--------
object.__enter__ : Python's built-in context manager entry operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__exit__",
                .get = reinterpret_cast<getter>(Descr::__exit__::get),
                .set = reinterpret_cast<setter>(Descr::__exit__::set),
                .doc = PyDoc_STR(
R"doc(Context manager exit operator.

See Also
--------
object.__exit__ : Python's built-in context manager exit operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__buffer__",
                .get = reinterpret_cast<getter>(Descr::__buffer__::get),
                .set = reinterpret_cast<setter>(Descr::__buffer__::set),
                .doc = PyDoc_STR(
R"doc(Buffer acquisition operator.

See Also
--------
object.__buffer__ : Python's built-in buffer acquisition operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__release_buffer__",
                .get = reinterpret_cast<getter>(Descr::__release_buffer__::get),
                .set = reinterpret_cast<setter>(Descr::__release_buffer__::set),
                .doc = PyDoc_STR(
R"doc(Buffer release operator.

See Also
--------
object.__release_buffer__ : Python's built-in buffer release operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__await__",
                .get = reinterpret_cast<getter>(Descr::__await__::get),
                .set = reinterpret_cast<setter>(Descr::__await__::set),
                .doc = PyDoc_STR(
R"doc(Async await operator.

See Also
--------
object.__await__ : Python's built-in async await operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__aiter__",
                .get = reinterpret_cast<getter>(Descr::__aiter__::get),
                .set = reinterpret_cast<setter>(Descr::__aiter__::set),
                .doc = PyDoc_STR(
R"doc(Async iteration operator.

See Also
--------
object.__aiter__ : Python's built-in async iteration operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__anext__",
                .get = reinterpret_cast<getter>(Descr::__anext__::get),
                .set = reinterpret_cast<setter>(Descr::__anext__::set),
                .doc = PyDoc_STR(
R"doc(Async iterator increment operator.

See Also
--------
object.__anext__ : Python's built-in async iterator increment operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__aenter__",
                .get = reinterpret_cast<getter>(Descr::__aenter__::get),
                .set = reinterpret_cast<setter>(Descr::__aenter__::set),
                .doc = PyDoc_STR(
R"doc(Async context manager entry operator.

See Also
--------
object.__aenter__ : Python's built-in async context manager entry operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {
                .name = "__aexit__",
                .get = reinterpret_cast<getter>(Descr::__aexit__::get),
                .set = reinterpret_cast<setter>(Descr::__aexit__::set),
                .doc = PyDoc_STR(
R"doc(Async context manager exit operator.

See Also
--------
object.__aexit__ : Python's built-in async context manager exit operator.

Notes
-----
When assigned to, the metaclass will store this method in an internal overload set,
which allows it to be overloaded equivalently from both languages.  Otherwise, the
metaclass will traverse the MRO to give the same result as base Python.  If the method
is deleted, the metaclass will reset the slot to its original value.)doc"
                ),
                .closure = nullptr
            },
            {nullptr, nullptr, nullptr, nullptr, nullptr}
        };

    };

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


template <>
struct interface<Type<BertrandMeta>> {};


/* The type object associated with Bertrand's metaclass.  This has to be separate
specialization to prevent Bertrand's generic `Type<T>` implementation from attempting
to make this an instance of itself, which would cause a circular dependency. */
template <>
struct Type<BertrandMeta> : Object, interface<Type<BertrandMeta>>, impl::TypeTag {
    struct __python__ : def<__python__, Type>, PyTypeObject {
        static Type __import__();
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
struct __isinstance__<T, BertrandMeta> : returns<bool> {
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
            return ptr(cls)->_instancecheck(ptr(cls), ptr(obj));
        } else {
            return false;
        }
    }
};


template <typename T>
struct __issubclass__<T, BertrandMeta> : returns<bool> {
    static constexpr bool operator()() {
        return impl::has_export<T>;
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
            return ptr(cls)->_subclasscheck(ptr(cls), ptr(obj));
        } else {
            return false;
        }
    }
};


template <>
struct __len__<BertrandMeta>                                : returns<size_t> {};
template <>
struct __iter__<BertrandMeta>                               : returns<BertrandMeta> {};
template <typename... T>
struct __getitem__<BertrandMeta, Type<T>...>                : returns<BertrandMeta> {
    static auto operator()(const BertrandMeta& cls, const Type<T>&... key) {
        if (ptr(cls)->templates == nullptr) {
            throw TypeError("class has no template instantiations");
        }
        PyObject* tuple = PyTuple_Pack(sizeof...(T), ptr(key)...);
        if (tuple == nullptr) {
            Exception::from_python();
        }
        PyObject* value = PyDict_GetItem(ptr(cls)->templates, tuple);
        Py_DECREF(tuple);
        if (value == nullptr) {
            Exception::from_python();
        }
        return steal<BertrandMeta>(value);
    }
};


template <impl::has_export T>
struct __len__<Type<T>>                                     : returns<size_t> {};
template <impl::has_export T>
struct __iter__<Type<T>>                                    : returns<BertrandMeta> {};
template <impl::has_export T, typename... U>
struct __getitem__<Type<T>, Type<U>...>                     : returns<BertrandMeta> {
    static auto operator()(const Type<T>& cls, const Type<U>&... key) {
        if (ptr(cls)->templates == nullptr) {
            throw TypeError("class has no template instantiations");
        }
        PyObject* tuple = PyTuple_Pack(sizeof...(U), ptr(key)...);
        if (tuple == nullptr) {
            Exception::from_python();
        }
        PyObject* value = PyDict_GetItem(ptr(cls)->templates, tuple);
        Py_DECREF(tuple);
        if (value == nullptr) {
            Exception::from_python();
        }
        return steal<BertrandMeta>(value);
    }
};


/// TODO: __call__, __cast__.  Calling an instance of the metaclass will produce an
/// Object.  Casting a metaclass to a specific type will incur an `issubclass()` check.
/// similar to Type<Object>


/* Implicitly convert an instance of the metaclass to a specific `Type<T>`
specialization by applying a dynamic `issubclass()` check. */
template <impl::has_export To>
struct __cast__<BertrandMeta, Type<To>>                     : returns<Type<To>> {
    static auto operator()(const BertrandMeta& from) {
        if (issubclass<typename __object__<To>::type>(from)) {
            return borrow<Type<To>>(ptr(from));
        } else {
            throw TypeError(
                "cannot convert Python type from '" + repr(from) + "' to '" +
                repr(Type<To>()) + "'"
            );
        }
    }
    static auto operator()(BertrandMeta&& from) {
        if (issubclass<typename __object__<To>::type>(from)) {
            return steal<Type<To>>(release(from));
        } else {
            throw TypeError(
                "cannot convert Python type from '" + repr(from) + "' to '" +
                repr(Type<To>()) + "'"
            );
        }
    }
};


/* Generic `Type<T>` specialization delegates to `T::__python__` and sets the type's
metaclass accordingly.  If the wrapper's `__python__` type implements `__export__()`,
then the metaclass will be set to `BertrandMeta::__python__`.  Otherwise, it defaults
to `PyTypeObject`. */
template <std::derived_from<Object> Wrapper>
struct Type<Wrapper> : Object, interface<Type<Wrapper>> {
private:

    template <typename T>
    static constexpr bool has_export = requires() {
        { &T::__export__ };
    };

    using metaclass = std::conditional_t<
        has_export<typename Wrapper::__python__>,
        BertrandMeta::__python__,
        PyTypeObject
    >;

public:
    struct __python__ : def<__python__, Type>, metaclass {
        static Type __import__() {
            return Wrapper::__python__::__import__();
        }
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


inline Type<Object> Object::__python__::__import__() {
    return borrow<Type<Object>>(reinterpret_cast<PyObject*>(
        &PyBaseObject_Type
    ));
}


inline Type<Code> Code::__python__::__import__() {
    return borrow<Type<Code>>(reinterpret_cast<PyObject*>(&PyCode_Type));
}


inline Type<Frame> Frame::__python__::__import__() {
    return borrow<Type<Frame>>(
        reinterpret_cast<PyObject*>(&PyFrame_Type)
    );
}


inline Type<Traceback> Traceback::__python__::__import__() {
    return borrow<Type<Traceback>>(
        reinterpret_cast<PyObject*>(&PyTraceBack_Type)
    );
}


#define IMPORT_EXCEPTION(CLS, PYTYPE)                                                   \
    inline Type<CLS> CLS::__python__::__import__() {                                    \
        return borrow<Type<CLS>>(PYTYPE);                                   \
    }


IMPORT_EXCEPTION(Exception, PyExc_Exception)
IMPORT_EXCEPTION(ArithmeticError, PyExc_ArithmeticError)
    IMPORT_EXCEPTION(FloatingPointError, PyExc_FloatingPointError)
    IMPORT_EXCEPTION(OverflowError, PyExc_OverflowError)
    IMPORT_EXCEPTION(ZeroDivisionError, PyExc_ZeroDivisionError)
IMPORT_EXCEPTION(AssertionError, PyExc_AssertionError)
IMPORT_EXCEPTION(AttributeError, PyExc_AttributeError)
IMPORT_EXCEPTION(BufferError, PyExc_BufferError)
IMPORT_EXCEPTION(EOFError, PyExc_EOFError)
IMPORT_EXCEPTION(ImportError, PyExc_ImportError)
    IMPORT_EXCEPTION(ModuleNotFoundError, PyExc_ModuleNotFoundError)
IMPORT_EXCEPTION(LookupError, PyExc_LookupError)
    IMPORT_EXCEPTION(IndexError, PyExc_IndexError)
    IMPORT_EXCEPTION(KeyError, PyExc_KeyError)
IMPORT_EXCEPTION(MemoryError, PyExc_MemoryError)
IMPORT_EXCEPTION(NameError, PyExc_NameError)
    IMPORT_EXCEPTION(UnboundLocalError, PyExc_UnboundLocalError)
IMPORT_EXCEPTION(OSError, PyExc_OSError)
    IMPORT_EXCEPTION(BlockingIOError, PyExc_BlockingIOError)
    IMPORT_EXCEPTION(ChildProcessError, PyExc_ChildProcessError)
    IMPORT_EXCEPTION(ConnectionError, PyExc_ConnectionError)
        IMPORT_EXCEPTION(BrokenPipeError, PyExc_BrokenPipeError)
        IMPORT_EXCEPTION(ConnectionAbortedError, PyExc_ConnectionAbortedError)
        IMPORT_EXCEPTION(ConnectionRefusedError, PyExc_ConnectionRefusedError)
        IMPORT_EXCEPTION(ConnectionResetError, PyExc_ConnectionResetError)
    IMPORT_EXCEPTION(FileExistsError, PyExc_FileExistsError)
    IMPORT_EXCEPTION(FileNotFoundError, PyExc_FileNotFoundError)
    IMPORT_EXCEPTION(InterruptedError, PyExc_InterruptedError)
    IMPORT_EXCEPTION(IsADirectoryError, PyExc_IsADirectoryError)
    IMPORT_EXCEPTION(NotADirectoryError, PyExc_NotADirectoryError)
    IMPORT_EXCEPTION(PermissionError, PyExc_PermissionError)
    IMPORT_EXCEPTION(ProcessLookupError, PyExc_ProcessLookupError)
    IMPORT_EXCEPTION(TimeoutError, PyExc_TimeoutError)
IMPORT_EXCEPTION(ReferenceError, PyExc_ReferenceError)
IMPORT_EXCEPTION(RuntimeError, PyExc_RuntimeError)
    IMPORT_EXCEPTION(NotImplementedError, PyExc_NotImplementedError)
    IMPORT_EXCEPTION(RecursionError, PyExc_RecursionError)
IMPORT_EXCEPTION(StopAsyncIteration, PyExc_StopAsyncIteration)
IMPORT_EXCEPTION(StopIteration, PyExc_StopIteration)
IMPORT_EXCEPTION(SyntaxError, PyExc_SyntaxError)
    IMPORT_EXCEPTION(IndentationError, PyExc_IndentationError)
        IMPORT_EXCEPTION(TabError, PyExc_TabError)
IMPORT_EXCEPTION(SystemError, PyExc_SystemError)
IMPORT_EXCEPTION(TypeError, PyExc_TypeError)
IMPORT_EXCEPTION(ValueError, PyExc_ValueError)
    IMPORT_EXCEPTION(UnicodeError, PyExc_UnicodeError)
        IMPORT_EXCEPTION(UnicodeDecodeError, PyExc_UnicodeDecodeError)
        IMPORT_EXCEPTION(UnicodeEncodeError, PyExc_UnicodeEncodeError)
        IMPORT_EXCEPTION(UnicodeTranslateError, PyExc_UnicodeTranslateError)


#undef IMPORT_EXCEPTION


inline PyObject* BertrandMeta::__python__::class_doc(__python__* cls, void*) {
    std::string doc = cls->tp_doc;
    if (cls->templates) {
        if (!doc.ends_with("\n")) {
            doc += "\n";
        }
        doc += "\n";
        std::string header = "Instantations (" + std::to_string(PyDict_Size(
            cls->templates
        )) + ")";
        doc += header + "\n" + std::string(header.size() - 1, '-') + "\n";

        std::string prefix = "    " + std::string(cls->tp_name) + "[";
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
                    reinterpret_cast<PyObject*>(ptr(Type<BertrandMeta>()))
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


template <static_str ModName>
inline Type<BertrandMeta> BertrandMeta::__python__::__export__(Module<ModName> module) {
    static PyType_Slot slots[] = {
        {Py_tp_doc, const_cast<char*>(__doc__.buffer)},
        {Py_tp_dealloc, reinterpret_cast<void*>(__dealloc__)},
        {Py_tp_traverse, reinterpret_cast<void*>(__traverse__)},
        {Py_tp_clear, reinterpret_cast<void*>(__clear__)},
        {Py_tp_repr, reinterpret_cast<void*>(class_repr)},
        {Py_tp_getattro, reinterpret_cast<void*>(class_getattro)},
        {Py_tp_setattro, reinterpret_cast<void*>(class_setattro)},
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
    return steal<Type<BertrandMeta>>(cls);
}


template <static_str Name, static_str ModName>
inline BertrandMeta BertrandMeta::__python__::stub_type(
    Module<ModName>& module,
    std::string&& doc
) {
    static std::string doc_str;
    if (doc_str.empty()) {
        if (doc.empty()) {
            doc_str = "A public interface for the '" + ModName + "." + Name;
            doc_str += "' template hierarchy.\n\n";
            doc_str +=
R"doc(This type cannot be used directly, but indexing it with one or more Python
types allows the user to navigate instantiations with the same syntax as C++.  Note
that Python cannot instantiate C++ templates directly; this class simply acesses the
existing instantiations at the C++ level and retrieves their corresponding Python
types.)doc";
        } else {
            doc_str = std::move(doc);
        }
    }
    static PyType_Slot slots[] = {
        {Py_tp_doc, const_cast<char*>(doc_str.c_str())},
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
        new (cls) __python__(Name);
        cls->_instancecheck = instancecheck;
        cls->_subclasscheck = subclasscheck;
        cls->templates = PyDict_New();
        if (cls->templates == nullptr) {
            Exception::from_python();
        }
        return steal<BertrandMeta>(reinterpret_cast<PyObject*>(cls));
    } catch (...) {
        Py_DECREF(cls);
        throw;
    }
}


inline PyObject* BertrandMeta::__python__::Slots::nb_add(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_add) {
            result = meta->base_nb_add(lhs, rhs);
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
    } else if (meta->base_nb_add) {
        return meta->base_nb_add(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_subtract(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_subtract) {
            result = meta->base_nb_subtract(lhs, rhs);
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
    } else if (meta->base_nb_subtract) {
        return meta->base_nb_subtract(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_multiply(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_multiply) {
            result = meta->base_nb_multiply(lhs, rhs);
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
    } else if (meta->base_nb_multiply) {
        return meta->base_nb_multiply(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_remainder(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_remainder) {
            result = meta->base_nb_remainder(lhs, rhs);
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
    } else if (meta->base_nb_remainder) {
        return meta->base_nb_remainder(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_divmod(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_divmod) {
            result = meta->base_nb_divmod(lhs, rhs);
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
    } else if (meta->base_nb_divmod) {
        return meta->base_nb_divmod(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_power(PyObject* base, PyObject* exp, PyObject* mod) {
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
        } else if (meta->base_nb_power) {
            result = meta->base_nb_power(base, exp, mod);
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
        } else if (meta->base_nb_power) {
            result = meta->base_nb_power(exp, base, mod);
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
    } else if (meta->base_nb_power) {
        return meta->base_nb_power(mod, base, exp);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_lshift(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_lshift) {
            result = meta->base_nb_lshift(lhs, rhs);
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
    } else if (meta->base_nb_lshift) {
        return meta->base_nb_lshift(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_rshift(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_rshift) {
            result = meta->base_nb_rshift(lhs, rhs);
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
    } else if (meta->base_nb_rshift) {
        return meta->base_nb_rshift(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_and(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_and) {
            result = meta->base_nb_and(lhs, rhs);
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
    } else if (meta->base_nb_and) {
        return meta->base_nb_and(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_xor(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_xor) {
            result = meta->base_nb_xor(lhs, rhs);
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
    } else if (meta->base_nb_xor) {
        return meta->base_nb_xor(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_or(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_or) {
            result = meta->base_nb_or(lhs, rhs);
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
    } else if (meta->base_nb_or) {
        return meta->base_nb_or(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_floor_divide(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_floor_divide) {
            result = meta->base_nb_floor_divide(lhs, rhs);
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
    } else if (meta->base_nb_floor_divide) {
        return meta->base_nb_floor_divide(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_true_divide(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_true_divide) {
            result = meta->base_nb_true_divide(lhs, rhs);
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
    } else if (meta->base_nb_true_divide) {
        return meta->base_nb_true_divide(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


inline PyObject* BertrandMeta::__python__::Slots::nb_matrix_multiply(PyObject* lhs, PyObject* rhs) {
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
        } else if (meta->base_nb_matrix_multiply) {
            result = meta->base_nb_matrix_multiply(lhs, rhs);
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
    } else if (meta->base_nb_matrix_multiply) {
        return meta->base_nb_matrix_multiply(rhs, lhs);
    }
    Py_RETURN_NOTIMPLEMENTED;
}


////////////////////////
////    BINDINGS    ////
////////////////////////


template <typename Cls, typename CRTP, typename Wrapper, static_str ModName>
struct Object::Bindings {
private:
    using Meta = BertrandMeta::__python__;
    inline static std::vector<PyMethodDef> tp_methods;
    inline static std::vector<PyGetSetDef> tp_getset;
    inline static std::vector<PyMemberDef> tp_members;

    /// TODO: most, if not all, of the complicated stuff here can be moved into the
    /// impl:: namespace and just called from here.

    struct Member {
        bool var = false;
        bool property = false;
        bool method = false;
        bool class_var = false;
        bool class_property = false;
        bool class_method = false;
        bool type = false;
        bool template_interface = false;
        std::vector<std::function<void(Type<Wrapper>&)>> callbacks;
    };

    /// TODO: Perhaps the way to implement register_type is to use a C++ unordered
    /// map that maps std::type_index to a context object that contains all the
    /// information needed to generate bindings for that type.  In fact, that
    /// might directly reference the type's template instantiations, which
    /// can be dynamically inserted into during the module's export process.
    /// -> Such a map can be added to the bertrand.python module in per-module
    /// state.

    /* Insert a Python type into the global `bertrand.python.types` registry, so
    that it can be mapped to its C++ equivalent and used when generating Python
    bindings. */
    static void register_type(Module<ModName>& mod, PyTypeObject* type);

    /// TODO: Dunder requirements might simply be specified in the function's template
    /// signature, and then use automated checks to ensure that the signature is
    /// observed.  That would probably eliminate the need for the Dunder struct.

    /* A collection of template constraints for particular overloads of the
    `method<"name">(&func)` helper that forces compatibility with Python's dunder
    interface. */
    struct Dunder {

        template <typename Return, typename Func, typename... Args>
        static constexpr bool static_invocable = false;
        template <
            typename Return,
            typename R,
            typename... A,
            typename... Args
        >
        static constexpr bool static_invocable<
            Return,
            R(*)(A...),
            Args...
        > = std::is_invocable_r_v<
            Return,
            R(*)(A...),
            Args...
        >;
        template <typename Return, typename R, typename... A, typename... Args>
        static constexpr bool static_invocable<
            Return,
            R(*)(A...) noexcept,
            Args...
        > = std::is_invocable_r_v<
            Return,
            R(*)(A...) noexcept,
            Args...
        >;

        template <typename Return, typename Func, typename... Args>
        static constexpr bool member_invocable = false;
        template <
            typename Return,
            typename R,
            typename T,
            typename... A,
            typename... Args
        >
        static constexpr bool member_invocable<
            Return,
            R(T::*)(A...),
            Args...
        > = std::is_invocable_r_v<
            Return,
            R(T::*)(A...),
            Args...
        >;
        template <typename Return, typename R, typename T, typename... A, typename... Args>
        static constexpr bool member_invocable<
            Return,
            R(T::*)(A...) volatile,
            Args...
        > = std::is_invocable_r_v<
            Return,
            R(T::*)(A...) volatile,
            Args...
        >;
        template <typename Return, typename R, typename T, typename... A, typename... Args>
        static constexpr bool member_invocable<
            Return,
            R(T::*)(A...) noexcept,
            Args...
        > = std::is_invocable_r_v<
            Return,
            R(T::*)(A...) noexcept,
            Args...
        >;
        template <typename Return, typename R, typename T, typename... A, typename... Args>
        static constexpr bool member_invocable<
            Return,
            R(T::*)(A...) volatile noexcept,
            Args...
        > = std::is_invocable_r_v<
            Return,
            R(T::*)(A...) volatile noexcept,
            Args...
        >;

        template <typename Return, typename Func, typename... Args>
        static constexpr bool const_member_invocable = false;
        template <typename Return, typename R, typename T, typename... A, typename... Args>
        static constexpr bool const_member_invocable<
            Return,
            R(T::*)(A...) const,
            Args...
        > = std::is_invocable_r_v<
            Return,
            R(T::*)(A...) const,
            Args...
        >;
        template <typename Return, typename R, typename T, typename... A, typename... Args>
        static constexpr bool const_member_invocable<
            Return,
            R(T::*)(A...) const volatile,
            Args...
        > = std::is_invocable_r_v<
            Return,
            R(T::*)(A...) const volatile,
            Args...
        >;
        template <typename Return, typename R, typename T, typename... A, typename... Args>
        static constexpr bool const_member_invocable<
            Return,
            R(T::*)(A...) const noexcept,
            Args...
        > = std::is_invocable_r_v<
            Return,
            R(T::*)(A...) const noexcept,
            Args...
        >;
        template <typename Return, typename R, typename T, typename... A, typename... Args>
        static constexpr bool const_member_invocable<
            Return,
            R(T::*)(A...) const volatile noexcept,
            Args...
        > = std::is_invocable_r_v<
            Return,
            R(T::*)(A...) const volatile noexcept,
            Args...
        >;

        template <static_str Name>
        struct check {
            template <typename T>
            static constexpr bool value = false;

            template <typename R, typename... Ts>
                requires (__object__<R>::enable && (__object__<Ts>::enable && ...))
            static constexpr bool value<R(*)(Ts...)> = true;

            template <typename R, typename T, typename... Ts>
                requires (__object__<R>::enable && (__object__<Ts>::enable && ...))
            static constexpr bool value<R(T::*)(Ts...)> = true;

            template <typename R, typename T, typename... Ts>
                requires (__object__<R>::enable && (__object__<Ts>::enable && ...))
            static constexpr bool value<R(T::*)(Ts...) const> = true;

            template <typename R, typename T, typename... Ts>
                requires (__object__<R>::enable && (__object__<Ts>::enable && ...))
            static constexpr bool value<R(T::*)(Ts...) volatile> = true;

            template <typename R, typename T, typename... Ts>
                requires (__object__<R>::enable && (__object__<Ts>::enable && ...))
            static constexpr bool value<R(T::*)(Ts...) const volatile> = true;

            template <typename R, typename... Ts>
                requires (__object__<R>::enable && (__object__<Ts>::enable && ...))
            static constexpr bool value<R(*)(Ts...) noexcept> = true;

            template <typename R, typename T, typename... Ts>
                requires (__object__<R>::enable && (__object__<Ts>::enable && ...))
            static constexpr bool value<R(T::*)(Ts...) noexcept> = true;

            template <typename R, typename T, typename... Ts>
                requires (__object__<R>::enable && (__object__<Ts>::enable && ...))
            static constexpr bool value<R(T::*)(Ts...) const noexcept> = true;

            template <typename R, typename T, typename... Ts>
                requires (__object__<R>::enable && (__object__<Ts>::enable && ...))
            static constexpr bool value<R(T::*)(Ts...) volatile noexcept> = true;

            template <typename R, typename T, typename... Ts>
                requires (__object__<R>::enable && (__object__<Ts>::enable && ...))
            static constexpr bool value<R(T::*)(Ts...) const volatile noexcept> = true;

        };

        template <>
        struct check<"__new__"> {
            template <typename T>
            static constexpr bool value = false;

            template <typename... Ts>
            static constexpr bool value<Cls(*)(Ts...)> =
                (std::convertible_to<Object, Ts> && ...);
            template <typename... Ts>
            static constexpr bool value<Cls(*)(Ts...) noexcept> =
                (std::convertible_to<Object, Ts> && ...);
        };

        template <>
        struct check<"__init__"> {
            template <typename T>
            static constexpr bool value = false;

            template <typename T, typename... Ts>
            static constexpr bool value<void(*)(T, Ts...)> =
                std::is_invocable_v<void(*)(T, Ts...), Cls, Ts...> &&
                (std::convertible_to<Object, Ts> && ...);
            template <typename T, typename... Ts>
            static constexpr bool value<void(*)(T, Ts...) noexcept> =
                std::is_invocable_v<void(*)(T, Ts...) noexcept, Cls, Ts...> &&
                (std::convertible_to<Object, Ts> && ...);

            template <typename... Ts>
            static constexpr bool value<void(Cls::*)(Ts...)> =
                (std::convertible_to<Object, Ts> && ...);
            template <typename... Ts>
            static constexpr bool value<void(Cls::*)(Ts...) volatile> =
                (std::convertible_to<Object, Ts> && ...);
            template <typename... Ts>
            static constexpr bool value<void(Cls::*)(Ts...) noexcept> =
                (std::convertible_to<Object, Ts> && ...);
            template <typename... Ts>
            static constexpr bool value<void(Cls::*)(Ts...) volatile noexcept> =
                (std::convertible_to<Object, Ts> && ...);
        };

        template <>
        struct check<"__del__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, Cls, T> ||
                member_invocable<void, T>;
        };

        template <>
        struct check<"__repr__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Str, Cls, T> ||
                member_invocable<Str, T> ||
                const_member_invocable<Str, T>;
        };

        template <>
        struct check<"__str__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Str, Cls, T> ||
                member_invocable<Str, T> ||
                const_member_invocable<Str, T>;
        };

        template <>
        struct check<"__bytes__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bytes, Cls, T> ||
                member_invocable<Bytes, T> ||
                const_member_invocable<Bytes, T>;
        };

        template <>
        struct check<"__format__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Str, T, Cls, Str> ||
                member_invocable<Str, T, Str> ||
                const_member_invocable<Str, T, Str>;
        };

        template <>
        struct check<"__lt__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bool, T, Cls, Object> ||
                member_invocable<Bool, T, Object> ||
                const_member_invocable<Bool, T, Object>;
        };

        template <>
        struct check<"__le__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bool, T, Cls, Object> ||
                member_invocable<Bool, T, Object> ||
                const_member_invocable<Bool, T, Object>;
        };

        template <>
        struct check<"__eq__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bool, T, Cls, Object> ||
                member_invocable<Bool, T, Object> ||
                const_member_invocable<Bool, T, Object>;
        };

        template <>
        struct check<"__ne__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bool, T, Cls, Object> ||
                member_invocable<Bool, T, Object> ||
                const_member_invocable<Bool, T, Object>;
        };

        template <>
        struct check<"__ge__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bool, T, Cls, Object> ||
                member_invocable<Bool, T, Object> ||
                const_member_invocable<Bool, T, Object>;
        };

        template <>
        struct check<"__gt__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bool, T, Cls, Object> ||
                member_invocable<Bool, T, Object> ||
                const_member_invocable<Bool, T, Object>;
        };

        template <>
        struct check<"__hash__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Int, T, Cls> ||
                member_invocable<Int, T> ||
                const_member_invocable<Int, T>;
        };

        template <>
        struct check<"__bool__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bool, T, Cls> ||
                member_invocable<Bool, T> ||
                const_member_invocable<Bool, T>;
        };

        template <>
        struct check<"__getattr__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Str> ||
                member_invocable<Object, T, Str> ||
                const_member_invocable<Object, T, Str>;
        };

        template <>
        struct check<"__getattribute__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Str> ||
                member_invocable<Object, T, Str> ||
                const_member_invocable<Object, T, Str>;
        };

        template <>
        struct check<"__setattr__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, T, Cls, Str, Object> ||
                member_invocable<void, T, Str, Object> ||
                const_member_invocable<void, T, Str, Object>;
        };

        template <>
        struct check<"__delattr__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, T, Cls, Str> ||
                member_invocable<void, T, Str> ||
                const_member_invocable<void, T, Str>;
        };

        template <>
        struct check<"__dir__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<List<Str>, T, Cls> ||
                member_invocable<List<Str>, T> ||
                const_member_invocable<List<Str>, T>;
        };

        template <>
        struct check<"__get__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object, Type<Object>> ||
                member_invocable<Object, T, Object, Type<Object>> ||
                const_member_invocable<Object, T, Object, Type<Object>>;
        };

        template <>
        struct check<"__set__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, T, Cls, Object, Object> ||
                member_invocable<void, T, Object, Object> ||
                const_member_invocable<void, T, Object, Object>;
        };

        template <>
        struct check<"__delete__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, T, Cls, Object> ||
                member_invocable<void, T, Object> ||
                const_member_invocable<void, T, Object>;
        };

        template <>
        struct check<"__init_subclass__"> {
            template <typename T>
            static constexpr bool value = false;

            template <typename T, typename... Ts>
            static constexpr bool value<void(*)(T, Ts...)> =
                std::is_invocable_v<void(*)(T, Ts...), Cls, Ts...> &&
                (std::convertible_to<Object, Ts> && ...);
            template <typename T, typename... Ts>
            static constexpr bool value<void(*)(T, Ts...) noexcept> =
                std::is_invocable_v<void(*)(T, Ts...) noexcept, Cls, Ts...> &&
                (std::convertible_to<Object, Ts> && ...);
        };

        template <>
        struct check<"__set_name__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, T, Cls, Type<Object>, Str> ||
                member_invocable<void, T, Type<Object>, Str> ||
                const_member_invocable<void, T, Type<Object>, Str>;
        };

        template <>
        struct check<"__mro_entries__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Tuple<Type<Object>>, T, Cls, Tuple<Type<Object>>> ||
                member_invocable<Tuple<Type<Object>>, T, Tuple<Type<Object>>> ||
                const_member_invocable<Tuple<Type<Object>>, T, Tuple<Type<Object>>>;
        };

        template <>
        struct check<"__instancecheck__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bool, T, Cls, Object> ||
                member_invocable<Bool, T, Object> ||
                const_member_invocable<Bool, T, Object>;
        };

        template <>
        struct check<"__subclasscheck__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bool, T, Cls, Object> ||
                member_invocable<Bool, T, Object> ||
                const_member_invocable<Bool, T, Object>;
        };

        template <>
        struct check<"__class_getitem__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__call__"> {
            template <typename T>
            static constexpr bool value = false;

            template <typename R, typename T, typename... Ts>
            static constexpr bool value<R(*)(T, Ts...)> =
                std::is_invocable_v<R(*)(T, Ts...), Cls, Ts...> &&
                (std::is_void_v<R> || std::convertible_to<R, Object>) &&
                (std::convertible_to<Object, Ts> && ...);
            template <typename R, typename T, typename... Ts>
            static constexpr bool value<R(*)(T, Ts...) noexcept> =
                std::is_invocable_v<R(*)(T, Ts...) noexcept, Cls, Ts...> &&
                (std::is_void_v<R> || std::convertible_to<R, Object>) &&
                (std::convertible_to<Object, Ts> && ...);

            template <typename R, typename... Ts>
            static constexpr bool value<R(Cls::*)(Ts...)> =
                (std::is_void_v<R> || std::convertible_to<R, Object>) &&
                (std::convertible_to<Object, Ts> && ...);
            template <typename R, typename... Ts>
            static constexpr bool value<R(Cls::*)(Ts...) volatile> =
                (std::is_void_v<R> || std::convertible_to<R, Object>) &&
                (std::convertible_to<Object, Ts> && ...);
            template <typename R, typename... Ts>
            static constexpr bool value<R(Cls::*)(Ts...) noexcept> =
                (std::is_void_v<R> || std::convertible_to<R, Object>) &&
                (std::convertible_to<Object, Ts> && ...);
            template <typename R, typename... Ts>
            static constexpr bool value<R(Cls::*)(Ts...) volatile noexcept> =
                (std::is_void_v<R> || std::convertible_to<R, Object>) &&
                (std::convertible_to<Object, Ts> && ...);

            template <typename R, typename... Ts>
            static constexpr bool value<R(Cls::*)(Ts...) const> =
                (std::is_void_v<R> || std::convertible_to<R, Object>) &&
                (std::convertible_to<Object, Ts> && ...);
            template <typename R, typename... Ts>
            static constexpr bool value<R(Cls::*)(Ts...) const volatile> =
                (std::is_void_v<R> || std::convertible_to<R, Object>) &&
                (std::convertible_to<Object, Ts> && ...);
            template <typename R, typename... Ts>
            static constexpr bool value<R(Cls::*)(Ts...) const noexcept> =
                (std::is_void_v<R> || std::convertible_to<R, Object>) &&
                (std::convertible_to<Object, Ts> && ...);
            template <typename R, typename... Ts>
            static constexpr bool value<R(Cls::*)(Ts...) const volatile noexcept> =
                (std::is_void_v<R> || std::convertible_to<R, Object>) &&
                (std::convertible_to<Object, Ts> && ...);
        };

        template <>
        struct check<"__len__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Int, T, Cls> ||
                member_invocable<Int, T> ||
                const_member_invocable<Int, T>;
        };

        template <>
        struct check<"__length_hint__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Int, T, Cls> ||
                member_invocable<Int, T> ||
                const_member_invocable<Int, T>;
        };

        template <>
        struct check<"__getitem__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__setitem__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, T, Cls, Object, Object> ||
                member_invocable<void, T, Object, Object> ||
                const_member_invocable<void, T, Object, Object>;
        };

        template <>
        struct check<"__delitem__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, T, Cls, Object> ||
                member_invocable<void, T, Object> ||
                const_member_invocable<void, T, Object>;
        };

        template <>
        struct check<"__missing__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        /// TODO: figure out how to handle overloading __iter__/__reversed__/__aiter__
        // template <>
        // struct check<"__iter__"> {
        //     template <typename T>
        //     static constexpr bool value =
        //         static_invocable<Object, T, Cls> ||
        //         member_invocable<Object, T> ||
        //         const_member_invocable<Object, T>;
        // };

        template <>
        struct check<"__next__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T>;
        };

        // template <>
        // struct check<"__reversed__"> {
        //     template <typename T>
        //     static constexpr bool value =
        //         static_invocable<Object, T, Cls> ||
        //         member_invocable<Object, T> ||
        //         const_member_invocable<Object, T>;
        // };

        template <>
        struct check<"__contains__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Bool, T, Cls, Object> ||
                member_invocable<Bool, T, Object> ||
                const_member_invocable<Bool, T, Object>;
        };

        template <>
        struct check<"__add__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__sub__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__mul__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int> ||
                const_member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__matmul__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__truediv__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__floordiv__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__mod__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__divmod__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Tuple<Object>, T, Cls, Object> ||
                member_invocable<Tuple<Object>, T, Object> ||
                const_member_invocable<Tuple<Object>, T, Object>;
        };

        template <>
        struct check<"__pow__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object, Object> ||
                member_invocable<Object, T, Object, Object> ||
                const_member_invocable<Object, T, Object, Object>;
        };

        template <>
        struct check<"__lshift__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int> ||
                const_member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__rshift__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int> ||
                const_member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__and__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__xor__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__or__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__radd__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__rsub__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__rmul__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int> ||
                const_member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__rmatmul__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__rtruediv__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__rfloordiv__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__rmod__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__rdivmod__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Tuple<Object>, T, Cls, Object> ||
                member_invocable<Tuple<Object>, T, Object> ||
                const_member_invocable<Tuple<Object>, T, Object>;
        };

        template <>
        struct check<"__rpow__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object, Object> ||
                member_invocable<Object, T, Object, Object> ||
                const_member_invocable<Object, T, Object, Object>;
        };

        template <>
        struct check<"__rlshift__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int> ||
                const_member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__rrshift__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int> ||
                const_member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__rand__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__rxor__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__ror__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object> ||
                const_member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__iadd__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__isub__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__imul__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__imatmul__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__itruediv__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__ifloordiv__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__imod__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__ipow__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object, Object> ||
                member_invocable<Object, T, Object, Object>;
        };

        template <>
        struct check<"__ilshift__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__irshift__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__iand__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__ixor__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__ior__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Object> ||
                member_invocable<Object, T, Object>;
        };

        template <>
        struct check<"__neg__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T> ||
                const_member_invocable<Object, T>;
        };

        template <>
        struct check<"__pos__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T> ||
                const_member_invocable<Object, T>;
        };

        template <>
        struct check<"__abs__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T> ||
                const_member_invocable<Object, T>;
        };

        template <>
        struct check<"__invert__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T> ||
                const_member_invocable<Object, T>;
        };

        template <>
        struct check<"__complex__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Complex, T, Cls> ||
                member_invocable<Complex, T> ||
                const_member_invocable<Complex, T>;
        };

        template <>
        struct check<"__int__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Int, T, Cls> ||
                member_invocable<Int, T> ||
                const_member_invocable<Int, T>;
        };

        template <>
        struct check<"__float__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Float, T, Cls> ||
                member_invocable<Float, T> ||
                const_member_invocable<Float, T>;
        };

        template <>
        struct check<"__index__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Int, T, Cls> ||
                member_invocable<Int, T> ||
                const_member_invocable<Int, T>;
        };

        template <>
        struct check<"__round__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int> ||
                const_member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__trunc__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T> ||
                const_member_invocable<Object, T>;
        };

        template <>
        struct check<"__floor__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T> ||
                const_member_invocable<Object, T>;
        };

        template <>
        struct check<"__ceil__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T> ||
                const_member_invocable<Object, T>;
        };

        template <>
        struct check<"__enter__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T> ||
                const_member_invocable<Object, T>;
        };

        template <>
        struct check<"__exit__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, T, Cls, Type<Exception>, Exception, Traceback> ||
                member_invocable<void, T, Type<Exception>, Exception, Traceback> ||
                const_member_invocable<void, T, Type<Exception>, Exception, Traceback>;
        };

        template <>
        struct check<"__buffer__"> {
            /// TODO: this should return specifically a MemoryView?
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls, Int> ||
                member_invocable<Object, T, Int> ||
                const_member_invocable<Object, T, Int>;
        };

        template <>
        struct check<"__release_buffer__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, T, Cls, Object> ||
                member_invocable<void, T, Object> ||
                const_member_invocable<void, T, Object>;
        };

        template <>
        struct check<"__await__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T> ||
                const_member_invocable<Object, T>;
        };

        // template <>
        // struct check<"__aiter__"> {
        //     template <typename T>
        //     static constexpr bool value =
        //         static_invocable<Object, T, Cls> ||
        //         member_invocable<Object, T> ||
        //         const_member_invocable<Object, T>;
        // };

        template <>
        struct check<"__anext__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T>;
        };

        template <>
        struct check<"__aenter__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<Object, T, Cls> ||
                member_invocable<Object, T> ||
                const_member_invocable<Object, T>;
        };

        template <>
        struct check<"__aexit__"> {
            template <typename T>
            static constexpr bool value =
                static_invocable<void, T, Cls, Type<Exception>, Exception, Traceback> ||
                member_invocable<void, T, Type<Exception>, Exception, Traceback> ||
                const_member_invocable<void, T, Type<Exception>, Exception, Traceback>;
        };

    };

    /// TODO: all of this crap should be placed in impl:: and referenced in each of the
    /// def specializations individually for maximum control.  They might be forward
    /// declared and filled in after functions are defined, which would mean I don't
    /// need to do anything here.

    /* A collection of template constraints for getset property getters. */
    struct Get {

        template <typename T>
        struct Static {
            static constexpr bool value = false;
        };

        template <typename T>
            requires (
                !std::is_member_function_pointer_v<T> &&
                std::is_invocable_v<T>
            )
        struct Static<T> {
            using type = std::invoke_result_t<T>;
            static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
        };

        template <typename T>
        struct Class {
            static constexpr bool value = false;
        };

        template <typename T>
            requires (
                !std::is_member_function_pointer_v<T> &&
                std::is_invocable_v<T, const Type<Wrapper>&>
            )
        struct Class<T> {
            using type = std::invoke_result_t<T, Type<Wrapper>&>;
            static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
            static constexpr bool is_const = true;
        };

        template <typename T>
            requires (
                !std::is_member_function_pointer_v<T> &&
                !std::is_invocable_v<T, const Type<Wrapper>&> &&
                std::is_invocable_v<T, Type<Wrapper>&>
            )
        struct Class<T> {
            using type = std::invoke_result_t<T, Type<Wrapper>&>;
            static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
            static constexpr bool is_const = false;
        };

        template <typename T>
        struct Instance {
            static constexpr bool value = false;
        };

        template <typename T>
            requires (
                !std::is_member_function_pointer_v<T> &&
                std::is_invocable_v<T, const Wrapper&>
            )
        struct Instance<T> {
            using type = std::invoke_result_t<T, const Wrapper&>;
            static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
            static constexpr bool is_const = true;
        };

        template <typename T>
            requires (
                !std::is_member_function_pointer_v<T> &&
                !std::is_invocable_v<T, const Wrapper&> &&
                std::is_invocable_v<T, Wrapper&>
            )
        struct Instance<T> {
            using type = std::invoke_result_t<T, Wrapper&>;
            static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
            static constexpr bool is_const = false;
        };

    };

    /* A collection of template constraints for getset property setters. */
    struct Set {

        template <typename T>
        struct Static {
            static constexpr bool value = false;
        };

        template <typename T>
        struct Static<void(*)(T)> {
            using type = T;
            static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
        };

        template <typename T> requires (impl::has_call_operator<T>)
        struct Static<T> {
            template <typename U>
            struct call {
                using type = void;
                static constexpr bool value = false;
            };

            template <typename U>
            struct call<void(*)(U)> {
                using type = U;
                static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
            };
            template <typename C, typename U>
            struct call<void(C::*)(U)> : call<void(*)(U)> {};
            template <typename C, typename U>
            struct call<void(C::*)(U) const> : call<void(*)(U)> {};
            template <typename C, typename U>
            struct call<void(C::*)(U) volatile> : call<void(*)(U)> {};
            template <typename C, typename U>
            struct call<void(C::*)(U) const volatile> : call<void(*)(U)> {};
            template <typename C, typename U>
            struct call<void(C::*)(U) noexcept> : call<void(*)(U)> {};
            template <typename C, typename U>
            struct call<void(C::*)(U) const noexcept> : call<void(*)(U)> {};
            template <typename C, typename U>
            struct call<void(C::*)(U) volatile noexcept> : call<void(*)(U)> {};
            template <typename C, typename U>
            struct call<void(C::*)(U) const volatile noexcept> : call<void(*)(U)> {};

            using type = typename call<decltype(&T::operator())>::type;
            static constexpr bool value = call<decltype(&T::operator())>::value;
        };

        template <typename T>
        struct Class {
            static constexpr bool value = false;
        };

        template <typename T>
        struct Class<void(*)(Type<Wrapper>&, T)> {
            using type = T;
            static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
            static constexpr bool is_const = false;
        };

        template <typename T>
        struct Class<void(*)(const Type<Wrapper>&, T)> {
            using type = T;
            static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
            static constexpr bool is_const = true;
        };

        template <typename T> requires (impl::has_call_operator<T>)
        struct Class<T> {
            template <typename U>
            struct call {
                using type = void;
                static constexpr bool value = false;
                static constexpr bool is_const = false;
            };

            template <typename C, typename U>
            struct call<void(C::*)(Type<Wrapper>&, U)> {
                using type = U;
                static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
                static constexpr bool is_const = false;
            };
            template <typename C, typename U>
            struct call<void(C::*)(Type<Wrapper>&, U) const> :
                call<void(C::*)(Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Type<Wrapper>&, U) volatile> :
                call<void(C::*)(Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Type<Wrapper>&, U) const volatile> :
                call<void(C::*)(Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Type<Wrapper>&, U) noexcept> :
                call<void(C::*)(Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Type<Wrapper>&, U) const noexcept> :
                call<void(C::*)(Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Type<Wrapper>&, U) volatile noexcept> :
                call<void(C::*)(Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Type<Wrapper>&, U) const volatile noexcept> :
                call<void(C::*)(Type<Wrapper>&, U)>
            {};

            template <typename C, typename U>
            struct call<void(C::*)(const Type<Wrapper>&, U)> {
                using type = U;
                static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
                static constexpr bool is_const = true;
            };
            template <typename C, typename U>
            struct call<void(C::*)(const Type<Wrapper>&, U) const> :
                call<void(C::*)(const Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Type<Wrapper>&, U) volatile> :
                call<void(C::*)(const Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Type<Wrapper>&, U) const volatile> :
                call<void(C::*)(const Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Type<Wrapper>&, U) noexcept> :
                call<void(C::*)(const Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Type<Wrapper>&, U) const noexcept> :
                call<void(C::*)(const Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Type<Wrapper>&, U) volatile noexcept> :
                call<void(C::*)(const Type<Wrapper>&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Type<Wrapper>&, U) const volatile noexcept> :
                call<void(C::*)(const Type<Wrapper>&, U)>
            {};

            using type = typename call<decltype(&T::operator())>::type;
            static constexpr bool value = call<decltype(&T::operator())>::value;
            static constexpr bool is_const = call<decltype(&T::operator())>::is_const;
        };

        template <typename T>
        struct Instance {
            static constexpr bool value = false;
        };

        template <typename T>
        struct Instance<void(*)(Wrapper&, T)> {
            using type = T;
            static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
            static constexpr bool is_const = false;
        };

        template <typename T>
        struct Instance<void(*)(const Wrapper&, T)> {
            using type = T;
            static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
            static constexpr bool is_const = true;
        };

        template <typename T> requires (impl::has_call_operator<T>)
        struct Instance<T> {
            template <typename U>
            struct call {
                using type = void;
                static constexpr bool value = false;
                static constexpr bool is_const = false;
            };

            template <typename C, typename U>
            struct call<void(C::*)(Wrapper&, U)> {
                using type = U;
                static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
                static constexpr bool is_const = false;
            };
            template <typename C, typename U>
            struct call<void(C::*)(Wrapper&, U) const> :
                call<void(C::*)(Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Wrapper&, U) volatile> :
                call<void(C::*)(Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Wrapper&, U) const volatile> :
                call<void(C::*)(Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Wrapper&, U) noexcept> :
                call<void(C::*)(Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Wrapper&, U) const noexcept> :
                call<void(C::*)(Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Wrapper&, U) volatile noexcept> :
                call<void(C::*)(Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(Wrapper&, U) const volatile noexcept> :
                call<void(C::*)(Wrapper&, U)>
            {};

            template <typename C, typename U>
            struct call<void(C::*)(const Wrapper&, U)> {
                using type = U;
                static constexpr bool value = std::derived_from<std::remove_cvref_t<type>, Object>;
                static constexpr bool is_const = true;
            };
            template <typename C, typename U>
            struct call<void(C::*)(const Wrapper&, U) const> :
                call<void(C::*)(const Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Wrapper&, U) volatile> :
                call<void(C::*)(const Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Wrapper&, U) const volatile> :
                call<void(C::*)(const Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Wrapper&, U) noexcept> :
                call<void(C::*)(const Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Wrapper&, U) const noexcept> :
                call<void(C::*)(const Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Wrapper&, U) volatile noexcept> :
                call<void(C::*)(const Wrapper&, U)>
            {};
            template <typename C, typename U>
            struct call<void(C::*)(const Wrapper&, U) const volatile noexcept> :
                call<void(C::*)(const Wrapper&, U)>
            {};

            using type = typename call<decltype(&T::operator())>::type;
            static constexpr bool value = call<decltype(&T::operator())>::value;
            static constexpr bool is_const = call<decltype(&T::operator())>::is_const;
        };

    };

    /* A collection of template constraints for getset property deleters. */
    struct Del {

        template <typename T>
        struct Static {
            static constexpr bool value = false;
        };

        template <typename T>
            requires (
                !std::is_member_function_pointer_v<T> &&
                std::is_invocable_r_v<void, T>
            )
        struct Static<T> {
            static constexpr bool value = true;
        };

        template <typename T>
        struct Class {
            static constexpr bool value = false;
        };

        template <typename T>
            requires (
                !std::is_member_function_pointer_v<T> &&
                std::is_invocable_r_v<void, T, const Type<Wrapper>&>
            )
        struct Class<T> {
            static constexpr bool value = true;
            static constexpr bool is_const = true;
        };

        template <typename T>
            requires (
                !std::is_member_function_pointer_v<T> &&
                !std::is_invocable_r_v<void, T, const Type<Wrapper>&> &&
                std::is_invocable_r_v<void, T, Type<Wrapper>&>
            )
        struct Class<T> {
            static constexpr bool value = true;
            static constexpr bool is_const = false;
        };

        template <typename T>
        struct Instance {
            static constexpr bool value = false;
        };

        template <typename T>
            requires (
                !std::is_member_function_pointer_v<T> &&
                std::is_invocable_r_v<void, T, const Wrapper&>
            )
        struct Instance<T> {
            static constexpr bool value = true;
            static constexpr bool is_const = true;
        };

        template <typename T>
            requires (
                !std::is_member_function_pointer_v<T> &&
                !std::is_invocable_r_v<void, T, const Wrapper&> &&
                std::is_invocable_r_v<void, T, Wrapper&>
            )
        struct Instance<T> {
            static constexpr bool value = true;
            static constexpr bool is_const = false;
        };

    };

    /* A collection of closure types to add context to a getset property descriptor. */
    struct Closures {

        template <typename Getter>
        struct Get {
            Getter getter;
        };

        template <typename Setter>
        struct Set {
            Setter setter;
        };

        template <typename Deleter>
        struct Del {
            Deleter deleter;
        };

        template <typename Getter, typename Setter>
        struct GetSet {
            Getter getter;
            Setter setter;
        };

        template <typename Getter, typename Deleter>
        struct GetDel {
            Getter* getter;
            Deleter* deleter;
        };

        template <typename Setter, typename Deleter>
        struct SetDel {
            Setter setter;
            Deleter deleter;
        };

        template <typename Getter, typename Setter, typename Deleter>
        struct GetSetDel {
            Getter getter;
            Setter setter;
            Deleter deleter;
        };

        template <typename T>
        struct Traits {
            static constexpr bool value = false;
        };
        template <typename G>
        struct Traits<Get<G>> {
            static constexpr bool value = true;
            static constexpr bool has_getter = true;
            static constexpr bool has_setter = false;
            static constexpr bool has_deleter = false;
            using Getter = G;
        };
        template <typename S>
        struct Traits<Set<S>> {
            static constexpr bool value = true;
            static constexpr bool has_getter = false;
            static constexpr bool has_setter = true;
            static constexpr bool has_deleter = false;
            using Setter = S;
        };
        template <typename D>
        struct Traits<Del<D>> {
            static constexpr bool value = true;
            static constexpr bool has_getter = false;
            static constexpr bool has_setter = false;
            static constexpr bool has_deleter = true;
            using Deleter = D;
        };
        template <typename G, typename S>
        struct Traits<GetSet<G, S>> {
            static constexpr bool value = true;
            static constexpr bool has_getter = true;
            static constexpr bool has_setter = true;
            static constexpr bool has_deleter = false;
            using Getter = G;
            using Setter = S;
        };
        template <typename G, typename D>
        struct Traits<GetDel<G, D>> {
            static constexpr bool value = true;
            static constexpr bool has_getter = true;
            static constexpr bool has_setter = false;
            static constexpr bool has_deleter = true;
            using Getter = G;
            using Deleter = D;
        };
        template <typename S, typename D>
        struct Traits<SetDel<S, D>> {
            static constexpr bool value = true;
            static constexpr bool has_getter = false;
            static constexpr bool has_setter = true;
            static constexpr bool has_deleter = true;
            using Setter = S;
            using Deleter = D;
        };
        template <typename G, typename S, typename D>
        struct Traits<GetSetDel<G, S, D>> {
            static constexpr bool value = true;
            static constexpr bool has_getter = true;
            static constexpr bool has_setter = true;
            static constexpr bool has_deleter = true;
            using Getter = G;
            using Setter = S;
            using Deleter = D;
        };

    };

    /* A function pointer that can be added to the metaclass's `class_getters` field.
    The closure will be interpreted according to the templated Closure type. */
    template <static_str Name, typename Closure>
    static PyObject* static_getter(Meta* cls, void* closure) {
        static_assert(
            Closure::template Triats<Closure>::value,
            "Closure type not recognized"
        );
        static_assert(
            Closure::template Traits<Closure>::has_getter,
            "Closure must have a getter."
        );

        auto get = [](void* closure) -> PyObject* {
            static_assert(
                Get::template Static<Closure>::value,
                "Getter must be callable without arguments."
            );
            using G = Closures::template Traits<Closure>::Getter;
            using Return  = Get::template Static<G>::type;
            G* fget = reinterpret_cast<Closure*>(closure)->getter;
            if constexpr (std::is_lvalue_reference_v<Return>) {
                return Py_NewRef(ptr(fget()));
            } else {
                return release(fget());
            }
        };

        try {
            return get(closure);
        } catch (...) {
            Exception::to_python();
            return nullptr;
        };
    }

    /* A function pointer that can be added to the metaclass's `class_setters` field.
    The closure will be interpreted according to the templated Closure type. */
    template <static_str Name, typename Closure>
    static int static_setter(Meta* cls, PyObject* value, void* closure) {
        static_assert(
            Closure::template Traits<Closure>::value,
            "Closure type not recognized"
        );
        static_assert(
            Closure::template Traits<Closure>::has_setter ||
            Closure::template Traits<Closure>::has_deleter,
            "Closure must have a setter or deleter."
        );

        auto set = [](void* closure, PyObject* value) -> void {
            if constexpr (!Closures::template Traits<Closure>::has_setter) {
                throw TypeError(
                    "property '" + Name + "' of type '" +
                    impl::demangle(typeid(Wrapper).name()) +
                    "' has no setter"
                );
            } else {
                static_assert(
                    Set::template Static<Closure>::value,
                    "Setter must accept a single argument."
                );
                reinterpret_cast<Closure*>(closure)->setter(
                    borrow<Object>(value)
                );
            }
        };

        auto del = [](void* closure) -> void {
            if constexpr (!Closures::template Traits<Closure>::has_deleter) {
                throw TypeError(
                    "property '" + Name + "' of type '" +
                    impl::demangle(typeid(Wrapper).name()) +
                    "' has no deleter"
                );
            } else {
                static_assert(
                    Del::template Static<Closure>::value,
                    "Deleter must be callable without arguments."
                );
                reinterpret_cast<Closure*>(closure)->deleter();
            }
        };

        try {
            if (value) {
                set(closure, value);
            } else {
                del(closure);
            }
            return 0;
        } catch (...) {
            Exception::to_python();
            return -1;
        }
    }

    /* A function pointer that can be added to the metaclass's `class_getters` field.
    The closure will be interpreted according to the templated Closure type. */
    template <static_str Name, typename Closure>
    static PyObject* class_getter(Meta* cls, void* closure) {
        static_assert(
            Closure::template Traits<Closure>::value,
            "Closure type not recognized"
        );
        static_assert(
            Closure::template Traits<Closure>::has_getter,
            "Closure must have a getter."
        );

        auto get = [](void* closure, Type<Wrapper>&& cls) -> PyObject* {
            static_assert(
                Get::template Class<Closure>::value,
                "Getter must accept a reference to the enclosing Object wrapper's "
                "Type<> as a first argument."
            );
            using G = Closures::template Traits<Closure>::Getter;
            using Return = Get::template Class<G>::type;
            G& fget = reinterpret_cast<Closure*>(closure)->getter;
            if constexpr (std::is_lvalue_reference_v<Return>) {
                return Py_NewRef(ptr(fget(cls)));
            } else {
                return release(fget(cls));
            }
        };

        try {
            return get(closure, borrow<Type<Wrapper>>(cls));
        } catch (...) {
            Exception::to_python();
            return nullptr;
        }
    }

    /* A function pointer that can be added to the metaclass' `class_setters` field.
    The closure will be interpreted according to the templated Closure type. */
    template <static_str Name, typename Closure>
    static int class_setter(Meta* cls, PyObject* value, void* closure) {
        static_assert(
            Closure::template Traits<Closure>::value,
            "Closure type not recognized"
        );
        static_assert(
            Closure::template Traits<Closure>::has_setter ||
            Closure::template Traits<Closure>::has_deleter,
            "Closure must have a setter or deleter."
        );

        auto set = [](void* closure, Type<Wrapper>&& cls, PyObject* value) -> void {
            if constexpr (!Closures::template Traits<Closure>::has_setter) {
                throw TypeError(
                    "property '" + Name + "' of type '" +
                    impl::demangle(typeid(Wrapper).name()) + "' has no setter"
                );
            } else {
                static_assert(
                    Set::template Class<Closure>::value,
                    "Setter must accept a reference to the enclosing Object "
                    "wrapper's Type<> as a first argument."
                );
                reinterpret_cast<Closure*>(closure)->setter(
                    cls,
                    borrow<Object>(value)
                );
            }
        };

        auto del = [](void* closure, Type<Wrapper>&& cls) -> void {
            if constexpr (!Closures::template Traits<Closure>::has_deleter) {
                throw TypeError(
                    "property '" + Name + "' of type '" +
                    impl::demangle(typeid(Wrapper).name()) + "' has no deleter"
                );
            } else {
                static_assert(
                    Del::template Class<Closure>::value,
                    "Deleter must accept a reference to the enclosing Object "
                    "wrapper's Type<> as a first argument."
                );
                reinterpret_cast<Closure*>(closure)->deleter(cls);
            }
        };

        try {
            if (value) {
                set(closure, borrow<Type<Wrapper>>(cls), value);
            } else {
                del(closure, borrow<Type<Wrapper>>(cls));
            }
            return 0;
        } catch (...) {
            Exception::to_python();
            return -1;
        }
    }

    /* A function pointer that can be placed in a `PyGetSetDef.get` slot.  The closure
    will be interpreted according to the templated closure type. */
    template <static_str Name, typename Closure>
    static PyObject* instance_getter(CRTP* self, void* closure) {
        static_assert(
            Closure::template Traits<Closure>::value,
            "Closure type not recognized"
        );
        static_assert(
            Closure::template Traits<Closure>::has_getter,
            "Closure must have a getter."
        );

        auto get = [](void* closure, Wrapper&& self) -> PyObject* {
            static_assert(
                Get::template Instance<Closure>::value,
                "Getter must accept a reference to the enclosing Object wrapper as a "
                "first argument."
            );
            using G = Closures::template Traits<Closure>::Getter;
            using Return = Get::template Instance<G>::type;
            G& fget = reinterpret_cast<Closure*>(closure)->getter;
            if constexpr (std::is_lvalue_reference_v<Return>) {
                return Py_NewRef(ptr(fget(self)));
            } else {
                return release(fget(self));
            }
        };

        try {
            return get(closure, borrow<Wrapper>(self));
        } catch (...) {
            Exception::to_python();
            return nullptr;
        }
    }

    /* A function pointer that can be placed in a `PyGetSetDef.set` slot.  The closure
    will be interpreted according to the templated Closure type. */
    template <static_str Name, typename Closure>
    static int instance_setter(CRTP* self, PyObject* value, void* closure) {
        static_assert(
            Closure::template Traits<Closure>::value,
            "Closure type not recognized"
        );
        static_assert(
            Closure::template Traits<Closure>::has_setter ||
            Closure::template Traits<Closure>::has_deleter,
            "Closure must have a setter or deleter."
        );

        auto set = [](void* closure, Wrapper&& self, PyObject* value) -> void {
            if constexpr (!Closures::template Traits<Closure>::has_setter) {
                throw TypeError(
                    "property '" + Name + "' of type '" +
                    impl::demangle(typeid(Wrapper).name()) + "' has no setter"
                );
            } else {
                static_assert(
                    Set::template Instance<Closure>::value,
                    "Setter must accept a reference to the enclosing Object wrapper "
                    "as a first argument."
                );
                reinterpret_cast<Closure*>(closure)->setter(
                    self,
                    borrow<Object>(value)
                );
            }
        };

        auto del = [](void* closure, Wrapper&& self) -> void {
            if constexpr (!Closures::template Traits<Closure>::has_deleter) {
                throw TypeError(
                    "property '" + Name + "' of type '" +
                    impl::demangle(typeid(Wrapper).name()) + "' has no deleter"
                );
            } else {
                static_assert(
                    Del::template Instance<Closure>::value,
                    "Deleter must accept a reference to the enclosing Object wrapper "
                    "as a first argument."
                );
                reinterpret_cast<Closure*>(closure)->deleter(self);
            }
        };

        try {
            if (value) {
                set(closure, borrow<Wrapper>(self), value);
            } else {
                del(closure, borrow<Wrapper>(self));
            }
            return 0;
        } catch (...) {
            Exception::to_python();
            return -1;
        }
    }

public:
    Module<ModName> module;
    std::unordered_map<std::string, Member> members;
    Meta::ClassGetters class_getters;
    Meta::ClassSetters class_setters;

    Bindings(const Module<ModName>& mod) : module(mod) {}
    Bindings(const Bindings&) = delete;
    Bindings(Bindings&&) = delete;

    /* Expose an immutable member variable to Python as a getset descriptor,
    which synchronizes its state. */
    template <static_str Name, typename T> requires (__object__<T>::enable)
    void var(const T Cls::*value) {
        if (members.contains(Name)) {
            throw AttributeError(
                "Class '" + impl::demangle(typeid(Wrapper).name()) +
                "' already has an attribute named '" + Name + "'."
            );
        }
        members[Name] = { .var = true };
        static bool skip = false;
        if (skip) {
            return;
        }
        static auto get = [](PyObject* self, void* closure) -> PyObject* {
            try {
                auto member = reinterpret_cast<const T Cls::*>(closure);
                if constexpr (std::same_as<CRTP, Cls>) {
                    if constexpr (impl::python_like<T>) {
                        return Py_NewRef(ptr(reinterpret_cast<CRTP*>(self)->*member));
                    } else {
                        return release(wrap(reinterpret_cast<CRTP*>(self)->*member));
                    }
                } else {
                    return std::visit(
                        visitor{
                            [member](const Cls& obj) {
                                if constexpr (impl::python_like<T>) {
                                    return Py_NewRef(ptr(obj.*member));
                                } else {
                                    return release(wrap(obj.*member));
                                }
                            },
                            [member](const Cls* obj) {
                                if constexpr (impl::python_like<T>) {
                                    return Py_NewRef(ptr(obj->*member));
                                } else {
                                    return release(wrap(obj->*member));
                                }
                            }
                        },
                        reinterpret_cast<CRTP*>(self)->m_cpp
                    );
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
    template <static_str Name, typename T> requires (__object__<T>::enable)
    void var(T Cls::*value) {
        if (members.contains(Name)) {
            throw AttributeError(
                "Class '" + impl::demangle(typeid(Wrapper).name()) +
                "' already has an attribute named '" + Name + "'."
            );
        }
        members[Name] = { .var = true };
        static bool skip = false;
        if (skip) {
            return;
        }
        static auto get = [](PyObject* self, void* closure) -> PyObject* {
            try {
                auto member = reinterpret_cast<T CRTP::*>(closure);
                if constexpr (std::same_as<CRTP, Cls>) {
                    if constexpr (impl::python_like<T>) {
                        return Py_NewRef(ptr(reinterpret_cast<CRTP*>(self)->*member));
                    } else {
                        return release(wrap(reinterpret_cast<CRTP*>(self)->*member));
                    }
                } else {
                    return std::visit(
                        visitor{
                            [member](Cls& obj) {
                                if constexpr (impl::python_like<T>) {
                                    return Py_NewRef(ptr(obj.*member));
                                } else {
                                    return release(wrap(obj.*member));
                                }
                            },
                            [member](Cls* obj) {
                                if constexpr (impl::python_like<T>) {
                                    return Py_NewRef(ptr(obj->*member));
                                } else {
                                    return release(wrap(obj->*member));
                                }
                            },
                            [member](const Cls* obj) {
                                if constexpr (impl::python_like<T>) {
                                    return Py_NewRef(ptr(obj->*member));
                                } else {
                                    return release(wrap(obj->*member));
                                }
                            }
                        },
                        reinterpret_cast<CRTP*>(self)->m_cpp
                    );
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
                auto member = reinterpret_cast<T CRTP::*>(closure);
                if constexpr (std::same_as<CRTP, Cls>) {
                    reinterpret_cast<CRTP*>(self)->*member = static_cast<T>(
                        borrow<Object>(new_val)
                    );
                } else {
                    std::visit(
                        visitor{
                            [member, new_val](Cls& obj) {
                                obj.*member = static_cast<T>(
                                    borrow<Object>(new_val)
                                );
                            },
                            [member, new_val](Cls* obj) {
                                obj->*member = static_cast<T>(
                                    borrow<Object>(new_val)
                                );
                            },
                            [new_val](const Cls* obj) {
                                throw TypeError(
                                    "variable '" + Name + "' of type '" +
                                    impl::demangle(typeid(Wrapper).name()) +
                                    "' is immutable."
                                );
                            }
                        },
                        reinterpret_cast<CRTP*>(self)->m_cpp
                    );
                }
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
    template <static_str Name, typename T> requires (__object__<T>::enable)
    void class_var(const T& value) {
        if (members.contains(Name)) {
            throw AttributeError(
                "Class '" + impl::demangle(typeid(Wrapper).name()) +
                "' already has an attribute named '" + Name + "'."
            );
        }
        members[Name] = { .class_var = true };
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
    template <static_str Name, typename T> requires (__object__<T>::enable)
    void class_var(T& value) {
        if (members.contains(Name)) {
            throw AttributeError(
                "Class '" + impl::demangle(typeid(Wrapper).name()) +
                "' already has an attribute named '" + Name + "'."
            );
        }
        members[Name] = { .class_var = true };
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
                    borrow<Object>(new_val)
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

    /// TODO: when overload sets are implemented, rework property() to accept a
    /// set for each of the getter, setter, and deleter.  Overload sets should be able
    /// to be constructed from an initializer list, so the final syntax would be
    /// something like this:
    ///
    ///     bindings.property<"x">(
    ///         "Python docstring for property `x`",
    ///         [](Wrapper& self) {  // overloads can refer to a single Python-style function
    ///             return unwrap(self).x;
    ///         },
    ///         {  // or several functions with compatible signatures
    ///             some_function_that_accepts_a_reference_to_wrapper,
    ///             &a_pointer_to_such_a_function,
    ///             [](Wrapper& self, const Int& value) {
    ///                 unwrap(self).x = value;
    ///             }
    ///         },
    ///         {}  // property has no deleter (can be omitted)
    ///     );
    ///
    /// This would require some way of constraining the overload set to only accept
    /// functions that meet the criteria for a getter, setter, or deleter.  This could
    /// maybe be a template signature for OverloadSet itself, which will be checked
    /// against every function that is added to the overload set.  On the C++ side,
    /// the .overload() method would constrain the function to be convertible to that
    /// signature and fail at compile time otherwise.  On the Python side, the overload
    /// set would extract the function's annotations and check them against the
    /// signature, throwing a TypeError if they don't match.  When you pass an overload
    /// set between the languages, I would do a nested type check similar to functions
    /// themselves, as well as all sorts of generic containers to enforce an exact
    /// and consistent match at all times.  The generic OverloadSet could be variadic
    /// just like Function<>.
    /// -> This might be trending towards a duplicate of the Function<> type itself,
    /// which might be a good thing.  If normal functions can be overloaded directly,
    /// then it enhances consistency with C++.  That means I would need to implement
    /// the trie directly in Function<>.  Unfortunately, that requires type erasure
    /// for the functions within the trie, which necessitates a nested Python type that
    /// can be called via vectorcall.  That might be how the internal nodes are
    /// constructed.  Each node would point to a PyObject* that implements the node's
    /// interface in such a way that the trie can be navigated from both Python and
    /// C++ with equal ease.
    /// -> Functions might be default-constructible, in which case they would initialize
    /// to an empty overload set.  They can also be constructed with any combination of
    /// C++ callables with differing signatures, in which case the overload trie would
    /// be initialized accordingly.  When the trie is built, if two functions resolve
    /// to the same key, an error will occur.  It may be possible to check that error
    /// at compile time as well.
    /// -> Also, if you template a py::Function with a member signature, then that
    /// function might be able to be overloaded with both member and non-member
    /// functions that take a reference to the class as their first argument.  That
    /// would absorb some of the crazy logic that's currently being implemented here,
    /// and standardizes it across the board.
    /// -> All of this is very complicated, but it's by far the cleanest and best
    /// way to bridge these languages in the long run.  If functions and overload sets
    /// are identical, then every function can potentially be overloaded by default,
    /// which simplifies things greatly for client code.  Plus, since functions are
    /// attachable, they also fulfill that purpose as well.

    /// TODO: Once overload sets are usable, and  functions are default-constructible,
    /// then just accept them by value here, defaulting to {} rather than nullptr.
    /// The closure will then store the getter, setter, and deleter as PyObject*
    /// pointers, and the Python getter, setter, and deleter wrappers will forward
    /// to this using the vectorcall protocol.

    /// TODO: also, class methods should maybe be able to take a reference to
    /// Type<Cls> as their first argument, which would be piped in via the metaclass's
    /// __getattr__, __setattr__, and __delattr__ slots.  That would allow me to model
    /// class methods separately from pure static methods, and would avoid an
    /// unnecessary import.

    /// TODO: in order for property getters, setters, and deleters to be overloadable,
    /// I would have to write custom descriptor classes that store the getters, setters
    /// and deleters as publicly-accessible overload sets, so that they can be
    /// referenced from the type object.

    /// @MyType.x.getter.overload
    /// def custom_getter(self: MyType[int]) -> int:
    ///     return self.x

    /// TODO: this means properties CAN'T use tp_getset slots.  They have to be
    /// implemented as dynamic Python objects that are attached to the type.
    /// -> How does this interface with class and static properties?  You would
    /// probably have to access them in the type object's dictionary in order to
    /// overload them, which means the metaclass's __getattr__ method would have to
    /// forward to them.

    /// TODO: overload sets cannot be used with getters/deleters?  Those must by
    /// definition not accept any additional arguments, so there will never be anything
    /// to dispatch on.  That might allow for performance optimization as well, since
    /// I never need to consider overload behavior.  I can thus probably stick with
    /// using tp_getset slots for these?  That may prevent new overloads from being
    /// registered from Python, unless I somehow found a way to expose the closure
    /// to Python.
    /// -> Perhaps the final solution is to create a Python type that stores the
    /// getter and deleter as normal function pointers/objects, as well as an overload
    /// set for the setter.  That would be placed into the type's dictionary using
    /// SetAttr like normal, and would be accessible from Python as a standard property.


    /* Expose a non-member getter/setter pair to Python as a getset descriptor. */
    template <
        static_str Name,
        typename Getter = std::nullptr_t,
        typename Setter = std::nullptr_t,
        typename Deleter = std::nullptr_t
    >
        requires ((
            std::is_null_pointer_v<Getter> ||
            Get::template NonMember<std::decay_t<Getter>>::value ||
            Get::template Member<std::decay_t<Getter>>::value
        ) && (
            std::is_null_pointer_v<Setter> ||
            Set::template NonMember<std::decay_t<Setter>>::value ||
            Set::template Member<std::decay_t<Setter>>::value
        ) && (
            std::is_null_pointer_v<Deleter> ||
            Del::template NonMember<std::decay_t<Deleter>>::value ||
            Del::template Member<std::decay_t<Deleter>>::value
        ))
    void property(
        std::string&& doc,
        Getter&& getter = {},
        Setter&& setter = {},
        Deleter&& deleter = {}
    ) {
        using G = std::decay_t<Getter>;
        using S = std::decay_t<Setter>;
        using D = std::decay_t<Deleter>;

        if (members.contains(Name)) {
            throw AttributeError(
                "Class '" + impl::demangle(typeid(Wrapper).name()) +
                "' already has an attribute named '" + Name + "'."
            );
        }
        members[Name] = { .property = true };
        static bool skip = false;
        if (skip) {
            return;
        }

        auto func = []<typename T>(T&& func) {
            using U = std::decay_t<T>;
            if constexpr (
                std::is_function_v<std::remove_pointer_t<U>> ||
                std::is_member_function_pointer_v<U>
            ) {
                return func;
            } else {
                static U f = std::forward<T>(func);
                return f;
            }
        };

        static std::string doc_str = std::move(doc);
        ::getter py_getter = nullptr;
        ::setter py_setter = nullptr;
        void* closure = nullptr;
        if constexpr (std::is_null_pointer_v<G>) {
            if constexpr (std::is_null_pointer_v<S>) {
                if constexpr (std::is_null_pointer_v<D>) {
                    // do nothing
                } else {
                    using Closure = Closures::template Del<D>;
                    static Closure context = {func(deleter)};
                    closure = &context;
                    py_setter = reinterpret_cast<::setter>(&instance_setter<Name, Closure>);
                }
            } else {
                if constexpr (std::is_null_pointer_v<Deleter>) {
                    using Closure = Closures::template Set<S>;
                    static Closure context = {func(setter)};
                    closure = &context;
                    py_setter = reinterpret_cast<::setter>(&instance_setter<Name, Closure>);
                } else {
                    using Closure = Closures::template SetDel<S, D>;
                    static Closure context = {func(setter), func(deleter)};
                    closure = &context;
                    py_setter = reinterpret_cast<::setter>(&instance_setter<Name, Closure>);
                }
            }
        } else {
            if constexpr (std::is_null_pointer_v<Setter>) {
                if constexpr (std::is_null_pointer_v<Deleter>) {
                    using Closure = Closures::template Get<G>;
                    static Closure context = {func(getter)};
                    closure = &context;
                    py_getter = reinterpret_cast<::getter>(&instance_getter<Name, Closure>);
                } else {
                    using Closure = Closures::template GetDel<G, D>;
                    static Closure context = {func(getter), func(deleter)};
                    closure = &context;
                    py_getter = reinterpret_cast<::getter>(&instance_getter<Name, Closure>);
                    py_setter = reinterpret_cast<::setter>(&instance_setter<Name, Closure>);
                }
            } else {
                if constexpr (std::is_null_pointer_v<Deleter>) {
                    using Closure = Closures::template GetSet<G, S>;
                    static Closure context = {func(getter), func(setter)};
                    closure = &context;
                    py_getter = reinterpret_cast<::getter>(&instance_getter<Name, Closure>);
                    py_setter = reinterpret_cast<::setter>(&instance_setter<Name, Closure>);
                } else {
                    using Closure = Closures::template GetSetDel<G, S, D>;
                    static Closure context = {func(getter), func(setter), func(deleter)};
                    closure = &context;
                    py_getter = reinterpret_cast<::getter>(&instance_getter<Name, Closure>);
                    py_setter = reinterpret_cast<::setter>(&instance_setter<Name, Closure>);
                }
            }
        }
        tp_getset.push_back({
            Name,
            py_getter,
            py_setter,
            PyDoc_STR(doc_str.c_str()),
            closure
        });
        skip = true;
    }

    /* Expose a C++-style static getter to Python using the `__getattr__()`
    slot of the metatype. */
    template <
        static_str Name,
        typename Getter = std::nullptr_t,
        typename Setter = std::nullptr_t,
        typename Deleter = std::nullptr_t
    >
        requires ((
            std::is_null_pointer_v<Getter> ||
            Get::template Static<std::decay_t<Getter>>::value
        ) && (
            std::is_null_pointer_v<Setter> ||
            Set::template Static<std::decay_t<Setter>>::value
        ) && (
            std::is_null_pointer_v<Deleter> ||
            Del::template Static<std::decay_t<Deleter>>::value
        ))
    void class_property(
        Getter&& getter = {},
        Setter&& setter = {},
        Deleter&& deleter = {}
    ) {
        using G = std::decay_t<Getter>;
        using S = std::decay_t<Setter>;
        using D = std::decay_t<Deleter>;

        if (members.contains(Name)) {
            throw AttributeError(
                "Class '" + impl::demangle(typeid(Wrapper).name()) +
                "' already has an attribute named '" + Name + "'."
            );
        }
        members[Name] = { .class_property = true };

        auto func = []<typename T>(T&& func) {
            using U = std::decay_t<T>;
            if constexpr (std::is_function_v<std::remove_pointer_t<U>>) {
                return func;
            } else {
                static U f = std::forward<T>(func);
                return f;
            }
        };

        PyObject*(*py_getter)(Meta*, void*) = nullptr;
        int(*py_setter)(Meta*, PyObject*, void*) = nullptr;
        void* closure = nullptr;
        if constexpr (std::is_null_pointer_v<G>) {
            if constexpr (std::is_null_pointer_v<S>) {
                if constexpr (std::is_null_pointer_v<D>) {
                    // do nothing
                } else {
                    using Closure = Closures::template Del<D>;
                    static Closure context = {func(deleter)};
                    closure = &context;
                    py_getter = &class_setter<Name, Closure>;
                }
            } else {
                if constexpr (std::is_null_pointer_v<Deleter>) {
                    using Closure = Closures::template Set<S>;
                    static Closure context = {func(setter)};
                    closure = &context;
                    py_setter = &class_setter<Name, Closure>;
                } else {
                    using Closure = Closures::template SetDel<S, D>;
                    static Closure context = {func(setter), func(deleter)};
                    closure = &context;
                    py_setter = &class_setter<Name, Closure>;
                }
            }
        } else {
            if constexpr (std::is_null_pointer_v<Setter>) {
                if constexpr (std::is_null_pointer_v<Deleter>) {
                    using Closure = Closures::template Get<G>;
                    static Closure context = {func(getter)};
                    closure = &context;
                    py_getter = &class_getter<Name, Closure>;
                } else {
                    using Closure = Closures::template GetDel<G, D>;
                    static Closure context = {func(getter), func(deleter)};
                    closure = &context;
                    py_getter = &class_getter<Name, Closure>;
                    py_setter = &class_setter<Name, Closure>;
                }
            } else {
                if constexpr (std::is_null_pointer_v<Deleter>) {
                    using Closure = Closures::template GetSet<G, S>;
                    static Closure context = {func(getter), func(setter)};
                    closure = &context;
                    py_getter = &class_getter<Name, Closure>;
                    py_setter = &class_setter<Name, Closure>;
                } else {
                    using Closure = Closures::template GetSetDel<G, S, D>;
                    static Closure context = {func(getter), func(setter), func(deleter)};
                    closure = &context;
                    py_getter = &class_getter<Name, Closure>;
                    py_setter = &class_setter<Name, Closure>;
                }
            }
        }
        if (py_getter) {
            class_getters[Name] = { py_getter, closure };
        }
        if (py_setter) {
            class_setters[Name] = { py_setter, closure };
        }
    }

    /* Expose a C++-style static getter to Python using the `__getattr__()`
    slot of the metatype. */
    template <
        static_str Name,
        typename Getter = std::nullptr_t,
        typename Setter = std::nullptr_t,
        typename Deleter = std::nullptr_t
    >
        requires ((
            std::is_null_pointer_v<Getter> ||
            Get::template Static<std::decay_t<Getter>>::value
        ) && (
            std::is_null_pointer_v<Setter> ||
            Set::template Static<std::decay_t<Setter>>::value
        ) && (
            std::is_null_pointer_v<Deleter> ||
            Del::template Static<std::decay_t<Deleter>>::value
        ))
    void static_property(
        Getter&& getter = {},
        Setter&& setter = {},
        Deleter&& deleter = {}
    ) {
        using G = std::decay_t<Getter>;
        using S = std::decay_t<Setter>;
        using D = std::decay_t<Deleter>;

        if (members.contains(Name)) {
            throw AttributeError(
                "Class '" + impl::demangle(typeid(Wrapper).name()) +
                "' already has an attribute named '" + Name + "'."
            );
        }
        members[Name] = { .class_property = true };

        auto func = []<typename T>(T&& func) {
            using U = std::decay_t<T>;
            if constexpr (std::is_function_v<std::remove_pointer_t<U>>) {
                return func;
            } else {
                static U f = std::forward<T>(func);
                return f;
            }
        };

        PyObject*(*py_getter)(Meta*, void*) = nullptr;
        int(*py_setter)(Meta*, PyObject*, void*) = nullptr;
        void* closure = nullptr;
        if constexpr (std::is_null_pointer_v<G>) {
            if constexpr (std::is_null_pointer_v<S>) {
                if constexpr (std::is_null_pointer_v<D>) {
                    // do nothing
                } else {
                    using Closure = Closures::template Del<D>;
                    static Closure context = {func(deleter)};
                    closure = &context;
                    py_getter = &static_setter<Name, Closure>;
                }
            } else {
                if constexpr (std::is_null_pointer_v<Deleter>) {
                    using Closure = Closures::template Set<S>;
                    static Closure context = {func(setter)};
                    closure = &context;
                    py_setter = &static_setter<Name, Closure>;
                } else {
                    using Closure = Closures::template SetDel<S, D>;
                    static Closure context = {func(setter), func(deleter)};
                    closure = &context;
                    py_setter = &static_setter<Name, Closure>;
                }
            }
        } else {
            if constexpr (std::is_null_pointer_v<Setter>) {
                if constexpr (std::is_null_pointer_v<Deleter>) {
                    using Closure = Closures::template Get<G>;
                    static Closure context = {func(getter)};
                    closure = &context;
                    py_getter = &static_getter<Name, Closure>;
                } else {
                    using Closure = Closures::template GetDel<G, D>;
                    static Closure context = {func(getter), func(deleter)};
                    closure = &context;
                    py_getter = &static_getter<Name, Closure>;
                    py_setter = &static_setter<Name, Closure>;
                }
            } else {
                if constexpr (std::is_null_pointer_v<Deleter>) {
                    using Closure = Closures::template GetSet<G, S>;
                    static Closure context = {func(getter), func(setter)};
                    closure = &context;
                    py_getter = &static_getter<Name, Closure>;
                    py_setter = &static_setter<Name, Closure>;
                } else {
                    using Closure = Closures::template GetSetDel<G, S, D>;
                    static Closure context = {func(getter), func(setter), func(deleter)};
                    closure = &context;
                    py_getter = &static_getter<Name, Closure>;
                    py_setter = &static_setter<Name, Closure>;
                }
            }
        }
        if (py_getter) {
            class_getters[Name] = { py_getter, closure };
        }
        if (py_setter) {
            class_setters[Name] = { py_setter, closure };
        }
    }

    /// TODO: method() should also be able to accept non-member methods that take
    /// self as a first argument.

    /* Expose a C++ instance method to Python as an instance method, which can
    be overloaded from either side of the language boundary.  */
    template <static_str Name, typename Return, typename... Target>
        requires (
            __object__<std::remove_cvref_t<Return>>::enable &&
            (__object__<std::remove_cvref_t<Target>>::enable && ...)
        )
    void method(Return(Cls::*func)(Target...)) {
        /// TODO: check for an existing overload set with the same name and
        /// insert into it, or create a new one if none exists.  Then, call
        /// PyObject_SetAttr() to insert the overload set into the class dict
        /// or invoke a metaclass descriptor to handle internal slots.

        /// func is a pointer to a member function of CppType, which can be
        /// called like this:
        ///      obj.*func(std::forward<Args>(args)...);
    }

    /// TODO: __iter__ and __reversed__ need special treatment, and require two
    /// functions to be supplied.
    /// -> I need to write a function that takes two C++ iterators and returns a
    /// unique Python type that can be used to iterate over them.  Then, in order to
    /// bind __iter__ and __reversed__, you'd just bind a lambda that calls this
    /// method appropriately.
    /// -> Perhaps that method is a py::Iterator constructor, and the way you overload
    /// __iter__ is by providing a lambda that calls this constructor on the self
    /// argument.

    /* Expose a C++ static method to Python as a static method, which can be
    overloaded from either side of the language boundary.  */
    template <static_str Name, typename Return, typename... Target>
        requires (
            __object__<std::remove_cvref_t<Return>>::enable &&
            (__object__<std::remove_cvref_t<Target>>::enable && ...)
        )
    void class_method(Return(*func)(Target...)) {
        /// TODO: function refactor
    }

    /* Expose a nested py::Object type to Python. */
    template <
        static_str Name,
        std::derived_from<Object> Class,
        std::derived_from<Object>... Bases
    > requires (
        !impl::is_generic<Class> &&
        impl::has_interface<Class> && (impl::has_interface<Bases> && ...) &&
        impl::has_python<Class> && (impl::has_python<Bases> && ...)
    )
    void type() {
        if (members.contains(Name)) {
            throw AttributeError(
                "Class '" + impl::demangle(typeid(Wrapper).name()) +
                "' already has an attribute named '" + Name + "'."
            );
        }
        members[Name] = {
            .type = true,
            .callbacks = {
                [](Type<Wrapper>& type) {
                    // get the module under which the parent type was exposed
                    Module<ModName> mod = steal<Module<ModName>>(
                        PyType_GetModule(ptr(type))
                    );
                    if (ptr(mod) == nullptr) {
                        Exception::from_python();
                    }

                    // instantiate the nested type and attach to the parent
                    Type<Class> cls = Class::__python__::__export__(mod);
                    if (PyObject_SetAttr(ptr(type), Name, ptr(cls))) {
                        Exception::from_python();
                    }

                    // insert into the module's C++ type map for fast lookup
                    // using C++ template syntax
                    ptr(mod)->type_map[typeid(Class)] = ptr(cls);

                    // insert into the global type map for use when generating
                    // C++ bindings from Python type hints
                    register_type<Class>(mod, ptr(cls));
                }
            }
        };
    }

    /* Expose a nested and templated py::Object type to Python. */
    template <
        static_str Name,
        template <typename...> typename Class
    >
    void type(std::string&& doc = "") {
        if (members.contains(Name)) {
            throw AttributeError(
                "Class '" + impl::demangle(typeid(Wrapper).name()) +
                "' already has an attribute named '" + Name + "'."
            );
        }
        members[Name] = {
            .type = true,
            .template_interface = true,
            .callbacks = {
                [doc = std::move(doc)](Type<Wrapper>& type) {
                    // get the module under which the parent type was exposed
                    Module<ModName> mod = steal<Module<ModName>>(
                        PyType_GetModule(ptr(type))
                    );
                    if (ptr(mod) == nullptr) {
                        Exception::from_python();
                    }

                    // create a stub type for the template interface
                    BertrandMeta stub = Meta::stub_type<Name>(
                        mod,
                        std::move(doc)
                    );
                    if (PyObject_SetAttr(ptr(type), Name, ptr(stub))) {
                        Exception::from_python();
                    }
                }
            }
        };
    }

    /* Expose a template instantiation of a nested py::Object type to Python. */
    template <
        static_str Name,
        std::derived_from<Object> Class,
        std::derived_from<Object>... Bases
    > requires (
        impl::is_generic<Class> &&
        impl::has_interface<Class> && (impl::has_interface<Bases> && ...) &&
        impl::has_python<Class> && (impl::has_python<Bases> && ...)
    )
    void type() {
        auto it = members.find(Name);
        if (it == members.end()) {
            throw TypeError(
                "No template interface found for type '" + Name + "' in "
                "class '" + impl::demangle(typeid(Wrapper).name()) + "' "
                "with specialization '" +
                impl::demangle(typeid(Class).name()) + "'.  Did you "
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
            BertrandMeta existing = steal<BertrandMeta>(
                PyObject_GetAttr(
                    ptr(type),
                    impl::TemplateString<Name>::ptr
                )
            );
            if (ptr(existing) == nullptr) {
                Exception::from_python();
            }

            // call the type's __export__() method to instantiate the type
            Module<ModName> mod = steal<Module<ModName>>(
                PyType_GetModule(ptr(type))
            );
            if (ptr(mod) == nullptr) {
                Exception::from_python();
            }
            Type<Class> cls = Type<Class>::__python__::__export__(mod);

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
            if (PyDict_SetItem(ptr(existing)->templates, key, ptr(cls))) {
                Py_DECREF(key);
                Exception::from_python();
            }
            Py_DECREF(key);

            // insert into the module's C++ type map for fast lookup
            // using C++ template syntax
            ptr(mod)->type_map[typeid(Class)] = ptr(cls);

            // insert into the global type map for use when generating C++
            // bindings from Python type hints
            register_type<Class>(mod, ptr(cls));
        });
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
        static unsigned int flags =
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE | Py_TPFLAGS_HAVE_GC;
        static std::vector<PyType_Slot> slots {
            {Py_tp_dealloc, reinterpret_cast<void*>(CRTP::__dealloc__)},
            {Py_tp_traverse, reinterpret_cast<void*>(CRTP::__traverse__)},
            {Py_tp_clear, reinterpret_cast<void*>(CRTP::__clear__)},
        };
        if (slots[slots.size() - 1].slot != 0) {
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
        Type<Wrapper> cls = steal<Type<Wrapper>>(
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
        new (ptr(cls)) Meta(impl::demangle(typeid(Wrapper).name()));
        ptr(cls)->_instancecheck = Meta::instancecheck<Wrapper>;
        ptr(cls)->_subclasscheck = Meta::subclasscheck<Wrapper>;
        ptr(cls)->class_getters = std::move(class_getters);
        ptr(cls)->class_setters = std::move(class_setters);

        // execute the callbacks to populate the type object
        for (auto&& [name, ctx] : members) {
            for (auto&& callback : ctx.callbacks) {
                callback(cls);
            }
        }
        PyType_Modified(ptr(cls));

        // insert into the global type map for use when generating bindings
        register_type<Wrapper>(module, ptr(cls));
        return cls;
    }

};


namespace impl {

    // template <typename CRTP, typename Wrapper, typename CppType>
    // struct TypeTag::def : BaseDef<CRTP, Wrapper, CppType> {
    //     static constexpr static_str __doc__ = [] {
    //         return (
    //             "A Bertrand-generated Python wrapper for the '" +
    //             impl::demangle(typeid(CppType).name()) + "' C++ type."
    //         );
    //     }();

    //     /* tp_hash delegates to `std::hash` if it exists. */
    //     static Py_hash_t __hash__(CRTP* self) {
    //         try {
    //             return std::visit(Visitor{
    //                 [](const CppType& cpp) {
    //                     return std::hash<CppType>{}(cpp);
    //                 },
    //                 [](const CppType* cpp) {
    //                     return std::hash<CppType>{}(*cpp);
    //                 }
    //             }, self->m_cpp);
    //         } catch (...) {
    //             Exception::to_python();
    //             return -1;
    //         }
    //     }

    //     /* tp_iter delegates to the type's begin() and end() iterators if they exist,
    //     and provides a thin Python wrapper around them that respects reference
    //     semantics and const-ness. */
    //     static PyObject* __iter__(CRTP* self) {
    //         try {
    //             PyObject* iter = std::visit(Visitor{
    //                 [self](CppType& cpp) -> PyObject* {
    //                     using Begin = decltype(std::ranges::begin(std::declval<CppType>()));
    //                     using End = decltype(std::ranges::end(std::declval<CppType>()));
    //                     using Iterator = Iterator<Begin, End>;
    //                     PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
    //                         PyObject_GetAttrString(
    //                             reinterpret_cast<PyObject*>(Py_TYPE(self)),
    //                             typeid(Iterator).name()
    //                         )
    //                     );
    //                     if (iter_type == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     Iterator* iter = iter_type->tp_alloc(iter_type, 0);
    //                     Py_DECREF(iter_type);
    //                     if (iter == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     new (&iter->begin) Iterator::Begin(std::ranges::begin(cpp));
    //                     new (&iter->end) Iterator::End(std::ranges::end(cpp));
    //                     return iter;
    //                 },
    //                 [self](CppType* cpp) -> PyObject* {
    //                     using Begin = decltype(std::ranges::begin(std::declval<CppType>()));
    //                     using End = decltype(std::ranges::end(std::declval<CppType>()));
    //                     using Iterator = Iterator<Begin, End>;
    //                     PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
    //                         PyObject_GetAttrString(
    //                             reinterpret_cast<PyObject*>(Py_TYPE(self)),
    //                             typeid(Iterator).name()
    //                         )
    //                     );
    //                     if (iter_type == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     Iterator* iter = iter_type->tp_alloc(iter_type, 0);
    //                     Py_DECREF(iter_type);
    //                     if (iter == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     new (&iter->begin) Iterator::Begin(std::ranges::begin(*cpp));
    //                     new (&iter->end) Iterator::End(std::ranges::end(*cpp));
    //                     return iter;
    //                 },
    //                 [self](const CppType* cpp) -> PyObject* {
    //                     using Begin = decltype(std::ranges::begin(std::declval<const CppType>()));
    //                     using End = decltype(std::ranges::end(std::declval<const CppType>()));
    //                     using Iterator = Iterator<Begin, End>;
    //                     PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
    //                         PyObject_GetAttrString(
    //                             reinterpret_cast<PyObject*>(Py_TYPE(self)),
    //                             typeid(Iterator).name()
    //                         )
    //                     );
    //                     if (iter_type == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     Iterator* iter = iter_type->tp_alloc(iter_type, 0);
    //                     Py_DECREF(iter_type);
    //                     if (iter == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     new (&iter->begin) Iterator::Begin(std::ranges::begin(*cpp));
    //                     new (&iter->end) Iterator::End(std::ranges::end(*cpp));
    //                     return iter;
    //                 }
    //             }, self->m_cpp);
    //             PyObject_GC_Track(iter);
    //             return iter;
    //         } catch (...) {
    //             Exception::to_python();
    //             return nullptr;
    //         }
    //     }

    //     /* __reversed__ delegates to the type's rbegin() and rend() iterators if they
    //     exist, and provides a thin Python wrapper around them that respects reference
    //     semantics and const-ness. */
    //     static PyObject* __reversed__(CRTP* self) {
    //         try {
    //             PyObject* iter = std::visit(Visitor{
    //                 [self](CppType& cpp) -> PyObject* {
    //                     using Begin = decltype(std::ranges::rbegin(std::declval<CppType>()));
    //                     using End = decltype(std::ranges::rend(std::declval<CppType>()));
    //                     using Iterator = Iterator<Begin, End>;
    //                     PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
    //                         PyObject_GetAttrString(
    //                             reinterpret_cast<PyObject*>(Py_TYPE(self)),
    //                             typeid(Iterator).name()
    //                         )
    //                     );
    //                     if (iter_type == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     Iterator* iter = iter_type->tp_alloc(iter_type, 0);
    //                     Py_DECREF(iter_type);
    //                     if (iter == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     new (&iter->begin) Iterator::Begin(std::ranges::rbegin(cpp));
    //                     new (&iter->end) Iterator::End(std::ranges::rend(cpp));
    //                     return iter;
    //                 },
    //                 [self](CppType* cpp) -> PyObject* {
    //                     using Begin = decltype(std::ranges::rbegin(std::declval<CppType>()));
    //                     using End = decltype(std::ranges::rend(std::declval<CppType>()));
    //                     using Iterator = Iterator<Begin, End>;
    //                     PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
    //                         PyObject_GetAttrString(
    //                             reinterpret_cast<PyObject*>(Py_TYPE(self)),
    //                             typeid(Iterator).name()
    //                         )
    //                     );
    //                     if (iter_type == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     Iterator* iter = iter_type->tp_alloc(iter_type, 0);
    //                     Py_DECREF(iter_type);
    //                     if (iter == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     new (&iter->begin) Iterator::Begin(std::ranges::rbegin(*cpp));
    //                     new (&iter->end) Iterator::End(std::ranges::rend(*cpp));
    //                     return iter;
    //                 },
    //                 [self](const CppType* cpp) -> PyObject* {
    //                     using Begin = decltype(std::ranges::rbegin(std::declval<const CppType>()));
    //                     using End = decltype(std::ranges::rend(std::declval<const CppType>()));
    //                     using Iterator = Iterator<Begin, End>;
    //                     PyTypeObject* iter_type = reinterpret_cast<PyTypeObject*>(
    //                         PyObject_GetAttrString(
    //                             reinterpret_cast<PyObject*>(Py_TYPE(self)),
    //                             typeid(Iterator).name()
    //                         )
    //                     );
    //                     if (iter_type == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     Iterator* iter = iter_type->tp_alloc(iter_type, 0);
    //                     Py_DECREF(iter_type);
    //                     if (iter == nullptr) {
    //                         Exception::from_python();
    //                     }
    //                     new (&iter->begin) Iterator::Begin(std::ranges::rbegin(*cpp));
    //                     new (&iter->end) Iterator::End(std::ranges::rend(*cpp));
    //                     return iter;
    //                 }
    //             }, self->m_cpp);
    //             PyObject_GC_Track(iter);
    //             return iter;
    //         } catch (...) {
    //             Exception::to_python();
    //             return nullptr;
    //         }
    //     }

    //     /* tp_repr demangles the exact C++ type name and includes memory address
    //     similar to Python. */
    //     static PyObject* __repr__(CRTP* self) {
    //         try {
    //             std::string str = std::visit(Visitor{
    //                 [](const CppType& cpp) {
    //                     return repr(cpp);
    //                 },
    //                 [](const CppType* cpp) {
    //                     return repr(*cpp);
    //                 }
    //             }, self->m_cpp);
    //             PyObject* result = PyUnicode_FromStringAndSize(
    //                 str.c_str(),
    //                 str.size()
    //             );
    //             if (result == nullptr) {
    //                 return nullptr;
    //             }
    //             return result;
    //         } catch (...) {
    //             Exception::to_python();
    //             return nullptr;
    //         }
    //     }

    //     /// TODO: maybe there needs to be some handling to make sure that the container
    //     /// yields specifically lvalue references to Python objects?  I might also need
    //     /// to const_cast them in the __clear__ method in order to properly release
    //     /// everything.

    //     /* tp_traverse registers any Python objects that are owned by the C++ object
    //     with Python's cyclic garbage collector. */
    //     static int __traverse__(CRTP* self, visitproc visit, void* arg) {
    //         if constexpr (
    //             impl::iterable<CppType> &&
    //             std::derived_from<std::decay_t<impl::iter_type<CppType>>, Object>
    //         ) {
    //             if (std::holds_alternative<CppType>(self->m_cpp)) {
    //                 for (auto&& item : std::get<CppType>(self->m_cpp)) {
    //                     Py_VISIT(ptr(item));
    //                 }
    //             }
    //         }
    //         Py_VISIT(Py_TYPE(self));  // required for heap types
    //         return 0;
    //     }

    //     /* tp_clear breaks possible reference cycles for Python objects that are owned
    //     by the C++ object.  Note that special care has to be taken to avoid
    //     double-freeing any Python objects between this method and the `__dealloc__()`
    //     destructor. */
    //     static int __clear__(CRTP* self) {
    //         if constexpr (
    //             impl::iterable<CppType> &&
    //             std::derived_from<std::decay_t<impl::iter_type<CppType>>, Object>
    //         ) {
    //             if (std::holds_alternative<CppType>(self->m_cpp)) {
    //                 /// NOTE: In order to avoid a double free, we release() all of the
    //                 /// container's items here, so that when their destructors are called
    //                 /// normally in __dealloc__(), they see a null pointer and do nothing.
    //                 for (auto&& item : std::get<CppType>(self->m_cpp)) {
    //                     Py_XDECREF(release(item));
    //                 }
    //             }
    //         }
    //         return 0;
    //     }

    // };

}


/// NOTE: all types must manually define __object__ as they are generated, since it
/// can't be deduced any other way.


/// TODO: what about isinstance/issubclass?


template <typename T, impl::has_cpp Self>
struct __isinstance__<T, Self>                              : returns<bool> {
    /// TODO: is this default behavior?
    static bool operator()(const T& obj) {
        return PyType_IsSubtype(Py_TYPE(ptr(obj)), ptr(Type<Self>()));
    }
};


template <impl::has_cpp Self, typename T>
    requires (std::convertible_to<T, impl::cpp_type<Self>>)
struct __init__<Self, T>                                    : returns<Self> {
    static Self operator()(T&& value) {
        return impl::construct<Self>(std::forward<T>(value));
    }
};


template <impl::has_cpp Self, typename... Args>
    requires (std::constructible_from<impl::cpp_type<Self>, Args...>)
struct __explicit_init__<Self, Args...>                     : returns<Self> {
    static Self operator()(Args&&... args) {
        return impl::construct<Self>(std::forward<Args>(args)...);
    }
};


template <impl::has_cpp Self, typename T>
    requires (std::convertible_to<impl::cpp_type<Self>, T>)
struct __cast__<Self, T>                                    : returns<T> {
    static T operator()(const Self& self) {
        return unwrap(self);
    }
};


template <impl::has_cpp Self, typename... Args>
    requires (std::is_invocable_v<impl::cpp_type<Self>, Args...>)
struct __call__<Self, Args...> : returns<std::invoke_result_t<impl::cpp_type<Self>, Args...>> {};


/// TODO: __getattr__/__setattr__/__delattr__ must be defined for each type, and cannot
/// be deduced here.


template <impl::has_cpp Self, typename... Key>
    requires (impl::supports_lookup<impl::cpp_type<Self>, Key...>)
struct __getitem__<Self, Key...> : returns<impl::lookup_type<impl::cpp_type<Self>, Key...>> {
    template <typename... Ks>
    static decltype(auto) operator()(const Self& self, Ks&&... key) {
        return unwrap(self)[std::forward<Ks>(key)...];
    }
};


template <impl::has_cpp Self, typename Value, typename... Key>
    requires (impl::supports_item_assignment<impl::cpp_type<Self>, Value, Key...>)
struct __setitem__<Self, Value, Key...> : returns<void> {};


template <impl::has_cpp Self>
    requires (impl::has_size<impl::cpp_type<Self>>)
struct __len__<Self> : returns<size_t> {};


template <impl::has_cpp Self>
    requires (impl::iterable<impl::cpp_type<Self>>)
struct __iter__<Self> : returns<impl::iter_type<impl::cpp_type<Self>>> {};


template <impl::has_cpp Self>
    requires (impl::reverse_iterable<impl::cpp_type<Self>>)
struct __reversed__<Self> : returns<impl::reverse_iter_type<impl::cpp_type<Self>>> {};


template <impl::has_cpp Self, typename Key>
    requires (impl::has_contains<impl::cpp_type<Self>, Key>)
struct __contains__<Self, Key> : returns<bool> {};


template <impl::has_cpp Self>
    requires (impl::hashable<impl::cpp_type<Self>>)
struct __hash__<Self> : returns<size_t> {};


template <impl::has_cpp Self>
    requires (impl::has_abs<impl::cpp_type<Self>>)
struct __abs__<Self> : returns<impl::abs_type<impl::cpp_type<Self>>> {};


template <impl::has_cpp Self>
    requires (impl::has_pos<impl::cpp_type<Self>>)
struct __pos__<Self> : returns<impl::pos_type<impl::cpp_type<Self>>> {};


template <impl::has_cpp Self>
    requires (impl::has_neg<impl::cpp_type<Self>>)
struct __neg__<Self> : returns<impl::neg_type<impl::cpp_type<Self>>> {};


template <impl::has_cpp Self>
    requires (impl::has_preincrement<impl::cpp_type<Self>>)
struct __increment__<Self> : returns<impl::preincrement_type<impl::cpp_type<Self>>> {};


template <impl::has_cpp Self>
    requires (impl::has_predecrement<impl::cpp_type<Self>>)
struct __decrement__<Self> : returns<impl::predecrement_type<impl::cpp_type<Self>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_lt<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __lt__<L, R> : returns<impl::lt_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_le<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __le__<L, R> : returns<impl::le_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_eq<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __eq__<L, R> : returns<impl::eq_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_ne<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ne__<L, R> : returns<impl::ne_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_ge<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ge__<L, R> : returns<impl::ge_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_gt<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __gt__<L, R> : returns<impl::gt_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_add<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __add__<L, R> : returns<impl::add_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_iadd<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __iadd__<L, R> : returns<impl::iadd_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_sub<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __sub__<L, R> : returns<impl::sub_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_isub<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __isub__<L, R> : returns<impl::isub_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_mul<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __mul__<L, R> : returns<impl::mul_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_imul<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __imul__<L, R> : returns<impl::imul_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_truediv<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __truediv__<L, R> : returns<impl::truediv_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_itruediv<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __itruediv__<L, R> : returns<impl::itruediv_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_mod<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __mod__<L, R> : returns<impl::mod_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_imod<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __imod__<L, R> : returns<impl::imod_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_pow<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __pow__<L, R> : returns<impl::pow_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_lshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __lshift__<L, R> : returns<impl::lshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_ilshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ilshift__<L, R> : returns<impl::ilshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_rshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __rshift__<L, R> : returns<impl::rshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_irshift<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __irshift__<L, R> : returns<impl::irshift_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_and<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __and__<L, R> : returns<impl::and_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_iand<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __iand__<L, R> : returns<impl::iand_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_or<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __or__<L, R> : returns<impl::or_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_ior<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ior__<L, R> : returns<impl::ior_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_xor<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __xor__<L, R> : returns<impl::xor_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


template <typename L, typename R>
    requires (
        (impl::has_cpp<L> || impl::has_cpp<R>) &&
        impl::has_ixor<impl::cpp_type<L>, impl::cpp_type<R>>
    )
struct __ixor__<L, R> : returns<impl::ixor_type<impl::cpp_type<L>, impl::cpp_type<R>>> {};


}  // namespace py


#endif
