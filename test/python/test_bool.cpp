#include "../common.h"
#include <bertrand/python.h>

namespace py = bertrand::py;


/////////////////////
////    TYPES    ////
/////////////////////


TEST(py_bool, type) {
    py::Type expected = py::reinterpret_borrow<py::Type>((PyObject*) &PyBool_Type);
    EXPECT_EQ(py::Bool::type, expected);
}


TEST(py_bool, check) {
    EXPECT_EQ(py::Bool::check<bool>(), true);
    EXPECT_EQ(py::Bool::check<pybind11::bool_>(), true);
    EXPECT_EQ(py::Bool::check<py::Bool>(), true);

    EXPECT_EQ(py::Bool::check<int>(), false);
    EXPECT_EQ(py::Bool::check<double>(), false);
    EXPECT_EQ(py::Bool::check<std::string>(), false);
    EXPECT_EQ(py::Bool::check<py::Int>(), false);
    EXPECT_EQ(py::Bool::check<py::Float>(), false);
    EXPECT_EQ(py::Bool::check<py::Str>(), false);
}


/////////////////////////////////////
////    IMPLICIT CONSTRUCTORS    ////
/////////////////////////////////////


TEST(py_bool, default_construct) {
    py::Bool a;
    EXPECT_EQ(a, false);
}


TEST(py_bool, from_object) {
    // safe
    py::Object a = false;
    py::Object b = true;
    py::Bool a2 = a;
    py::Bool b2 = b;
    EXPECT_EQ(a2, false);
    EXPECT_EQ(b2, true);

    // not safe
    py::Object c = py::Int();
    py::Object d = py::Float();
    py::Object e = py::Str();
    py::Object f = py::Tuple{};
    py::Object g = py::List{};
    py::Object h = py::Set{};
    py::Object i = py::Dict{};
    EXPECT_THROW(py::Bool c2 = c, py::TypeError);
    EXPECT_THROW(py::Bool d2 = d, py::TypeError);
    EXPECT_THROW(py::Bool e2 = e, py::TypeError);
    EXPECT_THROW(py::Bool f2 = f, py::TypeError);
    EXPECT_THROW(py::Bool g2 = g, py::TypeError);
    EXPECT_THROW(py::Bool h2 = h, py::TypeError);
    EXPECT_THROW(py::Bool i2 = i, py::TypeError);
}


TEST(py_bool, from_accessor) {
    // safe
    py::List list = {true, false};
    py::Bool a = list[0];
    py::Bool b = list[1];
    EXPECT_EQ(a, true);
    EXPECT_EQ(b, false);

    // not safe
    py::List list2 = {1, 0};
    EXPECT_THROW(py::Bool c = list2[0], py::TypeError);
    EXPECT_THROW(py::Bool d = list2[1], py::TypeError);
}


TEST(py_bool, copy) {
    // copy construct
    py::Bool a = true;
    int a_refs = a.ref_count();
    py::Bool b(a);
    EXPECT_EQ(b, true);
    EXPECT_EQ(b.ref_count(), a_refs + 1);

    // copy assign
    py::Bool c = true;
    py::Bool d = false;
    int c_refs = c.ref_count();
    d = c;
    EXPECT_EQ(d, true);
    EXPECT_EQ(d.ref_count(), c_refs + 1);
}


TEST(py_bool, move) {
    // move construct
    py::Bool a = true;
    int a_refs = a.ref_count();
    py::Bool b(std::move(a));
    EXPECT_EQ(b, true);
    EXPECT_EQ(b.ref_count(), a_refs);

    // move assign
    py::Bool c = true;
    py::Bool d = false;
    int c_refs = c.ref_count();
    d = std::move(c);
    EXPECT_EQ(d, true);
    EXPECT_EQ(d.ref_count(), c_refs);
}


TEST(py_bool, from_bool) {
    // constructor
    EXPECT_EQ(py::Bool(true), true);
    EXPECT_EQ(py::Bool(false), false);

    // assignment
    py::Bool a = true;
    EXPECT_EQ(a, true);
    a = false;
    EXPECT_EQ(a, false);
    a = true;
    EXPECT_EQ(a, true);
}


/////////////////////////////////////
////    EXPLICIT CONSTRUCTORS    ////
/////////////////////////////////////


TEST(py_bool, from_int) {
    // constructor
    EXPECT_EQ(py::Bool(0), false);
    EXPECT_EQ(py::Bool(1), true);
    EXPECT_EQ(py::Bool(2), true);
    EXPECT_EQ(py::Bool(-1), true);
    EXPECT_EQ(py::Bool(py::Int(0)), false);
    EXPECT_EQ(py::Bool(py::Int(1)), true);
    EXPECT_EQ(py::Bool(py::Int(2)), true);
    EXPECT_EQ(py::Bool(py::Int(-1)), true);

    // assignment
    assertions::assign<py::Bool, int>::invalid();
    assertions::assign<py::Bool, unsigned int>::invalid();
    assertions::assign<py::Bool, py::Int>::invalid();
    assertions::assign<py::Bool&, int>::invalid();
    assertions::assign<py::Bool&, unsigned int>::invalid();
    assertions::assign<py::Bool&, py::Int>::invalid();
}


TEST(py_bool, from_float) {
    // assignment
    assertions::assign<py::Bool, double>::invalid();
    assertions::assign<py::Bool, py::Float>::invalid();
    assertions::assign<py::Bool&, double>::invalid();
    assertions::assign<py::Bool&, py::Float>::invalid();

    // constructor
    EXPECT_EQ(py::Bool(0.0), false);
    EXPECT_EQ(py::Bool(1.0), true);
    EXPECT_EQ(py::Bool(2.0), true);
    EXPECT_EQ(py::Bool(-1.0), true);
    EXPECT_EQ(py::Bool(py::Float(0.0)), false);
    EXPECT_EQ(py::Bool(py::Float(1.0)), true);
    EXPECT_EQ(py::Bool(py::Float(2.0)), true);
    EXPECT_EQ(py::Bool(py::Float(-1.0)), true);
}


TEST(py_bool, from_string) {
    // assignment
    assertions::assign<py::Bool, const char*>::invalid();
    assertions::assign<py::Bool, std::string>::invalid();
    assertions::assign<py::Bool, std::string_view>::invalid();
    assertions::assign<py::Bool, py::Str>::invalid();
    assertions::assign<py::Bool&, const char*>::invalid();
    assertions::assign<py::Bool&, std::string>::invalid();
    assertions::assign<py::Bool&, std::string_view>::invalid();
    assertions::assign<py::Bool&, py::Str>::invalid();

    // constructor
    EXPECT_EQ(py::Bool(""), false);
    EXPECT_EQ(py::Bool("a"), true);
    EXPECT_EQ(py::Bool(std::string("")), false);
    EXPECT_EQ(py::Bool(std::string("a")), true);
    EXPECT_EQ(py::Bool(std::string_view("")), false);
    EXPECT_EQ(py::Bool(std::string_view("a")), true);
    EXPECT_EQ(py::Bool(py::Str("")), false);
    EXPECT_EQ(py::Bool(py::Str("a")), true);
}


TEST(py_bool, from_tuple) {
    // assignment
    assertions::assign<py::Bool, std::pair<int, int>>::invalid();
    assertions::assign<py::Bool, std::tuple<>>::invalid();
    assertions::assign<py::Bool, std::tuple<int>>::invalid();
    assertions::assign<py::Bool, py::Tuple>::invalid();
    assertions::assign<py::Bool&, std::pair<int, int>>::invalid();
    assertions::assign<py::Bool&, std::tuple<>>::invalid();
    assertions::assign<py::Bool&, std::tuple<int>>::invalid();
    assertions::assign<py::Bool&, py::Tuple>::invalid();

    // constructor
    EXPECT_EQ(py::Bool(std::tuple<>{}), false);
    EXPECT_EQ(py::Bool(std::tuple<int>{1}), true);
    EXPECT_EQ(py::Bool(py::Tuple{}), false);
    EXPECT_EQ(py::Bool(py::Tuple{1}), true);
}


