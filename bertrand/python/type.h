#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_TYPE_H
#define BERTRAND_PYTHON_TYPE_H

#include "common.h"
#include "str.h"
#include "tuple.h"
#include "dict.h"


namespace bertrand {
namespace py {


template <typename T>
struct __issubclass__<T, Type>                              : Returns<bool> {
    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() { return impl::type_like<T>; }
};


template <typename T>
struct __isinstance__<T, Type>                              : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Type>();
        } else if constexpr (issubclass<T, Type>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyType_Check(obj.ptr());
        } else {
            return false;
        }
    }
};


/* Represents a statically-typed Python type object in C++.  Note that new types can be
created on the fly by invoking the `type` metaclass directly, using an optional name,
bases, and namespace. */
class Type : public Object {
    using Base = Object;
    using Self = Type;

    PyTypeObject* self() const noexcept {
        return reinterpret_cast<PyTypeObject*>(m_ptr);
    }

public:
    static const Type type;

    Type(Handle h, borrowed_t t) : Base(h, t) {}
    Type(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Type, __init__<Type, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Type, std::remove_cvref_t<Args>...>::enable
        )
    Type(Args&&... args) : Base(
        __init__<Type, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

    template <typename... Args>
        requires (
            !__init__<Type, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Type, __explicit_init__<Type, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Type, std::remove_cvref_t<Args>...>::enable
        )
    explicit Type(Args&&... args) : Base(
        __explicit_init__<Type, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

    /* Get the Python type of a registered pybind11 extension type. */
    template <typename T>
    [[nodiscard]] static Type of() {
        return reinterpret_steal<Type>(pybind11::type::of<T>().release());
    }

    #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

        /* Get the module that the type is defined in.  Can throw if called on a
        static type rather than a heap type (one that was created using
        PyType_FromModuleAndSpec() or higher). */
        [[nodiscard]] auto module_() const {
            PyObject* result = PyType_GetModule(self());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Module>(result);
        }

    #endif

    #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

        /* Get the type's qualified name. */
        [[nodiscard]] Str qualname() const;

    #endif

    /* Get type's tp_name slot. */
    [[nodiscard]] auto name() const noexcept {
        return self()->tp_name;
    }

    /* Get the type's tp_basicsize slot. */
    [[nodiscard]] auto basicsize() const noexcept {
        return self()->tp_basicsize;
    }

    /* Get the type's tp_itemsize slot. */
    [[nodiscard]] auto itemsize() const noexcept {
        return self()->tp_itemsize;
    }

    /* Get the type's tp_dealloc slot. */
    [[nodiscard]] auto dealloc() const noexcept {
        return self()->tp_dealloc;
    }

    /* Get the type's tp_as_async slot. */
    [[nodiscard]] auto as_async() const noexcept {
        return self()->tp_as_async;
    }

    /* Get the type's tp_repr slot. */
    [[nodiscard]] auto repr() const noexcept {
        return self()->tp_repr;
    }

    /* Get the type's tp_as_number slot. */
    [[nodiscard]] auto as_number() const noexcept {
        return self()->tp_as_number;
    }

    /* Get the type's tp_as_sequence slot. */
    [[nodiscard]] auto as_sequence() const noexcept {
        return self()->tp_as_sequence;
    }

    /* Get the type's tp_as_mapping slot. */
    [[nodiscard]] auto as_mapping() const noexcept {
        return self()->tp_as_mapping;
    }

    /* Get the type's tp_hash slot. */
    [[nodiscard]] auto hash() const noexcept {
        return self()->tp_hash;
    }

    /* Get the type's tp_call slot. */
    [[nodiscard]] auto call() const noexcept {
        return self()->tp_call;
    }

    /* Get the type's tp_str slot. */
    [[nodiscard]] auto str() const noexcept {
        return self()->tp_str;
    }

    /* Get the type's tp_getattro slot. */
    [[nodiscard]] auto getattro() const noexcept {
        return self()->tp_getattro;
    }

    /* Get the type's tp_setattro slot. */
    [[nodiscard]] auto setattro() const noexcept {
        return self()->tp_setattro;
    }

    /* Get the type's tp_as_buffer slot. */
    [[nodiscard]] auto as_buffer() const noexcept {
        return self()->tp_as_buffer;
    }

    /* Get the type's tp_flags slot. */
    [[nodiscard]] auto flags() const noexcept {
        return self()->tp_flags;
    }

    /* Get the type's tp_doc slot. */
    [[nodiscard]] auto doc() const noexcept {
        return self()->tp_doc;
    }

    /* Get the type's tp_traverse slot. */
    [[nodiscard]] auto traverse() const noexcept {
        return self()->tp_traverse;
    }

    /* Get the type's tp_clear slot. */
    [[nodiscard]] auto clear() const noexcept {
        return self()->tp_clear;
    }

    /* Get the type's tp_richcompare slot. */
    [[nodiscard]] auto richcompare() const noexcept {
        return self()->tp_richcompare;
    }

    /* Get the type's tp_iter slot. */
    [[nodiscard]] auto iter() const noexcept {
        return self()->tp_iter;
    }

    /* Get the type's tp_iternext slot. */
    [[nodiscard]] auto iternext() const noexcept {
        return self()->tp_iternext;
    }

    /* Get the type's tp_methods slot. */
    [[nodiscard]] auto methods() const noexcept {
        return self()->tp_methods;
    }

    /* Get the type's tp_members slot. */
    [[nodiscard]] auto members() const noexcept {
        return self()->tp_members;
    }

    /* Get the type's tp_getset slot. */
    [[nodiscard]] auto getset() const noexcept {
        return self()->tp_getset;
    }

    /* Get the type's tp_base slot. */
    [[nodiscard]] auto base() const noexcept {
        return reinterpret_borrow<Type>(reinterpret_cast<PyObject*>(self()->tp_base));
    }

    /* Get the type's tp_dict slot. */
    [[nodiscard]] auto dict() const noexcept {
        return reinterpret_borrow<Dict<Str, Object>>(self()->tp_dict);
    }

    /* Get the type's tp_descr_get slot. */
    [[nodiscard]] auto descr_get() const noexcept {
        return self()->tp_descr_get;
    }

    /* Get the type's tp_descr_set slot. */
    [[nodiscard]] auto descr_set() const noexcept {
        return self()->tp_descr_set;
    }

    /* Get the type's tp_bases slot. */
    [[nodiscard]] auto bases() const noexcept {
        return reinterpret_borrow<Tuple<Type>>(self()->tp_bases);
    }

    /* Get the type's tp_mro slot. */
    [[nodiscard]] auto mro() const noexcept {
        return reinterpret_borrow<Tuple<Type>>(self()->tp_mro);
    }

    /* Get the type's tp_finalize slot. */
    [[nodiscard]] auto finalize() const noexcept {
        return self()->tp_finalize;
    }

    /* Get the type's tp_vectorcall slot. */
    [[nodiscard]] auto vectorcall() const noexcept {
        return self()->tp_vectorcall;
    }

    /* Get the type's tp_vectorcall_offset slot. */
    [[nodiscard]] auto vectorcall_offset() const noexcept {
        return self()->tp_vectorcall_offset;
    }

    /* Clear the lookup cache for the type and all of its subtypes.  This method should
    be called after any manual modification to the attributes or this class or any of
    its bases at the C++ level, in order to synchronize them with the Python
    interpreter.  Most users will never need to use this in practice. */
    void clear_cache() const noexcept {
        PyType_Modified(reinterpret_cast<PyTypeObject*>(this->ptr()));
    }

};


template <>
struct __init__<Type>                                       : Returns<Type> {
    static auto operator()() {
        return reinterpret_borrow<Type>((PyObject*)&PyType_Type);
    }
};


template <impl::python_like T>
struct __explicit_init__<Type, T>                           : Returns<Type> {
    static auto operator()(const T& obj) {
        return reinterpret_borrow<Type>((PyObject*)Py_TYPE(obj.ptr()));
    }
};


// TODO: maybe these are a case where I need to place the constructor in the class
// itself, so that there's no ambiguity about constructing vs converting to a type.
// -> Or I only expose the 3 argument version and allow conversion from Dict<> to
// Dict<Str, Object>?


template <
    impl::cpp_like Name,
    std::convertible_to<Tuple<Type>> Bases,
    std::convertible_to<Dict<Str, Object>> Namespace
> requires (std::convertible_to<Name, Str>)
struct __explicit_init__<Type, Name, Bases, Namespace>      : Returns<Type> {
    static auto operator()(
        const Str& name,
        const Tuple<Type>& bases,
        const Dict<Str, Object>& dict
    ) {
        PyObject* result = PyObject_CallFunctionObjArgs(
            (PyObject*)&PyType_Type,
            name.ptr(),
            bases.ptr(),
            dict.ptr(),
            nullptr
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Type>(result);
    }
};
template <impl::cpp_like Name, std::convertible_to<Tuple<Type>> Bases>
    requires (std::convertible_to<Name, Str>)
struct __explicit_init__<Type, Name, Bases>                 : Returns<Type> {
    static auto operator()(const Str& name, const Tuple<Type>& bases) {
        PyObject* dict = PyDict_New();
        if (dict == nullptr) {
            Exception::from_python();
        }
        PyObject* result = PyObject_CallFunctionObjArgs(
            (PyObject*)&PyType_Type,
            name.ptr(),
            bases.ptr(),
            dict,
            nullptr
        );
        if (result == nullptr) {
            Py_DECREF(dict);
            Exception::from_python();
        }
        return reinterpret_steal<Type>(result);
    }
};
template <impl::cpp_like Name>
    requires (std::convertible_to<Name, Str>)
struct __explicit_init__<Type, Name>                        : Returns<Type> {
    static auto operator()(const Str& name) {
        PyObject* bases = PyTuple_New(0);
        if (bases == nullptr) {
            Exception::from_python();
        }
        PyObject* dict = PyDict_New();
        if (dict == nullptr) {
            Py_DECREF(bases);
            Exception::from_python();
        }
        PyObject* result = PyObject_CallFunctionObjArgs(
            (PyObject*)&PyType_Type,
            name.ptr(),
            bases,
            dict,
            nullptr
        );
        if (result == nullptr) {
            Py_DECREF(bases);
            Py_DECREF(dict);
            Exception::from_python();
        }
        return reinterpret_steal<Type>(result);
    }
};


/////////////////////
////    SUPER    ////
/////////////////////


template <typename T>
struct __issubclass__<T, Super>                             : Returns<bool> {
    static consteval bool operator()(const T&) { return operator()(); }
    static consteval bool operator()() { return std::derived_from<T, Super>; }
};


template <typename T>
struct __isinstance__<T, Super>                             : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return issubclass<T, Super>();
        } else if constexpr (issubclass<T, Super>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyObject_IsInstance(
                obj.ptr(),
                reinterpret_cast<PyObject*>(&PySuper_Type)
            );
        } else {
            return false;
        }
    }
};


