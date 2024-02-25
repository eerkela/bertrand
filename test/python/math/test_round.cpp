#include <gtest/gtest.h>
#include <bertrand/python.h>

namespace py = bertrand::py;


////////////////////////
////    INTEGERS    ////
////////////////////////


// TEST(py, round_integer_0_digits_does_nothing) {
//     int a = 3;
//     int b = -a;
//     Int py_a(a);
//     Int py_b(b);

//     EXPECT_EQ(py::round(a,      0,    py::Round::FLOOR),    3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::FLOOR),    3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::FLOOR),    -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::FLOOR),    -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::CEILING),  3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::CEILING),  3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::CEILING),  -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::CEILING),  -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::DOWN),     3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::DOWN),     3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::DOWN),     -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::DOWN),     -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::UP),       3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::UP),       3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::UP),       -3);    
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::UP),       -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::HALF_FLOOR), 3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_FLOOR), 3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::HALF_FLOOR), -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_FLOOR), -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::HALF_CEILING), 3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_CEILING), 3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::HALF_CEILING), -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_CEILING), -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::HALF_DOWN),  3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_DOWN),  3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::HALF_DOWN),  -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_DOWN),  -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::HALF_UP),    3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_UP),    3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::HALF_UP),    -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_UP),    -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::HALF_EVEN),  3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_EVEN),  3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::HALF_EVEN),  -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_EVEN),  -3);
// }


// TEST(py, round_integer_positive_digits_does_nothing) {
//     int a = 3;
//     int b = -a;
//     Int py_a(a);
//     Int py_b(b);

//     EXPECT_EQ(py::round(a,      0,    py::Round::FLOOR),    3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::FLOOR),    3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::FLOOR),    -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::FLOOR),    -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::CEILING),  3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::CEILING),  3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::CEILING),  -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::CEILING),  -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::DOWN),     3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::DOWN),     3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::DOWN),     -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::DOWN),     -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::UP),       3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::UP),       3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::UP),       -3);    
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::UP),       -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::HALF_FLOOR), 3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_FLOOR), 3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::HALF_FLOOR), -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_FLOOR), -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::HALF_CEILING), 3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_CEILING), 3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::HALF_CEILING), -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_CEILING), -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::HALF_DOWN),  3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_DOWN),  3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::HALF_DOWN),  -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_DOWN),  -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::HALF_UP),    3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_UP),    3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::HALF_UP),    -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_UP),    -3);

//     EXPECT_EQ(py::round(a,      0,    py::Round::HALF_EVEN),  3);
//     EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_EVEN),  3);
//     EXPECT_EQ(py::round(b,      0,    py::Round::HALF_EVEN),  -3);
//     EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_EVEN),  -3);
// }


//////////////////////
////    FLOATS    ////
//////////////////////