TEST(py_bool, from_list) {
    // assignment
    assertions::assign<py::Bool, std::array<int, 3>>::invalid();
    assertions::assign<py::Bool, std::list<int>>::invalid();
    assertions::assign<py::Bool, std::vector<int>>::invalid();
    assertions::assign<py::Bool, py::List>::invalid();
    assertions::assign<py::Bool&, std::array<int, 3>>::invalid();
    assertions::assign<py::Bool&, std::list<int>>::invalid();
    assertions::assign<py::Bool&, std::vector<int>>::invalid();
    assertions::assign<py::Bool&, py::List>::invalid();

    // constructor
    EXPECT_EQ(py::Bool(std::vector<int>{}), false);
    EXPECT_EQ(py::Bool(std::vector<int>{1}), true);
    EXPECT_EQ(py::Bool(py::List{}), false);
    EXPECT_EQ(py::Bool(py::List{1}), true);
}


TEST(py_bool, from_set) {
    // assignment
    assertions::assign<py::Bool, std::set<int>>::invalid();
    assertions::assign<py::Bool, std::unordered_set<int>>::invalid();
    assertions::assign<py::Bool, py::Set>::invalid();
    assertions::assign<py::Bool, py::FrozenSet>::invalid();
    assertions::assign<py::Bool&, std::set<int>>::invalid();
    assertions::assign<py::Bool&, std::unordered_set<int>>::invalid();
    assertions::assign<py::Bool&, py::Set>::invalid();
    assertions::assign<py::Bool&, py::FrozenSet>::invalid();

    // constructor
    EXPECT_EQ(py::Bool(std::unordered_set<int>{}), false);
    EXPECT_EQ(py::Bool(std::unordered_set<int>{1}), true);
    EXPECT_EQ(py::Bool(py::Set{}), false);
    EXPECT_EQ(py::Bool(py::Set{1}), true);
}


TEST(py_bool, from_dict) {
    // assignment
    assertions::assign<py::Bool, std::map<int, int>>::invalid();
    assertions::assign<py::Bool, std::unordered_map<int, int>>::invalid();
    assertions::assign<py::Bool, py::Dict>::invalid();
    assertions::assign<py::Bool, py::MappingProxy>::invalid();
    assertions::assign<py::Bool&, std::map<int, int>>::invalid();
    assertions::assign<py::Bool&, std::unordered_map<int, int>>::invalid();
    assertions::assign<py::Bool&, py::Dict>::invalid();
    assertions::assign<py::Bool&, py::MappingProxy>::invalid();

    // constructor
    EXPECT_EQ(py::Bool(std::unordered_map<int, int>{}), false);
    EXPECT_EQ(py::Bool(std::unordered_map<int, int>{{1, 1}}), true);
    EXPECT_EQ(py::Bool(py::Dict{}), false);
    EXPECT_EQ(py::Bool(py::Dict{{"a", 1}}), true);
}


TEST(py_bool, from_custom_type) {
    struct Implicit { operator bool() const { return true; } };
    struct Explicit { explicit operator bool() const { return true; } };
    EXPECT_EQ(py::Bool(Implicit{}), true);
    EXPECT_EQ(py::Bool(Explicit{}), true);

    // assignment
    assertions::assign<py::Bool, Implicit>::invalid();
    assertions::assign<py::Bool, Explicit>::invalid();
    assertions::assign<py::Bool&, Implicit>::invalid();
    assertions::assign<py::Bool&, Explicit>::invalid();

    // Python type
    py::Type Foo("Foo");
    Foo.attr("__bool__") = py::Method([](const py::Object& self) { return true; });
    py::Object foo = Foo();
    EXPECT_EQ(py::Bool(foo), true);
    EXPECT_THROW(py::Bool a = foo, py::TypeError);
    py::Bool a = false;
    EXPECT_THROW(a = foo, py::TypeError);
}


////////////////////////////////////
////    CONVERSION OPERATORS    ////
////////////////////////////////////


TEST(py_bool, to_bool) {
    // assignment
    bool a = py::Bool(true);
    bool b = py::Bool(false);
    EXPECT_EQ(a, true);
    EXPECT_EQ(b, false);

    // function parameters
    auto func = [](bool x) { return x; };
    EXPECT_EQ(func(py::Bool(true)), true);
    EXPECT_EQ(func(py::Bool(false)), false);
}


TEST(py_bool, to_int) {
    // assignment
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

    // function parameters
    auto func1 = [](int x) { return x; };
    auto func2 = [](unsigned int x) { return x; };
    auto func3 = [](const py::Int& x) { return x; };
    EXPECT_EQ(func1(py::Bool(true)), 1);
    EXPECT_EQ(func2(py::Bool(false)), static_cast<unsigned int>(0));
    EXPECT_EQ(func3(py::Bool(true)), 1);
}


TEST(py_bool, to_float) {
    // assignment
    double a = py::Bool(true);
    double b = py::Bool(false);
    EXPECT_EQ(a, 1.0);
    EXPECT_EQ(b, 0.0);
    py::Float c = py::Bool(true);
    py::Float d = py::Bool(false);
    EXPECT_EQ(c, 1.0);
    EXPECT_EQ(d, 0.0);

    // function parameters
    auto func1 = [](double x) { return x; };
    auto func2 = [](const py::Float& x) { return x; };
    EXPECT_EQ(func1(py::Bool(true)), 1.0);
    EXPECT_EQ(func2(py::Bool(false)), 0.0);
}


TEST(py_bool, to_string) {
    // assignment
    assertions::assign<std::string, py::Bool>::invalid();
    assertions::assign<std::string&, py::Bool>::invalid();

    // explicit cast
    EXPECT_EQ(static_cast<std::string>(py::Bool(true)), "True");
    EXPECT_EQ(static_cast<std::string>(py::Bool(false)), "False");
}


TEST(py_bool, to_any_other_type) {
    // assignment
    assertions::assign<std::tuple<>, py::Bool>::invalid();
    assertions::assign<std::vector<int>, py::Bool>::invalid();
    assertions::assign<std::unordered_set<int>, py::Bool>::invalid();
    assertions::assign<std::unordered_map<int, int>, py::Bool>::invalid();
    assertions::assign<py::Str, py::Bool>::invalid();
    assertions::assign<py::Tuple, py::Bool>::invalid();
    assertions::assign<py::List, py::Bool>::invalid();
    assertions::assign<py::Set, py::Bool>::invalid();
    assertions::assign<py::Dict, py::Bool>::invalid();

    // explicit cast
    using unordered_map = std::unordered_map<int, int>;
    EXPECT_THROW(static_cast<std::tuple<int>>(py::Bool(true)), py::TypeError);
    EXPECT_THROW(static_cast<std::vector<int>>(py::Bool(true)), py::TypeError);
    EXPECT_THROW(static_cast<std::unordered_set<int>>(py::Bool(true)), py::TypeError);
    EXPECT_THROW(static_cast<unordered_map>(py::Bool(true)), py::TypeError);
}


/////////////////////////
////    OPERATORS    ////
/////////////////////////


TEST(py_bool, mem_ops) {
    assertions::dereference<py::Bool>::invalid();
    assertions::address_of<py::Bool>::returns<py::Bool*>::valid();
}


TEST(py_bool, call) {
    assertions::call<py::Bool>::invalid();
}


