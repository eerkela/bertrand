#ifndef BERTRAND_PYTHON_TYPE_H
#define BERTRAND_PYTHON_TYPE_H

#include "common.h"
#include "datetime.h"
#include "tuple.h"
#include "set.h"
#include "dict.h"


namespace bertrand {
namespace py {


/* Wrapper around a pybind11::type that enables extra C API functionality, such as the
ability to create new types on the fly by calling the type() metaclass, or directly
querying PyTypeObject* fields. */
class Type : public Object, public impl::Ops<Type> {

    static PyObject* convert_to_type(PyObject* obj) {
        return Py_NewRef(reinterpret_cast<PyObject*>(Py_TYPE(obj)));
    }

public:
    BERTRAND_PYTHON_CONSTRUCTORS(Object, Type, PyType_Check, convert_to_type);

    /* Default constructor.  Initializes to the built-in type metaclass. */
    inline Type() : Object((PyObject*) &PyType_Type, borrowed_t{}) {}

    /* Dynamically create a new Python type by calling the type() metaclass. */
    template <typename T, typename U, typename V>
    explicit Type(T&& name, U&& bases, V&& dict) {
        m_ptr = PyObject_CallFunctionObjArgs(
            reinterpret_cast<PyObject*>(&PyType_Type),
            detail::object_or_cast(std::forward<T>(name)).ptr(),
            detail::object_or_cast(std::forward<U>(bases)).ptr(),
            detail::object_or_cast(std::forward<V>(dict)).ptr(),
            nullptr
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Create a new heap type from a CPython PyType_Spec*.  Note that this is not
    exactly interchangeable with a standard call to the type metaclass directly, as it
    does not invoke any of the __init__(), __new__(), __init_subclass__(), or
    __set_name__() methods for the type or any of its bases. */
    explicit Type(PyType_Spec* spec) : Object(PyType_FromSpec(spec), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Create a new heap type from a CPython PyType_Spec and bases.  See
    Type(PyType_Spec*) for more information. */
    template <typename T>
    explicit Type(PyType_Spec* spec, T&& bases) {
        m_ptr = PyType_FromSpecWithBases(spec, bases);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

        /* Create a new heap type from a module name, CPython PyType_Spec, and bases.
        See Type(PyType_Spec*) for more information. */
        template <typename T, typename U>
        explicit Type(T&& module, PyType_Spec* spec, U&& bases) {
            m_ptr = PyType_FromModuleAndSpec(
                detail::object_or_cast(std::forward<T>(module)).ptr(),
                spec,
                detail::object_or_cast(std::forward<U>(bases)).ptr()
            );
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
        }

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Create a new heap type from a full CPython metaclass, module name,
        PyType_Spec and bases.  See Type(PyType_Spec*) for more information. */
        template <typename T, typename U, typename V>
        explicit Type(T&& metaclass, U&& module, PyType_Spec* spec, V&& bases) {
            m_ptr = PyType_FromMetaClass(
                detail::object_or_cast(std::forward<T>(metaclass)).ptr(),
                detail::object_or_cast(std::forward<U>(module)).ptr(),
                spec,
                detail::object_or_cast(std::forward<V>(bases)).ptr()
            );
            if (m_ptr == nullptr) {
                throw error_already_set();
            }
        }

    #endif

    ///////////////////////////
    ////    PyType_ API    ////
    ///////////////////////////

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

    /* Finalize a type object in a C++ extension, filling in any inherited slots.  This
    should be called on a raw CPython type struct to finish its initialization. */
    inline static void READY(PyTypeObject* type) {
        if (PyType_Ready(type) < 0) {
            throw error_already_set();
        }
    }

    /* Clear the lookup cache for the type and all of its subtypes.  This method must
    be called after any manual modification to the attributes or this class or any of
    its bases. */
    inline void clear_cache() const noexcept {
        PyType_Modified(reinterpret_cast<PyTypeObject*>(this->ptr()));
    }

    /* Check whether this type is an actual subtype of a another type.  This avoids
    calling `__subclasscheck__()` on the parent type. */
    inline bool is_subtype(const pybind11::type& base) const {
        return PyType_IsSubtype(
            reinterpret_cast<PyTypeObject*>(this->ptr()),
            reinterpret_cast<PyTypeObject*>(base.ptr())
        );
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using impl::Ops<Type>::operator==;
    using impl::Ops<Type>::operator!=;

    // NOTE: indexing a Type object calls its __class_getitem__() method, just like
    // normal python.
};


/* New subclass of pybind11::object that represents Python's built-in super() type. */
class Super : public Object, public impl::Ops<Super> {

    inline static int check_super(PyObject* obj) {
        int result = PyObject_IsInstance(obj, reinterpret_cast<PyObject*>(&PySuper_Type));
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_super(PyObject* obj) {
        throw TypeError("cannot convert to py::Super");
    }

public:
    static py::Type Type;
    BERTRAND_PYTHON_CONSTRUCTORS(Object, Super, check_super, convert_super);

    /* Default constructor.  Equivalent to Python `super()` with no arguments, which
    uses the calling context's inheritance hierarchy. */
    Super() {
        m_ptr = PyObject_CallNoArgs(reinterpret_cast<PyObject*>(&PySuper_Type));
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicit constructor.  Equivalent to Python `super(type, self)` with 2
    arguments. */
    explicit Super(const pybind11::type& type, const pybind11::handle& self) {
        m_ptr = PyObject_CallFunctionObjArgs(
            reinterpret_cast<PyObject*>(&PySuper_Type),
            type.ptr(),
            self.ptr(),
            nullptr
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicit constructor.  Equivalent to Python `super(type, self)` with 2
    arguments. */
    explicit Super(const py::Type& type, const pybind11::handle& self) {
        m_ptr = PyObject_CallFunctionObjArgs(
            reinterpret_cast<PyObject*>(&PySuper_Type),
            type.ptr(),
            self.ptr(),
            nullptr
        );
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    BERTRAND_PYTHON_OPERATORS(impl::Ops<Super>)
};


// TODO: Regex and Decimal types?


/* Every Python type has a static `Type` member that gives access to the Python type
object associated with instances of that class. */
Type Object::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyBaseObject_Type));
Type Bool::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyBool_Type));
Type Int::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyLong_Type));
Type Float::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyFloat_Type));
Type Complex::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyComplex_Type));
Type Slice::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PySlice_Type));
Type Range::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyRange_Type));
Type List::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyList_Type));
Type Tuple::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyTuple_Type));
Type Set::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PySet_Type));
Type FrozenSet::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyFrozenSet_Type));
Type Dict::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyDict_Type));
Type MappingProxy::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyDictProxy_Type));
Type KeysView::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyDictKeys_Type));
Type ValuesView::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyDictValues_Type));
Type ItemsView::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyDictItems_Type));
Type Str::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyUnicode_Type));
Type Code::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyCode_Type));
Type Frame::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyFrame_Type));
Type Function::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyFunction_Type));
Type Method::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyInstanceMethod_Type));
Type ClassMethod::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyClassMethodDescr_Type));
Type StaticMethod::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyStaticMethod_Type));
Type Property::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyProperty_Type));
Type Timedelta::Type = [] {
    if (impl::DATETIME_IMPORTED) {
        return reinterpret_borrow<py::Type>(impl::PyDelta_Type.ptr());
    } else {
        return py::Type();
    }
}();
Type Timezone::Type = [] {
    if (impl::DATETIME_IMPORTED) {
        return reinterpret_borrow<py::Type>(impl::PyTZInfo_Type.ptr());
    } else {
        return py::Type();
    }
}();
Type Date::Type = [] {
    if (impl::DATETIME_IMPORTED) {
        return reinterpret_borrow<py::Type>(impl::PyDate_Type.ptr());
    } else {
        return py::Type();
    }
}();
Type Time::Type = [] {
    if (impl::DATETIME_IMPORTED) {
        return reinterpret_borrow<py::Type>(impl::PyTime_Type.ptr());
    } else {
        return py::Type();
    }
}();
Type Datetime::Type = [] {
    if (impl::DATETIME_IMPORTED) {
        return reinterpret_borrow<py::Type>(impl::PyDateTime_Type.ptr());
    } else {
        return py::Type();
    }
}();


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Type)


#endif  // BERTRAND_PYTHON_TYPE_H
