#include "../common.h"
#include <bertrand/python.h>
#include <cstdint>

namespace py = bertrand::py;


/////////////////////
////    TYPES    ////
/////////////////////


TEST(py_int, type) {
    py::Type expected = py::reinterpret_borrow<py::Type>((PyObject*) &PyLong_Type);
    EXPECT_EQ(py::Int::type, expected);
}


TEST(py_int, check) {
    EXPECT_EQ(py::Int::check<int>(), true);
    EXPECT_EQ(py::Int::check<unsigned int>(), true);
    EXPECT_EQ(py::Int::check<py::Int>(), true);
    EXPECT_EQ(py::Int::check<pybind11::int_>(), true);

    EXPECT_EQ(py::Int::check<bool>(), false);
    EXPECT_EQ(py::Int::check<double>(), false);
    EXPECT_EQ(py::Int::check<std::string>(), false);
    EXPECT_EQ(py::Int::check<py::Bool>(), false);
    EXPECT_EQ(py::Int::check<py::Float>(), false);
    EXPECT_EQ(py::Int::check<py::Str>(), false);
}


/////////////////////////////////////
////    IMPLICIT CONSTRUCTORS    ////
/////////////////////////////////////


TEST(py_int, default_construct) {
    py::Int a;
    EXPECT_EQ(a, 0);
}


TEST(py_int, from_object) {
    // safe
    py::Object a = py::Bool(true);
    py::Object b = py::Int(0);
    py::Object c = py::Int(1);
    py::Int a2 = a;
    py::Int b2 = b;
    py::Int c2 = c;
    EXPECT_EQ(a2, 1);
    EXPECT_EQ(b2, 0);
    EXPECT_EQ(c2, 1);

    // not safe
    py::Object d = py::Float(1.0);
    py::Object e = py::Str("1");
    py::Object f = py::Tuple{};
    py::Object g = py::List{};
    py::Object h = py::Set{};
    py::Object i = py::Dict{};
    EXPECT_THROW(py::Int d2 = d, py::TypeError);
    EXPECT_THROW(py::Int e2 = e, py::TypeError);
    EXPECT_THROW(py::Int f2 = f, py::TypeError);
    EXPECT_THROW(py::Int g2 = g, py::TypeError);
    EXPECT_THROW(py::Int h2 = h, py::TypeError);
    EXPECT_THROW(py::Int i2 = i, py::TypeError);
}


TEST(py_int, from_accessor) {
    // safe
    py::List list = {0, 1};
    py::Int a = list[0];
    py::Int b = list[1];
    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 1);
    py::List list2 = {false, true};
    py::Int c = list2[0];
    py::Int d = list2[1];
    EXPECT_EQ(c, 0);
    EXPECT_EQ(d, 1);

    // not safe
    py::List list3 = {0.0, 1.0};
    EXPECT_THROW(py::Int e = list3[0], py::TypeError);
    EXPECT_THROW(py::Int f = list3[1], py::TypeError);
}


TEST(py_int, copy) {
    // copy construct
    py::Int a = 1;
    int a_refs = a.ref_count();
    py::Int b(a);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(b.ref_count(), a_refs + 1);

    // copy assign
    py::Int c = 1;
    py::Int d = 2;
    int c_refs = c.ref_count();
    d = c;
    EXPECT_EQ(d, 1);
    EXPECT_EQ(d.ref_count(), c_refs + 1);
}


TEST(py_int, move) {
    // move construct
    py::Int a = 1;
    int a_refs = a.ref_count();
    py::Int b(std::move(a));
    EXPECT_EQ(b, 1);
    EXPECT_EQ(b.ref_count(), a_refs);

    // move assign
    py::Int c = 1;
    py::Int d = 2;
    int c_refs = c.ref_count();
    d = std::move(c);
    EXPECT_EQ(d, 1);
    EXPECT_EQ(d.ref_count(), c_refs);
}


TEST(py_int, from_bool) {
    // constructor
    EXPECT_EQ(py::Int(true), 1);
    EXPECT_EQ(py::Int(false), 0);
    EXPECT_EQ(py::Int(py::Bool(true)), 1);
    EXPECT_EQ(py::Int(py::Bool(false)), 0);

    // assignment
    py::Int a = 1;
    EXPECT_EQ(a, 1);
    a = true;
    EXPECT_EQ(a, 1);
    a = false;
    EXPECT_EQ(a, 0);
}


TEST(py_int, from_int) {
    // constructor
    EXPECT_EQ(py::Int(0), 0);
    EXPECT_EQ(py::Int(1), 1);
    EXPECT_EQ(py::Int(-1), -1);
    EXPECT_EQ(py::Int(static_cast<unsigned int>(0)), 0);
    EXPECT_EQ(py::Int(static_cast<unsigned int>(1)), 1);

    // assignment
    py::Int a = 1;
    EXPECT_EQ(a, 1);
    a = 0;
    EXPECT_EQ(a, 0);
    a = 1;
    EXPECT_EQ(a, 1);
    a = -1;
    EXPECT_EQ(a, -1);
    a = static_cast<unsigned int>(0);
    EXPECT_EQ(a, 0);
    a = static_cast<unsigned int>(1);
    EXPECT_EQ(a, 1);
}


/////////////////////////////////////
////    EXPLICIT CONSTRUCTORS    ////
/////////////////////////////////////


TEST(py_int, from_float) {
    // constructor
    EXPECT_EQ(py::Int(0.0), 0);
    EXPECT_EQ(py::Int(1.0), 1);
    EXPECT_EQ(py::Int(-1.0), -1);
    EXPECT_EQ(py::Int(py::Float(0.0)), 0);
    EXPECT_EQ(py::Int(py::Float(1.0)), 1);
    EXPECT_EQ(py::Int(py::Float(-1.0)), -1);

    // assignment
    assertions::assign<py::Int, double>::invalid();
    assertions::assign<py::Int, double>::invalid();
}