TEST(py_bool, hash) {
    std::hash<py::Bool> hash;
    EXPECT_EQ(hash(py::Bool(true)), hash(py::Bool(true)));
    EXPECT_EQ(hash(py::Bool(false)), hash(py::Bool(false)));
    EXPECT_NE(hash(py::Bool(true)), hash(py::Bool(false)));
    EXPECT_NE(hash(py::Bool(false)), hash(py::Bool(true)));
}


TEST(py_bool, iter) {
    assertions::iter<py::Bool>::invalid();
    assertions::reverse_iter<py::Bool>::invalid();
}


TEST(py_bool, index) {
    assertions::index<py::Bool>::invalid();
}


TEST(py_bool, contains) {
    assertions::contains<py::Bool>::invalid();
}


TEST(py_bool, less_than) {
    // py::Bool < bool
    EXPECT_EQ(py::Bool(false) < false,              false);
    EXPECT_EQ(py::Bool(false) < true,               true);
    EXPECT_EQ(py::Bool(true) < false,               false);
    EXPECT_EQ(py::Bool(true) < true,                false);
    EXPECT_EQ(py::Bool(false) < py::Bool(false),    false);
    EXPECT_EQ(py::Bool(false) < py::Bool(true),     true);
    EXPECT_EQ(py::Bool(true) < py::Bool(false),     false);
    EXPECT_EQ(py::Bool(true) < py::Bool(true),      false);

    // py::Bool < int
    EXPECT_EQ(py::Bool(false) < 0,                  false);
    EXPECT_EQ(py::Bool(false) < 1,                  true);
    EXPECT_EQ(py::Bool(true) < 0,                   false);
    EXPECT_EQ(py::Bool(true) < 1,                   false);
    EXPECT_EQ(py::Bool(true) < 2,                   true);
    EXPECT_EQ(py::Bool(false) < py::Int(0),         false);
    EXPECT_EQ(py::Bool(false) < py::Int(1),         true);
    EXPECT_EQ(py::Bool(true) < py::Int(0),          false);
    EXPECT_EQ(py::Bool(true) < py::Int(1),          false);
    EXPECT_EQ(py::Bool(true) < py::Int(2),          true);

    // py::Bool < double
    EXPECT_EQ(py::Bool(false) < 0.0,                false);
    EXPECT_EQ(py::Bool(false) < 1.0,                true);
    EXPECT_EQ(py::Bool(true) < 0.0,                 false);
    EXPECT_EQ(py::Bool(true) < 1.0,                 false);
    EXPECT_EQ(py::Bool(true) < 2.0,                 true);
    EXPECT_EQ(py::Bool(false) < py::Float(0.0),     false);
    EXPECT_EQ(py::Bool(false) < py::Float(1.0),     true);
    EXPECT_EQ(py::Bool(true) < py::Float(0.0),      false);
    EXPECT_EQ(py::Bool(true) < py::Float(1.0),      false);
    EXPECT_EQ(py::Bool(true) < py::Float(2.0),      true);

    // py::Bool < T
    assertions::less_than<py::Bool, std::string>::invalid();
    assertions::less_than<py::Bool, std::tuple<>>::invalid();
    assertions::less_than<py::Bool, std::vector<int>>::invalid();
    assertions::less_than<py::Bool, std::unordered_set<int>>::invalid();
    assertions::less_than<py::Bool, std::unordered_map<int, int>>::invalid();
    assertions::less_than<py::Bool, py::Str>::invalid();
    assertions::less_than<py::Bool, py::Tuple>::invalid();
    assertions::less_than<py::Bool, py::List>::invalid();
    assertions::less_than<py::Bool, py::Set>::invalid();
    assertions::less_than<py::Bool, py::Dict>::invalid();
}


TEST(py_bool, less_than_or_equal_to) {
    // py::Bool <= bool
    EXPECT_EQ(py::Bool(false) <= false,             true);
    EXPECT_EQ(py::Bool(false) <= true,              true);
    EXPECT_EQ(py::Bool(true) <= false,              false);
    EXPECT_EQ(py::Bool(true) <= true,               true);
    EXPECT_EQ(py::Bool(false) <= py::Bool(false),   true);
    EXPECT_EQ(py::Bool(false) <= py::Bool(true),    true);
    EXPECT_EQ(py::Bool(true) <= py::Bool(false),    false);
    EXPECT_EQ(py::Bool(true) <= py::Bool(true),     true);

    // py::Bool <= int
    EXPECT_EQ(py::Bool(false) <= 0,                 true);
    EXPECT_EQ(py::Bool(false) <= 1,                 true);
    EXPECT_EQ(py::Bool(true) <= 0,                  false);
    EXPECT_EQ(py::Bool(true) <= 1,                  true);
    EXPECT_EQ(py::Bool(true) <= 2,                  true);
    EXPECT_EQ(py::Bool(false) <= py::Int(0),        true);
    EXPECT_EQ(py::Bool(false) <= py::Int(1),        true);
    EXPECT_EQ(py::Bool(true) <= py::Int(0),         false);
    EXPECT_EQ(py::Bool(true) <= py::Int(1),         true);
    EXPECT_EQ(py::Bool(true) <= py::Int(2),         true);

    // py::Bool <= double
    EXPECT_EQ(py::Bool(false) <= 0.0,               true);
    EXPECT_EQ(py::Bool(false) <= 1.0,               true);
    EXPECT_EQ(py::Bool(true) <= 0.0,                false);
    EXPECT_EQ(py::Bool(true) <= 1.0,                true);
    EXPECT_EQ(py::Bool(true) <= 2.0,                true);
    EXPECT_EQ(py::Bool(false) <= py::Float(0.0),    true);
    EXPECT_EQ(py::Bool(false) <= py::Float(1.0),    true);
    EXPECT_EQ(py::Bool(true) <= py::Float(0.0),     false);
    EXPECT_EQ(py::Bool(true) <= py::Float(1.0),     true);
    EXPECT_EQ(py::Bool(true) <= py::Float(2.0),     true);

    // py::Bool <= T
    assertions::less_than_or_equal_to<py::Bool, std::string>::invalid();
    assertions::less_than_or_equal_to<py::Bool, std::tuple<>>::invalid();
    assertions::less_than_or_equal_to<py::Bool, std::vector<int>>::invalid();
    assertions::less_than_or_equal_to<py::Bool, std::unordered_set<int>>::invalid();
    assertions::less_than_or_equal_to<py::Bool, std::unordered_map<int, int>>::invalid();
    assertions::less_than_or_equal_to<py::Bool, py::Str>::invalid();
    assertions::less_than_or_equal_to<py::Bool, py::Tuple>::invalid();
    assertions::less_than_or_equal_to<py::Bool, py::List>::invalid();
    assertions::less_than_or_equal_to<py::Bool, py::Set>::invalid();
    assertions::less_than_or_equal_to<py::Bool, py::Dict>::invalid();
}


