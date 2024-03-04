#include "../common.h"
#include <bertrand/python.h>
#include <cstdint>

namespace py = bertrand::py;


/////////////////////
////    TYPES    ////
/////////////////////


TEST(py, int_type) {
    py::Type expected = py::reinterpret_borrow<py::Type>((PyObject*) &PyLong_Type);
    EXPECT_EQ(py::Int::type, expected);
}


TEST(py, int_check) {
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


TEST(py, int_is_implicitly_convertible_from_object) {
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
    EXPECT_THROW(py::Int d2 = d, py::TypeError);
    EXPECT_THROW(py::Int e2 = e, py::TypeError);
}


TEST(py, int_is_implicitly_convertible_from_accessor) {
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

    py::List list3 = {0.0, 1.0};
    EXPECT_THROW(py::Int e = list3[0], py::TypeError);
    EXPECT_THROW(py::Int f = list3[1], py::TypeError);
}


TEST(py, int_is_copy_constructible) {
    py::Int a = 1;
    int a_refs = a.ref_count();
    py::Int b(a);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(b.ref_count(), a_refs + 1);

    py::Int c = -5;
    int c_refs = c.ref_count();
    py::Int d(c);
    EXPECT_EQ(d, -5);
    EXPECT_EQ(d.ref_count(), c_refs + 1);
}


TEST(py, int_is_move_constructible) {
    py::Int a = 1;
    int a_refs = a.ref_count();
    py::Int b(std::move(a));
    EXPECT_EQ(b, 1);
    EXPECT_EQ(b.ref_count(), a_refs);

    py::Int c = -5;
    int c_refs = c.ref_count();
    py::Int d(std::move(c));
    EXPECT_EQ(d, -5);
    EXPECT_EQ(d.ref_count(), c_refs);
}


TEST(py, int_is_default_constructible) {
    py::Int a;
    EXPECT_EQ(a, 0);
}


TEST(py, int_is_constructible_from_bool) {
    EXPECT_EQ(py::Int(true), 1);
    EXPECT_EQ(py::Int(false), 0);
    EXPECT_EQ(py::Int(py::Bool(true)), 1);
    EXPECT_EQ(py::Int(py::Bool(false)), 0);
}


TEST(py, int_is_constructible_from_int) {
    EXPECT_EQ(py::Int(0), 0);
    EXPECT_EQ(py::Int(1), 1);
    EXPECT_EQ(py::Int(-1), -1);
    EXPECT_EQ(py::Int(py::Int(0)), 0);
    EXPECT_EQ(py::Int(py::Int(1)), 1);
    EXPECT_EQ(py::Int(py::Int(-1)), -1);

    unsigned int a = 0;
    unsigned int b = 1;
    EXPECT_EQ(py::Int(a), 0);
    EXPECT_EQ(py::Int(b), 1);
}


/////////////////////////////////////////////
////    TYPE-SAFE ASSIGNMENT OPERATOR    ////
/////////////////////////////////////////////


TEST(py, int_is_copy_assignable) {
    py::Int a = 1;
    py::Int b = 2;
    int a_refs = a.ref_count();
    b = a;
    EXPECT_EQ(b, 1);
    EXPECT_EQ(b.ref_count(), a_refs + 1);
}


TEST(py, int_is_move_assignable) {
    py::Int a = 1;
    py::Int b = 2;
    int a_refs = a.ref_count();
    b = std::move(a);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(b.ref_count(), a_refs);
}


TEST(py, int_is_assignable_from_bool) {
    py::Int a = 1;
    a = true;
    EXPECT_EQ(a, 1);
    a = false;
    EXPECT_EQ(a, 0);
}


TEST(py, int_is_assignable_from_int) {
    py::Int a = 1;
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


TEST(py, int_is_not_assignable_from_float) {
    bool int_is_assignable_from_float = assertions::is_assignable<py::Int, double>;
    bool int_is_assignable_from_py_float = assertions::is_assignable<py::Int, py::Float>;
    EXPECT_FALSE(int_is_assignable_from_float);
    EXPECT_FALSE(int_is_assignable_from_py_float);
}


TEST(py, int_is_not_assignable_from_string) {
    bool int_is_assignable_from_string = assertions::is_assignable<py::Int, std::string>;
    bool int_is_assignable_from_string_view = assertions::is_assignable<py::Int, std::string_view>;
    bool int_is_assignable_from_py_str = assertions::is_assignable<py::Int, py::Str>;
    EXPECT_FALSE(int_is_assignable_from_string);
    EXPECT_FALSE(int_is_assignable_from_string_view);
    EXPECT_FALSE(int_is_assignable_from_py_str);
}


/////////////////////////////////////
////    EXPLICIT CONSTRUCTORS    ////
/////////////////////////////////////


TEST(py, int_is_constructible_from_float) {
    EXPECT_EQ(py::Int(0.0), 0);
    EXPECT_EQ(py::Int(1.0), 1);
    EXPECT_EQ(py::Int(-1.0), -1);
    EXPECT_EQ(py::Int(py::Float(0.0)), 0);
    EXPECT_EQ(py::Int(py::Float(1.0)), 1);
    EXPECT_EQ(py::Int(py::Float(-1.0)), -1);
}


TEST(py, int_is_constructible_from_string) {
    // without base
    EXPECT_EQ(py::Int("0"), 0);
    EXPECT_EQ(py::Int("1"), 1);
    EXPECT_EQ(py::Int("-1"), -1);
    EXPECT_EQ(py::Int("0b101"), 5);
    EXPECT_EQ(py::Int("-0b101"), -5);
    EXPECT_EQ(py::Int("0o10"), 8);
    EXPECT_EQ(py::Int("-0o10"), -8);
    EXPECT_EQ(py::Int("0x10"), 16);
    EXPECT_EQ(py::Int("-0x10"), -16);

    EXPECT_EQ(py::Int(std::string("0")), 0);
    EXPECT_EQ(py::Int(std::string("1")), 1);
    EXPECT_EQ(py::Int(std::string("-1")), -1);
    EXPECT_EQ(py::Int(std::string("0b101")), 5);
    EXPECT_EQ(py::Int(std::string("-0b101")), -5);
    EXPECT_EQ(py::Int(std::string("0o10")), 8);
    EXPECT_EQ(py::Int(std::string("-0o10")), -8);
    EXPECT_EQ(py::Int(std::string("0x10")), 16);
    EXPECT_EQ(py::Int(std::string("-0x10")), -16);

    EXPECT_EQ(py::Int(std::string_view("0")), 0);
    EXPECT_EQ(py::Int(std::string_view("1")), 1);
    EXPECT_EQ(py::Int(std::string_view("-1")), -1);
    EXPECT_EQ(py::Int(std::string_view("0b101")), 5);
    EXPECT_EQ(py::Int(std::string_view("-0b101")), -5);
    EXPECT_EQ(py::Int(std::string_view("0o10")), 8);
    EXPECT_EQ(py::Int(std::string_view("-0o10")), -8);
    EXPECT_EQ(py::Int(std::string_view("0x10")), 16);
    EXPECT_EQ(py::Int(std::string_view("-0x10")), -16);

    EXPECT_EQ(py::Int(py::Str("0")), 0);
    EXPECT_EQ(py::Int(py::Str("1")), 1);
    EXPECT_EQ(py::Int(py::Str("-1")), -1);
    EXPECT_EQ(py::Int(py::Str("0b101")), 5);
    EXPECT_EQ(py::Int(py::Str("-0b101")), -5);
    EXPECT_EQ(py::Int(py::Str("0o10")), 8);
    EXPECT_EQ(py::Int(py::Str("-0o10")), -8);
    EXPECT_EQ(py::Int(py::Str("0x10")), 16);
    EXPECT_EQ(py::Int(py::Str("-0x10")), -16);

    // with base
    EXPECT_EQ(py::Int("0", 10), 0);
    EXPECT_EQ(py::Int("1", 10), 1);
    EXPECT_EQ(py::Int("-1", 10), -1);
    EXPECT_EQ(py::Int("0b101", 2), 5);
    EXPECT_EQ(py::Int("-0b101", 2), -5);
    EXPECT_EQ(py::Int("0o10", 8), 8);
    EXPECT_EQ(py::Int("-0o10", 8), -8);
    EXPECT_EQ(py::Int("0x10", 16), 16);
    EXPECT_EQ(py::Int("-0x10", 16), -16);

    EXPECT_EQ(py::Int(std::string("0"), 10), 0);
    EXPECT_EQ(py::Int(std::string("1"), 10), 1);
    EXPECT_EQ(py::Int(std::string("-1"), 10), -1);
    EXPECT_EQ(py::Int(std::string("0b101"), 2), 5);
    EXPECT_EQ(py::Int(std::string("-0b101"), 2), -5);
    EXPECT_EQ(py::Int(std::string("0o10"), 8), 8);
    EXPECT_EQ(py::Int(std::string("-0o10"), 8), -8);
    EXPECT_EQ(py::Int(std::string("0x10"), 16), 16);
    EXPECT_EQ(py::Int(std::string("-0x10"), 16), -16);

    EXPECT_EQ(py::Int(std::string_view("0"), 10), 0);
    EXPECT_EQ(py::Int(std::string_view("1"), 10), 1);
    EXPECT_EQ(py::Int(std::string_view("-1"), 10), -1);
    EXPECT_EQ(py::Int(std::string_view("0b101"), 2), 5);
    EXPECT_EQ(py::Int(std::string_view("-0b101"), 2), -5);
    EXPECT_EQ(py::Int(std::string_view("0o10"), 8), 8);
    EXPECT_EQ(py::Int(std::string_view("-0o10"), 8), -8);
    EXPECT_EQ(py::Int(std::string_view("0x10"), 16), 16);
    EXPECT_EQ(py::Int(std::string_view("-0x10"), 16), -16);

    EXPECT_EQ(py::Int(py::Str("0"), 10), 0);
    EXPECT_EQ(py::Int(py::Str("1"), 10), 1);
    EXPECT_EQ(py::Int(py::Str("-1"), 10), -1);
    EXPECT_EQ(py::Int(py::Str("0b101"), 2), 5);
    EXPECT_EQ(py::Int(py::Str("-0b101"), 2), -5);
    EXPECT_EQ(py::Int(py::Str("0o10"), 8), 8);
    EXPECT_EQ(py::Int(py::Str("-0o10"), 8), -8);
    EXPECT_EQ(py::Int(py::Str("0x10"), 16), 16);
    EXPECT_EQ(py::Int(py::Str("-0x10"), 16), -16);
}


TEST(py, int_is_constructible_from_custom_struct) {
    // uint64_t
    struct ImplicitUInt64_T { operator uint64_t() const { return 1; } };
    struct ExplicitUInt64_T { explicit operator uint64_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUInt64_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitUInt64_T{}), 1);

    // unsigned long long
    struct ImplicitUnsignedLongLong { operator unsigned long long() const { return 1; } };
    struct ExplicitUnsignedLongLong { explicit operator unsigned long long() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUnsignedLongLong{}), 1);
    EXPECT_EQ(py::Int(ExplicitUnsignedLongLong{}), 1);

    // int64_t
    struct ImplicitInt64_T { operator int64_t() const { return 1; } };
    struct ExplicitInt64_T { explicit operator int64_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitInt64_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitInt64_T{}), 1);

    // long long
    struct ImplicitLongLong { operator long long() const { return 1; } };
    struct ExplicitLongLong { explicit operator long long() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitLongLong{}), 1);
    EXPECT_EQ(py::Int(ExplicitLongLong{}), 1);

    // uint32_t
    struct ImplicitUInt32_T { operator uint32_t() const { return 1; } };
    struct ExplicitUInt32_T { explicit operator uint32_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUInt32_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitUInt32_T{}), 1);

    // unsigned long
    struct ImplicitUnsignedLong { operator unsigned long() const { return 1; } };
    struct ExplicitUnsignedLong { explicit operator unsigned long() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUnsignedLong{}), 1);
    EXPECT_EQ(py::Int(ExplicitUnsignedLong{}), 1);

    // unsigned int
    struct ImplicitUnsignedInt { operator unsigned int() const { return 1; } };
    struct ExplicitUnsignedInt { explicit operator unsigned int() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUnsignedInt{}), 1);
    EXPECT_EQ(py::Int(ExplicitUnsignedInt{}), 1);

    // int32_t
    struct ImplicitInt32_T { operator int32_t() const { return 1; } };
    struct ExplicitInt32_T { explicit operator int32_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitInt32_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitInt32_T{}), 1);

    // long
    struct ImplicitLong { operator long() const { return 1; } };
    struct ExplicitLong { explicit operator long() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitLong{}), 1);
    EXPECT_EQ(py::Int(ExplicitLong{}), 1);

    // int
    struct ImplicitInt { operator int() const { return 1; } };
    struct ExplicitInt { explicit operator int() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitInt{}), 1);
    EXPECT_EQ(py::Int(ExplicitInt{}), 1);

    // uint16_t
    struct ImplicitUInt16_T { operator uint16_t() const { return 1; } };
    struct ExplicitUInt16_T { explicit operator uint16_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUInt16_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitUInt16_T{}), 1);

    // unsigned short
    struct ImplicitUnsignedShort { operator unsigned short() const { return 1; } };
    struct ExplicitUnsignedShort { explicit operator unsigned short() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUnsignedShort{}), 1);
    EXPECT_EQ(py::Int(ExplicitUnsignedShort{}), 1);

    // int16_t
    struct ImplicitInt16_T { operator int16_t() const { return 1; } };
    struct ExplicitInt16_T { explicit operator int16_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitInt16_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitInt16_T{}), 1);

    // short
    struct ImplicitShort { operator short() const { return 1; } };
    struct ExplicitShort { explicit operator short() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitShort{}), 1);
    EXPECT_EQ(py::Int(ExplicitShort{}), 1);

    // uint8_t
    struct ImplicitUInt8_T { operator uint8_t() const { return 1; } };
    struct ExplicitUInt8_T { explicit operator uint8_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUInt8_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitUInt8_T{}), 1);

    // unsigned char
    struct ImplicitUnsignedChar { operator unsigned char() const { return 1; } };
    struct ExplicitUnsignedChar { explicit operator unsigned char() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitUnsignedChar{}), 1);
    EXPECT_EQ(py::Int(ExplicitUnsignedChar{}), 1);

    // int8_t
    struct ImplicitInt8_T { operator int8_t() const { return 1; } };
    struct ExplicitInt8_T { explicit operator int8_t() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitInt8_T{}), 1);
    EXPECT_EQ(py::Int(ExplicitInt8_T{}), 1);

    // char
    struct ImplicitChar { operator char() const { return 1; } };
    struct ExplicitChar { explicit operator char() const { return 1; } };
    EXPECT_EQ(py::Int(ImplicitChar{}), 1);
    EXPECT_EQ(py::Int(ExplicitChar{}), 1);

    // bool
    struct ImplicitBool { operator bool() const { return true; } };
    struct ExplicitBool { explicit operator bool() const { return true; } };
    EXPECT_EQ(py::Int(ImplicitBool{}), 1);
    EXPECT_EQ(py::Int(ExplicitBool{}), 1);

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
}