TEST(py, round_float_0_digits) {
    double a = 2.2;
    py::Float py_a(a);
    double b = -a;
    py::Float py_b(b);

    double c = 3.7;
    py::Float py_c(c);
    double d = -c;
    py::Float py_d(d);

    EXPECT_EQ(py::round(a,      0,    py::Round::FLOOR),    2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::FLOOR),    2);
    EXPECT_EQ(py::round(b,      0,    py::Round::FLOOR),    -3);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::FLOOR),    -3);
    EXPECT_EQ(py::round(c,      0,    py::Round::FLOOR),    3);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::FLOOR),    3);
    EXPECT_EQ(py::round(d,      0,    py::Round::FLOOR),    -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::FLOOR),    -4);

    EXPECT_EQ(py::round(a,      0,    py::Round::CEILING),  3);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::CEILING),  3);
    EXPECT_EQ(py::round(b,      0,    py::Round::CEILING),  -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::CEILING),  -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::CEILING),  4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::CEILING),  4);
    EXPECT_EQ(py::round(d,      0,    py::Round::CEILING),  -3);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::CEILING),  -3);

    EXPECT_EQ(py::round(a,      0,    py::Round::DOWN),     2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::DOWN),     2);
    EXPECT_EQ(py::round(b,      0,    py::Round::DOWN),     -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::DOWN),     -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::DOWN),     3);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::DOWN),     3);
    EXPECT_EQ(py::round(d,      0,    py::Round::DOWN),     -3);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::DOWN),     -3);

    EXPECT_EQ(py::round(a,      0,    py::Round::UP),       3);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::UP),       3);
    EXPECT_EQ(py::round(b,      0,    py::Round::UP),       -3);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::UP),       -3);
    EXPECT_EQ(py::round(c,      0,    py::Round::UP),       4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::UP),       4);
    EXPECT_EQ(py::round(d,      0,    py::Round::UP),       -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::UP),       -4);

    EXPECT_EQ(py::round(a,      0,    py::Round::HALF_FLOOR), 2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_FLOOR), 2);
    EXPECT_EQ(py::round(b,      0,    py::Round::HALF_FLOOR), -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_FLOOR), -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::HALF_FLOOR), 4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::HALF_FLOOR), 4);
    EXPECT_EQ(py::round(d,      0,    py::Round::HALF_FLOOR), -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::HALF_FLOOR), -4);

    EXPECT_EQ(py::round(a,      0,    py::Round::HALF_CEILING), 2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_CEILING), 2);
    EXPECT_EQ(py::round(b,      0,    py::Round::HALF_CEILING), -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_CEILING), -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::HALF_CEILING), 4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::HALF_CEILING), 4);
    EXPECT_EQ(py::round(d,      0,    py::Round::HALF_CEILING), -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::HALF_CEILING), -4);

    EXPECT_EQ(py::round(a,      0,    py::Round::HALF_DOWN),  2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_DOWN),  2);
    EXPECT_EQ(py::round(b,      0,    py::Round::HALF_DOWN),  -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_DOWN),  -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::HALF_DOWN),  4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::HALF_DOWN),  4);
    EXPECT_EQ(py::round(d,      0,    py::Round::HALF_DOWN),  -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::HALF_DOWN),  -4);

    EXPECT_EQ(py::round(a,      0,    py::Round::HALF_UP),    2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_UP),    2);
    EXPECT_EQ(py::round(b,      0,    py::Round::HALF_UP),    -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_UP),    -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::HALF_UP),    4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::HALF_UP),    4);
    EXPECT_EQ(py::round(d,      0,    py::Round::HALF_UP),    -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::HALF_UP),    -4);

    EXPECT_EQ(py::round(a,      0,    py::Round::HALF_EVEN),  2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_EVEN),  2);
    EXPECT_EQ(py::round(b,      0,    py::Round::HALF_EVEN),  -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_EVEN),  -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::HALF_EVEN),  4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::HALF_EVEN),  4);
    EXPECT_EQ(py::round(d,      0,    py::Round::HALF_EVEN),  -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::HALF_EVEN),  -4);
}