TEST(py_int, from_string) {
    // constructor - string literal without base
    EXPECT_EQ(py::Int("0"), 0);
    EXPECT_EQ(py::Int("1"), 1);
    EXPECT_EQ(py::Int("-1"), -1);
    EXPECT_EQ(py::Int("0b101"), 5);
    EXPECT_EQ(py::Int("-0b101"), -5);
    EXPECT_EQ(py::Int("0o10"), 8);
    EXPECT_EQ(py::Int("-0o10"), -8);
    EXPECT_EQ(py::Int("0x10"), 16);
    EXPECT_EQ(py::Int("-0x10"), -16);

    // constructor - std::string without base
    EXPECT_EQ(py::Int(std::string("0")), 0);
    EXPECT_EQ(py::Int(std::string("1")), 1);
    EXPECT_EQ(py::Int(std::string("-1")), -1);
    EXPECT_EQ(py::Int(std::string("0b101")), 5);
    EXPECT_EQ(py::Int(std::string("-0b101")), -5);
    EXPECT_EQ(py::Int(std::string("0o10")), 8);
    EXPECT_EQ(py::Int(std::string("-0o10")), -8);
    EXPECT_EQ(py::Int(std::string("0x10")), 16);
    EXPECT_EQ(py::Int(std::string("-0x10")), -16);

    // constructor - std::string_view without base
    EXPECT_EQ(py::Int(std::string_view("0")), 0);
    EXPECT_EQ(py::Int(std::string_view("1")), 1);
    EXPECT_EQ(py::Int(std::string_view("-1")), -1);
    EXPECT_EQ(py::Int(std::string_view("0b101")), 5);
    EXPECT_EQ(py::Int(std::string_view("-0b101")), -5);
    EXPECT_EQ(py::Int(std::string_view("0o10")), 8);
    EXPECT_EQ(py::Int(std::string_view("-0o10")), -8);
    EXPECT_EQ(py::Int(std::string_view("0x10")), 16);
    EXPECT_EQ(py::Int(std::string_view("-0x10")), -16);

    // constructor - py::Str without base
    EXPECT_EQ(py::Int(py::Str("0")), 0);
    EXPECT_EQ(py::Int(py::Str("1")), 1);
    EXPECT_EQ(py::Int(py::Str("-1")), -1);
    EXPECT_EQ(py::Int(py::Str("0b101")), 5);
    EXPECT_EQ(py::Int(py::Str("-0b101")), -5);
    EXPECT_EQ(py::Int(py::Str("0o10")), 8);
    EXPECT_EQ(py::Int(py::Str("-0o10")), -8);
    EXPECT_EQ(py::Int(py::Str("0x10")), 16);
    EXPECT_EQ(py::Int(py::Str("-0x10")), -16);

    // constructor - string literal with base
    EXPECT_EQ(py::Int("0", 10), 0);
    EXPECT_EQ(py::Int("1", 10), 1);
    EXPECT_EQ(py::Int("-1", 10), -1);
    EXPECT_EQ(py::Int("0b101", 2), 5);
    EXPECT_EQ(py::Int("-0b101", 2), -5);
    EXPECT_EQ(py::Int("0o10", 8), 8);
    EXPECT_EQ(py::Int("-0o10", 8), -8);
    EXPECT_EQ(py::Int("0x10", 16), 16);
    EXPECT_EQ(py::Int("-0x10", 16), -16);

    // constructor - std::string with base
    EXPECT_EQ(py::Int(std::string("0"), 10), 0);
    EXPECT_EQ(py::Int(std::string("1"), 10), 1);
    EXPECT_EQ(py::Int(std::string("-1"), 10), -1);
    EXPECT_EQ(py::Int(std::string("0b101"), 2), 5);
    EXPECT_EQ(py::Int(std::string("-0b101"), 2), -5);
    EXPECT_EQ(py::Int(std::string("0o10"), 8), 8);
    EXPECT_EQ(py::Int(std::string("-0o10"), 8), -8);
    EXPECT_EQ(py::Int(std::string("0x10"), 16), 16);
    EXPECT_EQ(py::Int(std::string("-0x10"), 16), -16);

    // constructor - std::string_view with base
    EXPECT_EQ(py::Int(std::string_view("0"), 10), 0);
    EXPECT_EQ(py::Int(std::string_view("1"), 10), 1);
    EXPECT_EQ(py::Int(std::string_view("-1"), 10), -1);
    EXPECT_EQ(py::Int(std::string_view("0b101"), 2), 5);
    EXPECT_EQ(py::Int(std::string_view("-0b101"), 2), -5);
    EXPECT_EQ(py::Int(std::string_view("0o10"), 8), 8);
    EXPECT_EQ(py::Int(std::string_view("-0o10"), 8), -8);
    EXPECT_EQ(py::Int(std::string_view("0x10"), 16), 16);
    EXPECT_EQ(py::Int(std::string_view("-0x10"), 16), -16);

    // constructor - py::Str with base
    EXPECT_EQ(py::Int(py::Str("0"), 10), 0);
    EXPECT_EQ(py::Int(py::Str("1"), 10), 1);
    EXPECT_EQ(py::Int(py::Str("-1"), 10), -1);
    EXPECT_EQ(py::Int(py::Str("0b101"), 2), 5);
    EXPECT_EQ(py::Int(py::Str("-0b101"), 2), -5);
    EXPECT_EQ(py::Int(py::Str("0o10"), 8), 8);
    EXPECT_EQ(py::Int(py::Str("-0o10"), 8), -8);
    EXPECT_EQ(py::Int(py::Str("0x10"), 16), 16);
    EXPECT_EQ(py::Int(py::Str("-0x10"), 16), -16);

    // assignment
    assertions::assign<py::Int, const char*>::invalid();
    assertions::assign<py::Int, std::string>::invalid();
    assertions::assign<py::Int, std::string_view>::invalid();
    assertions::assign<py::Int, py::Str>::invalid();
}


