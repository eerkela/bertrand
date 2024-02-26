#include "../common.h"
#include <bertrand/python.h>

namespace py = bertrand::py;


//////////////////////
////    STATIC    ////
//////////////////////


TEST(py, bool_type) {
    py::Type expected = py::reinterpret_borrow<py::Type>((PyObject*) &PyBool_Type);
    EXPECT_EQ(py::Bool::Type, expected);
}


////////////////////////////
////    CONSTRUCTORS    ////
////////////////////////////


TEST(py, bool_is_default_constructable) {
    py::Bool a;
    EXPECT_EQ(a, false);
}


TEST(py, bool_is_constructable_from_bool) {
    EXPECT_EQ(py::Bool(true), true);
    EXPECT_EQ(py::Bool(false), false);
    EXPECT_EQ(py::Bool(py::Bool(true)), true);
    EXPECT_EQ(py::Bool(py::Bool(false)), false);
}


TEST(py, bool_is_constructable_from_int) {
    EXPECT_EQ(py::Bool(0), false);
    EXPECT_EQ(py::Bool(1), true);
    EXPECT_EQ(py::Bool(2), true);
    EXPECT_EQ(py::Bool(-1), true);
    EXPECT_EQ(py::Bool(py::Int(0)), false);
    EXPECT_EQ(py::Bool(py::Int(1)), true);
    EXPECT_EQ(py::Bool(py::Int(2)), true);
    EXPECT_EQ(py::Bool(py::Int(-1)), true);
}


TEST(py, bool_is_constructable_from_float) {
    EXPECT_EQ(py::Bool(0.0), false);
    EXPECT_EQ(py::Bool(1.0), true);
    EXPECT_EQ(py::Bool(2.0), true);
    EXPECT_EQ(py::Bool(-1.0), true);
    EXPECT_EQ(py::Bool(py::Float(0.0)), false);
    EXPECT_EQ(py::Bool(py::Float(1.0)), true);
    EXPECT_EQ(py::Bool(py::Float(2.0)), true);
    EXPECT_EQ(py::Bool(py::Float(-1.0)), true);
}


TEST(py, bool_is_constructable_from_string) {
    EXPECT_EQ(py::Bool(""), false);
    EXPECT_EQ(py::Bool("a"), true);
    EXPECT_EQ(py::Bool(std::string("")), false);
    EXPECT_EQ(py::Bool(std::string("a")), true);
    EXPECT_EQ(py::Bool(std::string_view("")), false);
    EXPECT_EQ(py::Bool(std::string_view("a")), true);
    EXPECT_EQ(py::Bool(py::Str("")), false);
    EXPECT_EQ(py::Bool(py::Str("a")), true);
}


TEST(py, bool_is_constructable_from_tuple) {
    EXPECT_EQ(py::Bool(std::tuple<>{}), false);
    EXPECT_EQ(py::Bool(std::tuple<int>{1}), true);
    EXPECT_EQ(py::Bool(py::Tuple{}), false);
    EXPECT_EQ(py::Bool(py::Tuple{1}), true);
}


TEST(py, bool_is_constructable_from_list) {
    EXPECT_EQ(py::Bool(std::vector<int>{}), false);
    EXPECT_EQ(py::Bool(std::vector<int>{1}), true);
    EXPECT_EQ(py::Bool(py::List{}), false);
    EXPECT_EQ(py::Bool(py::List{1}), true);
}


TEST(py, bool_is_constructable_from_set) {
    EXPECT_EQ(py::Bool(std::unordered_set<int>{}), false);
    EXPECT_EQ(py::Bool(std::unordered_set<int>{1}), true);
    EXPECT_EQ(py::Bool(py::Set{}), false);
    EXPECT_EQ(py::Bool(py::Set{1}), true);
}


TEST(py, bool_is_constructable_from_dict) {
    EXPECT_EQ(py::Bool(std::unordered_map<int, int>{}), false);
    EXPECT_EQ(py::Bool(std::unordered_map<int, int>{{1, 1}}), true);
    EXPECT_EQ(py::Bool(py::Dict{}), false);
    EXPECT_EQ(py::Bool(py::Dict{{"a", 1}}), true);
}


///////////////////////////
////    CONVERSIONS    ////
///////////////////////////


TEST(py, bool_is_implicitly_convertable_to_bool) {
    bool a = py::Bool(true);
    bool b = py::Bool(false);
    EXPECT_EQ(a, true);
    EXPECT_EQ(b, false);
}


TEST(py, bool_is_implicitly_convertable_to_int) {
    int a = py::Bool(true);
    int b = py::Bool(false);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 0);

    unsigned int c = py::Bool(true);
    unsigned int d = py::Bool(false);
    EXPECT_EQ(c, static_cast<unsigned int>(1));
    EXPECT_EQ(d, static_cast<unsigned int>(0));

    py::Int e = py::Bool(true);
    py::Int f = py::Bool(false);
    EXPECT_EQ(e, 1);
    EXPECT_EQ(f, 0);
}


TEST(py, bool_is_implicitly_convertable_to_float) {
    double a = py::Bool(true);
    double b = py::Bool(false);
    EXPECT_EQ(a, 1.0);
    EXPECT_EQ(b, 0.0);

    py::Float c = py::Bool(true);
    py::Float d = py::Bool(false);
    EXPECT_EQ(c, 1.0);
    EXPECT_EQ(d, 0.0);
}


