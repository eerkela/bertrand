#include <gtest/gtest.h>
#include <bertrand/python.h>

#include <vector>

namespace py = bertrand::py;


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