TEST(py, int_is_constructible_from_python_object) {
    py::Type Foo("Foo");
    Foo.attr("__index__") = py::Method([](const py::Object& self) { return 42; });
    py::Object foo = Foo();
    EXPECT_EQ(py::Int(foo), 42);
}


///////////////////////////
////    CONVERSIONS    ////
///////////////////////////


TEST(py, int_is_implicitly_convertible_to_bool) {
    bool a = py::Int(0);
    bool b = py::Int(1);
    bool c = py::Int(-1);
    EXPECT_EQ(a, false);
    EXPECT_EQ(b, true);
    EXPECT_EQ(c, true);
}


TEST(py, int_is_implicitly_convertible_to_int) {
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

    py::Int g = py::Int(0);
    py::Int h = py::Int(1);
    py::Int i = py::Int(-1);
    EXPECT_EQ(g, 0);
    EXPECT_EQ(h, 1);
    EXPECT_EQ(i, -1);
}


TEST(py, int_is_implicitly_convertible_to_float) {
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
}


TEST(py, int_is_explicitly_convertible_to_string) {
    EXPECT_EQ(static_cast<std::string>(py::Int(0)), "0");
    EXPECT_EQ(static_cast<std::string>(py::Int(1)), "1");
    EXPECT_EQ(static_cast<std::string>(py::Int(-1)), "-1");
}