TEST(py, bool_is_explicitly_convertable_to_string) {
    EXPECT_EQ(static_cast<std::string>(py::Bool(true)), "True");
    EXPECT_EQ(static_cast<std::string>(py::Bool(false)), "False");

    py::Str a = py::Bool(true);
    py::Str b = py::Bool(false);
    EXPECT_EQ(static_cast<std::string>(a), "True");
    EXPECT_EQ(static_cast<std::string>(b), "False");
}


///////////////////////////
////    COMPARISONS    ////
///////////////////////////


TEST(py, bool_lt) {
    EXPECT_EQ(py::Bool(false) < false,              false);
    EXPECT_EQ(py::Bool(false) < true,               true);
    EXPECT_EQ(py::Bool(false) < 0,                  false);
    EXPECT_EQ(py::Bool(false) < 1,                  true);
    EXPECT_EQ(py::Bool(false) < 0.0,                false);
    EXPECT_EQ(py::Bool(false) < 1.0,                true);
    EXPECT_EQ(py::Bool(false) < py::Bool(false),    false);
    EXPECT_EQ(py::Bool(false) < py::Bool(true),     true);
    EXPECT_EQ(py::Bool(false) < py::Int(0),         false);
    EXPECT_EQ(py::Bool(false) < py::Int(1),         true);
    EXPECT_EQ(py::Bool(false) < py::Float(0.0),     false);
    EXPECT_EQ(py::Bool(false) < py::Float(1.0),     true);

    EXPECT_EQ(py::Bool(true) < false,               false);
    EXPECT_EQ(py::Bool(true) < true,                false);
    EXPECT_EQ(py::Bool(true) < 0,                   false);
    EXPECT_EQ(py::Bool(true) < 1,                   false);
    EXPECT_EQ(py::Bool(true) < 2,                   true);
    EXPECT_EQ(py::Bool(true) < 0.0,                 false);
    EXPECT_EQ(py::Bool(true) < 1.0,                 false);
    EXPECT_EQ(py::Bool(true) < 2.0,                 true);
    EXPECT_EQ(py::Bool(true) < py::Bool(false),     false);
    EXPECT_EQ(py::Bool(true) < py::Bool(true),      false);
    EXPECT_EQ(py::Bool(true) < py::Int(0),          false);
    EXPECT_EQ(py::Bool(true) < py::Int(1),          false);
    EXPECT_EQ(py::Bool(true) < py::Int(2),          true);
    EXPECT_EQ(py::Bool(true) < py::Float(0.0),      false);
    EXPECT_EQ(py::Bool(true) < py::Float(1.0),      false);
    EXPECT_EQ(py::Bool(true) < py::Float(2.0),      true);
}


TEST(py, bool_le) {
    EXPECT_EQ(py::Bool(false) <= false,             true);
    EXPECT_EQ(py::Bool(false) <= true,              true);
    EXPECT_EQ(py::Bool(false) <= 0,                 true);
    EXPECT_EQ(py::Bool(false) <= 1,                 true);
    EXPECT_EQ(py::Bool(false) <= 0.0,               true);
    EXPECT_EQ(py::Bool(false) <= 1.0,               true);
    EXPECT_EQ(py::Bool(false) <= py::Bool(false),   true);
    EXPECT_EQ(py::Bool(false) <= py::Bool(true),    true);
    EXPECT_EQ(py::Bool(false) <= py::Int(0),        true);
    EXPECT_EQ(py::Bool(false) <= py::Int(1),        true);
    EXPECT_EQ(py::Bool(false) <= py::Float(0.0),    true);
    EXPECT_EQ(py::Bool(false) <= py::Float(1.0),    true);

    EXPECT_EQ(py::Bool(true) <= false,              false);
    EXPECT_EQ(py::Bool(true) <= true,               true);
    EXPECT_EQ(py::Bool(true) <= 0,                  false);
    EXPECT_EQ(py::Bool(true) <= 1,                  true);
    EXPECT_EQ(py::Bool(true) <= 2,                  true);
    EXPECT_EQ(py::Bool(true) <= 0.0,                false);
    EXPECT_EQ(py::Bool(true) <= 1.0,                true);
    EXPECT_EQ(py::Bool(true) <= 2.0,                true);
    EXPECT_EQ(py::Bool(true) <= py::Bool(false),    false);
    EXPECT_EQ(py::Bool(true) <= py::Bool(true),     true);
    EXPECT_EQ(py::Bool(true) <= py::Int(0),         false);
    EXPECT_EQ(py::Bool(true) <= py::Int(1),         true);
    EXPECT_EQ(py::Bool(true) <= py::Int(2),         true);
    EXPECT_EQ(py::Bool(true) <= py::Float(0.0),     false);
    EXPECT_EQ(py::Bool(true) <= py::Float(1.0),     true);
    EXPECT_EQ(py::Bool(true) <= py::Float(2.0),     true);
}


