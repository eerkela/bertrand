#include <gtest/gtest.h>
#include <bertrand/python.h>
#include <string>

namespace py = bertrand::py;


TEST(py, int_type) {
    py::Type expected = py::reinterpret_borrow<py::Type>((PyObject*)&PyLong_Type);
    EXPECT_EQ(py::Int::Type, expected);
}


////////////////////////////
////    CONSTRUCTORS    ////
////////////////////////////


TEST(py, int_is_default_constructable) {
    py::Int a;
    EXPECT_EQ(a, 0);
}


TEST(py, int_is_constructable_from_bool) {
    EXPECT_EQ(py::Int(true), 1);
    EXPECT_EQ(py::Int(false), 0);
    EXPECT_EQ(py::Int(py::Bool(true)), 1);
    EXPECT_EQ(py::Int(py::Bool(false)), 0);
}


TEST(py, int_is_constructable_from_int) {
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


TEST(py, int_is_constructable_from_float) {
    EXPECT_EQ(py::Int(0.0), 0);
    EXPECT_EQ(py::Int(1.0), 1);
    EXPECT_EQ(py::Int(-1.0), -1);
    EXPECT_EQ(py::Int(py::Float(0.0)), 0);
    EXPECT_EQ(py::Int(py::Float(1.0)), 1);
    EXPECT_EQ(py::Int(py::Float(-1.0)), -1);
}


TEST(py, int_is_constructable_from_string) {
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

    py::Bool d = py::Int(0);
    py::Bool e = py::Int(1);
    py::Bool f = py::Int(-1);
    EXPECT_EQ(d, false);
    EXPECT_EQ(e, true);
    EXPECT_EQ(f, true);
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


TEST(py, int_is_explicitly_convertable_to_string) {
    EXPECT_EQ(static_cast<std::string>(py::Int(0)), "0");
    EXPECT_EQ(static_cast<std::string>(py::Int(1)), "1");
    EXPECT_EQ(static_cast<std::string>(py::Int(-1)), "-1");

    py::Str a = py::Int(0);
    py::Str b = py::Int(1);
    py::Str c = py::Int(-1);
    EXPECT_EQ(static_cast<std::string>(a), "0");
    EXPECT_EQ(static_cast<std::string>(b), "1");
    EXPECT_EQ(static_cast<std::string>(c), "-1");
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


///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


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


TEST(py, int_std_hash) {
    std::hash<py::Int> hash;
    EXPECT_EQ(hash(py::Int(0)), hash(py::Int(0)));
    EXPECT_EQ(hash(py::Int(1)), hash(py::Int(1)));
    EXPECT_EQ(hash(py::Int(-1)), hash(py::Int(-1)));
    EXPECT_NE(hash(py::Int(0)), hash(py::Int(1)));
    EXPECT_NE(hash(py::Int(0)), hash(py::Int(-1)));
    EXPECT_NE(hash(py::Int(1)), hash(py::Int(-1)));
}


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


// TODO: look these over and make sure they're comprehensive


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
    EXPECT_EQ(py::Int(5) * -1,                 -5);
    EXPECT_EQ(py::Int(5) * 0.0,                0.0);
    EXPECT_EQ(py::Int(5) * 1.0,                5.0);
    EXPECT_EQ(py::Int(5) * -1.0,               -5.0);
    EXPECT_EQ(py::Int(5) * py::Bool(false),    0);
    EXPECT_EQ(py::Int(5) * py::Bool(true),     5);
    EXPECT_EQ(py::Int(5) * py::Int(0),         0);
    EXPECT_EQ(py::Int(5) * py::Int(1),         5);
    EXPECT_EQ(py::Int(5) * py::Int(-1),        -5);
    EXPECT_EQ(py::Int(5) * py::Float(0.0),     0.0);
    EXPECT_EQ(py::Int(5) * py::Float(1.0),     5.0);
    EXPECT_EQ(py::Int(5) * py::Float(-1.0),    -5.0);
}


TEST(py, int_division) {
    EXPECT_THROW(   py::Int(5) / false,              py::error_already_set);
    EXPECT_EQ(      py::Int(5) / true,               5);

}