///////////////////////////////
////    BASIC OPERATORS    ////
///////////////////////////////


TEST(py, int_is_not_indexable) {
    bool int_is_indexable = assertions::is_indexable<py::Int>;
    EXPECT_FALSE(int_is_indexable);
}


TEST(py, int_is_not_iterable) {
    bool int_is_iterable = assertions::is_iterable<py::Int>;
    EXPECT_FALSE(int_is_iterable);
}


TEST(py, int_is_not_callable) {
    bool int_is_callable = assertions::is_callable<py::Int>;
    EXPECT_FALSE(int_is_callable);
}


TEST(py, int_is_hashable) {
    std::hash<py::Int> hash;
    EXPECT_EQ(hash(py::Int(0)), hash(py::Int(0)));
    EXPECT_EQ(hash(py::Int(1)), hash(py::Int(1)));
    EXPECT_EQ(hash(py::Int(-1)), hash(py::Int(-1)));
    EXPECT_NE(hash(py::Int(0)), hash(py::Int(1)));
    EXPECT_NE(hash(py::Int(0)), hash(py::Int(-1)));
    EXPECT_NE(hash(py::Int(1)), hash(py::Int(-1)));
}


TEST(py, int_unary_invert) {
    EXPECT_EQ(~py::Int(0), -1);
    EXPECT_EQ(~py::Int(1), -2);
    EXPECT_EQ(~py::Int(-1), 0);
}


