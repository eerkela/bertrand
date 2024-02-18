#ifndef BERTRAND_PYTHON_FUNC_H
#define BERTRAND_PYTHON_FUNC_H

#include "common.h"


namespace bertrand {
namespace python {


namespace impl {

    // PyTypeObject* PyMethod_Type = nullptr;  // provided in Python.h
    PyTypeObject* PyClassMethod_Type = nullptr;
    PyTypeObject* PyStaticMethod_Type = nullptr;
    // PyTypeObject* PyProperty_Type = nullptr;  // provided in Python.h?

} // namespace impl


// TODO: class Code?




/* Wrapper around a pybind11::Function that allows it to be constructed from a C++
lambda or function pointer, and enables extra introspection via the C API. */
class Function : public pybind11::function {
    using Base = pybind11::function;

public:
    using Base::Base;
    using Base::operator=;

    template <typename Func>
    explicit Function(Func&& func) : Base([&func] {
        return pybind11::cpp_function(std::forward<Func>(func));
    }()) {}

    ///////////////////////////////
    ////    PyFunction_ API    ////
    ///////////////////////////////

    // TODO: introspection tools: name(), code(), etc.

    // /* Get the name of the file from which the code was compiled. */
    // inline std::string filename() const {
    //     return code().filename();
    // }


    // /* Get the first line number of the function. */
    // inline size_t line_number() const noexcept {
    //     return code().line_number();
    // }

    // /* Get the function's base name. */
    // inline std::string name() const {
    //     return code().name();
    // }


    // /* Get the module that the function is defined in. */
    // inline std::optional<Module<Ref::BORROW>> module_() const {
    //     PyObject* mod = PyFunction_GetModule(this->obj);
    //     if (mod == nullptr) {
    //         return std::nullopt;
    //     } else {
    //         return std::make_optional(Module<Ref::BORROW>(module));
    //     }
    // }



};


/* New subclass of pybind11::object that represents a bound method at the Python
level. */
class Method : public pybind11::object {
    using Base = pybind11::object;

public:



};


/* New subclass of pybind11::object that represents a bound classmethod at the Python
level. */
class ClassMethod : public pybind11::object {
    using Base = pybind11::object;

public:



};


/* Wrapper around a pybind11::StaticMethod that allows it to be constructed from a
C++ lambda or function pointer, and enables extra introspection via the C API. */
class StaticMethod : public pybind11::staticmethod {
    using Base = pybind11::staticmethod;

public:



};


/* New subclass of pybind11::object that represents a property descriptor at the
Python level. */
class Property : public pybind11::object {
    using Base = pybind11::object;

public:



};


/* Equivalent to Python `callable(obj)`. */
inline bool callable(const pybind11::handle& obj) {
    return PyCallable_Check(obj.ptr());
}


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_FUNC_H