TEST(py_int, from_custom_type) {
    // uint64_t
    struct ImplicitUInt64_T { operator uint64_t() const { return 1; } };
    struct ExplicitUInt64_T { explicit operator uint64_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUInt64_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitUInt64_T{}), 1);
    assertions::assign<py::Int, ImplicitUInt64_T>::invalid();
    assertions::assign<py::Int, ExplicitUInt64_T>::invalid();
    assertions::assign<py::Int&, ImplicitUInt64_T>::invalid();
    assertions::assign<py::Int&, ExplicitUInt64_T>::invalid();

    // unsigned long long
    struct ImplicitUnsignedLongLong { operator unsigned long long() const { return 1; } };
    struct ExplicitUnsignedLongLong { explicit operator unsigned long long() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUnsignedLongLong{}), 1);
    EXPECT_EQ(py::Int(ExplicitUnsignedLongLong{}), 1);
    assertions::assign<py::Int, ImplicitUnsignedLongLong>::invalid();
    assertions::assign<py::Int, ExplicitUnsignedLongLong>::invalid();
    assertions::assign<py::Int&, ImplicitUnsignedLongLong>::invalid();
    assertions::assign<py::Int&, ExplicitUnsignedLongLong>::invalid();

    // int64_t
    struct ImplicitInt64_T { operator int64_t() const { return 1; } };
    struct ExplicitInt64_T { explicit operator int64_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitInt64_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitInt64_T{}), 1);
    assertions::assign<py::Int, ImplicitInt64_T>::invalid();
    assertions::assign<py::Int, ExplicitInt64_T>::invalid();
    assertions::assign<py::Int&, ImplicitInt64_T>::invalid();
    assertions::assign<py::Int&, ExplicitInt64_T>::invalid();

    // long long
    struct ImplicitLongLong { operator long long() const { return 1; } };
    struct ExplicitLongLong { explicit operator long long() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitLongLong{}), 1);
    EXPECT_EQ(py::Int(ExplicitLongLong{}), 1);
    assertions::assign<py::Int, ImplicitLongLong>::invalid();
    assertions::assign<py::Int, ExplicitLongLong>::invalid();
    assertions::assign<py::Int&, ImplicitLongLong>::invalid();
    assertions::assign<py::Int&, ExplicitLongLong>::invalid();

    // uint32_t
    struct ImplicitUInt32_T { operator uint32_t() const { return 1; } };
    struct ExplicitUInt32_T { explicit operator uint32_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUInt32_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitUInt32_T{}), 1);
    assertions::assign<py::Int, ImplicitUInt32_T>::invalid();
    assertions::assign<py::Int, ExplicitUInt32_T>::invalid();
    assertions::assign<py::Int&, ImplicitUInt32_T>::invalid();
    assertions::assign<py::Int&, ExplicitUInt32_T>::invalid();

    // unsigned long
    struct ImplicitUnsignedLong { operator unsigned long() const { return 1; } };
    struct ExplicitUnsignedLong { explicit operator unsigned long() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUnsignedLong{}), 1);
    EXPECT_EQ(py::Int(ExplicitUnsignedLong{}), 1);
    assertions::assign<py::Int, ImplicitUnsignedLong>::invalid();
    assertions::assign<py::Int, ExplicitUnsignedLong>::invalid();
    assertions::assign<py::Int&, ImplicitUnsignedLong>::invalid();
    assertions::assign<py::Int&, ExplicitUnsignedLong>::invalid();

    // unsigned int
    struct ImplicitUnsignedInt { operator unsigned int() const { return 1; } };
    struct ExplicitUnsignedInt { explicit operator unsigned int() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUnsignedInt{}), 1);
    EXPECT_EQ(py::Int(ExplicitUnsignedInt{}), 1);
    assertions::assign<py::Int, ImplicitUnsignedInt>::invalid();
    assertions::assign<py::Int, ExplicitUnsignedInt>::invalid();
    assertions::assign<py::Int&, ImplicitUnsignedInt>::invalid();
    assertions::assign<py::Int&, ExplicitUnsignedInt>::invalid();

    // int32_t
    struct ImplicitInt32_T { operator int32_t() const { return 1; } };
    struct ExplicitInt32_T { explicit operator int32_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitInt32_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitInt32_T{}), 1);
    assertions::assign<py::Int, ImplicitInt32_T>::invalid();
    assertions::assign<py::Int, ExplicitInt32_T>::invalid();
    assertions::assign<py::Int&, ImplicitInt32_T>::invalid();
    assertions::assign<py::Int&, ExplicitInt32_T>::invalid();

    // long
    struct ImplicitLong { operator long() const { return 1; } };
    struct ExplicitLong { explicit operator long() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitLong{}), 1);
    EXPECT_EQ(py::Int(ExplicitLong{}), 1);
    assertions::assign<py::Int, ImplicitLong>::invalid();
    assertions::assign<py::Int, ExplicitLong>::invalid();
    assertions::assign<py::Int&, ImplicitLong>::invalid();
    assertions::assign<py::Int&, ExplicitLong>::invalid();

    // int
    struct ImplicitInt { operator int() const { return 1; } };
    struct ExplicitInt { explicit operator int() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitInt{}), 1);
    EXPECT_EQ(py::Int(ExplicitInt{}), 1);
    assertions::assign<py::Int, ImplicitInt>::invalid();
    assertions::assign<py::Int, ExplicitInt>::invalid();
    assertions::assign<py::Int&, ImplicitInt>::invalid();
    assertions::assign<py::Int&, ExplicitInt>::invalid();

    // uint16_t
    struct ImplicitUInt16_T { operator uint16_t() const { return 1; } };
    struct ExplicitUInt16_T { explicit operator uint16_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUInt16_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitUInt16_T{}), 1);
    assertions::assign<py::Int, ImplicitUInt16_T>::invalid();
    assertions::assign<py::Int, ExplicitUInt16_T>::invalid();
    assertions::assign<py::Int&, ImplicitUInt16_T>::invalid();
    assertions::assign<py::Int&, ExplicitUInt16_T>::invalid();

    // unsigned short
    struct ImplicitUnsignedShort { operator unsigned short() const { return 1; } };
    struct ExplicitUnsignedShort { explicit operator unsigned short() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUnsignedShort{}), 1);
    EXPECT_EQ(py::Int(ExplicitUnsignedShort{}), 1);
    assertions::assign<py::Int, ImplicitUnsignedShort>::invalid();
    assertions::assign<py::Int, ExplicitUnsignedShort>::invalid();
    assertions::assign<py::Int&, ImplicitUnsignedShort>::invalid();
    assertions::assign<py::Int&, ExplicitUnsignedShort>::invalid();

    // int16_t
    struct ImplicitInt16_T { operator int16_t() const { return 1; } };
    struct ExplicitInt16_T { explicit operator int16_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitInt16_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitInt16_T{}), 1);
    assertions::assign<py::Int, ImplicitInt16_T>::invalid();
    assertions::assign<py::Int, ExplicitInt16_T>::invalid();
    assertions::assign<py::Int&, ImplicitInt16_T>::invalid();
    assertions::assign<py::Int&, ExplicitInt16_T>::invalid();

    // short
    struct ImplicitShort { operator short() const { return 1; } };
    struct ExplicitShort { explicit operator short() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitShort{}), 1);
    EXPECT_EQ(py::Int(ExplicitShort{}), 1);
    assertions::assign<py::Int, ImplicitShort>::invalid();
    assertions::assign<py::Int, ExplicitShort>::invalid();
    assertions::assign<py::Int&, ImplicitShort>::invalid();
    assertions::assign<py::Int&, ExplicitShort>::invalid();

    // uint8_t
    struct ImplicitUInt8_T { operator uint8_t() const { return 1; } };
    struct ExplicitUInt8_T { explicit operator uint8_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUInt8_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitUInt8_T{}), 1);
    assertions::assign<py::Int, ImplicitUInt8_T>::invalid();
    assertions::assign<py::Int, ExplicitUInt8_T>::invalid();
    assertions::assign<py::Int&, ImplicitUInt8_T>::invalid();
    assertions::assign<py::Int&, ExplicitUInt8_T>::invalid();

    // unsigned char
    struct ImplicitUnsignedChar { operator unsigned char() const { return 1; } };
    struct ExplicitUnsignedChar { explicit operator unsigned char() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUnsignedChar{}), 1);
    EXPECT_EQ(py::Int(ExplicitUnsignedChar{}), 1);
    assertions::assign<py::Int, ImplicitUnsignedChar>::invalid();
    assertions::assign<py::Int, ExplicitUnsignedChar>::invalid();
    assertions::assign<py::Int&, ImplicitUnsignedChar>::invalid();
    assertions::assign<py::Int&, ExplicitUnsignedChar>::invalid();

    // int8_t
    struct ImplicitInt8_T { operator int8_t() const { return 1; } };
    struct ExplicitInt8_T { explicit operator int8_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitInt8_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitInt8_T{}), 1);
    assertions::assign<py::Int, ImplicitInt8_T>::invalid();
    assertions::assign<py::Int, ExplicitInt8_T>::invalid();
    assertions::assign<py::Int&, ImplicitInt8_T>::invalid();
    assertions::assign<py::Int&, ExplicitInt8_T>::invalid();

    // char
    struct ImplicitChar { operator char() const { return 1; } };
    struct ExplicitChar { explicit operator char() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitChar{}), 1);
    EXPECT_EQ(py::Int(ExplicitChar{}), 1);
    assertions::assign<py::Int, ImplicitChar>::invalid();
    assertions::assign<py::Int, ExplicitChar>::invalid();
    assertions::assign<py::Int&, ImplicitChar>::invalid();
    assertions::assign<py::Int&, ExplicitChar>::invalid();

    // bool
    struct ImplicitBool { operator bool() const { return true; } };
    struct ExplicitBool { explicit operator bool() const { return true; } };
    EXPECT_EQ(py::Int(ImplicitBool{}), 1);
    EXPECT_EQ(py::Int(ExplicitBool{}), 1);
    assertions::assign<py::Int, ImplicitBool>::invalid();
    assertions::assign<py::Int, ExplicitBool>::invalid();
    assertions::assign<py::Int&, ImplicitBool>::invalid();
    assertions::assign<py::Int&, ExplicitBool>::invalid();

    // multiple options
    struct ImplicitMultiple {
        operator int64_t() const { return 1; }
        operator int32_t() const { return 1; }
        operator int16_t() const { return 1; }
        operator int8_t() const { return 1; }
        operator uint64_t() const { return 1; }
        operator uint32_t() const { return 1; }
        operator uint16_t() const { return 1; }
        operator uint8_t() const { return 1; }
    };
    struct ExplicitMultiple {
        explicit operator int64_t() const { return 1; }
        explicit operator int32_t() const { return 1; }
        explicit operator int16_t() const { return 1; }
        explicit operator int8_t() const { return 1; }
        explicit operator uint64_t() const { return 1; }
        explicit operator uint32_t() const { return 1; }
        explicit operator uint16_t() const { return 1; }
        explicit operator uint8_t() const { return 1; }
    };
    EXPECT_EQ(py::Int(ImplicitMultiple{}), 1);
    EXPECT_EQ(py::Int(ExplicitMultiple{}), 1);
    assertions::assign<py::Int, ImplicitMultiple>::invalid();
    assertions::assign<py::Int, ExplicitMultiple>::invalid();
    assertions::assign<py::Int&, ImplicitMultiple>::invalid();
    assertions::assign<py::Int&, ExplicitMultiple>::invalid();

    // Python type
    py::Type Foo("Foo");
    Foo.attr("__index__") = py::Method([](const py::Object& self) { return 42; });
    py::Object foo = Foo();
    EXPECT_EQ(py::Int(foo), 42);
    EXPECT_THROW(py::Int a = foo, py::TypeError);
    py::Int a = 0;
    EXPECT_THROW(a = foo, py::TypeError);
}


///////////////////////////
////    CONVERSIONS    ////
///////////////////////////


TEST(py_int, to_bool) {
    assertions::assign<bool, py::Int>::invalid();
    assertions::assign<bool&, py::Int>::invalid();
    assertions::assign<py::Bool, py::Int>::invalid();
    assertions::assign<py::Bool&, py::Int>::invalid();
}