TEST(py, int_unary_plus) {
    EXPECT_EQ(+py::Int(0), 0);
    EXPECT_EQ(+py::Int(1), 1);
    EXPECT_EQ(+py::Int(-1), -1);
}


TEST(py, int_unary_minus) {
    EXPECT_EQ(-py::Int(0), 0);
    EXPECT_EQ(-py::Int(1), -1);
    EXPECT_EQ(-py::Int(-1), 1);
}


///////////////////////////
////    COMPARISONS    ////
///////////////////////////


TEST(py, int_lt) {
    EXPECT_EQ(py::Int(0) < false,               false);
    EXPECT_EQ(py::Int(0) < true,                true);
    EXPECT_EQ(py::Int(0) < 0,                   false);
    EXPECT_EQ(py::Int(0) < 1,                   true);
    EXPECT_EQ(py::Int(0) < -1,                  false);
    EXPECT_EQ(py::Int(0) < 0.0,                 false);
    EXPECT_EQ(py::Int(0) < 1.0,                 true);
    EXPECT_EQ(py::Int(0) < -1.0,                false);
    EXPECT_EQ(py::Int(0) < py::Bool(false),     false);
    EXPECT_EQ(py::Int(0) < py::Bool(true),      true);
    EXPECT_EQ(py::Int(0) < py::Int(0),          false);
    EXPECT_EQ(py::Int(0) < py::Int(1),          true);
    EXPECT_EQ(py::Int(0) < py::Int(-1),         false);
    EXPECT_EQ(py::Int(0) < py::Float(0.0),      false);
    EXPECT_EQ(py::Int(0) < py::Float(1.0),      true);
    EXPECT_EQ(py::Int(0) < py::Float(-1.0),     false);
}