TEST(py, round_float_0_digits_handles_halves) {
    double a = 2.5;
    py::Float py_a(a);
    double b = -a;
    py::Float py_b(b);

    double c = 3.5;
    py::Float py_c(c);
    double d = -c;
    py::Float py_d(d);

    EXPECT_EQ(py::round(a,      0,    py::Round::FLOOR),    2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::FLOOR),    2);
    EXPECT_EQ(py::round(b,      0,    py::Round::FLOOR),    -3);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::FLOOR),    -3);
    EXPECT_EQ(py::round(c,      0,    py::Round::FLOOR),    3);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::FLOOR),    3);
    EXPECT_EQ(py::round(d,      0,    py::Round::FLOOR),    -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::FLOOR),    -4);

    EXPECT_EQ(py::round(a,      0,    py::Round::CEILING),  3);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::CEILING),  3);
    EXPECT_EQ(py::round(b,      0,    py::Round::CEILING),  -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::CEILING),  -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::CEILING),  4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::CEILING),  4);
    EXPECT_EQ(py::round(d,      0,    py::Round::CEILING),  -3);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::CEILING),  -3);

    EXPECT_EQ(py::round(a,      0,    py::Round::DOWN),     2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::DOWN),     2);
    EXPECT_EQ(py::round(b,      0,    py::Round::DOWN),     -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::DOWN),     -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::DOWN),     3);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::DOWN),     3);
    EXPECT_EQ(py::round(d,      0,    py::Round::DOWN),     -3);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::DOWN),     -3);

    EXPECT_EQ(py::round(a,      0,    py::Round::UP),       3);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::UP),       3);
    EXPECT_EQ(py::round(b,      0,    py::Round::UP),       -3);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::UP),       -3);
    EXPECT_EQ(py::round(c,      0,    py::Round::UP),       4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::UP),       4);
    EXPECT_EQ(py::round(d,      0,    py::Round::UP),       -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::UP),       -4);

    EXPECT_EQ(py::round(a,      0,    py::Round::HALF_FLOOR), 2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_FLOOR), 2);
    EXPECT_EQ(py::round(b,      0,    py::Round::HALF_FLOOR), -3);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_FLOOR), -3);
    EXPECT_EQ(py::round(c,      0,    py::Round::HALF_FLOOR), 3);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::HALF_FLOOR), 3);
    EXPECT_EQ(py::round(d,      0,    py::Round::HALF_FLOOR), -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::HALF_FLOOR), -4);

    EXPECT_EQ(py::round(a,      0,    py::Round::HALF_CEILING), 3);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_CEILING), 3);
    EXPECT_EQ(py::round(b,      0,    py::Round::HALF_CEILING), -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_CEILING), -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::HALF_CEILING), 4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::HALF_CEILING), 4);
    EXPECT_EQ(py::round(d,      0,    py::Round::HALF_CEILING), -3);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::HALF_CEILING), -3);

    EXPECT_EQ(py::round(a,      0,    py::Round::HALF_DOWN),  2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_DOWN),  2);
    EXPECT_EQ(py::round(b,      0,    py::Round::HALF_DOWN),  -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_DOWN),  -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::HALF_DOWN),  3);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::HALF_DOWN),  3);
    EXPECT_EQ(py::round(d,      0,    py::Round::HALF_DOWN),  -3);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::HALF_DOWN),  -3);

    EXPECT_EQ(py::round(a,      0,    py::Round::HALF_UP),    3);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_UP),    3);
    EXPECT_EQ(py::round(b,      0,    py::Round::HALF_UP),    -3);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_UP),    -3);
    EXPECT_EQ(py::round(c,      0,    py::Round::HALF_UP),    4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::HALF_UP),    4);
    EXPECT_EQ(py::round(d,      0,    py::Round::HALF_UP),    -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::HALF_UP),    -4);

    EXPECT_EQ(py::round(a,      0,    py::Round::HALF_EVEN),  2);
    EXPECT_EQ(py::round(py_a,   0,    py::Round::HALF_EVEN),  2);
    EXPECT_EQ(py::round(b,      0,    py::Round::HALF_EVEN),  -2);
    EXPECT_EQ(py::round(py_b,   0,    py::Round::HALF_EVEN),  -2);
    EXPECT_EQ(py::round(c,      0,    py::Round::HALF_EVEN),  4);
    EXPECT_EQ(py::round(py_c,   0,    py::Round::HALF_EVEN),  4);
    EXPECT_EQ(py::round(d,      0,    py::Round::HALF_EVEN),  -4);
    EXPECT_EQ(py::round(py_d,   0,    py::Round::HALF_EVEN),  -4);
}


