
// // example.cpp
// #include <string>
// #include <bertrand/bertrand.h>
// namespace py = bertrand::py;
// using namespace py::literals;


// void hello() {
//     py::print(R"(
//         def foo(func):
//             return func("World")  # calling a C++ function from Python
//     )"_python()["foo"]([](const std::string& x) {  // calling a Python function from C++
//         return "Hello, " + x + "!";
//     }));
// }


// void run() {
//     py::Str py_str = "This is a type-safe Python string ";
//     std::string cpp_str = py_str.replace("Python", "C++");

//     py_str = [](std::string str) -> std::string {
//         return str += "that can be passed to and from C++ functions\n\t";
//     }(cpp_str);

//     py_str = R"(
//         import numpy as np
//         x = np.arange(10)
//         string += "or inline Python scripts, "
//     )"_python({{"string", py_str}})["string"];

//     py_str += "with native performance, ";
//     py::print(py::import("timeit")->attr("timeit")([&] {
//         static const py::Static<py::Str> lookup("C++");
//         py_str.contains(*lookup);
//     }));

//     py::Str temp = std::move(py_str += "automatic reference counting,\n\t");
//     cpp_str = temp + "implicit conversions, ";
//     throw py::TypeError(cpp_str + "and seamless error propagation.");
// }


// PYBIND11_MODULE(example, m) {
//     m.doc() = "example bertrand plugin";
//     m.def("hello", &hello, "Prints 'Hello, World!' using mixed Python/C++");
//     m.def("run", &run, "A test function to demonstrate bertrand");
// }


// /* >>> import example
//  * >>> example.hello()
//  * Hello, World!
//  * >>> example.run()
//  * 0.0525730510125868
//  * Traceback (most recent call last):
//  *   File "<stdin>", line 1, in <module>
//  * TypeError: This is a type-safe C++ string that can be passed to C++ functions or
//  *         inline Python scripts, with native performance, automatic reference counting,
//  *         implicit conversions, and seamless error propagation.
//  */



///////////////////////
////    TESTING    ////
///////////////////////


#include <bertrand/python.h>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <complex>

#include <limits>

namespace py = bertrand::py;
using namespace py::literals;


template <bertrand::StaticStr str>
void func() {
    // constexpr bertrand::StaticStr s = bertrand::static_str::lstrip<str>;
    // py::print(s);
    // py::print(s.size());

    constexpr auto splits = bertrand::static_str::rsplit<str, ", ">;
    py::print(std::get<2>(splits));
    py::print(std::get<2>(splits).size());
    py::print(std::tuple_size<decltype(splits)>::value);
}


void run() {
    using Clock = std::chrono::high_resolution_clock;
    std::chrono::time_point<Clock> start = Clock::now();


    func<"abc, def, ghi">();


    // py::Function foo = R"(
    //     def foo(a, b=2, *, c=3):
    //         return a, b, c
    // )"_python()["foo"];

    // py::print(foo.defaults());


    // std::vector<int> vec = {1, 2, 3, 4, 5};
    // auto it = pybind11::make_iterator(vec);
    // py::print(it);
    // py::print(typeid(it).name());



    // static const py::Static<py::Timezone> tz = py::Timezone::utc();
    // py::print(tz);

    // py::Datetime dt("December 7th, 1941 at 8:30 AM", "US/Pacific");  // TODO: this breaks
    // py::print(dt);




    // py::KeysView keys = d.keys();
    // keys.mapping() = py::Dict{{"d", 4}};
    // py::print(py::repr(keys.mapping()));





    // py::List list = {1, 2, 3, 4};
    // pybind11::print(*list);  // py::print(*list) != pybind11::print(*list)



    // static const auto np = py::import("numpy");
    // static const auto array = np->attr("array");
    // py::print(py::repr(array(py::List{1, 2, 3}, np->attr("dtype")("float16"))));

    // for (auto&& x : list) {
    //     py::print(x);
    // }

    // py::print(py::repr(*list));  // ???




    // py::Timedelta td(1.23, py::Timedelta::Units::D);
    // py::print(td);




    // std::vector<py::Tuple> tuples = {{1, 2}, {3, 4}, {5, 6}};
    // for (std::pair<int, int> x : tuples) {
    //     py::print(x);
    // }


    // py::List l = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    // for (size_t i = 0; i < 1000000; ++i) {
    //     volatile std::vector<int> v = l;
    // }





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



    std::chrono::time_point<Clock> end = Clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed time: " << elapsed.count() << "s\n";
}

PYBIND11_MODULE(example, m) {
    m.doc() = "pybind11 example plugin"; // optional module docstring
    m.def("run", &run, "A test function to demonstrate pybind11");
}
