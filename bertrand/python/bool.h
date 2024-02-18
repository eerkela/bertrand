#ifndef BERTRAND_PYTHON_BOOL_H
#define BERTRAND_PYTHON_BOOL_H

#include "common.h"


namespace bertrand {
namespace py {


/* Wrapper around pybind11::bool_ that enables math operations with C++ inputs. */
class Bool :
    public pybind11::bool_,
    public impl::NumericOps<Bool>,
    public impl::FullCompare<Bool>
{
    using Base = pybind11::bool_;
    using Ops = impl::NumericOps<Bool>;
    using Compare = impl::FullCompare<Bool>;

public:
    using Base::Base;
    using Base::operator=;

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


#endif  // BERTRAND_PYTHON_BOOL_H