TEST(py_bool, equal_to) {
    // py::Bool == bool
    EXPECT_EQ(py::Bool(false) == false,             true);
    EXPECT_EQ(py::Bool(false) == true,              false);
    EXPECT_EQ(py::Bool(true) == false,              false);
    EXPECT_EQ(py::Bool(true) == true,               true);
    EXPECT_EQ(py::Bool(false) == py::Bool(false),   true);
    EXPECT_EQ(py::Bool(false) == py::Bool(true),    false);
    EXPECT_EQ(py::Bool(true) == py::Bool(false),    false);
    EXPECT_EQ(py::Bool(true) == py::Bool(true),     true);

    // py::Bool == int
    EXPECT_EQ(py::Bool(false) == 0,                 true);
    EXPECT_EQ(py::Bool(false) == 1,                 false);
    EXPECT_EQ(py::Bool(true) == 0,                  false);
    EXPECT_EQ(py::Bool(true) == 1,                  true);
    EXPECT_EQ(py::Bool(true) == 2,                  false);
    EXPECT_EQ(py::Bool(false) == py::Int(0),        true);
    EXPECT_EQ(py::Bool(false) == py::Int(1),        false);
    EXPECT_EQ(py::Bool(true) == py::Int(0),         false);
    EXPECT_EQ(py::Bool(true) == py::Int(1),         true);
    EXPECT_EQ(py::Bool(true) == py::Int(2),         false);

    // py::Bool == float
    EXPECT_EQ(py::Bool(false) == 0.0,               true);
    EXPECT_EQ(py::Bool(false) == 1.0,               false);
    EXPECT_EQ(py::Bool(true) == 0.0,                false);
    EXPECT_EQ(py::Bool(true) == 1.0,                true);
    EXPECT_EQ(py::Bool(true) == 2.0,                false);
    EXPECT_EQ(py::Bool(false) == py::Float(0.0),    true);
    EXPECT_EQ(py::Bool(false) == py::Float(1.0),    false);
    EXPECT_EQ(py::Bool(true) == py::Float(0.0),     false);
    EXPECT_EQ(py::Bool(true) == py::Float(1.0),     true);
    EXPECT_EQ(py::Bool(true) == py::Float(2.0),     false);

    // py::Bool == T
    auto d = std::unordered_map<int, int>{};
    EXPECT_EQ(py::Bool(false) == "abc",                             false);
    EXPECT_EQ(py::Bool(true) == std::tuple<>{},                     false);
    EXPECT_EQ(py::Bool(false) == std::vector<int>{},                false);
    EXPECT_EQ(py::Bool(true) == std::unordered_set<int>{},          false);
    EXPECT_EQ(py::Bool(false) == d,                                 false);
    EXPECT_EQ(py::Bool(true) == py::Str("abc"),                     false);
    EXPECT_EQ(py::Bool(false) == py::Tuple{},                       false);
    EXPECT_EQ(py::Bool(true) == py::List{},                         false);
    EXPECT_EQ(py::Bool(false) == py::Set{},                         false);
    EXPECT_EQ(py::Bool(true) == py::Dict{},                         false);
}


TEST(py_bool, not_equal_to) {
    // py::Bool != bool
    EXPECT_EQ(py::Bool(false) != false,             false);
    EXPECT_EQ(py::Bool(false) != true,              true);
    EXPECT_EQ(py::Bool(true) != false,              true);
    EXPECT_EQ(py::Bool(true) != true,               false);
    EXPECT_EQ(py::Bool(false) != py::Bool(false),   false);
    EXPECT_EQ(py::Bool(false) != py::Bool(true),    true);
    EXPECT_EQ(py::Bool(true) != py::Bool(false),    true);
    EXPECT_EQ(py::Bool(true) != py::Bool(true),     false);

    // py::Bool != int
    EXPECT_EQ(py::Bool(false) != 0,                 false);
    EXPECT_EQ(py::Bool(false) != 1,                 true);
    EXPECT_EQ(py::Bool(true) != 0,                  true);
    EXPECT_EQ(py::Bool(true) != 1,                  false);
    EXPECT_EQ(py::Bool(true) != 2,                  true);
    EXPECT_EQ(py::Bool(false) != py::Int(0),        false);
    EXPECT_EQ(py::Bool(false) != py::Int(1),        true);
    EXPECT_EQ(py::Bool(true) != py::Int(0),         true);
    EXPECT_EQ(py::Bool(true) != py::Int(1),         false);
    EXPECT_EQ(py::Bool(true) != py::Int(2),         true);

    // py::Bool != float
    EXPECT_EQ(py::Bool(false) != 0.0,               false);
    EXPECT_EQ(py::Bool(false) != 1.0,               true);
    EXPECT_EQ(py::Bool(true) != 0.0,                true);
    EXPECT_EQ(py::Bool(true) != 1.0,                false);
    EXPECT_EQ(py::Bool(true) != 2.0,                true);
    EXPECT_EQ(py::Bool(false) != py::Float(0.0),    false);
    EXPECT_EQ(py::Bool(false) != py::Float(1.0),    true);
    EXPECT_EQ(py::Bool(true) != py::Float(0.0),     true);
    EXPECT_EQ(py::Bool(true) != py::Float(1.0),     false);
    EXPECT_EQ(py::Bool(true) != py::Float(2.0),     true);

    // py::Bool != T
    auto d = std::unordered_map<int, int>{};
    EXPECT_EQ(py::Bool(false) != "abc",                             true);
    EXPECT_EQ(py::Bool(true) != std::tuple<>{},                     true);
    EXPECT_EQ(py::Bool(false) != std::vector<int>{},                true);
    EXPECT_EQ(py::Bool(true) != std::unordered_set<int>{},          true);
    EXPECT_EQ(py::Bool(false) != d,                                 true);
    EXPECT_EQ(py::Bool(true) != py::Str("abc"),                     true);
    EXPECT_EQ(py::Bool(false) != py::Tuple{},                       true);
    EXPECT_EQ(py::Bool(true) != py::List{},                         true);
    EXPECT_EQ(py::Bool(false) != py::Set{},                         true);
    EXPECT_EQ(py::Bool(true) != py::Dict{},                         true);
}


TEST(py_bool, greater_than_or_equal_to) {
    // py::Bool >= bool
    EXPECT_EQ(py::Bool(false) >= false,             true);
    EXPECT_EQ(py::Bool(false) >= true,              false);
    EXPECT_EQ(py::Bool(true) >= false,              true);
    EXPECT_EQ(py::Bool(true) >= true,               true);
    EXPECT_EQ(py::Bool(false) >= py::Bool(false),   true);
    EXPECT_EQ(py::Bool(false) >= py::Bool(true),    false);
    EXPECT_EQ(py::Bool(true) >= py::Bool(false),    true);
    EXPECT_EQ(py::Bool(true) >= py::Bool(true),     true);

    // py::Bool >= int
    EXPECT_EQ(py::Bool(false) >= 0,                 true);
    EXPECT_EQ(py::Bool(false) >= 1,                 false);
    EXPECT_EQ(py::Bool(true) >= 0,                  true);
    EXPECT_EQ(py::Bool(true) >= 1,                  true);
    EXPECT_EQ(py::Bool(true) >= 2,                  false);
    EXPECT_EQ(py::Bool(false) >= py::Int(0),        true);
    EXPECT_EQ(py::Bool(false) >= py::Int(1),        false);
    EXPECT_EQ(py::Bool(true) >= py::Int(0),         true);
    EXPECT_EQ(py::Bool(true) >= py::Int(1),         true);
    EXPECT_EQ(py::Bool(true) >= py::Int(2),         false);

    // py::Bool >= float
    EXPECT_EQ(py::Bool(false) >= 0.0,               true);
    EXPECT_EQ(py::Bool(false) >= 1.0,               false);
    EXPECT_EQ(py::Bool(true) >= 0.0,                true);
    EXPECT_EQ(py::Bool(true) >= 1.0,                true);
    EXPECT_EQ(py::Bool(true) >= 2.0,                false);
    EXPECT_EQ(py::Bool(false) >= py::Float(0.0),    true);
    EXPECT_EQ(py::Bool(false) >= py::Float(1.0),    false);
    EXPECT_EQ(py::Bool(true) >= py::Float(0.0),     true);
    EXPECT_EQ(py::Bool(true) >= py::Float(1.0),     true);
    EXPECT_EQ(py::Bool(true) >= py::Float(2.0),     false);

    // py::Bool >= T
    assertions::greater_than_or_equal_to<py::Bool, std::string>::invalid();
    assertions::greater_than_or_equal_to<py::Bool, std::tuple<>>::invalid();
    assertions::greater_than_or_equal_to<py::Bool, std::vector<int>>::invalid();
    assertions::greater_than_or_equal_to<py::Bool, std::unordered_set<int>>::invalid();
    assertions::greater_than_or_equal_to<py::Bool, std::unordered_map<int, int>>::invalid();
    assertions::greater_than_or_equal_to<py::Bool, py::Str>::invalid();
    assertions::greater_than_or_equal_to<py::Bool, py::Tuple>::invalid();
    assertions::greater_than_or_equal_to<py::Bool, py::List>::invalid();
    assertions::greater_than_or_equal_to<py::Bool, py::Set>::invalid();
    assertions::greater_than_or_equal_to<py::Bool, py::Dict>::invalid();
}