TEST(py, bool_eq) {
    EXPECT_EQ(py::Bool(false) == false,             true);
    EXPECT_EQ(py::Bool(false) == true,              false);
    EXPECT_EQ(py::Bool(false) == 0,                 true);
    EXPECT_EQ(py::Bool(false) == 1,                 false);
    EXPECT_EQ(py::Bool(false) == 0.0,               true);
    EXPECT_EQ(py::Bool(false) == 1.0,               false);
    EXPECT_EQ(py::Bool(false) == py::Bool(false),   true);
    EXPECT_EQ(py::Bool(false) == py::Bool(true),    false);
    EXPECT_EQ(py::Bool(false) == py::Int(0),        true);
    EXPECT_EQ(py::Bool(false) == py::Int(1),        false);
    EXPECT_EQ(py::Bool(false) == py::Float(0.0),    true);
    EXPECT_EQ(py::Bool(false) == py::Float(1.0),    false);

    EXPECT_EQ(py::Bool(true) == false,              false);
    EXPECT_EQ(py::Bool(true) == true,               true);
    EXPECT_EQ(py::Bool(true) == 0,                  false);
    EXPECT_EQ(py::Bool(true) == 1,                  true);
    EXPECT_EQ(py::Bool(true) == 2,                  false);
    EXPECT_EQ(py::Bool(true) == 0.0,                false);
    EXPECT_EQ(py::Bool(true) == 1.0,                true);
    EXPECT_EQ(py::Bool(true) == 2.0,                false);
    EXPECT_EQ(py::Bool(true) == py::Bool(false),    false);
    EXPECT_EQ(py::Bool(true) == py::Bool(true),     true);
    EXPECT_EQ(py::Bool(true) == py::Int(0),         false);
    EXPECT_EQ(py::Bool(true) == py::Int(1),         true);
    EXPECT_EQ(py::Bool(true) == py::Int(2),         false);
    EXPECT_EQ(py::Bool(true) == py::Float(0.0),     false);
    EXPECT_EQ(py::Bool(true) == py::Float(1.0),     true);
    EXPECT_EQ(py::Bool(true) == py::Float(2.0),     false);
}


TEST(py, bool_ne) {
    EXPECT_EQ(py::Bool(false) != false,             false);
    EXPECT_EQ(py::Bool(false) != true,              true);
    EXPECT_EQ(py::Bool(false) != 0,                 false);
    EXPECT_EQ(py::Bool(false) != 1,                 true);
    EXPECT_EQ(py::Bool(false) != 0.0,               false);
    EXPECT_EQ(py::Bool(false) != 1.0,               true);
    EXPECT_EQ(py::Bool(false) != py::Bool(false),   false);
    EXPECT_EQ(py::Bool(false) != py::Bool(true),    true);
    EXPECT_EQ(py::Bool(false) != py::Int(0),        false);
    EXPECT_EQ(py::Bool(false) != py::Int(1),        true);
    EXPECT_EQ(py::Bool(false) != py::Float(0.0),    false);
    EXPECT_EQ(py::Bool(false) != py::Float(1.0),    true);

    EXPECT_EQ(py::Bool(true) != false,              true);
    EXPECT_EQ(py::Bool(true) != true,               false);
    EXPECT_EQ(py::Bool(true) != 0,                  true);
    EXPECT_EQ(py::Bool(true) != 1,                  false);
    EXPECT_EQ(py::Bool(true) != 2,                  true);
    EXPECT_EQ(py::Bool(true) != 0.0,                true);
    EXPECT_EQ(py::Bool(true) != 1.0,                false);
    EXPECT_EQ(py::Bool(true) != 2.0,                true);
    EXPECT_EQ(py::Bool(true) != py::Bool(false),    true);
    EXPECT_EQ(py::Bool(true) != py::Bool(true),     false);
    EXPECT_EQ(py::Bool(true) != py::Int(0),         true);
    EXPECT_EQ(py::Bool(true) != py::Int(1),         false);
    EXPECT_EQ(py::Bool(true) != py::Int(2),         true);
    EXPECT_EQ(py::Bool(true) != py::Float(0.0),     true);
    EXPECT_EQ(py::Bool(true) != py::Float(1.0),     false);
    EXPECT_EQ(py::Bool(true) != py::Float(2.0),     true);
}


TEST(py, bool_ge) {
    EXPECT_EQ(py::Bool(false) >= false,             true);
    EXPECT_EQ(py::Bool(false) >= true,              false);
    EXPECT_EQ(py::Bool(false) >= 0,                 true);
    EXPECT_EQ(py::Bool(false) >= 1,                 false);
    EXPECT_EQ(py::Bool(false) >= 0.0,               true);
    EXPECT_EQ(py::Bool(false) >= 1.0,               false);
    EXPECT_EQ(py::Bool(false) >= py::Bool(false),   true);
    EXPECT_EQ(py::Bool(false) >= py::Bool(true),    false);
    EXPECT_EQ(py::Bool(false) >= py::Int(0),        true);
    EXPECT_EQ(py::Bool(false) >= py::Int(1),        false);
    EXPECT_EQ(py::Bool(false) >= py::Float(0.0),    true);
    EXPECT_EQ(py::Bool(false) >= py::Float(1.0),    false);

    EXPECT_EQ(py::Bool(true) >= false,              true);
    EXPECT_EQ(py::Bool(true) >= true,               true);
    EXPECT_EQ(py::Bool(true) >= 0,                  true);
    EXPECT_EQ(py::Bool(true) >= 1,                  true);
    EXPECT_EQ(py::Bool(true) >= 2,                  false);
    EXPECT_EQ(py::Bool(true) >= 0.0,                true);
    EXPECT_EQ(py::Bool(true) >= 1.0,                true);
    EXPECT_EQ(py::Bool(true) >= 2.0,                false);
    EXPECT_EQ(py::Bool(true) >= py::Bool(false),    true);
    EXPECT_EQ(py::Bool(true) >= py::Bool(true),     true);
    EXPECT_EQ(py::Bool(true) >= py::Int(0),         true);
    EXPECT_EQ(py::Bool(true) >= py::Int(1),         true);
    EXPECT_EQ(py::Bool(true) >= py::Int(2),         false);
    EXPECT_EQ(py::Bool(true) >= py::Float(0.0),     true);
    EXPECT_EQ(py::Bool(true) >= py::Float(1.0),     true);
    EXPECT_EQ(py::Bool(true) >= py::Float(2.0),     false);
}