TEST(py, round_float_positive_digits) {
    double a = 2.25;
    py::Float py_a(a);
    double b = -a;
    py::Float py_b(b);

    double c = 6.75;
    py::Float py_c(c);
    double d = -c;
    py::Float py_d(d);

    EXPECT_EQ(py::round(a,      1,    py::Round::FLOOR),    2.2);
    EXPECT_EQ(py::round(py_a,   1,    py::Round::FLOOR),    2.2);
    EXPECT_EQ(py::round(b,      1,    py::Round::FLOOR),    -2.3);
    EXPECT_EQ(py::round(py_b,   1,    py::Round::FLOOR),    -2.3);
    EXPECT_EQ(py::round(c,      1,    py::Round::FLOOR),    6.7);
    EXPECT_EQ(py::round(py_c,   1,    py::Round::FLOOR),    6.7);
    EXPECT_EQ(py::round(d,      1,    py::Round::FLOOR),    -6.8);
    EXPECT_EQ(py::round(py_d,   1,    py::Round::FLOOR),    -6.8);

    EXPECT_EQ(py::round(a,      1,    py::Round::CEILING),  2.3);
    EXPECT_EQ(py::round(py_a,   1,    py::Round::CEILING),  2.3);
    EXPECT_EQ(py::round(b,      1,    py::Round::CEILING),  -2.2);
    EXPECT_EQ(py::round(py_b,   1,    py::Round::CEILING),  -2.2);
    EXPECT_EQ(py::round(c,      1,    py::Round::CEILING),  6.8);
    EXPECT_EQ(py::round(py_c,   1,    py::Round::CEILING),  6.8);
    EXPECT_EQ(py::round(d,      1,    py::Round::CEILING),  -6.7);
    EXPECT_EQ(py::round(py_d,   1,    py::Round::CEILING),  -6.7);

    EXPECT_EQ(py::round(a,      1,    py::Round::DOWN),     2.2);
    EXPECT_EQ(py::round(py_a,   1,    py::Round::DOWN),     2.2);
    EXPECT_EQ(py::round(b,      1,    py::Round::DOWN),     -2.2);
    EXPECT_EQ(py::round(py_b,   1,    py::Round::DOWN),     -2.2);
    EXPECT_EQ(py::round(c,      1,    py::Round::DOWN),     6.7);
    EXPECT_EQ(py::round(py_c,   1,    py::Round::DOWN),     6.7);
    EXPECT_EQ(py::round(d,      1,    py::Round::DOWN),     -6.7);
    EXPECT_EQ(py::round(py_d,   1,    py::Round::DOWN),     -6.7);

    EXPECT_EQ(py::round(a,      1,    py::Round::UP),       2.3);
    EXPECT_EQ(py::round(py_a,   1,    py::Round::UP),       2.3);
    EXPECT_EQ(py::round(b,      1,    py::Round::UP),       -2.3);
    EXPECT_EQ(py::round(py_b,   1,    py::Round::UP),       -2.3);
    EXPECT_EQ(py::round(c,      1,    py::Round::UP),       6.8);
    EXPECT_EQ(py::round(py_c,   1,    py::Round::UP),       6.8);
    EXPECT_EQ(py::round(d,      1,    py::Round::UP),       -6.8);
    EXPECT_EQ(py::round(py_d,   1,    py::Round::UP),       -6.8);

    EXPECT_EQ(py::round(a,      1,    py::Round::HALF_FLOOR), 2.2);
    EXPECT_EQ(py::round(py_a,   1,    py::Round::HALF_FLOOR), 2.2);
    EXPECT_EQ(py::round(b,      1,    py::Round::HALF_FLOOR), -2.3);
    EXPECT_EQ(py::round(py_b,   1,    py::Round::HALF_FLOOR), -2.3);
    EXPECT_EQ(py::round(c,      1,    py::Round::HALF_FLOOR), 6.7);
    EXPECT_EQ(py::round(py_c,   1,    py::Round::HALF_FLOOR), 6.7);
    EXPECT_EQ(py::round(d,      1,    py::Round::HALF_FLOOR), -6.8);
    EXPECT_EQ(py::round(py_d,   1,    py::Round::HALF_FLOOR), -6.8);

    EXPECT_EQ(py::round(a,      1,    py::Round::HALF_CEILING), 2.3);
    EXPECT_EQ(py::round(py_a,   1,    py::Round::HALF_CEILING), 2.3);
    EXPECT_EQ(py::round(b,      1,    py::Round::HALF_CEILING), -2.2);
    EXPECT_EQ(py::round(py_b,   1,    py::Round::HALF_CEILING), -2.2);
    EXPECT_EQ(py::round(c,      1,    py::Round::HALF_CEILING), 6.8);
    EXPECT_EQ(py::round(py_c,   1,    py::Round::HALF_CEILING), 6.8);
    EXPECT_EQ(py::round(d,      1,    py::Round::HALF_CEILING), -6.7);
    EXPECT_EQ(py::round(py_d,   1,    py::Round::HALF_CEILING), -6.7);

    EXPECT_EQ(py::round(a,      1,    py::Round::HALF_DOWN),  2.2);
    EXPECT_EQ(py::round(py_a,   1,    py::Round::HALF_DOWN),  2.2);
    EXPECT_EQ(py::round(b,      1,    py::Round::HALF_DOWN),  -2.2);
    EXPECT_EQ(py::round(py_b,   1,    py::Round::HALF_DOWN),  -2.2);
    EXPECT_EQ(py::round(c,      1,    py::Round::HALF_DOWN),  6.7);
    EXPECT_EQ(py::round(py_c,   1,    py::Round::HALF_DOWN),  6.7);
    EXPECT_EQ(py::round(d,      1,    py::Round::HALF_DOWN),  -6.7);
    EXPECT_EQ(py::round(py_d,   1,    py::Round::HALF_DOWN),  -6.7);

    EXPECT_EQ(py::round(a,      1,    py::Round::HALF_UP),    2.3);
    EXPECT_EQ(py::round(py_a,   1,    py::Round::HALF_UP),    2.3);
    EXPECT_EQ(py::round(b,      1,    py::Round::HALF_UP),    -2.3);
    EXPECT_EQ(py::round(py_b,   1,    py::Round::HALF_UP),    -2.3);
    EXPECT_EQ(py::round(c,      1,    py::Round::HALF_UP),    6.8);
    EXPECT_EQ(py::round(py_c,   1,    py::Round::HALF_UP),    6.8);
    EXPECT_EQ(py::round(d,      1,    py::Round::HALF_UP),    -6.8);
    EXPECT_EQ(py::round(py_d,   1,    py::Round::HALF_UP),    -6.8);

    EXPECT_EQ(py::round(a,      1,    py::Round::HALF_EVEN),  2.2);
    EXPECT_EQ(py::round(py_a,   1,    py::Round::HALF_EVEN),  2.2);
    EXPECT_EQ(py::round(b,      1,    py::Round::HALF_EVEN),  -2.2);
    EXPECT_EQ(py::round(py_b,   1,    py::Round::HALF_EVEN),  -2.2);
    EXPECT_EQ(py::round(c,      1,    py::Round::HALF_EVEN),  6.8);
    EXPECT_EQ(py::round(py_c,   1,    py::Round::HALF_EVEN),  6.8);
    EXPECT_EQ(py::round(d,      1,    py::Round::HALF_EVEN),  -6.8);
    EXPECT_EQ(py::round(py_d,   1,    py::Round::HALF_EVEN),  -6.8);
}