TEST(py_bool, greater_than) {
    // py::Bool > bool
    EXPECT_EQ(py::Bool(false) > false,              false);
    EXPECT_EQ(py::Bool(false) > true,               false);
    EXPECT_EQ(py::Bool(true) > false,               true);
    EXPECT_EQ(py::Bool(true) > true,                false);
    EXPECT_EQ(py::Bool(false) > py::Bool(false),    false);
    EXPECT_EQ(py::Bool(false) > py::Bool(true),     false);
    EXPECT_EQ(py::Bool(true) > py::Bool(false),     true);
    EXPECT_EQ(py::Bool(true) > py::Bool(true),      false);

    // py::Bool > int
    EXPECT_EQ(py::Bool(false) > 0,                  false);
    EXPECT_EQ(py::Bool(false) > 1,                  false);
    EXPECT_EQ(py::Bool(true) > 0,                   true);
    EXPECT_EQ(py::Bool(true) > 1,                   false);
    EXPECT_EQ(py::Bool(true) > 2,                   false);
    EXPECT_EQ(py::Bool(false) > py::Int(0),         false);
    EXPECT_EQ(py::Bool(false) > py::Int(1),         false);
    EXPECT_EQ(py::Bool(true) > py::Int(0),          true);
    EXPECT_EQ(py::Bool(true) > py::Int(1),          false);
    EXPECT_EQ(py::Bool(true) > py::Int(2),          false);

    // py::Bool > float
    EXPECT_EQ(py::Bool(false) > 0.0,                false);
    EXPECT_EQ(py::Bool(false) > 1.0,                false);
    EXPECT_EQ(py::Bool(true) > 0.0,                 true);
    EXPECT_EQ(py::Bool(true) > 1.0,                 false);
    EXPECT_EQ(py::Bool(true) > 2.0,                 false);
    EXPECT_EQ(py::Bool(false) > py::Float(0.0),     false);
    EXPECT_EQ(py::Bool(false) > py::Float(1.0),     false);
    EXPECT_EQ(py::Bool(true) > py::Float(0.0),      true);
    EXPECT_EQ(py::Bool(true) > py::Float(1.0),      false);
    EXPECT_EQ(py::Bool(true) > py::Float(2.0),      false);

    // py::Bool > T
    assertions::greater_than<py::Bool, std::string>::invalid();
    assertions::greater_than<py::Bool, std::tuple<>>::invalid();
    assertions::greater_than<py::Bool, std::vector<int>>::invalid();
    assertions::greater_than<py::Bool, std::unordered_set<int>>::invalid();
    assertions::greater_than<py::Bool, std::unordered_map<int, int>>::invalid();
    assertions::greater_than<py::Bool, py::Str>::invalid();
    assertions::greater_than<py::Bool, py::Tuple>::invalid();
    assertions::greater_than<py::Bool, py::List>::invalid();
    assertions::greater_than<py::Bool, py::Set>::invalid();
    assertions::greater_than<py::Bool, py::Dict>::invalid();
}


TEST(py_bool, invert) {
    assertions::unary_invert<py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(~py::Bool(true), -2);
    EXPECT_EQ(~py::Bool(false), -1);
}


TEST(py_bool, plus) {
    // +py::Bool
    assertions::unary_plus<py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(+py::Bool(true), 1);
    EXPECT_EQ(+py::Bool(false), 0);

    // ++py::Bool, py::Bool++
    assertions::pre_increment<py::Bool>::invalid();
    assertions::post_increment<py::Bool>::invalid();

    // py::Bool + bool
    assertions::binary_plus<py::Bool, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) + true,                2);
    EXPECT_EQ(py::Bool(true) + false,               1);
    EXPECT_EQ(py::Bool(false) + true,               1);
    EXPECT_EQ(py::Bool(false) + false,              0);
    assertions::binary_plus<py::Bool, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) + py::Bool(true),      2);
    EXPECT_EQ(py::Bool(true) + py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) + py::Bool(true),     1);
    EXPECT_EQ(py::Bool(false) + py::Bool(false),    0);

    // py::Bool + int
    assertions::binary_plus<py::Bool, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) + 1,                   2);
    EXPECT_EQ(py::Bool(true) + 0,                   1);
    EXPECT_EQ(py::Bool(false) + 1,                  1);
    EXPECT_EQ(py::Bool(false) + 0,                  0);
    assertions::binary_plus<py::Bool, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) + py::Int(1),          2);
    EXPECT_EQ(py::Bool(true) + py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) + py::Int(1),         1);
    EXPECT_EQ(py::Bool(false) + py::Int(0),         0);

    // py::Bool + float
    assertions::binary_plus<py::Bool, double>::returns<py::Float>::valid();
    EXPECT_EQ(py::Bool(true) + 1.0,                 2.0);
    EXPECT_EQ(py::Bool(true) + 0.0,                 1.0);
    EXPECT_EQ(py::Bool(false) + 1.0,                1.0);
    EXPECT_EQ(py::Bool(false) + 0.0,                0.0);
    assertions::binary_plus<py::Bool, py::Float>::returns<py::Float>::valid();
    EXPECT_EQ(py::Bool(true) + py::Float(1.0),      2.0);
    EXPECT_EQ(py::Bool(true) + py::Float(0.0),      1.0);
    EXPECT_EQ(py::Bool(false) + py::Float(1.0),     1.0);
    EXPECT_EQ(py::Bool(false) + py::Float(0.0),     0.0);

    // py::Bool + complex
    assertions::binary_plus<py::Bool, std::complex<double>>::returns<py::Complex>::valid();
    assertions::binary_plus<py::Bool, py::Complex>::returns<py::Complex>::valid();

    // py::Bool + decimal
    // assertions::binary_plus<py::Bool, py::Decimal>::returns<py::Decimal>::valid();

    // py::Bool + T
    assertions::binary_plus<py::Bool, const char*>::invalid();
    assertions::binary_plus<py::Bool, std::string>::invalid();
    assertions::binary_plus<py::Bool, std::string_view>::invalid();
    assertions::binary_plus<py::Bool, py::Str>::invalid();

    // py::Bool += T
    assertions::inplace_plus<py::Bool, bool>::invalid();
    assertions::inplace_plus<py::Bool, int>::invalid();
    assertions::inplace_plus<py::Bool, double>::invalid();
    assertions::inplace_plus<py::Bool, py::Bool>::invalid();
    assertions::inplace_plus<py::Bool, py::Int>::invalid();
    assertions::inplace_plus<py::Bool, py::Float>::invalid();
}