TEST(py_int, to_int) {
    // assignment
    int a = py::Int(0);
    int b = py::Int(1);
    int c = py::Int(-1);
    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(c, -1);
    unsigned int d = py::Int(0);
    unsigned int e = py::Int(1);
    unsigned int f = py::Int(-1);
    EXPECT_EQ(d, static_cast<unsigned int>(0));
    EXPECT_EQ(e, static_cast<unsigned int>(1));
    EXPECT_EQ(f, static_cast<unsigned int>(-1));

    // function parameters
    auto func1 = [](int x) { return x; };
    auto func2 = [](unsigned int x) { return x; };
    EXPECT_EQ(func1(py::Int(0)), 0);
    EXPECT_EQ(func2(py::Int(1)), static_cast<unsigned int>(1));
}


TEST(py_int, to_float) {
    // assignment
    double a = py::Int(0);
    double b = py::Int(1);
    double c = py::Int(-1);
    EXPECT_EQ(a, 0.0);
    EXPECT_EQ(b, 1.0);
    EXPECT_EQ(c, -1.0);
    py::Float d = py::Int(0);
    py::Float e = py::Int(1);
    py::Float f = py::Int(-1);
    EXPECT_EQ(d, 0.0);
    EXPECT_EQ(e, 1.0);
    EXPECT_EQ(f, -1.0);

    // function parameters
    auto func1 = [](double x) { return x; };
    auto func2 = [](const py::Float& x) { return x; };
    EXPECT_EQ(func1(py::Int(0)), 0.0);
    EXPECT_EQ(func2(py::Int(1)), 1.0);
}


TEST(py_int, to_string) {
    // assignment
    assertions::assign<std::string, py::Int&&>::invalid();
    assertions::assign<std::string&, py::Int&&>::invalid();

    // explicit cast
    EXPECT_EQ(static_cast<std::string>(py::Int(0)), "0");
    EXPECT_EQ(static_cast<std::string>(py::Int(1)), "1");
    EXPECT_EQ(static_cast<std::string>(py::Int(-1)), "-1");
}


/////////////////////////
////    OPERATORS    ////
/////////////////////////


TEST(py_int, mem_ops) {
    assertions::dereference<py::Int>::invalid();
    assertions::address_of<py::Int>::returns<py::Int*>::valid();
}


TEST(py_int, call) {
    assertions::call<py::Int>::invalid();
}


TEST(py_int, hash) {
    std::hash<py::Int> hash;
    EXPECT_EQ(hash(py::Int(0)), hash(py::Int(0)));
    EXPECT_EQ(hash(py::Int(1)), hash(py::Int(1)));
    EXPECT_EQ(hash(py::Int(-1)), hash(py::Int(-1)));
    EXPECT_NE(hash(py::Int(0)), hash(py::Int(1)));
    EXPECT_NE(hash(py::Int(0)), hash(py::Int(-1)));
    EXPECT_NE(hash(py::Int(1)), hash(py::Int(-1)));
}


TEST(py_int, iter) {
    assertions::iter<py::Int>::invalid();
    assertions::reverse_iter<py::Int>::invalid();
}


TEST(py_int, index) {
    assertions::index<py::Int>::invalid();
}


TEST(py_int, contains) {
    assertions::contains<py::Int>::invalid();
}


TEST(py_int, less_than) {
    // py::Int < bool
    EXPECT_EQ(py::Int(0) < false,               false);
    EXPECT_EQ(py::Int(0) < true,                true);
    EXPECT_EQ(py::Int(0) < py::Bool(false),     false);
    EXPECT_EQ(py::Int(0) < py::Bool(true),      true);

    // py::Int < int
    EXPECT_EQ(py::Int(0) < 0,                   false);
    EXPECT_EQ(py::Int(0) < 1,                   true);
    EXPECT_EQ(py::Int(0) < -1,                  false);
    EXPECT_EQ(py::Int(0) < py::Int(0),          false);
    EXPECT_EQ(py::Int(0) < py::Int(1),          true);
    EXPECT_EQ(py::Int(0) < py::Int(-1),         false);

    // py::Int < float
    EXPECT_EQ(py::Int(0) < 0.0,                 false);
    EXPECT_EQ(py::Int(0) < 1.0,                 true);
    EXPECT_EQ(py::Int(0) < -1.0,                false);
    EXPECT_EQ(py::Int(0) < py::Float(0.0),      false);
    EXPECT_EQ(py::Int(0) < py::Float(1.0),      true);
    EXPECT_EQ(py::Int(0) < py::Float(-1.0),     false);

    // py::Int < T
    assertions::less_than<py::Int, std::string>::invalid();
    assertions::less_than<py::Int, std::tuple<>>::invalid();
    assertions::less_than<py::Int, std::vector<int>>::invalid();
    assertions::less_than<py::Int, std::unordered_set<int>>::invalid();
    assertions::less_than<py::Int, std::unordered_map<int, int>>::invalid();
    assertions::less_than<py::Int, py::Str>::invalid();
    assertions::less_than<py::Int, py::Tuple>::invalid();
    assertions::less_than<py::Int, py::List>::invalid();
    assertions::less_than<py::Int, py::Set>::invalid();
    assertions::less_than<py::Int, py::Dict>::invalid();
}


TEST(py_int, less_than_or_equal_to) {
    // py::Int < bool
    EXPECT_EQ(py::Int(0) <= false,              true);
    EXPECT_EQ(py::Int(0) <= true,               true);
    EXPECT_EQ(py::Int(0) <= py::Bool(false),    true);
    EXPECT_EQ(py::Int(0) <= py::Bool(true),     true);

    // py::Int < int
    EXPECT_EQ(py::Int(0) <= 0,                  true);
    EXPECT_EQ(py::Int(0) <= 1,                  true);
    EXPECT_EQ(py::Int(0) <= -1,                 false);
    EXPECT_EQ(py::Int(0) <= py::Int(0),         true);
    EXPECT_EQ(py::Int(0) <= py::Int(1),         true);
    EXPECT_EQ(py::Int(0) <= py::Int(-1),        false);

    // py::Int < float
    EXPECT_EQ(py::Int(0) <= 0.0,                true);
    EXPECT_EQ(py::Int(0) <= 1.0,                true);
    EXPECT_EQ(py::Int(0) <= -1.0,               false);
    EXPECT_EQ(py::Int(0) <= py::Float(0.0),     true);
    EXPECT_EQ(py::Int(0) <= py::Float(1.0),     true);
    EXPECT_EQ(py::Int(0) <= py::Float(-1.0),    false);

    // py::Int < T
    assertions::less_than_or_equal_to<py::Int, std::string>::invalid();
    assertions::less_than_or_equal_to<py::Int, std::tuple<>>::invalid();
    assertions::less_than_or_equal_to<py::Int, std::vector<int>>::invalid();
    assertions::less_than_or_equal_to<py::Int, std::unordered_set<int>>::invalid();
    assertions::less_than_or_equal_to<py::Int, std::unordered_map<int, int>>::invalid();
    assertions::less_than_or_equal_to<py::Int, py::Str>::invalid();
    assertions::less_than_or_equal_to<py::Int, py::Tuple>::invalid();
    assertions::less_than_or_equal_to<py::Int, py::List>::invalid();
    assertions::less_than_or_equal_to<py::Int, py::Set>::invalid();
    assertions::less_than_or_equal_to<py::Int, py::Dict>::invalid();
}


