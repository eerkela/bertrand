#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_BOOL_H
#define BERTRAND_PYTHON_BOOL_H

#include "common.h"


namespace bertrand {
namespace py {


/* Represents a statically-typed Python boolean in C++. */
class Bool : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    [[nodiscard]] static consteval bool typecheck() {
        return impl::bool_like<T>;
    }

    template <typename T>
    [[nodiscard]] static constexpr bool typecheck(const T& obj) {
        if (impl::cpp_like<T>) {
            return typecheck<T>();
        } else if constexpr (typecheck<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyBool_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Bool(Handle h, const borrowed_t& t) : Base(h, t) {}
    Bool(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (typecheck<T>())
    Bool(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Bool(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Bool>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to False. */
    Bool() : Base(Py_False, borrowed_t{}) {}

    /* Implicitly convert C++ booleans into py::Bool. */
    template <impl::cpp_like T> requires (impl::bool_like<T>)
    Bool(const T& value) : Base(value ? Py_True : Py_False, borrowed_t{}) {}

    /* Explicitly convert an arbitrary Python object into a boolean. */
    template <impl::python_like T> requires (!impl::bool_like<T>)
    explicit Bool(const T& obj) : Base(nullptr, stolen_t{}) {
        int result = PyObject_IsTrue(obj.ptr());
        if (result == -1) {
            Exception::from_python();
        }
        m_ptr = Py_NewRef(result ? Py_True : Py_False);
    }

    /* Trigger explicit conversion operators to bool. */
    template <impl::cpp_like T>
        requires (!impl::bool_like<T> && impl::explicitly_convertible_to<T, bool>)
    explicit Bool(const T& value) : Bool(static_cast<bool>(value)) {}

    /* Explicitly convert any C++ object that implements a `.size()` method into a
    py::Bool. */
    template <impl::cpp_like T>
        requires (
            !impl::bool_like<T> &&
            !impl::explicitly_convertible_to<T, bool> &&
            impl::has_size<T>
        )
    explicit Bool(const T& obj) : Bool(std::size(obj) > 0) {}

    /* Explicitly convert any C++ object that implements a `.empty()` method into a
    py::Bool. */
    template <impl::cpp_like T>
        requires (
            !impl::bool_like<T> &&
            !impl::explicitly_convertible_to<T, bool> &&
            !impl::has_size<T> &&
            impl::has_empty<T>
        )
    explicit Bool(const T& obj) : Bool(!obj.empty()) {}

    /* Explicitly convert a std::tuple into a py::Bool. */
    template <typename... Args>
    explicit Bool(const std::tuple<Args...>& obj) : Bool(sizeof...(Args) > 0) {}

    /* Explicitly convert a string literal into a py::Bool. */
    template <size_t N>
    explicit Bool(const char(&string)[N]) : Bool(N > 1) {}

    /* Explicitly convert a C string into a py::Bool. */
    template <std::same_as<const char*> T>
    explicit Bool(T str) : Bool(std::strcmp(str, "") != 0) {}

};


static const Bool True = reinterpret_borrow<Bool>(Py_True);
static const Bool False = reinterpret_borrow<Bool>(Py_False);


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_BOOL_H