TEST(py, bool_gt) {
    EXPECT_EQ(py::Bool(false) > false,              false);
    EXPECT_EQ(py::Bool(false) > true,               false);
    EXPECT_EQ(py::Bool(false) > 0,                  false);
    EXPECT_EQ(py::Bool(false) > 1,                  false);
    EXPECT_EQ(py::Bool(false) > 0.0,                false);
    EXPECT_EQ(py::Bool(false) > 1.0,                false);
    EXPECT_EQ(py::Bool(false) > py::Bool(false),    false);
    EXPECT_EQ(py::Bool(false) > py::Bool(true),     false);
    EXPECT_EQ(py::Bool(false) > py::Int(0),         false);
    EXPECT_EQ(py::Bool(false) > py::Int(1),         false);
    EXPECT_EQ(py::Bool(false) > py::Float(0.0),     false);
    EXPECT_EQ(py::Bool(false) > py::Float(1.0),     false);

    EXPECT_EQ(py::Bool(true) > false,               true);
    EXPECT_EQ(py::Bool(true) > true,                false);
    EXPECT_EQ(py::Bool(true) > 0,                   true);
    EXPECT_EQ(py::Bool(true) > 1,                   false);
    EXPECT_EQ(py::Bool(true) > 2,                   false);
    EXPECT_EQ(py::Bool(true) > 0.0,                 true);
    EXPECT_EQ(py::Bool(true) > 1.0,                 false);
    EXPECT_EQ(py::Bool(true) > 2.0,                 false);
    EXPECT_EQ(py::Bool(true) > py::Bool(false),     true);
    EXPECT_EQ(py::Bool(true) > py::Bool(true),      false);
    EXPECT_EQ(py::Bool(true) > py::Int(0),          true);
    EXPECT_EQ(py::Bool(true) > py::Int(1),          false);
    EXPECT_EQ(py::Bool(true) > py::Int(2),          false);
    EXPECT_EQ(py::Bool(true) > py::Float(0.0),      true);
    EXPECT_EQ(py::Bool(true) > py::Float(1.0),      false);
    EXPECT_EQ(py::Bool(true) > py::Float(2.0),      false);
}


///////////////////////////////
////    UNARY OPERATORS    ////
///////////////////////////////


TEST(py, bool_unary_invert) {
    EXPECT_EQ(~py::Bool(true), -2);
    EXPECT_EQ(~py::Bool(false), -1);
}


TEST(py, bool_unary_plus) {
    EXPECT_EQ(+py::Bool(true), 1);
    EXPECT_EQ(+py::Bool(false), 0);
}


TEST(py, bool_unary_minus) {
    EXPECT_EQ(-py::Bool(true), -1);
    EXPECT_EQ(-py::Bool(false), 0);
}


TEST(py, bool_std_hash) {
    std::hash<py::Bool> hash;
    EXPECT_EQ(hash(py::Bool(true)), hash(py::Bool(true)));
    EXPECT_EQ(hash(py::Bool(false)), hash(py::Bool(false)));
    EXPECT_NE(hash(py::Bool(true)), hash(py::Bool(false)));
    EXPECT_NE(hash(py::Bool(false)), hash(py::Bool(true)));
}


////////////////////////////////
////    BINARY OPERATORS    ////
////////////////////////////////


TEST(py, bool_addition) {
    EXPECT_EQ(py::Bool(true) + true,                2);
    EXPECT_EQ(py::Bool(true) + false,               1);
    EXPECT_EQ(py::Bool(false) + true,               1);
    EXPECT_EQ(py::Bool(false) + false,              0);
    EXPECT_EQ(py::Bool(true) + 1,                   2);
    EXPECT_EQ(py::Bool(true) + 0,                   1);
    EXPECT_EQ(py::Bool(false) + 1,                  1);
    EXPECT_EQ(py::Bool(false) + 0,                  0);
    EXPECT_EQ(py::Bool(true) + 1.0,                 2.0);
    EXPECT_EQ(py::Bool(true) + 0.0,                 1.0);
    EXPECT_EQ(py::Bool(false) + 1.0,                1.0);
    EXPECT_EQ(py::Bool(false) + 0.0,                0.0);
    EXPECT_EQ(py::Bool(true) + py::Bool(true),      2);
    EXPECT_EQ(py::Bool(true) + py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) + py::Bool(true),     1);
    EXPECT_EQ(py::Bool(false) + py::Bool(false),    0);
    EXPECT_EQ(py::Bool(true) + py::Int(1),          2);
    EXPECT_EQ(py::Bool(true) + py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) + py::Int(1),         1);
    EXPECT_EQ(py::Bool(false) + py::Int(0),         0);
    EXPECT_EQ(py::Bool(true) + py::Float(1.0),      2.0);
    EXPECT_EQ(py::Bool(true) + py::Float(0.0),      1.0);
    EXPECT_EQ(py::Bool(false) + py::Float(1.0),     1.0);
    EXPECT_EQ(py::Bool(false) + py::Float(0.0),     0.0);
}