TEST(py, int_le) {
    EXPECT_EQ(py::Int(0) <= false,              true);
    EXPECT_EQ(py::Int(0) <= true,               true);
    EXPECT_EQ(py::Int(0) <= 0,                  true);
    EXPECT_EQ(py::Int(0) <= 1,                  true);
    EXPECT_EQ(py::Int(0) <= -1,                 false);
    EXPECT_EQ(py::Int(0) <= 0.0,                true);
    EXPECT_EQ(py::Int(0) <= 1.0,                true);
    EXPECT_EQ(py::Int(0) <= -1.0,               false);
    EXPECT_EQ(py::Int(0) <= py::Bool(false),    true);
    EXPECT_EQ(py::Int(0) <= py::Bool(true),     true);
    EXPECT_EQ(py::Int(0) <= py::Int(0),         true);
    EXPECT_EQ(py::Int(0) <= py::Int(1),         true);
    EXPECT_EQ(py::Int(0) <= py::Int(-1),        false);
    EXPECT_EQ(py::Int(0) <= py::Float(0.0),     true);
    EXPECT_EQ(py::Int(0) <= py::Float(1.0),     true);
    EXPECT_EQ(py::Int(0) <= py::Float(-1.0),    false);
}


TEST(py, int_eq) {
    EXPECT_EQ(py::Int(0) == false,              true);
    EXPECT_EQ(py::Int(0) == true,               false);
    EXPECT_EQ(py::Int(0) == 0,                  true);
    EXPECT_EQ(py::Int(0) == 1,                  false);
    EXPECT_EQ(py::Int(0) == -1,                 false);
    EXPECT_EQ(py::Int(0) == 0.0,                true);
    EXPECT_EQ(py::Int(0) == 1.0,                false);
    EXPECT_EQ(py::Int(0) == -1.0,               false);
    EXPECT_EQ(py::Int(0) == py::Bool(false),    true);
    EXPECT_EQ(py::Int(0) == py::Bool(true),     false);
    EXPECT_EQ(py::Int(0) == py::Int(0),         true);
    EXPECT_EQ(py::Int(0) == py::Int(1),         false);
    EXPECT_EQ(py::Int(0) == py::Int(-1),        false);
    EXPECT_EQ(py::Int(0) == py::Float(0.0),     true);
    EXPECT_EQ(py::Int(0) == py::Float(1.0),     false);
    EXPECT_EQ(py::Int(0) == py::Float(-1.0),    false);
}


TEST(py, int_ne) {
    EXPECT_EQ(py::Int(0) != false,              false);
    EXPECT_EQ(py::Int(0) != true,               true);
    EXPECT_EQ(py::Int(0) != 0,                  false);
    EXPECT_EQ(py::Int(0) != 1,                  true);
    EXPECT_EQ(py::Int(0) != -1,                 true);
    EXPECT_EQ(py::Int(0) != 0.0,                false);
    EXPECT_EQ(py::Int(0) != 1.0,                true);
    EXPECT_EQ(py::Int(0) != -1.0,               true);
    EXPECT_EQ(py::Int(0) != py::Bool(false),    false);
    EXPECT_EQ(py::Int(0) != py::Bool(true),     true);
    EXPECT_EQ(py::Int(0) != py::Int(0),         false);
    EXPECT_EQ(py::Int(0) != py::Int(1),         true);
    EXPECT_EQ(py::Int(0) != py::Int(-1),        true);
    EXPECT_EQ(py::Int(0) != py::Float(0.0),     false);
    EXPECT_EQ(py::Int(0) != py::Float(1.0),     true);
    EXPECT_EQ(py::Int(0) != py::Float(-1.0),    true);
}


TEST(py, int_ge) {
    EXPECT_EQ(py::Int(0) >= false,              true);
    EXPECT_EQ(py::Int(0) >= true,               false);
    EXPECT_EQ(py::Int(0) >= 0,                  true);
    EXPECT_EQ(py::Int(0) >= 1,                  false);
    EXPECT_EQ(py::Int(0) >= -1,                 true);
    EXPECT_EQ(py::Int(0) >= 0.0,                true);
    EXPECT_EQ(py::Int(0) >= 1.0,                false);
    EXPECT_EQ(py::Int(0) >= -1.0,               true);
    EXPECT_EQ(py::Int(0) >= py::Bool(false),    true);
    EXPECT_EQ(py::Int(0) >= py::Bool(true),     false);
    EXPECT_EQ(py::Int(0) >= py::Int(0),         true);
    EXPECT_EQ(py::Int(0) >= py::Int(1),         false);
    EXPECT_EQ(py::Int(0) >= py::Int(-1),        true);
    EXPECT_EQ(py::Int(0) >= py::Float(0.0),     true);
    EXPECT_EQ(py::Int(0) >= py::Float(1.0),     false);
    EXPECT_EQ(py::Int(0) >= py::Float(-1.0),    true);
}


TEST(py, int_gt) {
    EXPECT_EQ(py::Int(0) > false,               false);
    EXPECT_EQ(py::Int(0) > true,                false);
    EXPECT_EQ(py::Int(0) > 0,                   false);
    EXPECT_EQ(py::Int(0) > 1,                   false);
    EXPECT_EQ(py::Int(0) > -1,                  true);
    EXPECT_EQ(py::Int(0) > 0.0,                 false);
    EXPECT_EQ(py::Int(0) > 1.0,                 false);
    EXPECT_EQ(py::Int(0) > -1.0,                true);
    EXPECT_EQ(py::Int(0) > py::Bool(false),     false);
    EXPECT_EQ(py::Int(0) > py::Bool(true),      false);
    EXPECT_EQ(py::Int(0) > py::Int(0),          false);
    EXPECT_EQ(py::Int(0) > py::Int(1),          false);
    EXPECT_EQ(py::Int(0) > py::Int(-1),         true);
    EXPECT_EQ(py::Int(0) > py::Float(0.0),      false);
    EXPECT_EQ(py::Int(0) > py::Float(1.0),      false);
    EXPECT_EQ(py::Int(0) > py::Float(-1.0),     true);
}


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