TEST(py_int, equal_to) {
    // py::Int == bool
    EXPECT_EQ(py::Int(0) == false,              true);
    EXPECT_EQ(py::Int(0) == true,               false);
    EXPECT_EQ(py::Int(0) == py::Bool(false),    true);
    EXPECT_EQ(py::Int(0) == py::Bool(true),     false);

    // py::Int == int
    EXPECT_EQ(py::Int(0) == 0,                  true);
    EXPECT_EQ(py::Int(0) == 1,                  false);
    EXPECT_EQ(py::Int(0) == -1,                 false);
    EXPECT_EQ(py::Int(0) == py::Int(0),         true);
    EXPECT_EQ(py::Int(0) == py::Int(1),         false);
    EXPECT_EQ(py::Int(0) == py::Int(-1),        false);

    // py::Int == float
    EXPECT_EQ(py::Int(0) == 0.0,                true);
    EXPECT_EQ(py::Int(0) == 1.0,                false);
    EXPECT_EQ(py::Int(0) == -1.0,               false);
    EXPECT_EQ(py::Int(0) == py::Float(0.0),     true);
    EXPECT_EQ(py::Int(0) == py::Float(1.0),     false);
    EXPECT_EQ(py::Int(0) == py::Float(-1.0),    false);

    // py::Int == T
    auto d = std::unordered_map<int, int>{};
    EXPECT_EQ(py::Int(0) == "abc",                          false);
    EXPECT_EQ(py::Int(0) == std::tuple<>{},                 false);
    EXPECT_EQ(py::Int(0) == std::vector<int>{},             false);
    EXPECT_EQ(py::Int(0) == std::unordered_set<int>{},      false);
    EXPECT_EQ(py::Int(0) == d,                              false);
    EXPECT_EQ(py::Int(0) == py::Str("abc"),                 false);
    EXPECT_EQ(py::Int(0) == py::Tuple{},                    false);
    EXPECT_EQ(py::Int(0) == py::List{},                     false);
    EXPECT_EQ(py::Int(0) == py::Set{},                      false);
    EXPECT_EQ(py::Int(0) == py::Dict{},                     false);
}


TEST(py_int, not_equal_to) {
    // py::Int != bool
    EXPECT_EQ(py::Int(0) != false,              false);
    EXPECT_EQ(py::Int(0) != true,               true);
    EXPECT_EQ(py::Int(0) != py::Bool(false),    false);
    EXPECT_EQ(py::Int(0) != py::Bool(true),     true);

    // py::Int != int
    EXPECT_EQ(py::Int(0) != 0,                  false);
    EXPECT_EQ(py::Int(0) != 1,                  true);
    EXPECT_EQ(py::Int(0) != -1,                 true);
    EXPECT_EQ(py::Int(0) != py::Int(0),         false);
    EXPECT_EQ(py::Int(0) != py::Int(1),         true);
    EXPECT_EQ(py::Int(0) != py::Int(-1),        true);

    // py::Int != float
    EXPECT_EQ(py::Int(0) != 0.0,                false);
    EXPECT_EQ(py::Int(0) != 1.0,                true);
    EXPECT_EQ(py::Int(0) != -1.0,               true);
    EXPECT_EQ(py::Int(0) != py::Float(0.0),     false);
    EXPECT_EQ(py::Int(0) != py::Float(1.0),     true);
    EXPECT_EQ(py::Int(0) != py::Float(-1.0),    true);

    // py::Int != T
    auto d = std::unordered_map<int, int>{};
    EXPECT_EQ(py::Int(0) != "abc",                          true);
    EXPECT_EQ(py::Int(0) != std::tuple<>{},                 true);
    EXPECT_EQ(py::Int(0) != std::vector<int>{},             true);
    EXPECT_EQ(py::Int(0) != std::unordered_set<int>{},      true);
    EXPECT_EQ(py::Int(0) != d,                              true);
    EXPECT_EQ(py::Int(0) != py::Str("abc"),                 true);
    EXPECT_EQ(py::Int(0) != py::Tuple{},                    true);
    EXPECT_EQ(py::Int(0) != py::List{},                     true);
    EXPECT_EQ(py::Int(0) != py::Set{},                      true);
    EXPECT_EQ(py::Int(0) != py::Dict{},                     true);
}


TEST(py_int, greater_than_or_equal_to) {
    // py::Int >= bool
    EXPECT_EQ(py::Int(0) >= false,              true);
    EXPECT_EQ(py::Int(0) >= true,               false);
    EXPECT_EQ(py::Int(0) >= py::Bool(false),    true);
    EXPECT_EQ(py::Int(0) >= py::Bool(true),     false);

    // py::Int >= int
    EXPECT_EQ(py::Int(0) >= 0,                  true);
    EXPECT_EQ(py::Int(0) >= 1,                  false);
    EXPECT_EQ(py::Int(0) >= -1,                 true);
    EXPECT_EQ(py::Int(0) >= py::Int(0),         true);
    EXPECT_EQ(py::Int(0) >= py::Int(1),         false);
    EXPECT_EQ(py::Int(0) >= py::Int(-1),        true);

    // py::Int >= float
    EXPECT_EQ(py::Int(0) >= 0.0,                true);
    EXPECT_EQ(py::Int(0) >= 1.0,                false);
    EXPECT_EQ(py::Int(0) >= -1.0,               true);
    EXPECT_EQ(py::Int(0) >= py::Float(0.0),     true);
    EXPECT_EQ(py::Int(0) >= py::Float(1.0),     false);
    EXPECT_EQ(py::Int(0) >= py::Float(-1.0),    true);

    // py::Int >= T
    assertions::greater_than_or_equal_to<py::Int, std::string>::invalid();
    assertions::greater_than_or_equal_to<py::Int, std::tuple<>>::invalid();
    assertions::greater_than_or_equal_to<py::Int, std::vector<int>>::invalid();
    assertions::greater_than_or_equal_to<py::Int, std::unordered_set<int>>::invalid();
    assertions::greater_than_or_equal_to<py::Int, std::unordered_map<int, int>>::invalid();
    assertions::greater_than_or_equal_to<py::Int, py::Str>::invalid();
    assertions::greater_than_or_equal_to<py::Int, py::Tuple>::invalid();
    assertions::greater_than_or_equal_to<py::Int, py::List>::invalid();
    assertions::greater_than_or_equal_to<py::Int, py::Set>::invalid();
    assertions::greater_than_or_equal_to<py::Int, py::Dict>::invalid();
}


TEST(py_int, greater_than) {
    // py::Int > bool
    EXPECT_EQ(py::Int(0) > false,               false);
    EXPECT_EQ(py::Int(0) > true,                false);
    EXPECT_EQ(py::Int(0) > py::Bool(false),     false);
    EXPECT_EQ(py::Int(0) > py::Bool(true),      false);

    // py::Int > int
    EXPECT_EQ(py::Int(0) > 0,                   false);
    EXPECT_EQ(py::Int(0) > 1,                   false);
    EXPECT_EQ(py::Int(0) > -1,                  true);
    EXPECT_EQ(py::Int(0) > py::Int(0),          false);
    EXPECT_EQ(py::Int(0) > py::Int(1),          false);
    EXPECT_EQ(py::Int(0) > py::Int(-1),         true);

    // py::Int > float
    EXPECT_EQ(py::Int(0) > 0.0,                 false);
    EXPECT_EQ(py::Int(0) > 1.0,                 false);
    EXPECT_EQ(py::Int(0) > -1.0,                true);
    EXPECT_EQ(py::Int(0) > py::Float(0.0),      false);
    EXPECT_EQ(py::Int(0) > py::Float(1.0),      false);
    EXPECT_EQ(py::Int(0) > py::Float(-1.0),     true);

    // py::Int > T
    assertions::greater_than<py::Int, std::string>::invalid();
    assertions::greater_than<py::Int, std::tuple<>>::invalid();
    assertions::greater_than<py::Int, std::vector<int>>::invalid();
    assertions::greater_than<py::Int, std::unordered_set<int>>::invalid();
    assertions::greater_than<py::Int, std::unordered_map<int, int>>::invalid();
    assertions::greater_than<py::Int, py::Str>::invalid();
    assertions::greater_than<py::Int, py::Tuple>::invalid();
    assertions::greater_than<py::Int, py::List>::invalid();
    assertions::greater_than<py::Int, py::Set>::invalid();
    assertions::greater_than<py::Int, py::Dict>::invalid();
}


TEST(py_int, invert) {
    assertions::unary_invert<py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(~py::Int(0), -1);
    EXPECT_EQ(~py::Int(1), -2);
    EXPECT_EQ(~py::Int(-1), 0);
}


