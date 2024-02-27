#include "../common.h"
#include <bertrand/python.h>

namespace py = bertrand::py;


TEST(py, int_type) {
    py::Type expected = py::reinterpret_borrow<py::Type>((PyObject*)&PyLong_Type);
    EXPECT_EQ(py::Int::Type, expected);
}


////////////////////////////
////    CONSTRUCTORS    ////
////////////////////////////


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


TEST(py, int_is_constructible_from_float) {
    EXPECT_EQ(py::Int(0.0), 0);
    EXPECT_EQ(py::Int(1.0), 1);
    EXPECT_EQ(py::Int(-1.0), -1);
    EXPECT_EQ(py::Int(py::Float(0.0)), 0);
    EXPECT_EQ(py::Int(py::Float(1.0)), 1);
    EXPECT_EQ(py::Int(py::Float(-1.0)), -1);
}


TEST(py, int_is_constructible_from_string) {
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


TEST(py, int_is_explicitly_convertible_to_string) {
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

    EXPECT_EQ(py::Type(py::Int(5) += false),            py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) += true),             py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) += 0),                py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) += 1),                py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) += -1),               py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) += py::Bool(false)),  py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) += py::Bool(true)),   py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) += py::Int(0)),       py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) += py::Int(1)),       py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) += py::Int(-1)),      py::Int::Type);
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

    EXPECT_EQ(py::Type(py::Int(5) -= false),            py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) -= true),             py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) -= 0),                py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) -= 1),                py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) -= -1),               py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) -= py::Bool(false)),  py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) -= py::Bool(true)),   py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) -= py::Int(0)),       py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) -= py::Int(1)),       py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) -= py::Int(-1)),      py::Int::Type);
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

    EXPECT_EQ(py::Type(py::Int(5) *= false),            py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) *= true),             py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) *= 0),                py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) *= 1),                py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) *= -1),               py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) *= py::Bool(false)),  py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) *= py::Bool(true)),   py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) *= py::Int(0)),       py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) *= py::Int(1)),       py::Int::Type);
    EXPECT_EQ(py::Type(py::Int(5) *= py::Int(-1)),      py::Int::Type);
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

