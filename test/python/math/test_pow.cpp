#include <gtest/gtest.h>
#include <bertrand/python.h>

namespace py = bertrand::py;


TEST(py, pow_int_positive_exponent) {
    int a = 2;
    py::Int py_a(a);
    int b = -2;
    py::Int py_b(b);

    EXPECT_EQ(py::pow(a,        2),     4);
    EXPECT_EQ(py::pow(py_a,     2),     4);
    EXPECT_EQ(py::pow(b,        2),     4);
    EXPECT_EQ(py::pow(py_b,     2),     4);

    EXPECT_EQ(py::pow(a,        3),     8);
    EXPECT_EQ(py::pow(py_a,     3),     8);
    EXPECT_EQ(py::pow(b,        3),     -8);
    EXPECT_EQ(py::pow(py_b,     3),     -8);

    // TODO: idk if these are the right answers
    // EXPECT_EQ(py::pow(a,        2,      3),     1);
    // EXPECT_EQ(py::pow(py_a,     2,      3),     1);
    // EXPECT_EQ(py::pow(b,        2,      3),     1);
    // EXPECT_EQ(py::pow(py_b,     2,      3),     1);

    // EXPECT_EQ(py::pow(a,        3,      3),     2);
    // EXPECT_EQ(py::pow(py_a,     3,      3),     2);
    // EXPECT_EQ(py::pow(b,        3,      3),     2);
    // EXPECT_EQ(py::pow(py_b,     3,      3),     2);
}