TEST(py_int, plus) {
    // +py::Int
    assertions::unary_plus<py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(+py::Int(0), 0);
    EXPECT_EQ(+py::Int(1), 1);
    EXPECT_EQ(+py::Int(-1), -1);

    // // ++py::Int, py::Int++
    EXPECT_EQ(++py::Int(0), 1);
    EXPECT_EQ(++py::Int(1), 2);
    EXPECT_EQ(++py::Int(-1), 0);
    py::Int a = 1;
    EXPECT_EQ(a++, 1);
    EXPECT_EQ(a, 2);

    // py::Int + bool
    assertions::binary_plus<py::Int, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) + false,              5);
    EXPECT_EQ(py::Int(5) + true,               6);
    assertions::binary_plus<py::Int, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) + py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) + py::Bool(true),     6);

    // py::Int + int
    assertions::binary_plus<py::Int, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) + 0,                  5);
    EXPECT_EQ(py::Int(5) + 1,                  6);
    EXPECT_EQ(py::Int(5) + -1,                 4);
    assertions::binary_plus<py::Int, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) + py::Int(0),         5);
    EXPECT_EQ(py::Int(5) + py::Int(1),         6);
    EXPECT_EQ(py::Int(5) + py::Int(-1),        4);

    // py::Int + float
    assertions::binary_plus<py::Int, double>::returns<py::Float>::valid();
    EXPECT_EQ(py::Int(5) + 0.0,                5.0);
    EXPECT_EQ(py::Int(5) + 1.0,                6.0);
    EXPECT_EQ(py::Int(5) + -1.0,               4.0);
    assertions::binary_plus<py::Int, py::Float>::returns<py::Float>::valid();
    EXPECT_EQ(py::Int(5) + py::Float(0.0),     5.0);
    EXPECT_EQ(py::Int(5) + py::Float(1.0),     6.0);
    EXPECT_EQ(py::Int(5) + py::Float(-1.0),    4.0);

    // py::Int + complex
    assertions::binary_plus<py::Int, std::complex<double>>::returns<py::Complex>::valid();
    assertions::binary_plus<py::Int, py::Complex>::returns<py::Complex>::valid();

    // py::Int + Decimal
    // assertions::binary_plus<py::Int, py::Decimal>::returns<py::Decimal>::valid();

    // py::Int + T
    assertions::binary_plus<py::Int, const char*>::invalid();
    assertions::binary_plus<py::Int, std::string>::invalid();
    assertions::binary_plus<py::Int, std::string_view>::invalid();
    assertions::binary_plus<py::Int, py::Str>::invalid();

    // py::Int += bool
    EXPECT_EQ(py::Int(5) += false,              5);
    EXPECT_EQ(py::Int(5) += true,               6);
    EXPECT_EQ(py::Int(5) += py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) += py::Bool(true),     6);

    // py::Int += int
    EXPECT_EQ(py::Int(5) += 0,                  5);
    EXPECT_EQ(py::Int(5) += 1,                  6);
    EXPECT_EQ(py::Int(5) += -1,                 4);
    EXPECT_EQ(py::Int(5) += py::Int(0),         5);
    EXPECT_EQ(py::Int(5) += py::Int(1),         6);
    EXPECT_EQ(py::Int(5) += py::Int(-1),        4);

    // py::Int += T
    assertions::inplace_plus<py::Int, double>::invalid();
    assertions::inplace_plus<py::Int, py::Float>::invalid();
}


TEST(py_int, minus) {
    // -py::Int
    assertions::unary_minus<py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(-py::Int(0), 0);
    EXPECT_EQ(-py::Int(1), -1);
    EXPECT_EQ(-py::Int(-1), 1);

    // // --py::Int, py::Int--
    EXPECT_EQ(--py::Int(0), -1);
    EXPECT_EQ(--py::Int(1), 0);
    EXPECT_EQ(--py::Int(-1), -2);
    py::Int a = 1;
    EXPECT_EQ(a--, 1);
    EXPECT_EQ(a, 0);

    // py::Int - bool
    assertions::binary_minus<py::Int, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) - false,              5);
    EXPECT_EQ(py::Int(5) - true,               4);
    assertions::binary_minus<py::Int, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) - py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) - py::Bool(true),     4);

    // py::Int - int
    assertions::binary_minus<py::Int, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) - 0,                  5);
    EXPECT_EQ(py::Int(5) - 1,                  4);
    EXPECT_EQ(py::Int(5) - -1,                 6);
    assertions::binary_minus<py::Int, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) - py::Int(0),         5);
    EXPECT_EQ(py::Int(5) - py::Int(1),         4);
    EXPECT_EQ(py::Int(5) - py::Int(-1),        6);

    // py::Int - float
    assertions::binary_minus<py::Int, double>::returns<py::Float>::valid();
    EXPECT_EQ(py::Int(5) - 0.0,                5.0);
    EXPECT_EQ(py::Int(5) - 1.0,                4.0);
    EXPECT_EQ(py::Int(5) - -1.0,               6.0);
    assertions::binary_minus<py::Int, py::Float>::returns<py::Float>::valid();
    EXPECT_EQ(py::Int(5) - py::Float(0.0),     5.0);
    EXPECT_EQ(py::Int(5) - py::Float(1.0),     4.0);
    EXPECT_EQ(py::Int(5) - py::Float(-1.0),    6.0);

    // py::Int - complex
    assertions::binary_minus<py::Int, std::complex<double>>::returns<py::Complex>::valid();
    assertions::binary_minus<py::Int, py::Complex>::returns<py::Complex>::valid();

    // py::Int - Decimal
    // assertions::binary_minus<py::Int, py::Decimal>::returns<py::Decimal>::valid();

    // py::Int - T
    assertions::binary_minus<py::Int, const char*>::invalid();
    assertions::binary_minus<py::Int, std::string>::invalid();
    assertions::binary_minus<py::Int, std::string_view>::invalid();
    assertions::binary_minus<py::Int, py::Str>::invalid();

    // py::Int -= bool
    EXPECT_EQ(py::Int(5) -= false,              5);
    EXPECT_EQ(py::Int(5) -= true,               4);
    EXPECT_EQ(py::Int(5) -= py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) -= py::Bool(true),     4);

    // py::Int -= int
    EXPECT_EQ(py::Int(5) -= 0,                  5);
    EXPECT_EQ(py::Int(5) -= 1,                  4);
    EXPECT_EQ(py::Int(5) -= -1,                 6);
    EXPECT_EQ(py::Int(5) -= py::Int(0),         5);
    EXPECT_EQ(py::Int(5) -= py::Int(1),         4);
    EXPECT_EQ(py::Int(5) -= py::Int(-1),        6);

    // py::Int -= T
    assertions::inplace_minus<py::Int, double>::invalid();
    assertions::inplace_minus<py::Int, py::Float>::invalid();
}