TEST(py, bool_subtraction) {
    EXPECT_EQ(py::Bool(true) - true,                0);
    EXPECT_EQ(py::Bool(true) - false,               1);
    EXPECT_EQ(py::Bool(false) - true,               -1);
    EXPECT_EQ(py::Bool(false) - false,              0);
    EXPECT_EQ(py::Bool(true) - 1,                   0);
    EXPECT_EQ(py::Bool(true) - 0,                   1);
    EXPECT_EQ(py::Bool(false) - 1,                  -1);
    EXPECT_EQ(py::Bool(false) - 0,                  0);
    EXPECT_EQ(py::Bool(true) - 1.0,                 0.0);
    EXPECT_EQ(py::Bool(true) - 0.0,                 1.0);
    EXPECT_EQ(py::Bool(false) - 1.0,                -1.0);
    EXPECT_EQ(py::Bool(false) - 0.0,                0.0);
    EXPECT_EQ(py::Bool(true) - py::Bool(true),      0);
    EXPECT_EQ(py::Bool(true) - py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) - py::Bool(true),     -1);
    EXPECT_EQ(py::Bool(false) - py::Bool(false),    0);
    EXPECT_EQ(py::Bool(true) - py::Int(1),          0);
    EXPECT_EQ(py::Bool(true) - py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) - py::Int(1),         -1);
    EXPECT_EQ(py::Bool(false) - py::Int(0),         0);
    EXPECT_EQ(py::Bool(true) - py::Float(1.0),      0.0);
    EXPECT_EQ(py::Bool(true) - py::Float(0.0),      1.0);
    EXPECT_EQ(py::Bool(false) - py::Float(1.0),     -1.0);
    EXPECT_EQ(py::Bool(false) - py::Float(0.0),     0.0);
}


TEST(py, bool_multiplication) {
    EXPECT_EQ(py::Bool(true) * true,                1);
    EXPECT_EQ(py::Bool(true) * false,               0);
    EXPECT_EQ(py::Bool(false) * true,               0);
    EXPECT_EQ(py::Bool(false) * false,              0);
    EXPECT_EQ(py::Bool(true) * 1,                   1);
    EXPECT_EQ(py::Bool(true) * 0,                   0);
    EXPECT_EQ(py::Bool(false) * 1,                  0);
    EXPECT_EQ(py::Bool(false) * 0,                  0);
    EXPECT_EQ(py::Bool(true) * 1.0,                 1.0);
    EXPECT_EQ(py::Bool(true) * 0.0,                 0.0);
    EXPECT_EQ(py::Bool(false) * 1.0,                0.0);
    EXPECT_EQ(py::Bool(false) * 0.0,                0.0);
    EXPECT_EQ(py::Bool(true) * py::Bool(true),      1);
    EXPECT_EQ(py::Bool(true) * py::Bool(false),     0);
    EXPECT_EQ(py::Bool(false) * py::Bool(true),     0);
    EXPECT_EQ(py::Bool(false) * py::Bool(false),    0);
    EXPECT_EQ(py::Bool(true) * py::Int(1),          1);
    EXPECT_EQ(py::Bool(true) * py::Int(0),          0);
    EXPECT_EQ(py::Bool(false) * py::Int(1),         0);
    EXPECT_EQ(py::Bool(false) * py::Int(0),         0);
    EXPECT_EQ(py::Bool(true) * py::Float(1.0),      1.0);
    EXPECT_EQ(py::Bool(true) * py::Float(0.0),      0.0);
    EXPECT_EQ(py::Bool(false) * py::Float(1.0),     0.0);
    EXPECT_EQ(py::Bool(false) * py::Float(0.0),     0.0);
}


TEST(py, bool_division) {
    EXPECT_EQ(      py::Bool(true) / true,              1.0);
    EXPECT_THROW(   py::Bool(true) / false,             py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / true,             0.0);
    EXPECT_THROW(   py::Bool(false) / false,            py::error_already_set);
    EXPECT_EQ(      py::Bool(true) / 1,                 1.0);
    EXPECT_THROW(   py::Bool(true) / 0,                 py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / 1,                0.0);
    EXPECT_THROW(   py::Bool(false) / 0,                py::error_already_set);
    EXPECT_EQ(      py::Bool(true) / 1.0,               1.0);
    EXPECT_THROW(   py::Bool(true) / 0.0,               py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / 1.0,              0.0);
    EXPECT_THROW(   py::Bool(false) / 0.0,              py::error_already_set);
    EXPECT_EQ(      py::Bool(true) / py::Bool(true),    1.0);
    EXPECT_THROW(   py::Bool(true) / py::Bool(false),   py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / py::Bool(true),   0.0);
    EXPECT_THROW(   py::Bool(false) / py::Bool(false),  py::error_already_set);
    EXPECT_EQ(      py::Bool(true) / py::Int(1),        1.0);
    EXPECT_THROW(   py::Bool(true) / py::Int(0),        py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / py::Int(1),       0.0);
    EXPECT_THROW(   py::Bool(false) / py::Int(0),       py::error_already_set);
    EXPECT_EQ(      py::Bool(true) / py::Float(1.0),    1.0);
    EXPECT_THROW(   py::Bool(true) / py::Float(0.0),    py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / py::Float(1.0),   0.0);
    EXPECT_THROW(   py::Bool(false) / py::Float(0.0),   py::error_already_set);
}


TEST(py, bool_modulus) {
    EXPECT_EQ(      py::Bool(true) % true,              0);
    EXPECT_THROW(   py::Bool(true) % false,             py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % true,             0);
    EXPECT_THROW(   py::Bool(false) % false,            py::error_already_set);
    EXPECT_EQ(      py::Bool(true) % 1,                 0);
    EXPECT_THROW(   py::Bool(true) % 0,                 py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % 1,                0);
    EXPECT_THROW(   py::Bool(false) % 0,                py::error_already_set);
    EXPECT_EQ(      py::Bool(true) % 1.0,               0.0);
    EXPECT_THROW(   py::Bool(true) % 0.0,               py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % 1.0,              0.0);
    EXPECT_THROW(   py::Bool(false) % 0.0,              py::error_already_set);
    EXPECT_EQ(      py::Bool(true) % py::Bool(true),    0);
    EXPECT_THROW(   py::Bool(true) % py::Bool(false),   py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % py::Bool(true),   0);
    EXPECT_THROW(   py::Bool(false) % py::Bool(false),  py::error_already_set);
    EXPECT_EQ(      py::Bool(true) % py::Int(1),        0);
    EXPECT_THROW(   py::Bool(true) % py::Int(0),        py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % py::Int(1),       0);
    EXPECT_THROW(   py::Bool(false) % py::Int(0),       py::error_already_set);
    EXPECT_EQ(      py::Bool(true) % py::Float(1.0),    0.0);
    EXPECT_THROW(   py::Bool(true) % py::Float(0.0),    py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % py::Float(1.0),   0.0);
    EXPECT_THROW(   py::Bool(false) % py::Float(0.0),   py::error_already_set);
}