TEST(py_bool, minus) {
    // -py::Bool
    assertions::unary_minus<py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(-py::Bool(true), -1);
    EXPECT_EQ(-py::Bool(false), 0);

    // --py::Bool, py::Bool--
    assertions::pre_decrement<py::Bool>::invalid();
    assertions::post_decrement<py::Bool>::returns<py::Int>::invalid();

    // py::Bool - bool
    assertions::binary_minus<py::Bool, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) - true,                0);
    EXPECT_EQ(py::Bool(true) - false,               1);
    EXPECT_EQ(py::Bool(false) - true,               -1);
    EXPECT_EQ(py::Bool(false) - false,              0);
    assertions::binary_minus<py::Bool, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) - py::Bool(true),      0);
    EXPECT_EQ(py::Bool(true) - py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) - py::Bool(true),     -1);
    EXPECT_EQ(py::Bool(false) - py::Bool(false),    0);

    // py::Bool - int
    assertions::binary_minus<py::Bool, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) - 1,                   0);
    EXPECT_EQ(py::Bool(true) - 0,                   1);
    EXPECT_EQ(py::Bool(false) - 1,                  -1);
    EXPECT_EQ(py::Bool(false) - 0,                  0);
    assertions::binary_minus<py::Bool, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) - py::Int(1),          0);
    EXPECT_EQ(py::Bool(true) - py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) - py::Int(1),         -1);
    EXPECT_EQ(py::Bool(false) - py::Int(0),         0);

    // py::Bool - float
    assertions::binary_minus<py::Bool, double>::returns<py::Float>::valid();
    EXPECT_EQ(py::Bool(true) - 1.0,                 0.0);
    EXPECT_EQ(py::Bool(true) - 0.0,                 1.0);
    EXPECT_EQ(py::Bool(false) - 1.0,                -1.0);
    EXPECT_EQ(py::Bool(false) - 0.0,                0.0);
    assertions::binary_minus<py::Bool, py::Float>::returns<py::Float>::valid();
    EXPECT_EQ(py::Bool(true) - py::Float(1.0),      0.0);
    EXPECT_EQ(py::Bool(true) - py::Float(0.0),      1.0);
    EXPECT_EQ(py::Bool(false) - py::Float(1.0),     -1.0);
    EXPECT_EQ(py::Bool(false) - py::Float(0.0),     0.0);

    // py::Bool - complex
    assertions::binary_minus<py::Bool, std::complex<double>>::returns<py::Complex>::valid();
    assertions::binary_minus<py::Bool, py::Complex>::returns<py::Complex>::valid();

    // py::Bool - decimal
    // assertions::binary_minus<py::Bool, py::Decimal>::returns<py::Decimal>::valid();

    // py::Bool -= T
    assertions::inplace_minus<py::Bool, bool>::invalid();
    assertions::inplace_minus<py::Bool, int>::invalid();
    assertions::inplace_minus<py::Bool, double>::invalid();
    assertions::inplace_minus<py::Bool, py::Bool>::invalid();
    assertions::inplace_minus<py::Bool, py::Int>::invalid();
    assertions::inplace_minus<py::Bool, py::Float>::invalid();

}


TEST(py_bool, multiply) {
    // py::Bool * bool
    assertions::binary_multiply<py::Bool, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) * true,                1);
    EXPECT_EQ(py::Bool(true) * false,               0);
    EXPECT_EQ(py::Bool(false) * true,               0);
    EXPECT_EQ(py::Bool(false) * false,              0);
    assertions::binary_multiply<py::Bool, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) * py::Bool(true),      1);
    EXPECT_EQ(py::Bool(true) * py::Bool(false),     0);
    EXPECT_EQ(py::Bool(false) * py::Bool(true),     0);
    EXPECT_EQ(py::Bool(false) * py::Bool(false),    0);

    // py::Bool * int
    assertions::binary_multiply<py::Bool, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) * 1,                   1);
    EXPECT_EQ(py::Bool(true) * 0,                   0);
    EXPECT_EQ(py::Bool(false) * 1,                  0);
    EXPECT_EQ(py::Bool(false) * 0,                  0);
    assertions::binary_multiply<py::Bool, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) * py::Int(1),          1);
    EXPECT_EQ(py::Bool(true) * py::Int(0),          0);
    EXPECT_EQ(py::Bool(false) * py::Int(1),         0);
    EXPECT_EQ(py::Bool(false) * py::Int(0),         0);

    // py::Bool * float
    assertions::binary_multiply<py::Bool, double>::returns<py::Float>::valid();
    EXPECT_EQ(py::Bool(true) * 1.0,                 1.0);
    EXPECT_EQ(py::Bool(true) * 0.0,                 0.0);
    EXPECT_EQ(py::Bool(false) * 1.0,                0.0);
    EXPECT_EQ(py::Bool(false) * 0.0,                0.0);
    assertions::binary_multiply<py::Bool, py::Float>::returns<py::Float>::valid();
    EXPECT_EQ(py::Bool(true) * py::Float(1.0),      1.0);
    EXPECT_EQ(py::Bool(true) * py::Float(0.0),      0.0);
    EXPECT_EQ(py::Bool(false) * py::Float(1.0),     0.0);
    EXPECT_EQ(py::Bool(false) * py::Float(0.0),     0.0);

    // py::Bool * complex
    assertions::binary_multiply<py::Bool, std::complex<double>>::returns<py::Complex>::valid();
    assertions::binary_multiply<py::Bool, py::Complex>::returns<py::Complex>::valid();

    // py::Bool * decimal
    // assertions::binary_multiply<py::Bool, py::Decimal>::returns<py::Decimal>::valid();

    // py::Bool *= T
    assertions::inplace_multiply<py::Bool, bool>::invalid();
    assertions::inplace_multiply<py::Bool, int>::invalid();
    assertions::inplace_multiply<py::Bool, double>::invalid();
    assertions::inplace_multiply<py::Bool, py::Bool>::invalid();
    assertions::inplace_multiply<py::Bool, py::Int>::invalid();
    assertions::inplace_multiply<py::Bool, py::Float>::invalid();
}


TEST(py_bool, divide) {
    // py::Bool / bool
    assertions::binary_divide<py::Bool, bool>::returns<py::Float>::valid();
    EXPECT_EQ(      py::Bool(true) / true,              1.0);
    EXPECT_THROW(   py::Bool(true) / false,             py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / true,             0.0);
    EXPECT_THROW(   py::Bool(false) / false,            py::error_already_set);
    assertions::binary_divide<py::Bool, py::Bool>::returns<py::Float>::valid();
    EXPECT_EQ(      py::Bool(true) / py::Bool(true),    1.0);
    EXPECT_THROW(   py::Bool(true) / py::Bool(false),   py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / py::Bool(true),   0.0);
    EXPECT_THROW(   py::Bool(false) / py::Bool(false),  py::error_already_set);

    // py::Bool / int
    assertions::binary_divide<py::Bool, int>::returns<py::Float>::valid();
    EXPECT_EQ(      py::Bool(true) / 1,                 1.0);
    EXPECT_THROW(   py::Bool(true) / 0,                 py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / 1,                0.0);
    EXPECT_THROW(   py::Bool(false) / 0,                py::error_already_set);
    assertions::binary_divide<py::Bool, py::Int>::returns<py::Float>::valid();
    EXPECT_EQ(      py::Bool(true) / py::Int(1),        1.0);
    EXPECT_THROW(   py::Bool(true) / py::Int(0),        py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / py::Int(1),       0.0);
    EXPECT_THROW(   py::Bool(false) / py::Int(0),       py::error_already_set);

    // py::Bool / float
    assertions::binary_divide<py::Bool, double>::returns<py::Float>::valid();
    EXPECT_EQ(      py::Bool(true) / 1.0,               1.0);
    EXPECT_THROW(   py::Bool(true) / 0.0,               py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / 1.0,              0.0);
    EXPECT_THROW(   py::Bool(false) / 0.0,              py::error_already_set);
    assertions::binary_divide<py::Bool, py::Float>::returns<py::Float>::valid();
    EXPECT_EQ(      py::Bool(true) / py::Float(1.0),    1.0);
    EXPECT_THROW(   py::Bool(true) / py::Float(0.0),    py::error_already_set);
    EXPECT_EQ(      py::Bool(false) / py::Float(1.0),   0.0);
    EXPECT_THROW(   py::Bool(false) / py::Float(0.0),   py::error_already_set);

    // py::Bool / complex
    assertions::binary_divide<py::Bool, std::complex<double>>::returns<py::Complex>::valid();
    assertions::binary_divide<py::Bool, py::Complex>::returns<py::Complex>::valid();

    // py::Bool / decimal
    // assertions::binary_divide<py::Bool, py::Decimal>::returns<py::Decimal>::valid();

    // py::Bool /= T
    assertions::inplace_divide<py::Bool, bool>::invalid();
    assertions::inplace_divide<py::Bool, int>::invalid();
    assertions::inplace_divide<py::Bool, double>::invalid();
    assertions::inplace_divide<py::Bool, py::Bool>::invalid();
    assertions::inplace_divide<py::Bool, py::Int>::invalid();
    assertions::inplace_divide<py::Bool, py::Float>::invalid();
}