TEST(py, int_addition) {
    EXPECT_EQ(py::Int(5) + false,              5);
    EXPECT_EQ(py::Int(5) + true,               6);
    EXPECT_EQ(py::Int(5) + 0,                  5);
    EXPECT_EQ(py::Int(5) + 1,                  6);
    EXPECT_EQ(py::Int(5) + -1,                 4);
    EXPECT_EQ(py::Int(5) + 0.0,                5.0);
    EXPECT_EQ(py::Int(5) + 1.0,                6.0);
    EXPECT_EQ(py::Int(5) + -1.0,               4.0);
    EXPECT_EQ(py::Int(5) + py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) + py::Bool(true),     6);
    EXPECT_EQ(py::Int(5) + py::Int(0),         5);
    EXPECT_EQ(py::Int(5) + py::Int(1),         6);
    EXPECT_EQ(py::Int(5) + py::Int(-1),        4);
    EXPECT_EQ(py::Int(5) + py::Float(0.0),     5.0);
    EXPECT_EQ(py::Int(5) + py::Float(1.0),     6.0);
    EXPECT_EQ(py::Int(5) + py::Float(-1.0),    4.0);
}


TEST(py, int_subtraction) {
    EXPECT_EQ(py::Int(5) - false,              5);
    EXPECT_EQ(py::Int(5) - true,               4);
    EXPECT_EQ(py::Int(5) - 0,                  5);
    EXPECT_EQ(py::Int(5) - 1,                  4);
    EXPECT_EQ(py::Int(5) - -1,                 6);
    EXPECT_EQ(py::Int(5) - 0.0,                5.0);
    EXPECT_EQ(py::Int(5) - 1.0,                4.0);
    EXPECT_EQ(py::Int(5) - -1.0,               6.0);
    EXPECT_EQ(py::Int(5) - py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) - py::Bool(true),     4);
    EXPECT_EQ(py::Int(5) - py::Int(0),         5);
    EXPECT_EQ(py::Int(5) - py::Int(1),         4);
    EXPECT_EQ(py::Int(5) - py::Int(-1),        6);
    EXPECT_EQ(py::Int(5) - py::Float(0.0),     5.0);
    EXPECT_EQ(py::Int(5) - py::Float(1.0),     4.0);
    EXPECT_EQ(py::Int(5) - py::Float(-1.0),    6.0);
}


TEST(py, int_multiplication) {
    EXPECT_EQ(py::Int(5) * false,              0);
    EXPECT_EQ(py::Int(5) * true,               5);
    EXPECT_EQ(py::Int(5) * 0,                  0);
    EXPECT_EQ(py::Int(5) * 1,                  5);
    EXPECT_EQ(py::Int(5) * -2,                 -10);
    EXPECT_EQ(py::Int(5) * 0.0,                0.0);
    EXPECT_EQ(py::Int(5) * 1.0,                5.0);
    EXPECT_EQ(py::Int(5) * -2.0,               -10.0);
    EXPECT_EQ(py::Int(5) * py::Bool(false),    0);
    EXPECT_EQ(py::Int(5) * py::Bool(true),     5);
    EXPECT_EQ(py::Int(5) * py::Int(0),         0);
    EXPECT_EQ(py::Int(5) * py::Int(1),         5);
    EXPECT_EQ(py::Int(5) * py::Int(-2),        -10);
    EXPECT_EQ(py::Int(5) * py::Float(0.0),     0.0);
    EXPECT_EQ(py::Int(5) * py::Float(1.0),     5.0);
    EXPECT_EQ(py::Int(5) * py::Float(-2.0),    -10.0);
}


TEST(py, int_division) {
    EXPECT_THROW(   py::Int(5) / false,              py::error_already_set);
    EXPECT_EQ(      py::Int(5) / true,               5.0);
    EXPECT_THROW(   py::Int(5) / 0,                  py::error_already_set);
    EXPECT_EQ(      py::Int(5) / 1,                  5.0);
    EXPECT_EQ(      py::Int(5) / -2,                 -2.5);
    EXPECT_THROW(   py::Int(5) / 0.0,                py::error_already_set);
    EXPECT_EQ(      py::Int(5) / 1.0,                5.0);
    EXPECT_EQ(      py::Int(5) / -2.0,               -2.5);
    EXPECT_THROW(   py::Int(5) / py::Bool(false),    py::error_already_set);
    EXPECT_EQ(      py::Int(5) / py::Bool(true),     5.0);
    EXPECT_THROW(   py::Int(5) / py::Int(0),         py::error_already_set);
    EXPECT_EQ(      py::Int(5) / py::Int(1),         5.0);
    EXPECT_EQ(      py::Int(5) / py::Int(-2),        -2.5);
    EXPECT_THROW(   py::Int(5) / py::Float(0.0),     py::error_already_set);
    EXPECT_EQ(      py::Int(5) / py::Float(1.0),     5.0);
    EXPECT_EQ(      py::Int(5) / py::Float(-2.0),    -2.5);
}


