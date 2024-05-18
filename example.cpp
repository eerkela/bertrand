
// // example.cpp
// #include <string>
// #include <bertrand/bertrand.h>
// namespace py = bertrand::py;
// using namespace py::literals;


// void hello() {
//     py::Function python = R"(
//         def func(s):
//             return s + ", "
//     )"_python()["func"];

//     py::Str str = python("Hello");

//     auto cpp = [](std::string s) -> std::string {
//         return s + "World!";
//     };

//     py::print(cpp(str));
// }


// void run() {
//     py::Str py_str = "This is a type-safe Python string ";
//     std::string cpp_str = py_str.replace("Python", "C++");

//     auto cpp_func = [](std::string str) -> std::string {
//         return str += "that can be passed to and from C++ functions\n\t";
//     };
//     py_str = cpp_func(py_str);

//     py_str = R"(
//         import numpy as np
//         x = np.arange(10)
//         string += "or inline Python scripts, "
//     )"_python({{"string", py_str}})["string"];

//     py_str += "with native performance, ";
//     py::print(py::import<"timeit">().attr<"timeit">()([&] {
//         static const py::Str lookup("C++");
//         py_str.contains(lookup);
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

namespace py = bertrand::py;
using namespace py::literals;


// #include <Python.h>
// #include <pybind11/pybind11.h>
// namespace py = pybind11;


// static const py::Module np = py::import<"numpy">();
// static const py::Function array = np.attr<"array">();
// static const py::Type dtype = np.attr<"dtype">();


int subtract(int x, int y) {
    return x - y;
}