/* Represents a statically-typed Python `super` object in C++. */
class Super : public Object {
    using Base = Object;
    using Self = Super;

public:
    static const Type type;

    Super(Handle h, borrowed_t t) : Base(h, t) {}
    Super(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Super, __init__<Super, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Super, std::remove_cvref_t<Args>...>::enable
        )
    Super(Args&&... args) : Base(
        __init__<Super, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

    template <typename... Args>
        requires (
            !__init__<Super, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Super, __explicit_init__<Super, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Super, std::remove_cvref_t<Args>...>::enable
        )
    explicit Super(Args&&... args) : Base(
        __explicit_init__<Super, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    ) {}

};


template <>
struct __init__<Super>                                      : Returns<Super> {
    static auto operator()() {
        PyObject* result = PyObject_CallNoArgs((PyObject*)&PySuper_Type);
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Super>(result);
    }
};


template <std::convertible_to<Type> Base, impl::python_like Self>
struct __explicit_init__<Super, Base, Self>                  : Returns<Super> {
    static auto operator()(const Type& base, const Self& self) {
        PyObject* result = PyObject_CallFunctionObjArgs(
            (PyObject*)&PySuper_Type,
            base.ptr(),
            self.ptr(),
            nullptr
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Super>(result);
    }
};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_TYPE_H