TEST(py, int_modulus) {
    EXPECT_THROW(   py::Int(5) % false,              py::error_already_set);
    EXPECT_EQ(      py::Int(5) % true,               0);
    EXPECT_THROW(   py::Int(5) % 0,                  py::error_already_set);
    EXPECT_EQ(      py::Int(5) % 1,                  0);
    EXPECT_EQ(      py::Int(5) % -2,                 -1);
    EXPECT_THROW(   py::Int(5) % 0.0,                py::error_already_set);
    EXPECT_EQ(      py::Int(5) % 1.0,                0.0);
    EXPECT_EQ(      py::Int(5) % -2.0,               -1.0);
    EXPECT_THROW(   py::Int(5) % py::Bool(false),    py::error_already_set);
    EXPECT_EQ(      py::Int(5) % py::Bool(true),     0);
    EXPECT_THROW(   py::Int(5) % py::Int(0),         py::error_already_set);
    EXPECT_EQ(      py::Int(5) % py::Int(1),         0);
    EXPECT_EQ(      py::Int(5) % py::Int(-2),        -1);
    EXPECT_THROW(   py::Int(5) % py::Float(0.0),     py::error_already_set);
    EXPECT_EQ(      py::Int(5) % py::Float(1.0),     0.0);
    EXPECT_EQ(      py::Int(5) % py::Float(-2.0),    -1.0);
}


TEST(py, int_left_shift) {
    EXPECT_EQ(py::Int(5) << false,              5);
    EXPECT_EQ(py::Int(5) << true,               10);
    EXPECT_EQ(py::Int(5) << 0,                  5);
    EXPECT_EQ(py::Int(5) << 1,                  10);
    EXPECT_EQ(py::Int(5) << py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) << py::Bool(true),     10);
    EXPECT_EQ(py::Int(5) << py::Int(0),         5);
    EXPECT_EQ(py::Int(5) << py::Int(1),         10);
}


TEST(py, int_right_shift) {
    EXPECT_EQ(py::Int(5) >> false,              5);
    EXPECT_EQ(py::Int(5) >> true,               2);
    EXPECT_EQ(py::Int(5) >> 0,                  5);
    EXPECT_EQ(py::Int(5) >> 1,                  2);
    EXPECT_EQ(py::Int(5) >> py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) >> py::Bool(true),     2);
    EXPECT_EQ(py::Int(5) >> py::Int(0),         5);
    EXPECT_EQ(py::Int(5) >> py::Int(1),         2);
}


TEST(py, int_bitwise_and) {
    EXPECT_EQ(py::Int(5) & false,              0);
    EXPECT_EQ(py::Int(5) & true,               1);
    EXPECT_EQ(py::Int(5) & 0,                  0);
    EXPECT_EQ(py::Int(5) & 1,                  1);
    EXPECT_EQ(py::Int(5) & py::Bool(false),    0);
    EXPECT_EQ(py::Int(5) & py::Bool(true),     1);
    EXPECT_EQ(py::Int(5) & py::Int(0),         0);
    EXPECT_EQ(py::Int(5) & py::Int(1),         1);
}


TEST(py, int_bitwise_or) {
    EXPECT_EQ(py::Int(4) | false,              4);
    EXPECT_EQ(py::Int(4) | true,               5);
    EXPECT_EQ(py::Int(4) | 0,                  4);
    EXPECT_EQ(py::Int(4) | 1,                  5);
    EXPECT_EQ(py::Int(4) | py::Bool(false),    4);
    EXPECT_EQ(py::Int(4) | py::Bool(true),     5);
    EXPECT_EQ(py::Int(4) | py::Int(0),         4);
    EXPECT_EQ(py::Int(4) | py::Int(1),         5);
}


TEST(py, int_bitwise_xor) {
    EXPECT_EQ(py::Int(5) ^ false,              5);
    EXPECT_EQ(py::Int(5) ^ true,               4);
    EXPECT_EQ(py::Int(5) ^ 0,                  5);
    EXPECT_EQ(py::Int(5) ^ 1,                  4);
    EXPECT_EQ(py::Int(5) ^ py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) ^ py::Bool(true),     4);
    EXPECT_EQ(py::Int(5) ^ py::Int(0),         5);
    EXPECT_EQ(py::Int(5) ^ py::Int(1),         4);
}


/////////////////////////////////
////    INPLACE_OPERATORS    ////
/////////////////////////////////

// NOTE: inplace operation with floats is not allowed for type safety
// inplace ops against booleans or other integers is allowed, though (except in the case
// of division, which is totally disabled)


