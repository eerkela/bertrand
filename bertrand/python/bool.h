#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_BOOL_H
#define BERTRAND_PYTHON_BOOL_H

#include "common.h"


namespace bertrand {
namespace py {


/* pybind11::bool_ equivalent with stronger type safety, math operators, etc. */
class Bool : public impl::Ops {
    using Base = impl::Ops;

    template <typename T>
    static constexpr bool constructor1 = !impl::is_python<T> && impl::is_bool_like<T>;
    template <typename T>
    static constexpr bool constructor2 = 
        !impl::is_python<T> && !impl::is_bool_like<T> &&
        impl::explicitly_convertible_to<T, bool>;
    template <typename T>
    static constexpr bool constructor3 = impl::is_python<T> && !impl::is_bool_like<T>;
    template <typename T>
    static constexpr bool constructor4 =
        !constructor1<T> && !constructor2<T> && !constructor3<T> && impl::has_empty<T>;
    template <typename T>
    static constexpr bool constructor5 =
        !constructor1<T> && !constructor2<T> && !constructor3<T> && !constructor4<T> &&
        impl::has_size<T>;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::is_bool_like<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Copy/move constructors from equivalent pybind11 type. */
    Bool(const pybind11::bool_& other) : Base(other.ptr(), borrowed_t{}) {}
    Bool(pybind11::bool_&& other) : Base(other.release(), stolen_t{}) {}

    BERTRAND_OBJECT_CONSTRUCTORS(Base, Bool, PyBool_Check)

    /* Default constructor.  Initializes to False. */
    Bool() : Base(Py_False, borrowed_t{}) {}

    /* Implicitly convert C++ booleans into py::Bool. */
    template <typename T, std::enable_if_t<constructor1<T>, int> = 0>
    Bool(const T& value) : Base(value ? Py_True : Py_False, borrowed_t{}) {}

    /* Trigger explicit conversion operators to bool. */
    template <typename T, std::enable_if_t<constructor2<T>, int> = 0>
    explicit Bool(const T& value) : Bool(static_cast<bool>(value)) {}

    /* Explicitly convert an arbitrary Python object into a boolean. */
    template <typename T, std::enable_if_t<constructor3<T>, int> = 0>
    explicit Bool(const T& obj) {
        int result = PyObject_IsTrue(obj.ptr());
        if (result == -1) {
            throw error_already_set();
        }
        m_ptr = Py_NewRef(result ? Py_True : Py_False);
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

    /* Explicitly convert any C++ object that implements a `.empty()` method into a
    py::Bool. */
    template <typename T, std::enable_if_t<constructor4<T>, int> = 0>
    explicit Bool(const T& obj) : Bool(!obj.empty()) {}

    /* Explicitly convert any C++ object that implements a `.size()` method into a
    py::Bool. */
    template <typename T, std::enable_if_t<constructor5<T>, int> = 0>
    explicit Bool(const T& obj) : Bool(obj.size() > 0) {}

    /* Explicitly convert a std::tuple into a py::Bool. */
    template <typename... Args>
    explicit Bool(const std::tuple<Args...>& obj) : Bool(sizeof...(Args) > 0) {}

    /* Explicitly convert a string literal into a py::Bool. */
    explicit Bool(const char* str) : Bool(std::string(str)) {}

    ///////////////////////////
    ////    CONVERSIONS    ////
    ///////////////////////////

    /* Implicitly convert a py::Bool into a C++ boolean. */
    inline operator bool() const {
        return Base::operator bool();
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    auto begin() const = delete;
    auto end() const = delete;

    DELETE_OPERATOR(operator[])
    DELETE_OPERATOR(operator())

    using Base::operator<;
    using Base::operator<=;
    using Base::operator==;
    using Base::operator!=;
    using Base::operator>=;
    using Base::operator>;

    DECLARE_TYPED_UNARY_OPERATOR(Bool, operator~, Int)

    DECLARE_TYPED_BINARY_OPERATOR(Bool, operator+, is_bool_like, Int)
    DECLARE_TYPED_BINARY_OPERATOR(Bool, operator+, is_int_like, Int)
    DECLARE_TYPED_BINARY_OPERATOR(Bool, operator+, is_float_like, Float)
    DECLARE_TYPED_BINARY_OPERATOR(Bool, operator+, is_complex_like, Complex)

    // using Base::operator+;

    using Base::operator-;
    using Base::operator*;
    using Base::operator/;
    using Base::operator%;
    using Base::operator<<;
    using Base::operator>>;
    using Base::operator&;
    using Base::operator|;
    using Base::operator^;

    DELETE_OPERATOR(operator+=)
    DELETE_OPERATOR(operator-=)
    DELETE_OPERATOR(operator*=)
    DELETE_OPERATOR(operator/=)
    DELETE_OPERATOR(operator%=)
    DELETE_OPERATOR(operator<<=)
    DELETE_OPERATOR(operator>>=)

    template <typename T, std::enable_if_t<impl::is_bool_like<T>, int> = 0>
    inline Bool& operator&=(const T& other) {
        Base::operator&=(other);
        return *this;
    }

    template <typename T, std::enable_if_t<impl::is_bool_like<T>, int> = 0>
    inline Bool& operator|=(const T& other) {
        Base::operator|=(other);
        return *this;
    }

    template <typename T, std::enable_if_t<impl::is_bool_like<T>, int> = 0>
    inline Bool& operator^=(const T& other) {
        Base::operator^=(other);
        return *this;
    }

};



// TODO: it's probably a good idea to specifically overload the math operators to
// return specific types (e.g. Int, Float, etc.) instead of generic Objects.  This
// should cut down on the number of implicit conversions that need to be performed to
// make things type safe.  It will also promote more errors to compile time.




}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Bool)


#endif  // BERTRAND_PYTHON_BOOL_H
