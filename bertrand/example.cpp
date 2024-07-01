
// // example.cpp
// #include <string>
// #include <bertrand/bertrand.h>
// namespace py = bertrand::py;
// using namespace py::literals;


// void hello() {
//     py::Function<py::Str(py::Str)> python = R"(
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
//         bool result = py_str.contains(lookup);
//     }));

//     py::Str temp = std::move(py_str += "automatic reference counting,\n\t");
//     cpp_str = temp + "implicit conversions, ";
//     throw py::TypeError(cpp_str + "and seamless error propagation.");
// }


// BERTRAND_MODULE(example, m) {
//     m.def("hello", "Prints 'Hello, World!' using mixed Python/C++", hello);
//     m.def("run", "A test function to demonstrate bertrand", run);
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
namespace py = bertrand::py;
using namespace py::literals;


#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>


// import example;
// import xyz;



void run() {
    using Clock = std::chrono::high_resolution_clock;
    std::chrono::time_point<Clock> start = Clock::now();

    // for (auto&& x : py::Tuple{1, 2, 3}) {
    //     py::print(x);
    // }

    py::List list = {1, 2, 3, 4};
    py::print(list[2]);
    py::Int x = list[1];
    list[0] = 10;
    py::print(list);


    // py::print(py::List{1, 2, 3, 4});
    // py::print(py::Float(2.5));

    // py::print(example::add(4, 5));


    // auto lambda = [](
    //     py::Arg<"x", const int&>::opt x,
    //     py::Arg<"y", const int&>::opt y
    // ) {
    //     return x.value() - y.value();
    // };

    // constexpr auto x = py::arg<"x">;
    // constexpr auto y = py::arg<"y">;
    // constexpr auto z = py::arg<"z">;

    // py::Function func("subtract", lambda, y = 2, x = 1);

    // py::print(func());

    // py::print(func(1));
    // py::print(func(x = 1));
    // py::print(func(y = 2));

    // py::print(func(1, 2));
    // py::print(func(1, y = 2));
    // py::print(func(x = 1, y = 2));
    // py::print(func(y = 2, x = 1));


    // py::print("123");

    // throw py::TypeError("hello, world!");

    // py::Bool x = 2;





    // TODO: not immediately clear if this should return the integer representation of
    // each character, or the parse the characters into equivalent integers.
    // py::print(py::Tuple{1, 2, 3});



    // TODO: keyword constructor causes a segfault

    // auto d = py::Dict(py::arg<"x"> = 2);
    // py::print(d);


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


    std::chrono::time_point<Clock> end = Clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Elapsed time: " << elapsed.count() << "s\n";
}


static const py::Function func(
    "example",
    [](
        py::Arg<"x", int>::opt x,
        py::Arg<"y", int>::opt y
    ) {
        return x.value() - y.value();
    },
    py::arg<"x"> = 1,
    py::arg<"y"> = 2
);


int main() {
    run();
}


BERTRAND_MODULE(example, m) {
    py::setattr<"__doc__">(m, "bertrand example plugin");
    py::setattr<"func">(m, func);
    m.def("run", "example function to demonstrate bertrand", &run);
}

