module;

#include "__init__.h"

export module bertrand.python;


export namespace bertrand::py {
    using bertrand::py::Interpreter;
    using bertrand::py::Arg;
    using bertrand::py::arg;
    using bertrand::py::Handle;
    using bertrand::py::WeakRef;
    using bertrand::py::Capsule;
    using bertrand::py::Buffer;
    using bertrand::py::MemoryView;
    using bertrand::py::Object;
    using bertrand::py::Function;
    using bertrand::py::Type;
    using bertrand::py::Super;
    using bertrand::py::Code;
    using bertrand::py::Frame;
    using bertrand::py::Module;
    using bertrand::py::NoneType;
    using bertrand::py::NotImplementedType;
    using bertrand::py::EllipsisType;
    using bertrand::py::Bool;
    using bertrand::py::Int;
    using bertrand::py::Float;
    using bertrand::py::Complex;
    using bertrand::py::Str;
    using bertrand::py::Bytes;
    using bertrand::py::ByteArray;
    using bertrand::py::Timezone;
    using bertrand::py::Date;
    using bertrand::py::Time;
    using bertrand::py::Datetime;
    using bertrand::py::Timedelta;
    using bertrand::py::Slice;
    using bertrand::py::Range;
    using bertrand::py::List;
    using bertrand::py::Tuple;
    using bertrand::py::Set;
    using bertrand::py::FrozenSet;
    using bertrand::py::Dict;
    using bertrand::py::KeyView;
    using bertrand::py::ValueView;
    using bertrand::py::ItemView;
    using bertrand::py::MappingProxy;

    using bertrand::py::Exception;
    using bertrand::py::ArithmeticError;
    using bertrand::py::FloatingPointError;
    using bertrand::py::OverflowError;
    using bertrand::py::ZeroDivisionError;
    using bertrand::py::AssertionError;
    using bertrand::py::AttributeError;
    using bertrand::py::BufferError;
    using bertrand::py::EOFError;
    using bertrand::py::ImportError;
    using bertrand::py::ModuleNotFoundError;
    using bertrand::py::LookupError;
    using bertrand::py::IndexError;
    using bertrand::py::KeyError;
    using bertrand::py::MemoryError;
    using bertrand::py::NameError;
    using bertrand::py::UnboundLocalError;
    using bertrand::py::OSError;
    using bertrand::py::BlockingIOError;
    using bertrand::py::ChildProcessError;
    using bertrand::py::ConnectionError;
    using bertrand::py::BrokenPipeError;
    using bertrand::py::ConnectionAbortedError;
    using bertrand::py::ConnectionRefusedError;
    using bertrand::py::ConnectionResetError;
    using bertrand::py::FileExistsError;
    using bertrand::py::FileNotFoundError;
    using bertrand::py::InterruptedError;
    using bertrand::py::IsADirectoryError;
    using bertrand::py::NotADirectoryError;
    using bertrand::py::PermissionError;
    using bertrand::py::ProcessLookupError;
    using bertrand::py::TimeoutError;
    using bertrand::py::ReferenceError;
    using bertrand::py::RuntimeError;
    using bertrand::py::NotImplementedError;
    using bertrand::py::RecursionError;
    using bertrand::py::StopAsyncIteration;
    using bertrand::py::StopIteration;
    using bertrand::py::SyntaxError;
    using bertrand::py::IndentationError;
    using bertrand::py::TabError;
    using bertrand::py::SystemError;
    using bertrand::py::TypeError;
    using bertrand::py::CastError;
    using bertrand::py::ReferenceCastError;
    using bertrand::py::ValueError;
    using bertrand::py::UnicodeError;
    using bertrand::py::UnicodeDecodeError;
    using bertrand::py::UnicodeEncodeError;
    using bertrand::py::UnicodeTranslateError;