TEST(py, round_float_negative_digits) {
    double a = 25.0;
    py::Float py_a(a);
    double b = -a;
    py::Float py_b(b);

    double c = 75.0;
    py::Float py_c(c);
    double d = -c;
    py::Float py_d(d);

    EXPECT_EQ(py::round(a,      -1,    py::Round::FLOOR),    20);
    EXPECT_EQ(py::round(py_a,   -1,    py::Round::FLOOR),    20);
    EXPECT_EQ(py::round(b,      -1,    py::Round::FLOOR),    -30);
    EXPECT_EQ(py::round(py_b,   -1,    py::Round::FLOOR),    -30);
    EXPECT_EQ(py::round(c,      -1,    py::Round::FLOOR),    70);
    EXPECT_EQ(py::round(py_c,   -1,    py::Round::FLOOR),    70);
    EXPECT_EQ(py::round(d,      -1,    py::Round::FLOOR),    -80);
    EXPECT_EQ(py::round(py_d,   -1,    py::Round::FLOOR),    -80);

    EXPECT_EQ(py::round(a,      -1,    py::Round::CEILING),  30);
    EXPECT_EQ(py::round(py_a,   -1,    py::Round::CEILING),  30);
    EXPECT_EQ(py::round(b,      -1,    py::Round::CEILING),  -20);
    EXPECT_EQ(py::round(py_b,   -1,    py::Round::CEILING),  -20);
    EXPECT_EQ(py::round(c,      -1,    py::Round::CEILING),  80);
    EXPECT_EQ(py::round(py_c,   -1,    py::Round::CEILING),  80);
    EXPECT_EQ(py::round(d,      -1,    py::Round::CEILING),  -70);
    EXPECT_EQ(py::round(py_d,   -1,    py::Round::CEILING),  -70);

    EXPECT_EQ(py::round(a,      -1,    py::Round::DOWN),     20);
    EXPECT_EQ(py::round(py_a,   -1,    py::Round::DOWN),     20);
    EXPECT_EQ(py::round(b,      -1,    py::Round::DOWN),     -20);
    EXPECT_EQ(py::round(py_b,   -1,    py::Round::DOWN),     -20);
    EXPECT_EQ(py::round(c,      -1,    py::Round::DOWN),     70);
    EXPECT_EQ(py::round(py_c,   -1,    py::Round::DOWN),     70);
    EXPECT_EQ(py::round(d,      -1,    py::Round::DOWN),     -70);
    EXPECT_EQ(py::round(py_d,   -1,    py::Round::DOWN),     -70);

    EXPECT_EQ(py::round(a,      -1,    py::Round::UP),       30);
    EXPECT_EQ(py::round(py_a,   -1,    py::Round::UP),       30);
    EXPECT_EQ(py::round(b,      -1,    py::Round::UP),       -30);
    EXPECT_EQ(py::round(py_b,   -1,    py::Round::UP),       -30);
    EXPECT_EQ(py::round(c,      -1,    py::Round::UP),       80);
    EXPECT_EQ(py::round(py_c,   -1,    py::Round::UP),       80);
    EXPECT_EQ(py::round(d,      -1,    py::Round::UP),       -80);
    EXPECT_EQ(py::round(py_d,   -1,    py::Round::UP),       -80);

    EXPECT_EQ(py::round(a,      -1,    py::Round::HALF_FLOOR), 20);
    EXPECT_EQ(py::round(py_a,   -1,    py::Round::HALF_FLOOR), 20);
    EXPECT_EQ(py::round(b,      -1,    py::Round::HALF_FLOOR), -30);
    EXPECT_EQ(py::round(py_b,   -1,    py::Round::HALF_FLOOR), -30);
    EXPECT_EQ(py::round(c,      -1,    py::Round::HALF_FLOOR), 70);
    EXPECT_EQ(py::round(py_c,   -1,    py::Round::HALF_FLOOR), 70);
    EXPECT_EQ(py::round(d,      -1,    py::Round::HALF_FLOOR), -80);
    EXPECT_EQ(py::round(py_d,   -1,    py::Round::HALF_FLOOR), -80);

    EXPECT_EQ(py::round(a,      -1,    py::Round::HALF_CEILING), 30);
    EXPECT_EQ(py::round(py_a,   -1,    py::Round::HALF_CEILING), 30);
    EXPECT_EQ(py::round(b,      -1,    py::Round::HALF_CEILING), -20);
    EXPECT_EQ(py::round(py_b,   -1,    py::Round::HALF_CEILING), -20);
    EXPECT_EQ(py::round(c,      -1,    py::Round::HALF_CEILING), 80);
    EXPECT_EQ(py::round(py_c,   -1,    py::Round::HALF_CEILING), 80);
    EXPECT_EQ(py::round(d,      -1,    py::Round::HALF_CEILING), -70);
    EXPECT_EQ(py::round(py_d,   -1,    py::Round::HALF_CEILING), -70);

    EXPECT_EQ(py::round(a,      -1,    py::Round::HALF_DOWN),  20);
    EXPECT_EQ(py::round(py_a,   -1,    py::Round::HALF_DOWN),  20);
    EXPECT_EQ(py::round(b,      -1,    py::Round::HALF_DOWN),  -20);
    EXPECT_EQ(py::round(py_b,   -1,    py::Round::HALF_DOWN),  -20);
    EXPECT_EQ(py::round(c,      -1,    py::Round::HALF_DOWN),  70);
    EXPECT_EQ(py::round(py_c,   -1,    py::Round::HALF_DOWN),  70);
    EXPECT_EQ(py::round(d,      -1,    py::Round::HALF_DOWN),  -70);
    EXPECT_EQ(py::round(py_d,   -1,    py::Round::HALF_DOWN),  -70);

    EXPECT_EQ(py::round(a,      -1,    py::Round::HALF_UP),    30);
    EXPECT_EQ(py::round(py_a,   -1,    py::Round::HALF_UP),    30);
    EXPECT_EQ(py::round(b,      -1,    py::Round::HALF_UP),    -30);
    EXPECT_EQ(py::round(py_b,   -1,    py::Round::HALF_UP),    -30);
    EXPECT_EQ(py::round(c,      -1,    py::Round::HALF_UP),    80);
    EXPECT_EQ(py::round(py_c,   -1,    py::Round::HALF_UP),    80);
    EXPECT_EQ(py::round(d,      -1,    py::Round::HALF_UP),    -80);
    EXPECT_EQ(py::round(py_d,   -1,    py::Round::HALF_UP),    -80);

    EXPECT_EQ(py::round(a,      -1,    py::Round::HALF_EVEN),  20);
    EXPECT_EQ(py::round(py_a,   -1,    py::Round::HALF_EVEN),  20);
    EXPECT_EQ(py::round(b,      -1,    py::Round::HALF_EVEN),  -20);
    EXPECT_EQ(py::round(py_b,   -1,    py::Round::HALF_EVEN),  -20);
    EXPECT_EQ(py::round(c,      -1,    py::Round::HALF_EVEN),  80);
    EXPECT_EQ(py::round(py_c,   -1,    py::Round::HALF_EVEN),  80);
    EXPECT_EQ(py::round(d,      -1,    py::Round::HALF_EVEN),  -80);
    EXPECT_EQ(py::round(py_d,   -1,    py::Round::HALF_EVEN),  -80);
}