TEST(py, int_inplace_addition) {
    bool inplace_addition_float = assertions::has_inplace_addition<py::Int, double>;
    bool inplace_addition_py_float = assertions::has_inplace_addition<py::Int, py::Float>;
    EXPECT_FALSE(inplace_addition_float);
    EXPECT_FALSE(inplace_addition_py_float);

    EXPECT_EQ(py::Int(5) += false,              5);
    EXPECT_EQ(py::Int(5) += true,               6);
    EXPECT_EQ(py::Int(5) += 0,                  5);
    EXPECT_EQ(py::Int(5) += 1,                  6);
    EXPECT_EQ(py::Int(5) += -1,                 4);
    EXPECT_EQ(py::Int(5) += py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) += py::Bool(true),     6);
    EXPECT_EQ(py::Int(5) += py::Int(0),         5);
    EXPECT_EQ(py::Int(5) += py::Int(1),         6);
    EXPECT_EQ(py::Int(5) += py::Int(-1),        4);

    EXPECT_EQ(py::Type(py::Int(5) += false),            py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) += true),             py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) += 0),                py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) += 1),                py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) += -1),               py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) += py::Bool(false)),  py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) += py::Bool(true)),   py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) += py::Int(0)),       py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) += py::Int(1)),       py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) += py::Int(-1)),      py::Int::type);
}


TEST(py, int_inplace_subtraction) {
    bool inplace_subtraction_float = assertions::has_inplace_subtraction<py::Int, double>;
    bool inplace_subtraction_py_float = assertions::has_inplace_subtraction<py::Int, py::Float>;
    EXPECT_FALSE(inplace_subtraction_float);
    EXPECT_FALSE(inplace_subtraction_py_float);

    EXPECT_EQ(py::Int(5) -= false,              5);
    EXPECT_EQ(py::Int(5) -= true,               4);
    EXPECT_EQ(py::Int(5) -= 0,                  5);
    EXPECT_EQ(py::Int(5) -= 1,                  4);
    EXPECT_EQ(py::Int(5) -= -1,                 6);
    EXPECT_EQ(py::Int(5) -= py::Bool(false),    5);
    EXPECT_EQ(py::Int(5) -= py::Bool(true),     4);
    EXPECT_EQ(py::Int(5) -= py::Int(0),         5);
    EXPECT_EQ(py::Int(5) -= py::Int(1),         4);
    EXPECT_EQ(py::Int(5) -= py::Int(-1),        6);

    EXPECT_EQ(py::Type(py::Int(5) -= false),            py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) -= true),             py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) -= 0),                py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) -= 1),                py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) -= -1),               py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) -= py::Bool(false)),  py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) -= py::Bool(true)),   py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) -= py::Int(0)),       py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) -= py::Int(1)),       py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) -= py::Int(-1)),      py::Int::type);
}


TEST(py, int_inplace_multiplication) {
    bool inplace_multiplication_float = assertions::has_inplace_multiplication<py::Int, double>;
    bool inplace_multiplication_py_float = assertions::has_inplace_multiplication<py::Int, py::Float>;
    EXPECT_FALSE(inplace_multiplication_float);
    EXPECT_FALSE(inplace_multiplication_py_float);

    EXPECT_EQ(py::Int(5) *= false,              0);
    EXPECT_EQ(py::Int(5) *= true,               5);
    EXPECT_EQ(py::Int(5) *= 0,                  0);
    EXPECT_EQ(py::Int(5) *= 1,                  5);
    EXPECT_EQ(py::Int(5) *= -2,                 -10);
    EXPECT_EQ(py::Int(5) *= py::Bool(false),    0);
    EXPECT_EQ(py::Int(5) *= py::Bool(true),     5);
    EXPECT_EQ(py::Int(5) *= py::Int(0),         0);
    EXPECT_EQ(py::Int(5) *= py::Int(1),         5);
    EXPECT_EQ(py::Int(5) *= py::Int(-2),        -10);

    EXPECT_EQ(py::Type(py::Int(5) *= false),            py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) *= true),             py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) *= 0),                py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) *= 1),                py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) *= -1),               py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) *= py::Bool(false)),  py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) *= py::Bool(true)),   py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) *= py::Int(0)),       py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) *= py::Int(1)),       py::Int::type);
    EXPECT_EQ(py::Type(py::Int(5) *= py::Int(-1)),      py::Int::type);
}


TEST(py, int_inplace_division) {
    bool inplace_division_bool = assertions::has_inplace_division<py::Int, bool>;
    bool inplace_division_py_bool = assertions::has_inplace_division<py::Int, py::Bool>;
    bool inplace_division_int = assertions::has_inplace_division<py::Int, int>;
    bool inplace_division_py_int = assertions::has_inplace_division<py::Int, py::Int>;
    bool inplace_division_float = assertions::has_inplace_division<py::Int, double>;
    bool inplace_division_py_float = assertions::has_inplace_division<py::Int, py::Float>;
    EXPECT_FALSE(inplace_division_bool);
    EXPECT_FALSE(inplace_division_py_bool);
    EXPECT_FALSE(inplace_division_int);
    EXPECT_FALSE(inplace_division_py_int);
    EXPECT_FALSE(inplace_division_float);
    EXPECT_FALSE(inplace_division_py_float);
}


// TODO: handle type safety wrt to modulo.  The other operand must be int-like


