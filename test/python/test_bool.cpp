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


TEST(py, bool_like) {
    EXPECT_EQ(py::Bool::like<bool>, true);
    EXPECT_EQ(py::Bool::like<int>, false);
    EXPECT_EQ(py::Bool::like<double>, false);
    EXPECT_EQ(py::Bool::like<std::string>, false);
    EXPECT_EQ(py::Bool::like<py::Bool>, true);
    EXPECT_EQ(py::Bool::like<pybind11::bool_>, true);
    EXPECT_EQ(py::Bool::like<py::Int>, false);
    EXPECT_EQ(py::Bool::like<py::Float>, false);
    EXPECT_EQ(py::Bool::like<py::Str>, false);
}


/////////////////////////////////////
////    IMPLICIT CONSTRUCTORS    ////
/////////////////////////////////////


TEST(py, bool_is_default_constructible) {
    py::Bool a;
    EXPECT_EQ(a, false);
}


TEST(py, bool_is_implicitly_convertible_from_object) {
    py::Object a = py::Bool(false);
    py::Object b = py::Int(0);
    py::Object c = py::Float(0.0);
    py::Object d = py::Str("");
    py::Object e = py::Tuple{};
    py::Object f = py::List{};
    py::Object g = py::Set{};
    py::Object h = py::Dict{};

    // implicit conversions
    py::Bool a2 = a;
    py::Bool b2 = b;
    py::Bool c2 = c;
    py::Bool d2 = d;
    py::Bool e2 = e;
    py::Bool f2 = f;
    py::Bool g2 = g;
    py::Bool h2 = h;

    EXPECT_EQ(a2, false);
    EXPECT_EQ(b2, false);
    EXPECT_EQ(c2, false);
    EXPECT_EQ(d2, false);
    EXPECT_EQ(e2, false);
    EXPECT_EQ(f2, false);
    EXPECT_EQ(g2, false);
    EXPECT_EQ(h2, false);

    py::Object i = py::Bool(true);
    py::Object j = py::Int(1);
    py::Object k = py::Float(1.0);
    py::Object l = py::Str("a");
    py::Object m = py::Tuple{1};
    py::Object n = py::List{1};
    py::Object o = py::Set{1};
    py::Object p = py::Dict{{"a", 1}};

    // implicit conversions
    py::Bool i2 = i;
    py::Bool j2 = j;
    py::Bool k2 = k;
    py::Bool l2 = l;
    py::Bool m2 = m;
    py::Bool n2 = n;
    py::Bool o2 = o;
    py::Bool p2 = p;

    EXPECT_EQ(i2, true);
    EXPECT_EQ(j2, true);
    EXPECT_EQ(k2, true);
    EXPECT_EQ(l2, true);
    EXPECT_EQ(m2, true);
    EXPECT_EQ(n2, true);
    EXPECT_EQ(o2, true);
    EXPECT_EQ(p2, true);
}



/////////////////////////////////////
////    EXPLICIT CONSTRUCTORS    ////
/////////////////////////////////////







TEST(py, bool_is_copy_constructible) {
    py::Bool a = true;
    py::Bool b = false;
    int a_refs = a.ref_count();
    int b_refs = b.ref_count();
    py::Bool c = a;
    py::Bool d = b;
    EXPECT_EQ(c.ref_count(), a_refs + 1);
    EXPECT_EQ(d.ref_count(), b_refs + 1);
    EXPECT_EQ(c, true);
    EXPECT_EQ(d, false);

    pybind11::bool_ e = true;
    pybind11::bool_ f = false;
    int e_refs = e.ref_count();
    int f_refs = f.ref_count();
    py::Bool g = e;
    py::Bool h = f;
    EXPECT_EQ(g.ref_count(), e_refs + 1);
    EXPECT_EQ(h.ref_count(), f_refs + 1);
    EXPECT_EQ(g, true);
    EXPECT_EQ(h, false);
}