TEST(py, bool_left_shift) {
    EXPECT_EQ(py::Bool(true) << true,                2);
    EXPECT_EQ(py::Bool(true) << false,               1);
    EXPECT_EQ(py::Bool(false) << true,               0);
    EXPECT_EQ(py::Bool(false) << false,              0);
    EXPECT_EQ(py::Bool(true) << 1,                   2);
    EXPECT_EQ(py::Bool(true) << 0,                   1);
    EXPECT_EQ(py::Bool(false) << 1,                  0);
    EXPECT_EQ(py::Bool(false) << 0,                  0);
    EXPECT_EQ(py::Bool(true) << py::Bool(true),      2);
    EXPECT_EQ(py::Bool(true) << py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) << py::Bool(true),     0);
    EXPECT_EQ(py::Bool(false) << py::Bool(false),    0);
    EXPECT_EQ(py::Bool(true) << py::Int(1),          2);
    EXPECT_EQ(py::Bool(true) << py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) << py::Int(1),         0);
    EXPECT_EQ(py::Bool(false) << py::Int(0),         0);
}


TEST(py, bool_right_shift) {
    EXPECT_EQ(py::Bool(true) >> true,                0);
    EXPECT_EQ(py::Bool(true) >> false,               1);
    EXPECT_EQ(py::Bool(false) >> true,               0);
    EXPECT_EQ(py::Bool(false) >> false,              0);
    EXPECT_EQ(py::Bool(true) >> 1,                   0);
    EXPECT_EQ(py::Bool(true) >> 0,                   1);
    EXPECT_EQ(py::Bool(false) >> 1,                  0);
    EXPECT_EQ(py::Bool(false) >> 0,                  0);
    EXPECT_EQ(py::Bool(true) >> py::Bool(true),      0);
    EXPECT_EQ(py::Bool(true) >> py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) >> py::Bool(true),     0);
    EXPECT_EQ(py::Bool(false) >> py::Bool(false),    0);
    EXPECT_EQ(py::Bool(true) >> py::Int(1),          0);
    EXPECT_EQ(py::Bool(true) >> py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) >> py::Int(1),         0);
    EXPECT_EQ(py::Bool(false) >> py::Int(0),         0);
}


TEST(py, bool_bitwise_and) {
    EXPECT_EQ(py::Bool(true) & true,                1);
    EXPECT_EQ(py::Bool(true) & false,               0);
    EXPECT_EQ(py::Bool(false) & true,               0);
    EXPECT_EQ(py::Bool(false) & false,              0);
    EXPECT_EQ(py::Bool(true) & 1,                   1);
    EXPECT_EQ(py::Bool(true) & 0,                   0);
    EXPECT_EQ(py::Bool(false) & 1,                  0);
    EXPECT_EQ(py::Bool(false) & 0,                  0);
    EXPECT_EQ(py::Bool(true) & py::Bool(true),      1);
    EXPECT_EQ(py::Bool(true) & py::Bool(false),     0);
    EXPECT_EQ(py::Bool(false) & py::Bool(true),     0);
    EXPECT_EQ(py::Bool(false) & py::Bool(false),    0);
    EXPECT_EQ(py::Bool(true) & py::Int(1),          1);
    EXPECT_EQ(py::Bool(true) & py::Int(0),          0);
    EXPECT_EQ(py::Bool(false) & py::Int(1),         0);
    EXPECT_EQ(py::Bool(false) & py::Int(0),         0);
}


TEST(py, bool_bitwise_or) {
    EXPECT_EQ(py::Bool(true) | true,                1);
    EXPECT_EQ(py::Bool(true) | false,               1);
    EXPECT_EQ(py::Bool(false) | true,               1);
    EXPECT_EQ(py::Bool(false) | false,              0);
    EXPECT_EQ(py::Bool(true) | 1,                   1);
    EXPECT_EQ(py::Bool(true) | 0,                   1);
    EXPECT_EQ(py::Bool(false) | 1,                  1);
    EXPECT_EQ(py::Bool(false) | 0,                  0);
    EXPECT_EQ(py::Bool(true) | py::Bool(true),      1);
    EXPECT_EQ(py::Bool(true) | py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) | py::Bool(true),     1);
    EXPECT_EQ(py::Bool(false) | py::Bool(false),    0);
    EXPECT_EQ(py::Bool(true) | py::Int(1),          1);
    EXPECT_EQ(py::Bool(true) | py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) | py::Int(1),         1);
    EXPECT_EQ(py::Bool(false) | py::Int(0),         0);
}


