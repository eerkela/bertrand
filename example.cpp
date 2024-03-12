
// // example.cpp
// #include <string>
// #include <bertrand/bertrand.h>
// namespace py = bertrand::py;


// void hello() {
//     py::print(R"(
//         def foo(func):
//             return func("World")
//     )"_python()["foo"]([](const std::string& x) {
//         return "Hello, " + x + "!";
//     }));
// }


// void run() {
//     py::Str py = "This is a type-safe Python string ";
//     std::string cpp = py.replace("Python", "C++");

//     py = [](std::string str) -> std::string {
//         return str += "that can be passed to C++ functions or\n\t";
//     }(cpp);

//     py = R"(
//         import numpy as np
//         x = np.arange(10)

//         str += "inline Python scripts, "
//     )"_python({{"str", py}})["str"];

//     py += "with native performance, ";
//     py::print(py::import("timeit")->attr("timeit")([&] {
//         static const py::Static<py::Str> lookup = "C++";
//         py.contains(lookup);
//     }));

//     py::Str py2 = std::move(py += "automatic reference counting,\n\t");
//     cpp = py2 + "implicit conversions, ";
//     throw py::TypeError(cpp + "and seamless error propagation.");
// }


// PYBIND11_MODULE(example, m) {
//     m.doc() = "example bertrand plugin";
//     m.def("hello", &hello, "Prints 'Hello, World!' using mixed Python/C++");
//     m.def("run", &run, "A test function to demonstrate bertrand");
// }


/* >>> import example
 * >>> example.hello()
 * Hello, World!
 * >>> example.run()
 * 0.0525730510125868
 * Traceback (most recent call last):
 *   File "<stdin>", line 1, in <module>
 * TypeError: This is a type-safe C++ string that can be passed to C++ functions or
 *         inline Python scripts, with native performance, automatic reference counting,
 *         implicit conversions, and seamless error propagation.
 */



///////////////////////
////    TESTING    ////
///////////////////////


#include <bertrand/python.h>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <complex>

#include <limits>


#include <Python.h>
#include <pybind11/pybind11.h>


// namespace py = bertrand::py;


// void func(const py::Int& i) {

// }