TEST(py, bool_is_move_constructible) {
    py::Bool a = true;
    py::Bool b = false;
    int a_refs = a.ref_count();
    int b_refs = b.ref_count();
    py::Bool c = std::move(a);
    py::Bool d = std::move(b);
    EXPECT_EQ(c.ref_count(), a_refs);
    EXPECT_EQ(d.ref_count(), b_refs);
    EXPECT_EQ(c, true);
    EXPECT_EQ(d, false);

    pybind11::bool_ e = true;
    pybind11::bool_ f = false;
    int e_refs = e.ref_count();
    int f_refs = f.ref_count();
    py::Bool g = std::move(e);
    py::Bool h = std::move(f);
    EXPECT_EQ(g.ref_count(), e_refs);
    EXPECT_EQ(h.ref_count(), f_refs);
    EXPECT_EQ(g, true);
    EXPECT_EQ(h, false);
}


TEST(py, bool_is_constructible_from_bool) {
    py::Bool a = true;
    py::Bool b = false;
    EXPECT_EQ(a, true);
    EXPECT_EQ(b, false);

    EXPECT_EQ(py::Bool(true), true);
    EXPECT_EQ(py::Bool(false), false);
}


TEST(py, bool_is_constructible_from_int) {
    // TODO: test implicit conversions separately, and assert they raise errors


    EXPECT_EQ(py::Bool(0), false);
    EXPECT_EQ(py::Bool(1), true);
    EXPECT_EQ(py::Bool(2), true);
    EXPECT_EQ(py::Bool(-1), true);
    EXPECT_EQ(py::Bool(py::Int(0)), false);
    EXPECT_EQ(py::Bool(py::Int(1)), true);
    EXPECT_EQ(py::Bool(py::Int(2)), true);
    EXPECT_EQ(py::Bool(py::Int(-1)), true);
}


TEST(py, bool_is_constructible_from_float) {
    EXPECT_EQ(py::Bool(0.0), false);
    EXPECT_EQ(py::Bool(1.0), true);
    EXPECT_EQ(py::Bool(2.0), true);
    EXPECT_EQ(py::Bool(-1.0), true);
    EXPECT_EQ(py::Bool(py::Float(0.0)), false);
    EXPECT_EQ(py::Bool(py::Float(1.0)), true);
    EXPECT_EQ(py::Bool(py::Float(2.0)), true);
    EXPECT_EQ(py::Bool(py::Float(-1.0)), true);
}


TEST(py, bool_is_constructible_from_string) {
    EXPECT_EQ(py::Bool(""), false);
    EXPECT_EQ(py::Bool("a"), true);
    EXPECT_EQ(py::Bool(std::string("")), false);
    EXPECT_EQ(py::Bool(std::string("a")), true);
    EXPECT_EQ(py::Bool(std::string_view("")), false);
    EXPECT_EQ(py::Bool(std::string_view("a")), true);
    EXPECT_EQ(py::Bool(py::Str("")), false);
    EXPECT_EQ(py::Bool(py::Str("a")), true);
}


TEST(py, bool_is_constructible_from_tuple) {
    EXPECT_EQ(py::Bool(std::tuple<>{}), false);
    EXPECT_EQ(py::Bool(std::tuple<int>{1}), true);
    EXPECT_EQ(py::Bool(py::Tuple{}), false);
    EXPECT_EQ(py::Bool(py::Tuple{1}), true);
}


TEST(py, bool_is_constructible_from_list) {
    EXPECT_EQ(py::Bool(std::vector<int>{}), false);
    EXPECT_EQ(py::Bool(std::vector<int>{1}), true);
    EXPECT_EQ(py::Bool(py::List{}), false);
    EXPECT_EQ(py::Bool(py::List{1}), true);
}


TEST(py, bool_is_constructible_from_set) {
    EXPECT_EQ(py::Bool(std::unordered_set<int>{}), false);
    EXPECT_EQ(py::Bool(std::unordered_set<int>{1}), true);
    EXPECT_EQ(py::Bool(py::Set{}), false);
    EXPECT_EQ(py::Bool(py::Set{1}), true);
}