TEST(py_int, multiply) {
    // py::Int * bool
    assertions::binary_multiply<py::Int, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) * false,              0);
    EXPECT_EQ(py::Int(5) * true,               5);
    assertions::binary_multiply<py::Int, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) * py::Bool(false),    0);
    EXPECT_EQ(py::Int(5) * py::Bool(true),     5);

    // py::Int * int
    assertions::binary_multiply<py::Int, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) * 0,                  0);
    EXPECT_EQ(py::Int(5) * 1,                  5);
    EXPECT_EQ(py::Int(5) * -2,                 -10);
    assertions::binary_multiply<py::Int, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) * py::Int(0),         0);
    EXPECT_EQ(py::Int(5) * py::Int(1),         5);
    EXPECT_EQ(py::Int(5) * py::Int(-2),        -10);

    // py::Int * float
    assertions::binary_multiply<py::Int, double>::returns<py::Float>::valid();
    EXPECT_EQ(py::Int(5) * 0.0,                0.0);
    EXPECT_EQ(py::Int(5) * 1.0,                5.0);
    EXPECT_EQ(py::Int(5) * -2.0,               -10.0);
    assertions::binary_multiply<py::Int, py::Float>::returns<py::Float>::valid();
    EXPECT_EQ(py::Int(5) * py::Float(0.0),     0.0);
    EXPECT_EQ(py::Int(5) * py::Float(1.0),     5.0);
    EXPECT_EQ(py::Int(5) * py::Float(-2.0),    -10.0);

    // py::Int * complex
    assertions::binary_multiply<py::Int, std::complex<double>>::returns<py::Complex>::valid();
    assertions::binary_multiply<py::Int, py::Complex>::returns<py::Complex>::valid();

    // py::Int * Decimal
    // assertions::binary_multiply<py::Int, py::Decimal>::returns<py::Decimal>::valid();

    // py::Int * T
    assertions::binary_multiply<py::Int, const char*>::invalid();
    assertions::binary_multiply<py::Int, std::string>::invalid();
    assertions::binary_multiply<py::Int, std::string_view>::invalid();
    assertions::binary_multiply<py::Int, py::Str>::invalid();

    // py::Int *= bool
    EXPECT_EQ(py::Int(5) *= false,              0);
    EXPECT_EQ(py::Int(5) *= true,               5);
    EXPECT_EQ(py::Int(5) *= py::Bool(false),    0);
    EXPECT_EQ(py::Int(5) *= py::Bool(true),     5);

    // py::Int *= int
    EXPECT_EQ(py::Int(5) *= 0,                  0);
    EXPECT_EQ(py::Int(5) *= 1,                  5);
    EXPECT_EQ(py::Int(5) *= -2,                 -10);
    EXPECT_EQ(py::Int(5) *= py::Int(0),         0);
    EXPECT_EQ(py::Int(5) *= py::Int(1),         5);
    EXPECT_EQ(py::Int(5) *= py::Int(-2),        -10);

    // py::Int *= T
    assertions::inplace_multiply<py::Int, double>::invalid();
    assertions::inplace_multiply<py::Int, py::Float>::invalid();
}


TEST(py_int, divide) {
    // py::Int / bool
    assertions::binary_divide<py::Int, bool>::returns<py::Float>::valid();
    EXPECT_THROW(   py::Int(5) / false,              py::error_already_set);
    EXPECT_EQ(      py::Int(5) / true,               5.0);
    assertions::binary_divide<py::Int, py::Bool>::returns<py::Float>::valid();
    EXPECT_THROW(   py::Int(5) / py::Bool(false),    py::error_already_set);
    EXPECT_EQ(      py::Int(5) / py::Bool(true),     5.0);

    // py::Int / int
    assertions::binary_divide<py::Int, int>::returns<py::Float>::valid();
    EXPECT_THROW(   py::Int(5) / 0,                  py::error_already_set);
    EXPECT_EQ(      py::Int(5) / 1,                  5.0);
    EXPECT_EQ(      py::Int(5) / -2,                 -2.5);
    assertions::binary_divide<py::Int, py::Int>::returns<py::Float>::valid();
    EXPECT_THROW(   py::Int(5) / py::Int(0),         py::error_already_set);
    EXPECT_EQ(      py::Int(5) / py::Int(1),         5.0);
    EXPECT_EQ(      py::Int(5) / py::Int(-2),        -2.5);

    // py::Int / float
    assertions::binary_divide<py::Int, double>::returns<py::Float>::valid();
    EXPECT_THROW(   py::Int(5) / 0.0,                py::error_already_set);
    EXPECT_EQ(      py::Int(5) / 1.0,                5.0);
    EXPECT_EQ(      py::Int(5) / -2.0,               -2.5);
    assertions::binary_divide<py::Int, py::Float>::returns<py::Float>::valid();
    EXPECT_THROW(   py::Int(5) / py::Float(0.0),     py::error_already_set);
    EXPECT_EQ(      py::Int(5) / py::Float(1.0),     5.0);
    EXPECT_EQ(      py::Int(5) / py::Float(-2.0),    -2.5);

    // py::Int / complex
    assertions::binary_divide<py::Int, std::complex<double>>::returns<py::Complex>::valid();
    assertions::binary_divide<py::Int, py::Complex>::returns<py::Complex>::valid();

    // py::Int / Decimal
    // assertions::binary_divide<py::Int, py::Decimal>::returns<py::Decimal>::valid();

    // py::Int / T
    assertions::binary_divide<py::Int, const char*>::invalid();
    assertions::binary_divide<py::Int, std::string>::invalid();
    assertions::binary_divide<py::Int, std::string_view>::invalid();
    assertions::binary_divide<py::Int, py::Str>::invalid();

    // py::Int /= T
    assertions::inplace_divide<py::Int, bool>::invalid();
    assertions::inplace_divide<py::Int, py::Bool>::invalid();
    assertions::inplace_divide<py::Int, int>::invalid();
    assertions::inplace_divide<py::Int, py::Int>::invalid();
    assertions::inplace_divide<py::Int, double>::invalid();
    assertions::inplace_divide<py::Int, py::Float>::invalid();
}


TEST(py_int, modulo) {
    // py::Int % bool
    assertions::binary_modulo<py::Int, bool>::returns<py::Int>::valid();
    EXPECT_THROW(   py::Int(5) % false,              py::error_already_set);
    EXPECT_EQ(      py::Int(5) % true,               0);
    assertions::binary_modulo<py::Int, py::Bool>::returns<py::Int>::valid();
    EXPECT_THROW(   py::Int(5) % py::Bool(false),    py::error_already_set);
    EXPECT_EQ(      py::Int(5) % py::Bool(true),     0);

    // py::Int % int
    assertions::binary_modulo<py::Int, int>::returns<py::Int>::valid();
    EXPECT_THROW(   py::Int(5) % 0,                  py::error_already_set);
    EXPECT_EQ(      py::Int(5) % 1,                  0);
    EXPECT_EQ(      py::Int(5) % -2,                 -1);
    assertions::binary_modulo<py::Int, py::Int>::returns<py::Int>::valid();
    EXPECT_THROW(   py::Int(5) % py::Int(0),         py::error_already_set);
    EXPECT_EQ(      py::Int(5) % py::Int(1),         0);
    EXPECT_EQ(      py::Int(5) % py::Int(-2),        -1);

    // py::Int % float
    assertions::binary_modulo<py::Int, double>::returns<py::Float>::valid();
    EXPECT_THROW(   py::Int(5) % 0.0,                py::error_already_set);
    EXPECT_EQ(      py::Int(5) % 1.0,                0.0);
    EXPECT_EQ(      py::Int(5) % -2.0,               -1.0);
    assertions::binary_modulo<py::Int, py::Float>::returns<py::Float>::valid();
    EXPECT_THROW(   py::Int(5) % py::Float(0.0),     py::error_already_set);
    EXPECT_EQ(      py::Int(5) % py::Float(1.0),     0.0);
    EXPECT_EQ(      py::Int(5) % py::Float(-2.0),    -1.0);

    // py::Int % complex
    assertions::binary_modulo<py::Int, std::complex<double>>::invalid();
    assertions::binary_modulo<py::Int, py::Complex>::invalid();

    // py::Int % Decimal
    // assertions::binary_modulo<py::Int, py::Decimal>::returns<py::Decimal>::valid();

    // py::Int % T
    assertions::binary_modulo<py::Int, const char*>::invalid();
    assertions::binary_modulo<py::Int, std::string>::invalid();
    assertions::binary_modulo<py::Int, std::string_view>::invalid();
    assertions::binary_modulo<py::Int, py::Str>::invalid();

    // py::Int %= T
    EXPECT_THROW(   py::Int(5) %= false,            py::error_already_set);
    EXPECT_EQ(      py::Int(5) %= true,             0);
    EXPECT_THROW(   py::Int(5) %= py::Bool(false),   py::error_already_set);
    EXPECT_EQ(      py::Int(5) %= py::Bool(true),    0);
    
    // py::Int %= int
    EXPECT_THROW(   py::Int(5) %= 0,                py::error_already_set);
    EXPECT_EQ(      py::Int(5) %= 1,                0);
    EXPECT_EQ(      py::Int(5) %= -2,               -1);
    EXPECT_THROW(   py::Int(5) %= py::Int(0),       py::error_already_set);
    EXPECT_EQ(      py::Int(5) %= py::Int(1),       0);
    EXPECT_EQ(      py::Int(5) %= py::Int(-2),      -1);

    // py::Int %= T
    assertions::inplace_modulo<py::Int, double>::invalid();
    assertions::inplace_modulo<py::Int, py::Float>::invalid();
}