void run() {
    using Clock = std::chrono::high_resolution_clock;
    std::chrono::time_point<Clock> start = Clock::now();

    // py::Int a = 1;
    // py::print(a < 2);


    // NOTE: PySequence_Concat is ~30-40% faster than PyNumber_Add

    // PyObject* list1 = PyList_New(3);
    // PyList_SET_ITEM(list1, 0, PyLong_FromLong(1));
    // PyList_SET_ITEM(list1, 1, PyLong_FromLong(2));
    // PyList_SET_ITEM(list1, 2, PyLong_FromLong(3));

    // PyObject* list2 = PyList_New(3);
    // PyList_SET_ITEM(list2, 0, PyLong_FromLong(4));
    // PyList_SET_ITEM(list2, 1, PyLong_FromLong(5));
    // PyList_SET_ITEM(list2, 2, PyLong_FromLong(6));

    // for (size_t i = 0; i < 1000000; ++i) {
    //     // PyObject* attr = PyObject_GetAttrString(list1, "append");
    //     // Py_DECREF(attr);

    //     // PyObject* temp = PyUnicode_FromStringAndSize("append", 6);
    //     // PyObject* attr = PyObject_GetAttr(list1, temp);
    //     // Py_DECREF(temp);
    //     // Py_DECREF(attr);


    //     if (i < 0 || i >= static_cast<size_t>(PyList_GET_SIZE(list1))) {
    //         // PyErr_SetString(PyExc_IndexError, "list index out of range");
    //         // return;
    //         volatile int x = 0;
    //     }

    //     PyObject* item = Py_NewRef(PyList_GET_ITEM(list1, 0));
    //     Py_DECREF(item);

    //     // PyObject** items = PySequence_Fast_ITEMS(list1);
    //     // PyObject* item = Py_NewRef(items[0]);
    //     // Py_DECREF(item);


    //     // PyObject* temp = PySequence_Concat(list1, list2);
    //     // Py_DECREF(temp);
    // }


    // PyObject* list3 = PySequence_Concat(list1, list2);
    // Py_DECREF(list1);
    // Py_DECREF(list2);

    // // PyObject_DelItem(list3, pybind11::cast(0).ptr());

    // PyObject* str = PyObject_Repr(list3);
    // Py_DECREF(list3);
    // const char* c_str = PyUnicode_AsUTF8(str);
    // Py_DECREF(str);
    // std::cout << c_str << std::endl;







    // Foo foo;
    // py::print(foo + 1.0);
    // py::print(foo(5, 6), typeid(decltype(foo(5, 6))).name());


    // py::Str a = "abc";
    // py::Str b = std::string("def") + a;
    // py::print(b);

    // py::print(py::impl::str_like<const char*>);



    // py::Tuple t = {1, 2, 3, 4, 5};
    // py::List s = {1, 2, 3, 4, 5};
    // py::print(t == s);

    // py::FrozenSet set = {1, 2, 3};    
    // for (size_t i = 0; i < 1000000; ++i) {
    //     py::FrozenSet set2 = set - py::FrozenSet{2, 3, 4};
    // }



    // // std::string x = py::Int(0);  // compile error!


    // py::Bool b = true;
    // std::vector<int> list = b.cast<std::vector<int>>();
    // py::print(list);


    
    // py::print(list);


    // py::print(typeid(decltype(a + static_cast<unsigned int>(1))).name());
    // // py::print(typeid(b).name());
    // py::print(typeid(py::Bool::__add__<int>::Return).name());
    // py::print(a + 1);




    // py::Bool a = true;
    // py::Int b = a + false;
    // py::print(test_op<py::Bool, bool, py::Int>);


    // static const py::Static<py::Timezone> tz = py::Timezone::utc();
    // py::print(tz);

    // py::Datetime dt("December 7th, 1941 at 8:30 AM", "US/Pacific");  // TODO: this breaks
    // py::print(dt);






    // static const py::Static<py::Int> a = 1;
    // py::print(a);



    // for (size_t i = 0; i < 1000000; ++i) {
    //     py::Float b = 4;
    // }

    // py::Int a("1");
    // py::Float b = a;
    // py::print(b);



    // static py::Static<py::Code> script = R"(
    //     print("hello, world!")
    // )"_python;



    // py::print(np_array(py::List{1, 2, 3}));

    // for (auto&& x : list) {
    //     py::print(x);
    // }

    // py::print(py::repr(*list));  // ???

    // py::Set s = {1, 2, 3, 4, 5};
    // py::print(s);
    // py::Object array = np->attr("array")(py::List{1, 2, 3}, np->attr("dtype")("float16"));
    // py::print(array.attr("dtype"));


    // py::print(np_array);



    // py::Timedelta td(1.23, py::Timedelta::Units::D);
    // py::print(td);




    // std::vector<py::Tuple> tuples = {{1, 2}, {3, 4}, {5, 6}};
    // for (std::pair<int, int> x : tuples) {
    //     py::print(x);
    // }


    // py::Tuple t = {};

    // if (t) {
    //     py::print("t is true");
    // } else {
    //     py::print("t is false");
    // }

    // py::List l = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    // for (size_t i = 0; i < 1000000; ++i) {
    //     volatile std::vector<int> v = l;
    // }



    // std::pair<int, int> x = t;
    // py::print(x);




    // py::Object o("abc");

    // py::Bool b = o;
    // // b = 1;
    // py::print(py::Object::like<py::Bool>);





    // auto mode = py::Round::HALF_EVEN;

    // double x = 3.5;
    // py::Float py_x(x);
    // double y = -x;
    // py::Float py_y(y);

    // py::print(py::round(x, 0, mode));
    // py::print(py::round(py_x, 0, mode));
    // py::print(py::round(y, 0, mode));
    // py::print(py::round(py_y, 0, mode));


    // py::print(py::div());



    // for (size_t i = 0; i < 1000000; ++i) {
    //     py::round(py_x, 0, mode);
    // }


    // static py::Code script = R"(
    //     print("hello, world!")
    // )"_python;

    // script();

    // py::Dict d1 {{"a", 1}, {"b", 2}, {"c", 3}};
    // py::Dict d2 {{"a", 1}, {"b", 2}, {"c", 3}};
    // py::print(d1 == d2);


    // py::Dict dict {{"a", 1}, {"b", 2}, {"c", 3}};
    // py::MappingProxy proxy(dict);
    // std::cout << proxy << std::endl;
    // for (const py::Handle& x : proxy) {
    //     py::print(x);
    // }
    // py::print(py::Object(list[1]) + 3);


    // py::Int x = -3;
    // x = x / 2;
    // py::print(x);


    // py::print(py::Int::Type);

    // py::Super s(py::Int)
    // py::print(d["super"]);



    // py::Type type = R"(
    //     class Foo:
    //         y = 2
    // )"_python()["Foo"];

    // type.attr("foo") = py::Property([](const py::Object& x) {
    //     return x.attr("y");
    // });
    // py::Int y = type.attr("y");


    // py::print(y);


    // py::Dict context = R"(
    //     spam = 0
    //     eggs = 1
    // )"_python();

    // context["ham"] = std::vector<int>{1, 1, 2, 3, 5, 8, 13, 21, 34, 55};

    // std::vector<int> fibonacci = R"(
    //     result = []
    //     for x in ham:
    //         spam, eggs = (spam + eggs, spam)
    //         assert(x == spam)
    //         result.append(eggs)
    // )"_python(context)["result"].cast<std::vector<int>>();

    // py::print(fibonacci);





    // py::Function f([](int x, int y) {
    //     return x + y;
    // });

    // py::Function f2 = R"(
    //     def foo(x, y = 2, *args):
    //         z = 3
    //         return x + y
    // )"_python()["foo"].cast<py::Function>();

    // // py::print(py::repr(py::callable<int>(f2)));
    // py::print(py::repr(f2.code().slots().n_locals()));


    // if (py::callable<double>(f2)) {
    //     py::print("func is callable");
    // } else {
    //     py::print("func is not callable");
    // }









    // py::Function f([](int x, int y) {
    //     return x + y;
    // });
    // py::Type t("Foo", py::Tuple{}, py::Dict{{"a", 1}, {"b", 2}, {"c", 3}});


    // py::builtins()["int"].attr()
    // py::print(py::ClassMethod(f));


    // py::Code script = R"(
    //     def func(x, y):
    //         return x + y

    //     z = func(a, b)
    // )"_python;

    // py::Int z = script({{"a", 1}, {"b", 2}})["z"];
    // py::print(z);


    // py::Code script1 = R"(
    //     x = 1
    //     y = 2
    // )"_python;

    // py::Code script2 = R"(
    //     z = x + y
    //     del x, y
    // )"_python;

    // py::Dict context;
    // script1(context);
    // script2(context);
    // py::print(context);  // prints {"z": 3}


    // py::Datetime epoch("2021-02-03 00:00:00-0800");

    // for (size_t i = 0; i < 1000000; ++i) {
    //     // py::Timedelta td("1 day, 22 hours, 32.45 SEC");
    //     // py::Timedelta td(12.3, py::Timedelta::Units::D);
    //     py::Datetime dt(12.3, py::Datetime::Units::s);
    // }
    // py::print(td);
    // py::Datetime dt = py::Datetime(12.3, py::Datetime::Units::D);
    // py::print(dt);

    // py::Time t(1, py::Time::Units::h);
    // py::print(t);

    // py::Datetime dt;
    // py::print(dt.timezone());



    // py::Dict context = script({{"a", 4}, {"b", 5}});
    // py::Int x = context["x"];
    // // script(context);
    // py::print(x);  // prints {"x": 1, "y": 2, "z": 3}

    // int x = context["x"].cast<int>();
    // int result = func(context["y"].cast<int>(), context["z"].cast<int>());
    // py::print(x, result);  // prints 5


    // auto re = py::impl::re.attr("compile")("");

    // py::Regex re = py::impl::re.attr("compile")("").cast<py::Regex>();

    // py::print(py::repr(re));




    // bertrand::Regex re("(?P<foo>abc)", bertrand::Regex::JIT);
    // bertrand::Regex::Match match = re.match("abcabcabc");
    // std::cout << match << std::endl;
    // py::print(re.sub("cd", "axabyabzab", 5));

    // for (auto&& match : re.finditer("abcabcabc")) {
    //     std::cout << match << std::endl;
    // }

    // bertrand::Regex::Match match = re.match("testxyz123");
    // py::print(match[{"foo", 2}]);

    // std::cout << match << std::endl;
    // for (auto&& [index, sub] : match) {
    //     py::print(index, sub);
    // }

    // py::print(re.match("abc"));  // TODO: specialize for
    // py::print(re.match("test")["foo"]);



    // py::Dict d {{"a", 1}, {"b", 2}, {"c", 3}};
    // py::print(py::List(d));  // TODO: does not work.  Should extract keys as a list

    // TODO: iterating over a dictionary should only cover keys?  Or should it return
    // key, value pairs?
    // -> Should probably match Python syntax.  So:
    //    for (auto&& key : dict)
    //    for (auto&& key : dict.keys())
    //    for (auto&& value : dict.values())
    //    for (auto&& [key, value] : dict.items())


    // py::Dict d {{"a", 1}, {"b", 2}, {"c", 3}};
    // for (auto&& [key, value] : d.attr("items")()) {
    //     py::print(key, value);
    // }


    // py::Dict d{{"a", 1}, {"b", 2}, {"c", 3}};
    // py::MappingProxy proxy(d);
    // py::print(proxy.contains("a"));
    // for (auto&& key : proxy) {
    //     py::print(key);
    // }




    // auto test = [] {
    //     py::print("hello, world!");
    // };
    // py::Function func(test);

    // py::print(func);
    // func();



    // py::Type type;
    // py::print(type[py::Type{}]);


    // py::List l;
    // l.extend(std::vector<int>{1, 2, 3});
    // l.extend({4, 5, 6});
    // l.extend({7, "a", true});
    // py::print(l);


    // py::List l({1, 2, 3});
    // l += {"a", true, 2.3};
    // py::print(l);

    // py::print(py::Dict{{"a", 1}, {"b", 2}, {"c", 3}});

    // py::print(py::Tuple{1, 2, 3}.concat({1, 2}));

    // py::List l {"a", 1, true, 2.3};
    // py::print(l += {"b", 2, false, 3.4});



    // TODO: dateutil inserts dateutil.tz.tzutc().  Should be able to convert to
    // ZoneInfo.  Same with pytz timezones.


    // py::Datetime dt1;  // initializes to current time
    // py::Datetime dt2(1970, 02, 1, 12, 30, 45.123456, "US/Pacific");
    // py::Datetime dt3("September 25th, 2019 at 7 PM", "US/Pacific");
    // py::Datetime dt4(10.2345, "W");
    // py::print(dt4);


    // py::Timedelta td(1, 23, 456789);
    // py::print(py::Timedelta(23.45, "D") / 2);

    // py::print(py::Timedelta(std::numeric_limits<long long>::max(), py::Timedelta::Units::us));



    // py::Datetime dt(1970, 02, 1, 12, 30, 45.123456);
    // py::print(dt);




    // auto s = py::Str{"{0}"}.format("abc");
    // py::print(s + " 123");


    // py::Datetime dt(1970, 02, 1, 12, 30, 45.123456);
    // py::print(dt);

    // py::Timezone tz;
    // py::print(tz);



    // py::Dict d {
    //     {"a", 1},
    //     {"b", 2},
    //     {"c", 3}
    // };
    // d |= {
    //     {"d", 4},
    //     {"e", 5},
    //     {"f", 6}
    // };
    // py::print(d);




    // py::Type Foo(
    //     "Foo",
    //     py::Tuple{},
    //     py::Dict{{"a", 1}, {"b", 2}, {"c", 3}}
    // );

    // py::print(Foo);
    // py::print(Foo.attr("a"));
    // py::print(Foo.attr("b"));
    // py::print(Foo.attr("c"));
    // py::print(Foo.attr("__bases__"));


    std::chrono::time_point<Clock> end = Clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed time: " << elapsed.count() << "s\n";
}

#include <pybind11/pybind11.h>


PYBIND11_MODULE(example, m) {
    m.doc() = "pybind11 example plugin"; // optional module docstring
    m.def("run", &run, "A test function to demonstrate pybind11");
}