#ifndef BERTRAND_PYTHON_INT_H
#define BERTRAND_PYTHON_INT_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::int_ that enables conversions from strings with different
bases, similar to Python's `int()` constructor, as well as converting math operators
that account for C++ inputs. */
class Int :
    public pybind11::int_,
    public impl::NumericOps<Int>,
    public impl::FullCompare<Int>
{
    using Base = pybind11::int_;
    using Ops = impl::NumericOps<Int>;
    using Compare = impl::FullCompare<Int>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct an Int from a string with an optional base. */
    explicit Int(const pybind11::str& str, int base = 0) : Base([&str, &base] {
        PyObject* result = PyLong_FromUnicodeObject(str.ptr(), base);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct an Int from a string with an optional base. */
    explicit Int(const char* str, int base = 0) : Base([&str, &base] {
        PyObject* result = PyLong_FromString(str, nullptr, base);
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Construct an Int from a string with an optional base. */
    explicit Int(const std::string& str, int base = 0) :
        Int(str.c_str(), base)
    {}

    /* Construct an Int from a string with an optional base. */
    explicit Int(const std::string_view& str, int base = 0) :
        Int(str.data(), base)
    {}

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator<;
    using Compare::operator<=;
    using Compare::operator==;
    using Compare::operator!=;
    using Compare::operator>;
    using Compare::operator>=;

    using Ops::operator+;
    using Ops::operator-;
    using Ops::operator*;
    using Ops::operator/;
    using Ops::operator%;
    using Ops::operator<<;
    using Ops::operator>>;
    using Ops::operator&;
    using Ops::operator|;
    using Ops::operator^;
    using Ops::operator+=;
    using Ops::operator-=;
    using Ops::operator*=;
    using Ops::operator/=;
    using Ops::operator%=;
    using Ops::operator<<=;
    using Ops::operator>>=;
    using Ops::operator&=;
    using Ops::operator|=;
    using Ops::operator^=;
};


}  // namespace python
}  // namespace bertrand


BERTRAND_STD_HASH(bertrand::py::Int)


#endif  // BERTRAND_PYTHON_INT_H