TEST(py_int, left_shift) {
    // py::Int << bool
    assertions::left_shift<py::Int, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) << false,              5);
    EXPECT_EQ(py::Int(5) << true,               10);
    assertions::left_shift<py::Int, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) << py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) << py::Bool(true),     10);

    // py::Int << int
    assertions::left_shift<py::Int, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) << 0,                  5);
    EXPECT_EQ(py::Int(5) << 1,                  10);
    assertions::left_shift<py::Int, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) << py::Int(0),         5);
    EXPECT_EQ(py::Int(5) << py::Int(1),         10);

    // py::Int << T
    assertions::left_shift<py::Int, double>::invalid();
    assertions::left_shift<py::Int, py::Float>::invalid();

    // py::Int <<= bool
    EXPECT_EQ(py::Int(5) <<= false,              5);
    EXPECT_EQ(py::Int(5) <<= true,               10);
    EXPECT_EQ(py::Int(5) <<= py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) <<= py::Bool(true),     10);

    // py::Int <<= int
    EXPECT_EQ(py::Int(5) <<= 0,                  5);
    EXPECT_EQ(py::Int(5) <<= 1,                  10);
    EXPECT_EQ(py::Int(5) <<= py::Int(0),         5);
    EXPECT_EQ(py::Int(5) <<= py::Int(1),         10);

    // py::Int <<= T
    assertions::inplace_left_shift<py::Int, double>::invalid();
    assertions::inplace_left_shift<py::Int, py::Float>::invalid();
}


TEST(py_int, right_shift) {
    // py::Int >> bool
    assertions::right_shift<py::Int, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) >> false,              5);
    EXPECT_EQ(py::Int(5) >> true,               2);
    assertions::right_shift<py::Int, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) >> py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) >> py::Bool(true),     2);

    // py::Int >> int
    assertions::right_shift<py::Int, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) >> 0,                  5);
    EXPECT_EQ(py::Int(5) >> 1,                  2);
    assertions::right_shift<py::Int, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) >> py::Int(0),         5);
    EXPECT_EQ(py::Int(5) >> py::Int(1),         2);

    // py::Int >> T
    assertions::right_shift<py::Int, double>::invalid();
    assertions::right_shift<py::Int, py::Float>::invalid();

    // py::Int >>= bool
    EXPECT_EQ(py::Int(5) >>= false,              5);
    EXPECT_EQ(py::Int(5) >>= true,               2);
    EXPECT_EQ(py::Int(5) >>= py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) >>= py::Bool(true),     2);

    // py::Int >>= int
    EXPECT_EQ(py::Int(5) >>= 0,                  5);
    EXPECT_EQ(py::Int(5) >>= 1,                  2);
    EXPECT_EQ(py::Int(5) >>= py::Int(0),         5);
    EXPECT_EQ(py::Int(5) >>= py::Int(1),         2);

    // py::Int >>= T
    assertions::inplace_right_shift<py::Int, double>::invalid();
    assertions::inplace_right_shift<py::Int, py::Float>::invalid();
}


TEST(py_int, bitwise_and) {
    // py::Int & bool
    assertions::bitwise_and<py::Int, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) & false,              0);
    EXPECT_EQ(py::Int(5) & true,               1);
    assertions::bitwise_and<py::Int, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) & py::Bool(false),    0);
    EXPECT_EQ(py::Int(5) & py::Bool(true),     1);

    // py::Int & int
    assertions::bitwise_and<py::Int, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) & 0,                  0);
    EXPECT_EQ(py::Int(5) & 1,                  1);
    assertions::bitwise_and<py::Int, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) & py::Int(0),         0);
    EXPECT_EQ(py::Int(5) & py::Int(1),         1);

    // py::Int & T
    assertions::bitwise_and<py::Int, double>::invalid();
    assertions::bitwise_and<py::Int, py::Float>::invalid();

    // py::Int &= bool
    EXPECT_EQ(py::Int(5) &= false,              0);
    EXPECT_EQ(py::Int(5) &= true,               1);
    EXPECT_EQ(py::Int(5) &= py::Bool(false),    0);
    EXPECT_EQ(py::Int(5) &= py::Bool(true),     1);

    // py::Int &= int
    EXPECT_EQ(py::Int(5) &= 0,                  0);
    EXPECT_EQ(py::Int(5) &= 1,                  1);
    EXPECT_EQ(py::Int(5) &= py::Int(0),         0);
    EXPECT_EQ(py::Int(5) &= py::Int(1),         1);

    // py::Int &= T
    assertions::inplace_bitwise_and<py::Int, double>::invalid();
    assertions::inplace_bitwise_and<py::Int, py::Float>::invalid();
}


TEST(py_int, bitwise_or) {
    // py::Int | bool
    assertions::bitwise_or<py::Int, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(4) | false,              4);
    EXPECT_EQ(py::Int(4) | true,               5);
    assertions::bitwise_or<py::Int, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(4) | py::Bool(false),    4);
    EXPECT_EQ(py::Int(4) | py::Bool(true),     5);

    // py::Int | int
    assertions::bitwise_or<py::Int, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(4) | 0,                  4);
    EXPECT_EQ(py::Int(4) | 1,                  5);
    assertions::bitwise_or<py::Int, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(4) | py::Int(0),         4);
    EXPECT_EQ(py::Int(4) | py::Int(1),         5);

    // py::Int | T
    assertions::bitwise_or<py::Int, double>::invalid();
    assertions::bitwise_or<py::Int, py::Float>::invalid();

    // py::Int |= bool
    EXPECT_EQ(py::Int(4) |= false,              4);
    EXPECT_EQ(py::Int(4) |= true,               5);
    EXPECT_EQ(py::Int(4) |= py::Bool(false),    4);
    EXPECT_EQ(py::Int(4) |= py::Bool(true),     5);

    // py::Int |= int
    EXPECT_EQ(py::Int(4) |= 0,                  4);
    EXPECT_EQ(py::Int(4) |= 1,                  5);
    EXPECT_EQ(py::Int(4) |= py::Int(0),         4);
    EXPECT_EQ(py::Int(4) |= py::Int(1),         5);

    // py::Int |= T
    assertions::inplace_bitwise_or<py::Int, double>::invalid();
    assertions::inplace_bitwise_or<py::Int, py::Float>::invalid();
}


TEST(py_int, bitwise_xor) {
    // py::Int ^ bool
    assertions::bitwise_xor<py::Int, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) ^ false,              5);
    EXPECT_EQ(py::Int(5) ^ true,               4);
    assertions::bitwise_xor<py::Int, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) ^ py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) ^ py::Bool(true),     4);

    // py::Int ^ int
    assertions::bitwise_xor<py::Int, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) ^ 0,                  5);
    EXPECT_EQ(py::Int(5) ^ 1,                  4);
    assertions::bitwise_xor<py::Int, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Int(5) ^ py::Int(0),         5);
    EXPECT_EQ(py::Int(5) ^ py::Int(1),         4);

    // py::Int ^ T
    assertions::bitwise_xor<py::Int, double>::invalid();
    assertions::bitwise_xor<py::Int, py::Float>::invalid();

    // py::Int ^= bool
    EXPECT_EQ(py::Int(5) ^= false,              5);
    EXPECT_EQ(py::Int(5) ^= true,               4);
    EXPECT_EQ(py::Int(5) ^= py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) ^= py::Bool(true),     4);

    // py::Int ^= int
    EXPECT_EQ(py::Int(5) ^= 0,                  5);
    EXPECT_EQ(py::Int(5) ^= 1,                  4);
    EXPECT_EQ(py::Int(5) ^= py::Int(0),         5);
    EXPECT_EQ(py::Int(5) ^= py::Int(1),         4);

    // py::Int ^= T
    assertions::inplace_bitwise_xor<py::Int, double>::invalid();
    assertions::inplace_bitwise_xor<py::Int, py::Float>::invalid();
}
