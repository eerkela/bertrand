#include <gtest/gtest.h>
#include <bertrand/python.h>

namespace py = bertrand::py;


TEST(py, abs_int) {
    int a = 4;
    py::Int py_a(a);
    int b = -a;
    py::Int py_b(b);

    EXPECT_EQ(py::abs(a), 4);
    EXPECT_EQ(py::abs(py_a), 4);
    EXPECT_EQ(py::abs(b), 4);
    EXPECT_EQ(py::abs(py_b), 4);
}


TEST(py, abs_float) {
    double a = 4.5;
    py::Float py_a(a);
    double b = -a;
    py::Float py_b(b);

    EXPECT_EQ(py::abs(a), 4.5);
    EXPECT_EQ(py::abs(py_a), 4.5);
    EXPECT_EQ(py::abs(b), 4.5);
    EXPECT_EQ(py::abs(py_b), 4.5);
}

