#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_TYPE_H
#define BERTRAND_PYTHON_TYPE_H

#include "common.h"
#include "tuple.h"
#include "dict.h"


namespace bertrand {
namespace py {


namespace impl {

template <typename... Args>
struct __call__<Type, Args...>                                  : Returns<Object> {};
template <typename T>
struct __getitem__<Type, T>                                     : Returns<Object> {};
template <>
struct __hash__<Type>                                           : Returns<size_t> {};
template <typename T>
struct __or__<Type, T>                                          : Returns<Object> {};

template <typename ... Args>
struct __call__<Super, Args...>                             : Returns<Object> {};
template <>
struct __len__<Super>                                       : Returns<size_t> {};
template <typename T>
struct __contains__<Super, T>                               : Returns<bool> {};
template <>
struct __iter__<Super>                                      : Returns<Object> {};
template <>
struct __reversed__<Super>                                  : Returns<Object> {};
template <typename Key>
struct __getitem__<Super, Key>                              : Returns<Object> {};
template <typename Key, typename Value>
struct __setitem__<Super, Key, Value>                       : Returns<void> {};
template <typename Key>
struct __delitem__<Super, Key>                              : Returns<void> {};
template <>
struct __pos__<Super>                                       : Returns<Object> {};
template <>
struct __neg__<Super>                                       : Returns<Object> {};
template <>
struct __abs__<Super>                                       : Returns<Object> {};
template <>
struct __invert__<Super>                                    : Returns<Object> {};
template <>
struct __increment__<Super>                                 : Returns<Object> {};
template <>
struct __decrement__<Super>                                 : Returns<Object> {};
template <typename T>
struct __lt__<Super, T>                                     : Returns<bool> {};
template <typename T>
struct __lt__<T, Super>                                     : Returns<bool> {};
template <typename T>
struct __le__<Super, T>                                     : Returns<bool> {};
template <typename T>
struct __le__<T, Super>                                     : Returns<bool> {};
template <typename T>
struct __ge__<Super, T>                                     : Returns<bool> {};
template <typename T>
struct __ge__<T, Super>                                     : Returns<bool> {};
template <typename T>
struct __gt__<Super, T>                                     : Returns<bool> {};
template <typename T>
struct __gt__<T, Super>                                     : Returns<bool> {};
template <typename T>
struct __add__<Super, T>                                    : Returns<Object> {};
template <typename T>
struct __add__<T, Super>                                    : Returns<Object> {};
template <typename T>
struct __sub__<Super, T>                                    : Returns<Object> {};
template <typename T>
struct __sub__<T, Super>                                    : Returns<Object> {};
template <typename T>
struct __mul__<Super, T>                                    : Returns<Object> {};
template <typename T>
struct __mul__<T, Super>                                    : Returns<Object> {};
template <typename T>
struct __truediv__<Super, T>                                : Returns<Object> {};
template <typename T>
struct __truediv__<T, Super>                                : Returns<Object> {};
template <typename T>
struct __mod__<Super, T>                                    : Returns<Object> {};
template <typename T>
struct __mod__<T, Super>                                    : Returns<Object> {};
template <typename T>
struct __lshift__<Super, T>                                 : Returns<Object> {};
template <typename T>
struct __lshift__<T, Super>                                 : Returns<Object> {};
template <typename T>
struct __rshift__<Super, T>                                 : Returns<Object> {};
template <typename T>
struct __rshift__<T, Super>                                 : Returns<Object> {};
template <typename T>
struct __and__<Super, T>                                    : Returns<Object> {};
template <typename T>
struct __and__<T, Super>                                    : Returns<Object> {};
template <typename T>
struct __or__<Super, T>                                     : Returns<Object> {};
template <typename T>
struct __or__<T, Super>                                     : Returns<Object> {};
template <typename T>
struct __xor__<Super, T>                                    : Returns<Object> {};
template <typename T>
struct __xor__<T, Super>                                    : Returns<Object> {};
template <typename T>
struct __iadd__<Super, T>                                   : Returns<Object> {};
template <typename T>
struct __isub__<Super, T>                                   : Returns<Object> {};
template <typename T>
struct __imul__<Super, T>                                   : Returns<Object> {};
template <typename T>
struct __itruediv__<Super, T>                               : Returns<Object> {};
template <typename T>
struct __imod__<Super, T>                                   : Returns<Object> {};
template <typename T>
struct __ilshift__<Super, T>                                : Returns<Object> {};
template <typename T>
struct __irshift__<Super, T>                                : Returns<Object> {};
template <typename T>
struct __iand__<Super, T>                                   : Returns<Object> {};
template <typename T>
struct __ior__<Super, T>                                    : Returns<Object> {};
template <typename T>
struct __ixor__<Super, T>                                   : Returns<Object> {};

}


/* Wrapper around a pybind11::type that enables extra C API functionality, such as the
ability to create new types on the fly by calling the type() metaclass, or directly
querying PyTypeObject* fields. */
class Type : public Object {
    using Base = Object;

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, Type, impl::type_like, PyType_Check)
    BERTRAND_OBJECT_OPERATORS(Type)

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Default constructor.  Initializes to the built-in type metaclass. */
    Type() : Base((PyObject*) &PyType_Type, borrowed_t{}) {}

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    Type(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicitly detect the type of an arbitrary Python object. */
    template <impl::python_like T>
    explicit Type(const T& obj) :
        Base(reinterpret_cast<PyObject*>(Py_TYPE(obj.ptr())), borrowed_t{})
    {}

    /* Dynamically create a new Python type by calling the type() metaclass. */
    explicit Type(const Str& name, const Tuple& bases = {}, const Dict& dict = {});

    /* Create a new heap type from a CPython PyType_Spec*.  Note that this is not
    exactly interchangeable with a standard call to the type metaclass directly, as it
    does not invoke any of the __init__(), __new__(), __init_subclass__(), or
    __set_name__() methods for the type or any of its bases. */
    explicit Type(PyType_Spec* spec) : Base(PyType_FromSpec(spec), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Create a new heap type from a CPython PyType_Spec and bases.  See
    Type(PyType_Spec*) for more information. */
    template <typename T>
    explicit Type(PyType_Spec* spec, const Tuple& bases) :
        Base(PyType_FromSpecWithBases(spec, bases.ptr()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

        /* Create a new heap type from a module name, CPython PyType_Spec, and bases.
        See Type(PyType_Spec*) for more information. */
        template <typename T, typename U>
        explicit Type(const Module& module, PyType_Spec* spec, const Tuple& bases) :
            Base(PyType_FromModuleAndSpec(module.ptr(), spec, bases.ptr()), stolen_t{})
        {
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
        }

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Create a new heap type from a full CPython metaclass, module name,
        PyType_Spec and bases.  See Type(PyType_Spec*) for more information. */
        template <typename T, typename U, typename V>
        explicit Type(
            const Type& metaclass,
            const Module& module,
            PyType_Spec* spec,
            const Tuple& bases
        ) : Base(
            PyType_FromMetaClass(
                reinterpret_cast<PyTypeObject*>(metaclass.ptr()),
                module.ptr(),
                spec,
                bases.ptr()
            ),
            stolen_t{}
        ) {
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
        }

    #endif

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* A proxy for the type's PyTypeObject* struct. */
    struct Slots {
        PyTypeObject* type_obj;

        #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

            /* Get the module that the type is defined in.  Can throw if called on a
            static type rather than a heap type (one that was created using
            PyType_FromModuleAndSpec() or higher). */
            inline Module module_() const noexcept {
                PyObject* result = PyType_GetModule(type_obj);
                if (result == nullptr) {
                    throw error_already_set();
                }
                return reinterpret_steal<Module>(result);
            }

        #endif

        /* Get type's tp_name slot. */
        inline const char* name() const noexcept {
            return type_obj->tp_name;
        }

        #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

            /* Get the type's qualified name. */
            inline Str qualname() const {
                PyObject* result = PyType_GetQualname(type_obj);
                if (result == nullptr) {
                    throw error_already_set();
                }
                return reinterpret_steal<Str>(result);
            }

        #endif

        /* Get the type's tp_basicsize slot. */
        inline Py_ssize_t basicsize() const noexcept {
            return type_obj->tp_basicsize;
        }

        /* Get the type's tp_itemsize slot. */
        inline Py_ssize_t itemsize() const noexcept {
            return type_obj->tp_itemsize;
        }

        /* Get the type's tp_dealloc slot. */
        inline destructor dealloc() const noexcept {
            return type_obj->tp_dealloc;
        }

        /* Get the type's tp_vectorcall slot. */
        inline Py_ssize_t tp_vectorcall_offset() const noexcept {
            return type_obj->tp_vectorcall_offset;
        }

        /* Get the type's tp_as_async slot. */
        inline PyAsyncMethods* as_async() const noexcept {
            return type_obj->tp_as_async;
        }

        /* Get the type's tp_repr slot. */
        inline reprfunc repr() const noexcept {
            return type_obj->tp_repr;
        }

        /* Get the type's tp_as_number slot. */
        inline PyNumberMethods* as_number() const noexcept {
            return type_obj->tp_as_number;
        }

        /* Get the type's tp_as_sequence slot. */
        inline PySequenceMethods* as_sequence() const noexcept {
            return type_obj->tp_as_sequence;
        }

        /* Get the type's tp_as_mapping slot. */
        inline PyMappingMethods* as_mapping() const noexcept {
            return type_obj->tp_as_mapping;
        }

        /* Get the type's tp_hash slot. */
        inline hashfunc hash() const noexcept {
            return type_obj->tp_hash;
        }

        /* Get the type's tp_call slot. */
        inline ternaryfunc call() const noexcept {
            return type_obj->tp_call;
        }

        /* Get the type's tp_str slot. */
        inline reprfunc str() const noexcept {
            return type_obj->tp_str;
        }

        /* Get the type's tp_getattro slot. */
        inline getattrofunc getattro() const noexcept {
            return type_obj->tp_getattro;
        }

        /* Get the type's tp_setattro slot. */
        inline setattrofunc setattro() const noexcept {
            return type_obj->tp_setattro;
        }

        /* Get the type's tp_as_buffer slot. */
        inline PyBufferProcs* as_buffer() const noexcept {
            return type_obj->tp_as_buffer;
        }

        /* Get the type's tp_flags slot. */
        inline unsigned long flags() const noexcept {
            return type_obj->tp_flags;
        }

        /* Get the type's tp_doc slot. */
        inline const char* doc() const noexcept {
            return type_obj->tp_doc;
        }

        /* Get the type's tp_traverse slot. */
        inline traverseproc traverse() const noexcept {
            return type_obj->tp_traverse;
        }

        /* Get the type's tp_clear slot. */
        inline inquiry clear() const noexcept {
            return type_obj->tp_clear;
        }

        /* Get the type's tp_richcompare slot. */
        inline richcmpfunc richcompare() const noexcept {
            return type_obj->tp_richcompare;
        }

        /* Get the type's tp_iter slot. */
        inline getiterfunc iter() const noexcept {
            return type_obj->tp_iter;
        }

        /* Get the type's tp_iternext slot. */
        inline iternextfunc iternext() const noexcept {
            return type_obj->tp_iternext;
        }

        /* Get the type's tp_methods slot. */
        inline PyMethodDef* methods() const noexcept {
            return type_obj->tp_methods;
        }

        /* Get the type's tp_members slot. */
        inline PyMemberDef* members() const noexcept {
            return type_obj->tp_members;
        }

        /* Get the type's tp_getset slot. */
        inline PyGetSetDef* getset() const noexcept {
            return type_obj->tp_getset;
        }

        /* Get the type's tp_base slot. */
        inline PyTypeObject* base() const noexcept {
            return type_obj->tp_base;
        }

        /* Get the type's tp_dict slot. */
        inline PyObject* dict() const noexcept {
            return type_obj->tp_dict;
        }

        /* Get the type's tp_descr_get slot. */
        inline descrgetfunc descr_get() const noexcept {
            return type_obj->tp_descr_get;
        }

        /* Get the type's tp_descr_set slot. */
        inline descrsetfunc descr_set() const noexcept {
            return type_obj->tp_descr_set;
        }

        /* Get the type's tp_bases slot. */
        inline Tuple bases() const noexcept {
            return reinterpret_borrow<Tuple>(type_obj->tp_bases);
        }

        /* Get the type's tp_mro slot. */
        inline Tuple mro() const noexcept {
            return reinterpret_borrow<Tuple>(type_obj->tp_mro);
        }

        /* Get the type's tp_finalize slot. */
        inline destructor finalize() const noexcept {
            return type_obj->tp_finalize;
        }

        /* Get the type's tp_vectorcall slot. */
        inline Py_ssize_t vectorcall() const noexcept {
            return type_obj->tp_vectorcall_offset;
        }

    };

    /* Access the type's internal slots. */
    inline Slots slots() const {
        return {reinterpret_cast<PyTypeObject*>(this->ptr())};
    }

    /* Clear the lookup cache for the type and all of its subtypes.  This method should
    be called after any manual modification to the attributes or this class or any of
    its bases at the C++ level, in order to synchronize them with the Python
    interpreter.  Most users will never need to use this in practice. */
    inline void clear_cache() const noexcept {
        PyType_Modified(reinterpret_cast<PyTypeObject*>(this->ptr()));
    }

};


///////////////////////
////    SUPER()    ////
///////////////////////


/* New subclass of pybind11::object that represents Python's built-in super() type. */
class Super : public Object {
    using Base = Object;

    template <typename T>
    static constexpr bool comptime_check = std::is_base_of_v<Super, T>;

    inline static int runtime_check(PyObject* obj) {
        int result = PyObject_IsInstance(
            obj,
            reinterpret_cast<PyObject*>(&PySuper_Type)
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    BERTRAND_OBJECT_COMMON(Base, Super, comptime_check, runtime_check)
    BERTRAND_OBJECT_OPERATORS(Super)

    /* Default constructor.  Equivalent to Python `super()` with no arguments, which
    uses the calling context's inheritance hierarchy. */
    Super() : Base(
        PyObject_CallNoArgs(reinterpret_cast<PyObject*>(&PySuper_Type)),
        stolen_t{}
    ) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Copy/move constructors. */
    template <typename T> requires (check<T>() && impl::python_like<T>)
    Super(T&& other) : Base(std::forward<T>(other)) {}

    /* Explicit constructor.  Equivalent to Python `super(type, self)` with 2
    arguments. */
    explicit Super(const Type& type, const Handle& self) :
        Base(PyObject_CallFunctionObjArgs(
            reinterpret_cast<PyObject*>(&PySuper_Type),
            type.ptr(),
            self.ptr(),
            nullptr
        ), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

};


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_TYPE_H