TEST(py_bool, modulo) {
    // py::Bool % bool
    assertions::binary_modulo<py::Bool, bool>::returns<py::Int>::valid();
    EXPECT_EQ(      py::Bool(true) % true,              0);
    EXPECT_THROW(   py::Bool(true) % false,             py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % true,             0);
    EXPECT_THROW(   py::Bool(false) % false,            py::error_already_set);
    assertions::binary_modulo<py::Bool, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(      py::Bool(true) % py::Bool(true),    0);
    EXPECT_THROW(   py::Bool(true) % py::Bool(false),   py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % py::Bool(true),   0);
    EXPECT_THROW(   py::Bool(false) % py::Bool(false),  py::error_already_set);

    // py::Bool % int
    assertions::binary_modulo<py::Bool, int>::returns<py::Int>::valid();
    EXPECT_EQ(      py::Bool(true) % 1,                 0);
    EXPECT_THROW(   py::Bool(true) % 0,                 py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % 1,                0);
    EXPECT_THROW(   py::Bool(false) % 0,                py::error_already_set);
    assertions::binary_modulo<py::Bool, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(      py::Bool(true) % py::Int(1),        0);
    EXPECT_THROW(   py::Bool(true) % py::Int(0),        py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % py::Int(1),       0);
    EXPECT_THROW(   py::Bool(false) % py::Int(0),       py::error_already_set);

    // py::Bool % float
    assertions::binary_modulo<py::Bool, double>::returns<py::Float>::valid();
    EXPECT_EQ(      py::Bool(true) % 1.0,               0.0);
    EXPECT_THROW(   py::Bool(true) % 0.0,               py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % 1.0,              0.0);
    EXPECT_THROW(   py::Bool(false) % 0.0,              py::error_already_set);
    assertions::binary_modulo<py::Bool, py::Float>::returns<py::Float>::valid();
    EXPECT_EQ(      py::Bool(true) % py::Float(1.0),    0.0);
    EXPECT_THROW(   py::Bool(true) % py::Float(0.0),    py::error_already_set);
    EXPECT_EQ(      py::Bool(false) % py::Float(1.0),   0.0);
    EXPECT_THROW(   py::Bool(false) % py::Float(0.0),   py::error_already_set);

    // py::Bool % complex
    assertions::binary_modulo<py::Bool, std::complex<double>>::invalid();
    assertions::binary_modulo<py::Bool, py::Complex>::invalid();

    // py::Bool % decimal
    // assertions::binary_modulo<py::Bool, py::Decimal>::returns<py::Decimal>::valid();

    // py::Bool %= T
    assertions::inplace_modulo<py::Bool, bool>::invalid();
    assertions::inplace_modulo<py::Bool, int>::invalid();
    assertions::inplace_modulo<py::Bool, double>::invalid();
    assertions::inplace_modulo<py::Bool, py::Bool>::invalid();
    assertions::inplace_modulo<py::Bool, py::Int>::invalid();
    assertions::inplace_modulo<py::Bool, py::Float>::invalid();
}


TEST(py_bool, left_shift) {
    // py::Bool << bool
    assertions::left_shift<py::Bool, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) << true,                2);
    EXPECT_EQ(py::Bool(true) << false,               1);
    EXPECT_EQ(py::Bool(false) << true,               0);
    EXPECT_EQ(py::Bool(false) << false,              0);
    assertions::left_shift<py::Bool, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) << py::Bool(true),      2);
    EXPECT_EQ(py::Bool(true) << py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) << py::Bool(true),     0);
    EXPECT_EQ(py::Bool(false) << py::Bool(false),    0);

    // py::Bool << int
    assertions::left_shift<py::Bool, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) << 1,                   2);
    EXPECT_EQ(py::Bool(true) << 0,                   1);
    EXPECT_EQ(py::Bool(false) << 1,                  0);
    EXPECT_EQ(py::Bool(false) << 0,                  0);
    assertions::left_shift<py::Bool, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) << py::Int(1),          2);
    EXPECT_EQ(py::Bool(true) << py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) << py::Int(1),         0);
    EXPECT_EQ(py::Bool(false) << py::Int(0),         0);

    // py::Bool << float
    assertions::left_shift<py::Bool, double>::returns<py::Int>::invalid();
    assertions::left_shift<py::Bool, py::Float>::returns<py::Int>::invalid();

    // py::Bool <<= T
    assertions::inplace_left_shift<py::Bool, bool>::invalid();
    assertions::inplace_left_shift<py::Bool, int>::invalid();
    assertions::inplace_left_shift<py::Bool, py::Bool>::invalid();
    assertions::inplace_left_shift<py::Bool, py::Int>::invalid();
}


TEST(py_bool, right_shift) {
    // py::Bool >> bool
    assertions::right_shift<py::Bool, bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) >> true,                0);
    EXPECT_EQ(py::Bool(true) >> false,               1);
    EXPECT_EQ(py::Bool(false) >> true,               0);
    EXPECT_EQ(py::Bool(false) >> false,              0);
    assertions::right_shift<py::Bool, py::Bool>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) >> py::Bool(true),      0);
    EXPECT_EQ(py::Bool(true) >> py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) >> py::Bool(true),     0);
    EXPECT_EQ(py::Bool(false) >> py::Bool(false),    0);

    // py::Bool >> int
    assertions::right_shift<py::Bool, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) >> 1,                   0);
    EXPECT_EQ(py::Bool(true) >> 0,                   1);
    EXPECT_EQ(py::Bool(false) >> 1,                  0);
    EXPECT_EQ(py::Bool(false) >> 0,                  0);
    assertions::right_shift<py::Bool, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) >> py::Int(1),          0);
    EXPECT_EQ(py::Bool(true) >> py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) >> py::Int(1),         0);
    EXPECT_EQ(py::Bool(false) >> py::Int(0),         0);

    // py::Bool >> float
    assertions::right_shift<py::Bool, double>::returns<py::Int>::invalid();
    assertions::right_shift<py::Bool, py::Float>::returns<py::Int>::invalid();

    // py::Bool >>= T
    assertions::inplace_right_shift<py::Bool, bool>::invalid();
    assertions::inplace_right_shift<py::Bool, int>::invalid();
    assertions::inplace_right_shift<py::Bool, py::Bool>::invalid();
    assertions::inplace_right_shift<py::Bool, py::Int>::invalid();
}