TEST(py, bool_is_constructible_from_dict) {
    EXPECT_EQ(py::Bool(std::unordered_map<int, int>{}), false);
    EXPECT_EQ(py::Bool(std::unordered_map<int, int>{{1, 1}}), true);
    EXPECT_EQ(py::Bool(py::Dict{}), false);
    EXPECT_EQ(py::Bool(py::Dict{{"a", 1}}), true);
}


//////////////////////////
////    ASSIGNMENT    ////
//////////////////////////


// TODO: bool_is_copy_assignable, move_assignable


TEST(py, bool_is_assignable_to_bool) {
    py::Bool a = true;
    py::Bool b = false;
    a = false;
    b = true;
    EXPECT_EQ(a, false);
    EXPECT_EQ(b, true);
}


TEST(py, bool_is_assignable_to_int) {
    bool bool_is_assignable_to_int = assertions::is_assignable<py::Bool, int>;
    bool bool_is_assignable_to_unsigned_int = assertions::is_assignable<py::Bool, unsigned int>;
    bool bool_is_assignable_to_py_int = assertions::is_assignable<py::Bool, py::Int>;
    EXPECT_FALSE(bool_is_assignable_to_int);
    EXPECT_FALSE(bool_is_assignable_to_unsigned_int);
    EXPECT_FALSE(bool_is_assignable_to_py_int);
}


TEST(py, bool_is_assignable_to_float) {
    bool bool_is_assignable_to_float = assertions::is_assignable<py::Bool, double>;
    bool bool_is_assignable_to_py_float = assertions::is_assignable<py::Bool, py::Float>;
    EXPECT_FALSE(bool_is_assignable_to_float);
    EXPECT_FALSE(bool_is_assignable_to_py_float);
}


TEST(py, bool_is_assignable_to_string) {
    bool bool_is_assignable_to_string_literal = assertions::is_assignable<py::Bool, const char*>;
    bool bool_is_assignable_to_std_string = assertions::is_assignable<py::Bool, std::string>;
    bool bool_is_assignable_to_std_string_view = assertions::is_assignable<py::Bool, std::string_view>;
    bool bool_is_assignable_to_py_string = assertions::is_assignable<py::Bool, py::Str>;
    EXPECT_FALSE(bool_is_assignable_to_string_literal);
    EXPECT_FALSE(bool_is_assignable_to_std_string);
    EXPECT_FALSE(bool_is_assignable_to_std_string_view);
    EXPECT_FALSE(bool_is_assignable_to_py_string);
}


TEST(py, bool_is_assignable_to_tuple) {
    bool bool_is_assignable_to_std_pair = assertions::is_assignable<py::Bool, std::pair<int, int>>;
    bool bool_is_assignable_to_empty_std_tuple = assertions::is_assignable<py::Bool, std::tuple<>>;
    bool bool_is_assignable_to_std_tuple = assertions::is_assignable<py::Bool, std::tuple<int>>;
    bool bool_is_assignable_to_empty_std_array = assertions::is_assignable<py::Bool, std::array<int, 0>>;
    bool bool_is_assignable_to_std_array = assertions::is_assignable<py::Bool, std::array<int, 1>>;
    bool bool_is_assignable_to_py_tuple = assertions::is_assignable<py::Bool, py::Tuple>;
    EXPECT_FALSE(bool_is_assignable_to_std_pair);
    EXPECT_FALSE(bool_is_assignable_to_empty_std_tuple);
    EXPECT_FALSE(bool_is_assignable_to_std_tuple);
    EXPECT_FALSE(bool_is_assignable_to_empty_std_array);
    EXPECT_FALSE(bool_is_assignable_to_std_array);
    EXPECT_FALSE(bool_is_assignable_to_py_tuple);
} 


TEST(py, bool_is_assignable_to_list) {
    bool bool_is_assignable_to_std_list = assertions::is_assignable<py::Bool, std::list<int>>;
    bool bool_is_assignable_to_std_vector = assertions::is_assignable<py::Bool, std::vector<int>>;
    bool bool_is_assignable_to_py_list = assertions::is_assignable<py::Bool, py::List>;
    EXPECT_FALSE(bool_is_assignable_to_std_list);
    EXPECT_FALSE(bool_is_assignable_to_std_vector);
    EXPECT_FALSE(bool_is_assignable_to_py_list);
}