    using bertrand::py::Disable;
    using bertrand::py::Returns;
    using bertrand::py::__as_object__;
    using bertrand::py::__isinstance__;
    using bertrand::py::__issubclass__;
    using bertrand::py::__init__;
    using bertrand::py::__explicit_init__;
    using bertrand::py::__cast__;
    using bertrand::py::__explicit_cast__;
    using bertrand::py::__call__;
    using bertrand::py::__getattr__;
    using bertrand::py::__setattr__;
    using bertrand::py::__delattr__;
    using bertrand::py::__getitem__;
    using bertrand::py::__setitem__;
    using bertrand::py::__delitem__;
    using bertrand::py::__len__;
    using bertrand::py::__iter__;
    using bertrand::py::__reversed__;
    using bertrand::py::__contains__;
    using bertrand::py::__hash__;
    using bertrand::py::__abs__;
    using bertrand::py::__invert__;
    using bertrand::py::__pos__;
    using bertrand::py::__neg__;
    using bertrand::py::__increment__;
    using bertrand::py::__decrement__;
    using bertrand::py::__lt__;
    using bertrand::py::__le__;
    using bertrand::py::__eq__;
    using bertrand::py::__ne__;
    using bertrand::py::__ge__;
    using bertrand::py::__gt__;
    using bertrand::py::__add__;
    using bertrand::py::__iadd__;
    using bertrand::py::__sub__;
    using bertrand::py::__isub__;
    using bertrand::py::__mul__;
    using bertrand::py::__imul__;
    using bertrand::py::__truediv__;
    using bertrand::py::__itruediv__;
    using bertrand::py::__floordiv__;
    using bertrand::py::__ifloordiv__;
    using bertrand::py::__mod__;
    using bertrand::py::__imod__;
    using bertrand::py::__pow__;
    using bertrand::py::__ipow__;
    using bertrand::py::__lshift__;
    using bertrand::py::__ilshift__;
    using bertrand::py::__rshift__;
    using bertrand::py::__irshift__;
    using bertrand::py::__and__;
    using bertrand::py::__iand__;
    using bertrand::py::__or__;
    using bertrand::py::__ior__;
    using bertrand::py::__xor__;
    using bertrand::py::__ixor__;

    using bertrand::py::reinterpret_borrow;
    using bertrand::py::reinterpret_steal;
    using bertrand::py::as_object;
    using bertrand::py::isinstance;
    using bertrand::py::issubclass;
    using bertrand::py::hasattr;
    using bertrand::py::getattr;
    using bertrand::py::setattr;
    using bertrand::py::delattr;
    using bertrand::py::print;
    using bertrand::py::repr;
    using bertrand::py::hash;
    using bertrand::py::len;
    using bertrand::py::size;
    using bertrand::py::iter;
    using bertrand::py::begin;
    using bertrand::py::cbegin;
    using bertrand::py::end;
    using bertrand::py::cend;
    using bertrand::py::reversed;
    using bertrand::py::rbegin;
    using bertrand::py::crbegin;
    using bertrand::py::rend;
    using bertrand::py::crend;
    using bertrand::py::abs;
    using bertrand::py::pow;
    using bertrand::py::Round;
    using bertrand::py::div;
    using bertrand::py::mod;
    using bertrand::py::divmod;
    using bertrand::py::round;
    using bertrand::py::assert_;
    using bertrand::py::visit;
    using bertrand::py::transform;
    using bertrand::py::callable;
    using bertrand::py::all;
    using bertrand::py::any;
    using bertrand::py::enumerate;
    using bertrand::py::filter;
    using bertrand::py::map;
    using bertrand::py::max;
    using bertrand::py::min;
    using bertrand::py::next;
    using bertrand::py::sum;
    using bertrand::py::zip;
    using bertrand::py::builtins;
    using bertrand::py::globals;
    using bertrand::py::locals;
    using bertrand::py::aiter;
    using bertrand::py::anext;
    using bertrand::py::ascii;
    using bertrand::py::bin;
    using bertrand::py::chr;
    using bertrand::py::dir;
    using bertrand::py::eval;
    using bertrand::py::exec;
    using bertrand::py::hex;
    using bertrand::py::id;
    using bertrand::py::oct;
    using bertrand::py::ord;
    using bertrand::py::vars;

    using bertrand::py::operator~;
    using bertrand::py::operator<;
    using bertrand::py::operator<=;
    using bertrand::py::operator==;
    using bertrand::py::operator!=;
    using bertrand::py::operator>=;
    using bertrand::py::operator>;
    using bertrand::py::operator+;
    using bertrand::py::operator-;
    using bertrand::py::operator++;
    using bertrand::py::operator--;
    using bertrand::py::operator+=;
    using bertrand::py::operator-=;
    using bertrand::py::operator*;
    using bertrand::py::operator*=;
    using bertrand::py::operator/;
    using bertrand::py::operator/=;
    using bertrand::py::operator%;
    using bertrand::py::operator%=;
    using bertrand::py::operator<<;
    using bertrand::py::operator<<=;
    using bertrand::py::operator>>;
    using bertrand::py::operator>>=;
    using bertrand::py::operator&;
    using bertrand::py::operator&=;
    using bertrand::py::operator|;
    using bertrand::py::operator|=;
    using bertrand::py::operator^;
    using bertrand::py::operator^=;

    using bertrand::py::True;
    using bertrand::py::False;
    using bertrand::py::None;
    using bertrand::py::NotImplemented;
    using bertrand::py::Ellipsis;
}


export namespace std {
    using std::hash;
    using std::equal_to;
}


export namespace pybind11::detail {
    using pybind11::detail::type_caster;
}