void run() {
    using Clock = std::chrono::high_resolution_clock;
    std::chrono::time_point<Clock> start = Clock::now();

    // py::Set a = {1, 2, 3};
    // py::Set b("abc");
    // py::print(a);
    // py::print(b);
    // py::print(typeid(decltype(b)::value_type).name());


    // py::Set<py::Tuple<py::Int>> set = {{1, 2, 3}, {4, 5, 6}};
    // py::print(set);
    // py::print(typeid(decltype(set)::value_type).name());


    // py::Dict<py::Str> dict = {{"a", 1}, {"b", 2}, {"c", 3}};
    // py::Dict dict2 = dict | py::Dict<py::Str>{{"d", 4}};
    // // decltype(dict2) dict3 = dict2;
    // // dict2[4] = "d";
    // py::print(dict);

    // for (const auto& x : dict.values()) {
    //     py::print(x);
    // }




    // py::Dict<py::Int, py::NoneType> dict;
    // for (size_t i = 0; i < 1'000'000; ++i) {
    //     dict[i] = py::None;
    // }

    // auto keys = dict.keys();

    // using Clock = std::chrono::high_resolution_clock;
    // std::chrono::time_point<Clock> start = Clock::now();

    // for (size_t i = 0; i < 10; ++i) {
    //     for (const auto& x : keys) {}
    // }




    // using T = py::Arg<"z", const int&>;
    // T z(1);
    // py::print(std::same_as<typename T::type, const int&>);




    // TODO: this should be an example showcase

    auto lambda = [](
        py::Arg<"x", int>::opt x,
        py::Arg<"y", int>::opt y
    ) {
        return x - y;
    };

    constexpr auto x = py::arg_<"x">;
    constexpr auto y = py::arg_<"y">;
    // constexpr auto z = py::arg_<"z">;

    py::Function_ func("subtract", lambda, y = 2, x = 1);
    // py::Function_ func("subtract", lambda, 1, 2);

    py::print(func());

    py::print(func(1));
    py::print(func(x = 1));
    py::print(func(y = 2));

    py::print(func(1, 2));
    py::print(func(1, y = 2));
    py::print(func(x = 1, y = 2));
    py::print(func(y = 2, x = 1));





    // auto lambda = [](
    //     py::Arg<"x", const std::string&>::opt x
    // ) {
    //     return x.value;
    // };

    // constexpr auto x = py::arg_<"x">;

    // py::Function_ func("test", lambda, x = "hello");

    // py::print(func());
    // py::print(func("abc"));
    // py::print(func(x = "abc"));



    // py::Function_ variadic(
    //     "variadic_positional",
    //     [](
    //         std::string x,
    //         py::Arg<"y", std::string> y,
    //         py::Arg<"args", int>::args args,
    //         py::Arg<"z", std::string>::kw::opt z,
    //         py::Arg<"kwargs", int>::kwargs kwargs
    //     ) {
    //         return py::List(args.value);
    //     },
    //     "xyz"
    // );

    // py::print(variadic("abc", "def", 1, 2, 3, py::arg_<"key"> = 2));
    // py::print(variadic("abc", py::arg_<"x"> = "xyz", py::arg_<"z"> = 2));




    // py::print(py::impl::supports_lookup<std::unordered_map<std::string, int>, const char*>);





    // TODO: the extra speedup comes mostly from not interacting with reference counts
    // rather than anything else, and the difference is extremely marginal



    // PyObject* dict = PyDict_New();
    // for (size_t i = 0; i < 1'000'000; ++i) {
    //     PyObject* key = PyLong_FromLong(i);
    //     PyDict_SetItem(dict, key, Py_None);
    //     Py_DECREF(key);
    // }

    // PyObject* keys = PyObject_CallMethod(dict, "keys", nullptr);
    // // py::print(py::repr(keys));

    // using Clock = std::chrono::high_resolution_clock;
    // std::chrono::time_point<Clock> start = Clock::now();

    // for (size_t i = 0; i < 10; ++i) {
    //     // PyObject* iter = PyObject_GetIter(keys);
    //     // PyObject* item;
    //     // while ((item = PyIter_Next(iter))) {
    //     //     Py_DECREF(item);
    //     // }
    //     // Py_DECREF(iter);

    //     // PyObject* key;
    //     // PyObject* val;
    //     // Py_ssize_t pos = 0;
    //     // while (PyDict_Next(dict, &pos, &key, &val)) {

    //     // }
    // }







    // py::Float x = 1;
    // py::Float y = 2.5;
    // int z = static_cast<int>(x + y);
    // py::print(z);



    // py::Tuple<py::Int> t = {1, 2, 3};
    // py::Tuple t2 = t;
    // py::print(t);
    // py::print(t2);
    // py::print(typeid(decltype(t)::value_type).name());
    // py::print(typeid(decltype(t2)::value_type).name());



    // int x = 1;
    // int y = 2;
    // for (size_t i = 0; i < 1000000; ++i) {
    //     volatile int z = x + y;
    // }





    // py::Bool x = true;
    // pybind11::bool_ y = x;
    // py::Str y = x;
    // py::print(y);



    // py::Tuple<py::Str> tuple("abc");
    // py::Tuple<> tuple2 = tuple;
    // py::Tuple<> tuple3 = {1, 2, 3};
    // tuple = tuple3;
    // py::print(tuple);
    // py::print(tuple2);
    // py::print(typeid(decltype(tuple2)::value_type).name());



    // py::Tuple<py::Str> x("xyz");
    // py::Tuple y = x;
    // x = {"a", "b", "c"};
    // // y = {1, true, 3.0};
    // py::print(y);
    // py::print(x);
    // py::print(typeid(decltype(y)::value_type).name());

    // for (const auto& item : y) {
    //     py::print(item);
    // }

    // py::Tuple<py::Str> x = {"a", "b", "c"};
    // std::vector<std::string> vec = x;
    // for (const auto& item : vec) {
    //     py::print(item);
    // }


    // py::Tuple x = 1;





    // py::Tuple<py::Str> y = x;
    // // py::Tuple<py::Object> t = x;  // TODO: enable this
    // py::Tuple t = x;
    // py::print(t[0]);
    // py::print(typeid(decltype(t)::value_type).name());
    // py::print(typeid(decltype(t[0].value())).name());


    // for (auto&& item : t) {
    //     py::print(item);
    // }






    // Foo foo = {1, 2, 3, 4, 5, "abc"};
    // Foo foo2 = foo;
    // py::print(typeid(decltype(foo2)::Wrapped).name());
    // py::print(Foo<py::Int>::x);





    // py::List list = {1, 2, 3};
    // py::print(list);


    // py::Object foo("abc");
    // int x = py::visit(foo,
    //     [](const py::Float& obj) {
    //         py::print(obj);
    //         return 1;
    //     },
    //     [](const py::Str& obj, int y = 2) {
    //         py::print(obj, y);
    //         return 2;
    //     },
    //     test,
    //     [](const py::Int& obj) {
    //         py::print(obj);
    //         return 0;
    //     }
    // );
    // py::print(x);



    // py::List list = {1, 2, 3.0, 4.5, "5"};
    // auto view = list | py::transform(
    //     [](const py::Int& obj) {
    //         return py::Str("int");
    //     },
    //     [](const py::Float& obj) {
    //         return py::Str("float");
    //     },
    //     [](const py::Str& obj) {
    //         return py::Str("string");
    //     }
    // );
    // for (auto&& x : view) {
    //     py::print(x);
    // }





    // static const py::Code script = R"Foo(
    //     class Foo:
    //         def __init__(self, x):
    //             self.x = x

    //         def __repr__(self):
    //             return f"Foo({self.x})"
    // )Foo"_python;

    // py::Type Foo = script()["Foo"];
    // Foo.attr<"bar">() = py::Function([](const py::Object& self) {
    //     return self.attr<"x">();
    // });


    // static const bertrand::Regex re("hello");
    // for (size_t i = 0; i < 1000000; ++i) {
    //     volatile auto match = re.match("hello, world!");
    // }
    // py::print(!!re.match("hello, world!"));


    // py::import<"bertrand.regex">();  // NOTE: this works to bring things into scope


    // // NOTE: this works as long as the module has been loaded.
    // py::Object x("abc");
    // bertrand::Regex re(static_cast<std::string>(x));
    // py::Object y(re);
    // py::print(re);
    // py::print(y);
    // bertrand::Regex re2 = y;
    // py::print(re2);


    // py::Int z = 1;
    // py::Object w = z;
    // py::print(w);




    // py::Function func = [](const py::Object& x) {
    //     return x;
    // };
    // py::print(py::Object(func).attr<"__get__">());



    // py::List list = {1, 2, 3, 4, 5};
    // py::print(py::Dict(
    //     list |
    //     std::views::transform([](auto&& val){ 
    //         return std::pair(val, val * val);
    //     })
    // ));




    // TODO: in order to fully satisfy the requirements for views and ranges, all
    // container types need to use special constructors for these objects, and they
    // must implement cbegin(), cend(), etc. to be fully qualified.

    // TODO: get this to work in order to allow iterator piping using std::views
    // py::print(std::ranges::random_access_range<py::List>);  // TODO: this should be true
    // py::print(std::ranges::random_access_iterator<py::List>);




    // throws_an_error();
    // throw py::TypeError();

    // throw py::Exception("test error", 0);


    
    // py::impl::current_exception exception;
    // std::cout << (size_t)exception.pop() << std::endl;
    // std::cout << (size_t)exception.pop() << std::endl;
    







    // py::Function f(hello);
    // f(true);
    // f(py::Type("foo"), py::None, py::None);


    // py::print(py::impl::python_like<bool>);




    // py::List list = {1, 2, 3, 4};
    // py::Int x = 1;
    // py::Str y = "abc";
    // py::print(++x);
    // py::print(x);


    // py::Object x = 1;
    // py::print(py::repr(x));

    // py::Object arr = array(py::List{1, 2, 3}, dtype("float16"));
    // py::print(py::repr(arr[{py::None, 2}]));


    // py::Bool x = true;
    // py::Bool y = false;
    // // py::print(x < y);

    // py::Object a = 1;
    // py::Bool b = false;
    // py::print(b == "abc");
    // py::print(py::impl::str_like<py::Bool>);

    // py::print(array.attr<"__doc__">());



    // for (size_t i = 0; i < 1000000; ++i) {
    //     volatile py::Int z = x + y;
    // }


    // py::Foo z = true;
    // py::print(z + y);






    // py::Range r(5);
    // py::Str s = "abc";
    // py::print(s[0] + r[1]);  // gives a somewhat complicated error message








    // bool x = true;
    // for (size_t i = 0; i < 1000000; ++i) {
    //     if (x) {
    //         volatile int y = 1;
    //     }
    // }

    // for (size_t i = 0; i < 1000000; ++i) {
    //     if (Py_IsInitialized()) {
    //         volatile int y = 1;
    //     }
    // }







    // py::Str x = "abc";
    // py::print(x + "def");







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







py::Object hello() {
    // py::Function_ func("hello", [] {
    //     return std::string("hello, world!\n");
    // });
    // return py::reinterpret_borrow<py::Object>(func.ptr());

    py::Function_ example(
        "example",
        [](
            py::Arg<"x", const py::Object&>::pos x,
            py::Arg<"y", const py::Object&>::pos y  // TODO: breaks if I use a default value
        ) {
            return x.value() - y.value();
        }
    );
    return py::reinterpret_borrow<py::Object>(example.ptr());
}


PYBIND11_MODULE(example, m) {
    m.doc() = "pybind11 example plugin"; // optional module docstring
    m.def("hello", &hello, "pass a python function from C++ to Python");
    m.def("run", &run, "A test function to demonstrate pybind11");
}