TEST(py, bool_is_assignable_to_set) {
    bool bool_is_assignable_to_std_set = assertions::is_assignable<py::Bool, std::set<int>>;
    bool bool_is_assignable_to_std_unordered_set = assertions::is_assignable<py::Bool, std::unordered_set<int>>;
    bool bool_is_assignable_to_py_set = assertions::is_assignable<py::Bool, py::Set>;
    bool bool_is_assignable_to_py_frozenset = assertions::is_assignable<py::Bool, py::FrozenSet>;
    EXPECT_FALSE(bool_is_assignable_to_std_set);
    EXPECT_FALSE(bool_is_assignable_to_std_unordered_set);
    EXPECT_FALSE(bool_is_assignable_to_py_set);
    EXPECT_FALSE(bool_is_assignable_to_py_frozenset);
}


TEST(py, bool_is_assignable_to_dict) {
    bool bool_is_assignable_to_std_map = assertions::is_assignable<py::Bool, std::map<int, int>>;
    bool bool_is_assignable_to_std_unordered_map = assertions::is_assignable<py::Bool, std::unordered_map<int, int>>;
    bool bool_is_assignable_to_py_dict = assertions::is_assignable<py::Bool, py::Dict>;
    bool bool_is_assignable_to_py_mappingproxy = assertions::is_assignable<py::Bool, py::MappingProxy>;
    EXPECT_FALSE(bool_is_assignable_to_std_map);
    EXPECT_FALSE(bool_is_assignable_to_std_unordered_map);
    EXPECT_FALSE(bool_is_assignable_to_py_dict);
    EXPECT_FALSE(bool_is_assignable_to_py_mappingproxy);
}


///////////////////////////
////    CONVERSIONS    ////
///////////////////////////


TEST(py, bool_is_implicitly_convertible_to_bool) {
    bool a = py::Bool(true);
    bool b = py::Bool(false);
    EXPECT_EQ(a, true);
    EXPECT_EQ(b, false);
}


TEST(py, bool_is_implicitly_convertible_to_int) {
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


TEST(py, bool_is_implicitly_convertible_to_float) {
    double a = py::Bool(true);
    double b = py::Bool(false);
    EXPECT_EQ(a, 1.0);
    EXPECT_EQ(b, 0.0);

    py::Float c = py::Bool(true);
    py::Float d = py::Bool(false);
    EXPECT_EQ(c, 1.0);
    EXPECT_EQ(d, 0.0);
}


TEST(py, bool_is_explicitly_convertible_to_string) {
    EXPECT_EQ(static_cast<std::string>(py::Bool(true)), "True");
    EXPECT_EQ(static_cast<std::string>(py::Bool(false)), "False");
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
    bool inplace_bitwise_and_int = assertions::has_inplace_bitwise_and<py::Bool, int>;
    bool inplace_bitwise_and_py_int = assertions::has_inplace_bitwise_and<py::Bool, py::Int>;
    bool inplace_bitwise_and_float = assertions::has_inplace_bitwise_and<py::Bool, double>;
    bool inplace_bitwise_and_py_float = assertions::has_inplace_bitwise_and<py::Bool, py::Float>;
    EXPECT_FALSE(inplace_bitwise_and_int);
    EXPECT_FALSE(inplace_bitwise_and_py_int);
    EXPECT_FALSE(inplace_bitwise_and_float);
    EXPECT_FALSE(inplace_bitwise_and_py_float);

    EXPECT_EQ(py::Bool(true) &= true,               true);
    EXPECT_EQ(py::Bool(true) &= false,              false);
    EXPECT_EQ(py::Bool(false) &= true,              false);
    EXPECT_EQ(py::Bool(false) &= false,             false);
    EXPECT_EQ(py::Bool(true) &= py::Bool(true),     true);
    EXPECT_EQ(py::Bool(true) &= py::Bool(false),    false);
    EXPECT_EQ(py::Bool(false) &= py::Bool(true),    false);
    EXPECT_EQ(py::Bool(false) &= py::Bool(false),   false);

    EXPECT_EQ(py::Type(py::Bool(true) &= true),             py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(true) &= false),            py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) &= true),            py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) &= false),           py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(true) &= py::Bool(true)),   py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(true) &= py::Bool(false)),  py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) &= py::Bool(true)),  py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) &= py::Bool(false)), py::Bool::Type);
}