TEST(py_bool, bitwise_and) {
    // py::Bool & bool
    assertions::bitwise_and<py::Bool, bool>::returns<py::Bool>::valid();
    EXPECT_EQ(py::Bool(true) & true,                1);
    EXPECT_EQ(py::Bool(true) & false,               0);
    EXPECT_EQ(py::Bool(false) & true,               0);
    EXPECT_EQ(py::Bool(false) & false,              0);
    assertions::bitwise_and<py::Bool, py::Bool>::returns<py::Bool>::valid();
    EXPECT_EQ(py::Bool(true) & py::Bool(true),      1);
    EXPECT_EQ(py::Bool(true) & py::Bool(false),     0);
    EXPECT_EQ(py::Bool(false) & py::Bool(true),     0);
    EXPECT_EQ(py::Bool(false) & py::Bool(false),    0);

    // py::Bool & int
    assertions::bitwise_and<py::Bool, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) & 1,                   1);
    EXPECT_EQ(py::Bool(true) & 0,                   0);
    EXPECT_EQ(py::Bool(false) & 1,                  0);
    EXPECT_EQ(py::Bool(false) & 0,                  0);
    assertions::bitwise_and<py::Bool, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) & py::Int(1),          1);
    EXPECT_EQ(py::Bool(true) & py::Int(0),          0);
    EXPECT_EQ(py::Bool(false) & py::Int(1),         0);
    EXPECT_EQ(py::Bool(false) & py::Int(0),         0);

    // py::Bool & T
    assertions::bitwise_and<py::Bool, double>::returns<py::Int>::invalid();
    assertions::bitwise_and<py::Bool, py::Float>::returns<py::Int>::invalid();

    // py::Bool &= bool
    EXPECT_EQ(py::Bool(true) &= true,               true);
    EXPECT_EQ(py::Bool(true) &= false,              false);
    EXPECT_EQ(py::Bool(false) &= true,              false);
    EXPECT_EQ(py::Bool(false) &= false,             false);
    EXPECT_EQ(py::Bool(true) &= py::Bool(true),     true);
    EXPECT_EQ(py::Bool(true) &= py::Bool(false),    false);
    EXPECT_EQ(py::Bool(false) &= py::Bool(true),    false);
    EXPECT_EQ(py::Bool(false) &= py::Bool(false),   false);

    // py::Bool &= T
    assertions::inplace_bitwise_and<py::Bool, int>::invalid();
    assertions::inplace_bitwise_and<py::Bool, py::Int>::invalid();
    assertions::inplace_bitwise_and<py::Bool, double>::invalid();
    assertions::inplace_bitwise_and<py::Bool, py::Float>::invalid();
}


TEST(py_bool, bitwise_or) {
    // py::Bool | bool
    assertions::bitwise_or<py::Bool, bool>::returns<py::Bool>::valid();
    EXPECT_EQ(py::Bool(true) | true,                1);
    EXPECT_EQ(py::Bool(true) | false,               1);
    EXPECT_EQ(py::Bool(false) | true,               1);
    EXPECT_EQ(py::Bool(false) | false,              0);
    assertions::bitwise_or<py::Bool, py::Bool>::returns<py::Bool>::valid();
    EXPECT_EQ(py::Bool(true) | py::Bool(true),      1);
    EXPECT_EQ(py::Bool(true) | py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) | py::Bool(true),     1);
    EXPECT_EQ(py::Bool(false) | py::Bool(false),    0);

    // py::Bool | int
    assertions::bitwise_or<py::Bool, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) | 1,                   1);
    EXPECT_EQ(py::Bool(true) | 0,                   1);
    EXPECT_EQ(py::Bool(false) | 1,                  1);
    EXPECT_EQ(py::Bool(false) | 0,                  0);
    assertions::bitwise_or<py::Bool, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) | py::Int(1),          1);
    EXPECT_EQ(py::Bool(true) | py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) | py::Int(1),         1);
    EXPECT_EQ(py::Bool(false) | py::Int(0),         0);

    // py::Bool | T
    assertions::bitwise_or<py::Bool, double>::returns<py::Int>::invalid();
    assertions::bitwise_or<py::Bool, py::Float>::returns<py::Int>::invalid();

    // py::Bool |= bool
    EXPECT_EQ(py::Bool(true) |= true,                true);
    EXPECT_EQ(py::Bool(true) |= false,               true);
    EXPECT_EQ(py::Bool(false) |= true,               true);
    EXPECT_EQ(py::Bool(false) |= false,              false);
    EXPECT_EQ(py::Bool(true) |= py::Bool(true),      true);
    EXPECT_EQ(py::Bool(true) |= py::Bool(false),     true);
    EXPECT_EQ(py::Bool(false) |= py::Bool(true),     true);
    EXPECT_EQ(py::Bool(false) |= py::Bool(false),    false);

    // py::Bool |= T
    assertions::inplace_bitwise_or<py::Bool, int>::invalid();
    assertions::inplace_bitwise_or<py::Bool, py::Int>::invalid();
    assertions::inplace_bitwise_or<py::Bool, double>::invalid();
    assertions::inplace_bitwise_or<py::Bool, py::Float>::invalid();
}


TEST(py_bool, bitwise_xor) {
    // py::Bool ^ bool
    assertions::bitwise_xor<py::Bool, bool>::returns<py::Bool>::valid();
    EXPECT_EQ(py::Bool(true) ^ true,                0);
    EXPECT_EQ(py::Bool(true) ^ false,               1);
    EXPECT_EQ(py::Bool(false) ^ true,               1);
    EXPECT_EQ(py::Bool(false) ^ false,              0);
    assertions::bitwise_xor<py::Bool, py::Bool>::returns<py::Bool>::valid();
    EXPECT_EQ(py::Bool(true) ^ py::Bool(true),      0);
    EXPECT_EQ(py::Bool(true) ^ py::Bool(false),     1);
    EXPECT_EQ(py::Bool(false) ^ py::Bool(true),     1);
    EXPECT_EQ(py::Bool(false) ^ py::Bool(false),    0);

    // py::Bool ^ int
    assertions::bitwise_xor<py::Bool, int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) ^ 1,                   0);
    EXPECT_EQ(py::Bool(true) ^ 0,                   1);
    EXPECT_EQ(py::Bool(false) ^ 1,                  1);
    EXPECT_EQ(py::Bool(false) ^ 0,                  0);
    assertions::bitwise_xor<py::Bool, py::Int>::returns<py::Int>::valid();
    EXPECT_EQ(py::Bool(true) ^ py::Int(1),          0);
    EXPECT_EQ(py::Bool(true) ^ py::Int(0),          1);
    EXPECT_EQ(py::Bool(false) ^ py::Int(1),         1);
    EXPECT_EQ(py::Bool(false) ^ py::Int(0),         0);

    // py::Bool ^ T
    assertions::bitwise_xor<py::Bool, double>::returns<py::Int>::invalid();
    assertions::bitwise_xor<py::Bool, py::Float>::returns<py::Int>::invalid();

    // py::Bool ^= bool
    EXPECT_EQ(py::Bool(true) ^= true,                false);
    EXPECT_EQ(py::Bool(true) ^= false,               true);
    EXPECT_EQ(py::Bool(false) ^= true,               true);
    EXPECT_EQ(py::Bool(false) ^= false,              false);
    EXPECT_EQ(py::Bool(true) ^= py::Bool(true),      false);
    EXPECT_EQ(py::Bool(true) ^= py::Bool(false),     true);
    EXPECT_EQ(py::Bool(false) ^= py::Bool(true),     true);
    EXPECT_EQ(py::Bool(false) ^= py::Bool(false),    false);

    // py::Bool ^= T
    assertions::inplace_bitwise_xor<py::Bool, int>::invalid();
    assertions::inplace_bitwise_xor<py::Bool, py::Int>::invalid();
    assertions::inplace_bitwise_xor<py::Bool, double>::invalid();
    assertions::inplace_bitwise_xor<py::Bool, py::Float>::invalid();
}