TEST(py, bool_bitwise_xor) {
    EXPECT_EQ(py::Bool(true) ^ true,                0);
    EXPECT_EQ(py::Bool(true) ^ false,               1);
    EXPECT_EQ(py::Bool(false) ^ true,               1);
    EXPECT_EQ(py::Bool(false) ^ false,              0);
    EXPECT_EQ(py::Bool(true) ^ 1,                   0);
    EXPECT_EQ(py::Bool(true) ^ 0,                   1);
    EXPECT_EQ(py::Bool(false) ^ 1,                  1);
    EXPECT_EQ(py::Bool(false) ^ 0,                  0);
    EXPECT_EQ(py::Bool(true) ^ py::Bool(true),      0);
    EXPECT_EQ(py::Bool(true) ^ py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) ^ py::Bool(true),     1);
    EXPECT_EQ(py::Bool(false) ^ py::Bool(false),    0);
    EXPECT_EQ(py::Bool(true) ^ py::Int(1),          0);
    EXPECT_EQ(py::Bool(true) ^ py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) ^ py::Int(1),         1);
    EXPECT_EQ(py::Bool(false) ^ py::Int(0),         0);
}


/////////////////////////////////
////    INPLACE OPERATORS    ////
/////////////////////////////////


TEST(py, bool_inplace_addition) {
    bool inplace_add_bool = assertions::has_inplace_addition<py::Bool, bool>;
    bool inplace_add_int = assertions::has_inplace_addition<py::Bool, int>;
    bool inplace_add_float = assertions::has_inplace_addition<py::Bool, double>;
    bool inplace_add_py_bool = assertions::has_inplace_addition<py::Bool, py::Bool>;
    bool inplace_add_py_int = assertions::has_inplace_addition<py::Bool, py::Int>;
    bool inplace_add_py_float = assertions::has_inplace_addition<py::Bool, py::Float>;

    EXPECT_FALSE(inplace_add_bool);
    EXPECT_FALSE(inplace_add_int);
    EXPECT_FALSE(inplace_add_float);
    EXPECT_FALSE(inplace_add_py_bool);
    EXPECT_FALSE(inplace_add_py_int);
    EXPECT_FALSE(inplace_add_py_float);
}


TEST(py, bool_inplace_subtraction) {
    bool inplace_sub_bool = assertions::has_inplace_subtraction<py::Bool, bool>;
    bool inplace_sub_int = assertions::has_inplace_subtraction<py::Bool, int>;
    bool inplace_sub_float = assertions::has_inplace_subtraction<py::Bool, double>;
    bool inplace_sub_py_bool = assertions::has_inplace_subtraction<py::Bool, py::Bool>;
    bool inplace_sub_py_int = assertions::has_inplace_subtraction<py::Bool, py::Int>;
    bool inplace_sub_py_float = assertions::has_inplace_subtraction<py::Bool, py::Float>;

    EXPECT_FALSE(inplace_sub_bool);
    EXPECT_FALSE(inplace_sub_int);
    EXPECT_FALSE(inplace_sub_float);
    EXPECT_FALSE(inplace_sub_py_bool);
    EXPECT_FALSE(inplace_sub_py_int);
    EXPECT_FALSE(inplace_sub_py_float);
}


TEST(py, bool_inplace_multiplication) {
    bool inplace_mul_bool = assertions::has_inplace_multiplication<py::Bool, bool>;
    bool inplace_mul_int = assertions::has_inplace_multiplication<py::Bool, int>;
    bool inplace_mul_float = assertions::has_inplace_multiplication<py::Bool, double>;
    bool inplace_mul_py_bool = assertions::has_inplace_multiplication<py::Bool, py::Bool>;
    bool inplace_mul_py_int = assertions::has_inplace_multiplication<py::Bool, py::Int>;
    bool inplace_mul_py_float = assertions::has_inplace_multiplication<py::Bool, py::Float>;

    EXPECT_FALSE(inplace_mul_bool);
    EXPECT_FALSE(inplace_mul_int);
    EXPECT_FALSE(inplace_mul_float);
    EXPECT_FALSE(inplace_mul_py_bool);
    EXPECT_FALSE(inplace_mul_py_int);
    EXPECT_FALSE(inplace_mul_py_float);
}


TEST(py, bool_inplace_division) {
    bool inplace_div_bool = assertions::has_inplace_division<py::Bool, bool>;
    bool inplace_div_int = assertions::has_inplace_division<py::Bool, int>;
    bool inplace_div_float = assertions::has_inplace_division<py::Bool, double>;
    bool inplace_div_py_bool = assertions::has_inplace_division<py::Bool, py::Bool>;
    bool inplace_div_py_int = assertions::has_inplace_division<py::Bool, py::Int>;
    bool inplace_div_py_float = assertions::has_inplace_division<py::Bool, py::Float>;

    EXPECT_FALSE(inplace_div_bool);
    EXPECT_FALSE(inplace_div_int);
    EXPECT_FALSE(inplace_div_float);
    EXPECT_FALSE(inplace_div_py_bool);
    EXPECT_FALSE(inplace_div_py_int);
    EXPECT_FALSE(inplace_div_py_float);
}


TEST(py, bool_inplace_modulus) {
    bool inplace_mod_bool = assertions::has_inplace_modulus<py::Bool, bool>;
    bool inplace_mod_int = assertions::has_inplace_modulus<py::Bool, int>;
    bool inplace_mod_float = assertions::has_inplace_modulus<py::Bool, double>;
    bool inplace_mod_py_bool = assertions::has_inplace_modulus<py::Bool, py::Bool>;
    bool inplace_mod_py_int = assertions::has_inplace_modulus<py::Bool, py::Int>;
    bool inplace_mod_py_float = assertions::has_inplace_modulus<py::Bool, py::Float>;

    EXPECT_FALSE(inplace_mod_bool);
    EXPECT_FALSE(inplace_mod_int);
    EXPECT_FALSE(inplace_mod_float);
    EXPECT_FALSE(inplace_mod_py_bool);
    EXPECT_FALSE(inplace_mod_py_int);
    EXPECT_FALSE(inplace_mod_py_float);
}


