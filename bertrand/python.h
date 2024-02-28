#ifndef BERTRAND_PYTHON_H
#define BERTRAND_PYTHON_H
#define BERTRAND_PYTHON_INCLUDED

#include "python/common.h"

#include "python/bool.h"
#include "python/int.h"
#include "python/float.h"
#include "python/complex.h"
#include "python/slice.h"
#include "python/range.h"
#include "python/list.h"
#include "python/tuple.h"
#include "python/set.h"
#include "python/dict.h"
#include "python/str.h"
#include "python/func.h"
#include "python/datetime.h"
#include "python/math.h"
#include "python/type.h"


namespace bertrand {
namespace py {


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


/* Some attributes are forward declared to avoid circular dependencies.
 *
 * Since this file is the only valid entry point for the python ecosystem, it makes
 * sense to define them here, with full context.  This avoids complicated include paths
 * and decouples some of the types from one another.  It also relaxes some of the
 * strictness around inclusion order, and generally promotes code organization.
 */


// TODO: Regex and Decimal types?


/* Every Python type has a static `Type` member that gives access to the Python type
object associated with instances of that class. */
inline Type Object::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyBaseObject_Type));
inline Type Module::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyModule_Type));
inline Type Bool::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyBool_Type));
inline Type Int::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyLong_Type));
inline Type Float::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyFloat_Type));
inline Type Complex::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyComplex_Type));
inline Type Slice::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PySlice_Type));
inline Type Range::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyRange_Type));
inline Type List::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyList_Type));
inline Type Tuple::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyTuple_Type));
inline Type Set::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PySet_Type));
inline Type FrozenSet::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyFrozenSet_Type));
inline Type Dict::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyDict_Type));
inline Type MappingProxy::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyDictProxy_Type));
inline Type KeysView::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyDictKeys_Type));
inline Type ValuesView::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyDictValues_Type));
inline Type ItemsView::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyDictItems_Type));
inline Type Str::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyUnicode_Type));
inline Type Code::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyCode_Type));
inline Type Frame::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyFrame_Type));
inline Type Function::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyFunction_Type));
inline Type Method::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyInstanceMethod_Type));
inline Type ClassMethod::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyClassMethodDescr_Type));
inline Type StaticMethod::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyStaticMethod_Type));
inline Type Property::Type = reinterpret_borrow<py::Type>(reinterpret_cast<PyObject*>(&PyProperty_Type));
inline Type Timedelta::Type = [] {
    if (impl::DATETIME_IMPORTED) {
        return reinterpret_borrow<py::Type>(impl::PyDelta_Type.ptr());
    } else {
        return py::Type();
    }
}();
inline Type Timezone::Type = [] {
    if (impl::DATETIME_IMPORTED) {
        return reinterpret_borrow<py::Type>(impl::PyTZInfo_Type.ptr());
    } else {
        return py::Type();
    }
}();
inline Type Date::Type = [] {
    if (impl::DATETIME_IMPORTED) {
        return reinterpret_borrow<py::Type>(impl::PyDate_Type.ptr());
    } else {
        return py::Type();
    }
}();
inline Type Time::Type = [] {
    if (impl::DATETIME_IMPORTED) {
        return reinterpret_borrow<py::Type>(impl::PyTime_Type.ptr());
    } else {
        return py::Type();
    }
}();
inline Type Datetime::Type = [] {
    if (impl::DATETIME_IMPORTED) {
        return reinterpret_borrow<py::Type>(impl::PyDateTime_Type.ptr());
    } else {
        return py::Type();
    }
}();


template <
    typename T,
    std::enable_if_t<impl::is_str_like<T> && impl::is_object<T>, int> = 0
>
inline Int::Int(const T& str, int base) {
    m_ptr = PyLong_FromUnicodeObject(str.ptr(), base);
    if (m_ptr == nullptr) {
        throw error_already_set();
    }
}


template <
    typename T,
    std::enable_if_t<impl::is_str_like<T> && impl::is_object<T>, int> = 0
>
inline Float::Float(const T& str) {
    m_ptr = PyFloat_FromString(str.ptr());
    if (m_ptr == nullptr) {
        throw error_already_set();
    }
}


// template <typename T, std::enable_if_t<impl::is_str_like<T>, int> = 0>
// inline Complex::Complex(const T& value) : Object(Complex::Type(Str(value))) {
//     if (m_ptr == nullptr) {
//         throw error_already_set();
//     }
// }


// inline void List::sort(const Function& key, const Bool& reverse) {
//     this->attr("sort")(py::arg("key") = key, py::arg("reverse") = reverse);
// }


// inline Type::Type(const Str& name, const Tuple& bases, const Dict& dict) {
//     m_ptr = PyObject_CallFunctionObjArgs(
//         reinterpret_cast<PyObject*>(&PyType_Type),
//         name.ptr(),
//         bases.ptr(),
//         dict.ptr(),
//         nullptr
//     );
//     if (m_ptr == nullptr) {
//         throw error_already_set();
//     }
// }






/* Equivalent to Python `dir()` with no arguments.  Returns a list of names in the
current local scope. */
inline List dir() {
    PyObject* result = PyObject_Dir(nullptr);
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<List>(result);
}


/* Equivalent to Python `dir(obj)`. */
inline List dir(const pybind11::handle& obj) {
    if (obj.ptr() == nullptr) {
        throw TypeError("cannot call dir() on a null object");
    }
    PyObject* result = PyObject_Dir(obj.ptr());
    if (result == nullptr) {
        throw error_already_set();
    }
    return reinterpret_steal<List>(result);
}


/* Equivalent to Python `sorted(obj)`. */
inline List sorted(const pybind11::handle& obj) {
    PyObject* endpoint = PyDict_GetItemString(PyEval_GetBuiltins(), "sorted");
    if (endpoint == nullptr) {
        throw error_already_set();
    }
    Object call = reinterpret_steal<Object>(endpoint);
    return call(obj);
}


/* Equivalent to Python `sorted(obj, key=key, reverse=reverse)`. */
template <typename Func>
inline List sorted(
    const pybind11::handle& obj,
    const Function& key,
    bool reverse = false
) {
    PyObject* endpoint = PyDict_GetItemString(PyEval_GetBuiltins(), "sorted");
    if (endpoint == nullptr) {
        throw error_already_set();
    }
    Object call = reinterpret_steal<Object>(endpoint);
    return call(obj, py::arg("key") = key, py::arg("reverse") = reverse);
}


// TODO: sorted() for C++ types


}  // namespace py
}  // namespace bertrand



#undef BERTRAND_PYTHON_CONSTRUCTORS
#undef BERTRAND_STD_HASH
#undef BERTRAND_STD_EQUAL_TO
#undef BERTRAND_PYTHON_INCLUDED
#endif  // BERTRAND_PYTHON_H