TEST(py, bool_inplace_bitwise_or) {
    bool inplace_bitwise_or_int = assertions::has_inplace_bitwise_or<py::Bool, int>;
    bool inplace_bitwise_or_py_int = assertions::has_inplace_bitwise_or<py::Bool, py::Int>;
    bool inplace_bitwise_or_float = assertions::has_inplace_bitwise_or<py::Bool, double>;
    bool inplace_bitwise_or_py_float = assertions::has_inplace_bitwise_or<py::Bool, py::Float>;
    EXPECT_FALSE(inplace_bitwise_or_int);
    EXPECT_FALSE(inplace_bitwise_or_py_int);
    EXPECT_FALSE(inplace_bitwise_or_float);
    EXPECT_FALSE(inplace_bitwise_or_py_float);

    EXPECT_EQ(py::Bool(true) |= true,                true);
    EXPECT_EQ(py::Bool(true) |= false,               true);
    EXPECT_EQ(py::Bool(false) |= true,               true);
    EXPECT_EQ(py::Bool(false) |= false,              false);
    EXPECT_EQ(py::Bool(true) |= py::Bool(true),      true);
    EXPECT_EQ(py::Bool(true) |= py::Bool(false),     true);
    EXPECT_EQ(py::Bool(false) |= py::Bool(true),     true);
    EXPECT_EQ(py::Bool(false) |= py::Bool(false),    false);

    EXPECT_EQ(py::Type(py::Bool(true) |= true),             py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(true) |= false),            py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) |= true),            py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) |= false),           py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(true) |= py::Bool(true)),   py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(true) |= py::Bool(false)),  py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) |= py::Bool(true)),  py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) |= py::Bool(false)), py::Bool::Type);
}


TEST(py, bool_inplace_bitwise_xor) {
    bool inplace_bitwise_xor_int = assertions::has_inplace_bitwise_xor<py::Bool, int>;
    bool inplace_bitwise_xor_py_int = assertions::has_inplace_bitwise_xor<py::Bool, py::Int>;
    bool inplace_bitwise_xor_float = assertions::has_inplace_bitwise_xor<py::Bool, double>;
    bool inplace_bitwise_xor_py_float = assertions::has_inplace_bitwise_xor<py::Bool, py::Float>;
    EXPECT_FALSE(inplace_bitwise_xor_int);
    EXPECT_FALSE(inplace_bitwise_xor_py_int);
    EXPECT_FALSE(inplace_bitwise_xor_float);
    EXPECT_FALSE(inplace_bitwise_xor_py_float);

    EXPECT_EQ(py::Bool(true) ^= true,                false);
    EXPECT_EQ(py::Bool(true) ^= false,               true);
    EXPECT_EQ(py::Bool(false) ^= true,               true);
    EXPECT_EQ(py::Bool(false) ^= false,              false);
    EXPECT_EQ(py::Bool(true) ^= py::Bool(true),      false);
    EXPECT_EQ(py::Bool(true) ^= py::Bool(false),     true);
    EXPECT_EQ(py::Bool(false) ^= py::Bool(true),     true);
    EXPECT_EQ(py::Bool(false) ^= py::Bool(false),    false);

    EXPECT_EQ(py::Type(py::Bool(true) ^= true),             py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(true) ^= false),            py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) ^= true),            py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) ^= false),           py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(true) ^= py::Bool(true)),   py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(true) ^= py::Bool(false)),  py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) ^= py::Bool(true)),  py::Bool::Type);
    EXPECT_EQ(py::Type(py::Bool(false) ^= py::Bool(false)), py::Bool::Type);
}