TEST(py, bool_inplace_left_shift) {
    bool inplace_lshift_bool = assertions::has_inplace_left_shift<py::Bool, bool>;
    bool inplace_lshift_int = assertions::has_inplace_left_shift<py::Bool, int>;
    bool inplace_lshift_py_bool = assertions::has_inplace_left_shift<py::Bool, py::Bool>;
    bool inplace_lshift_py_int = assertions::has_inplace_left_shift<py::Bool, py::Int>;

    EXPECT_FALSE(inplace_lshift_bool);
    EXPECT_FALSE(inplace_lshift_int);
    EXPECT_FALSE(inplace_lshift_py_bool);
    EXPECT_FALSE(inplace_lshift_py_int);
}


TEST(py, bool_inplace_right_shift) {
    bool inplace_rshift_bool = assertions::has_inplace_right_shift<py::Bool, bool>;
    bool inplace_rshift_int = assertions::has_inplace_right_shift<py::Bool, int>;
    bool inplace_rshift_py_bool = assertions::has_inplace_right_shift<py::Bool, py::Bool>;
    bool inplace_rshift_py_int = assertions::has_inplace_right_shift<py::Bool, py::Int>;

    EXPECT_FALSE(inplace_rshift_bool);
    EXPECT_FALSE(inplace_rshift_int);
    EXPECT_FALSE(inplace_rshift_py_bool);
    EXPECT_FALSE(inplace_rshift_py_int);
}


TEST(py, bool_inplace_bitwise_and) {
    EXPECT_EQ(py::Bool(true) &= true,                true);
    EXPECT_EQ(py::Bool(true) &= false,               false);
    EXPECT_EQ(py::Bool(false) &= true,               false);
    EXPECT_EQ(py::Bool(false) &= false,              false);
    EXPECT_EQ(py::Bool(true) &= 1,                   true);
    EXPECT_EQ(py::Bool(true) &= 0,                   false);
    EXPECT_EQ(py::Bool(false) &= 1,                  false);
    EXPECT_EQ(py::Bool(false) &= 0,                  false);
    EXPECT_EQ(py::Bool(true) &= py::Bool(true),      true);
    EXPECT_EQ(py::Bool(true) &= py::Bool(false),     false);
    EXPECT_EQ(py::Bool(false) &= py::Bool(true),     false);
    EXPECT_EQ(py::Bool(false) &= py::Bool(false),    false);
    EXPECT_EQ(py::Bool(true) &= py::Int(1),          true);
    EXPECT_EQ(py::Bool(true) &= py::Int(0),          false);
    EXPECT_EQ(py::Bool(false) &= py::Int(1),         false);
    EXPECT_EQ(py::Bool(false) &= py::Int(0),         false);
}


TEST(py, bool_inplace_bitwise_or) {
    EXPECT_EQ(py::Bool(true) |= true,                true);
    EXPECT_EQ(py::Bool(true) |= false,               true);
    EXPECT_EQ(py::Bool(false) |= true,               true);
    EXPECT_EQ(py::Bool(false) |= false,              false);
    EXPECT_EQ(py::Bool(true) |= 1,                   true);
    EXPECT_EQ(py::Bool(true) |= 0,                   true);
    EXPECT_EQ(py::Bool(false) |= 1,                  true);
    EXPECT_EQ(py::Bool(false) |= 0,                  false);
    EXPECT_EQ(py::Bool(true) |= py::Bool(true),      true);
    EXPECT_EQ(py::Bool(true) |= py::Bool(false),     true);
    EXPECT_EQ(py::Bool(false) |= py::Bool(true),     true);
    EXPECT_EQ(py::Bool(false) |= py::Bool(false),    false);
    EXPECT_EQ(py::Bool(true) |= py::Int(1),          true);
    EXPECT_EQ(py::Bool(true) |= py::Int(0),          true);
    EXPECT_EQ(py::Bool(false) |= py::Int(1),         true);
    EXPECT_EQ(py::Bool(false) |= py::Int(0),         false);
}


TEST(py, bool_inplace_bitwise_xor) {
    EXPECT_EQ(py::Bool(true) ^= true,                false);
    EXPECT_EQ(py::Bool(true) ^= false,               true);
    EXPECT_EQ(py::Bool(false) ^= true,               true);
    EXPECT_EQ(py::Bool(false) ^= false,              false);
    EXPECT_EQ(py::Bool(true) ^= 1,                   false);
    EXPECT_EQ(py::Bool(true) ^= 0,                   true);
    EXPECT_EQ(py::Bool(false) ^= 1,                  true);
    EXPECT_EQ(py::Bool(false) ^= 0,                  false);
    EXPECT_EQ(py::Bool(true) ^= py::Bool(true),      false);
    EXPECT_EQ(py::Bool(true) ^= py::Bool(false),     true);
    EXPECT_EQ(py::Bool(false) ^= py::Bool(true),     true);
    EXPECT_EQ(py::Bool(false) ^= py::Bool(false),    false);
    EXPECT_EQ(py::Bool(true) ^= py::Int(1),          false);
    EXPECT_EQ(py::Bool(true) ^= py::Int(0),          true);
    EXPECT_EQ(py::Bool(false) ^= py::Int(1),         true);
    EXPECT_EQ(py::Bool(false) ^= py::Int(0),         false);
}
