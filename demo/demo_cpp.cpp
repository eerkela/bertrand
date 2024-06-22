#include <bertrand/bertrand.h>
namespace py = bertrand::py;
using namespace py::literals;




// 1
static const py::Code script = R"(
    import numpy as np

    print(f"hello, {name}!")

    def fibonacci2(n: int = 0, m: int = 1, steps: int = 10) -> list[int]:
        """Compute the fibonacci sequence starting at n, m and continuing for `length`
        iterations.

        Parameters
        ----------
        n : int, default 0
            first number in the sequence
        m : int, default 1
            second number in the sequence
        steps : int, default 10
            number of iterations to compute

        Returns
        -------
        list[int]
            The computed sequence.
        """
        result = [n, m]

        def helper(n: int, m: int, steps: int) -> None:
            if steps > 0:
                x = n + m
                result.append(x)
                helper(m, x, steps - 1)

        helper(n, m, steps - 2)
        return result

)"_python;




// 2
static const py::Function fibonacci3(
    "fibonacci3",
    [](
        py::Arg<"n", int>::opt n,
        py::Arg<"m", int>::opt m,
        py::Arg<"steps", int>::opt steps
    ) {
        std::vector<int> result = {n, m};

        std::function<void(int, int, int)> helper = [&](int n, int m, int steps) {
            if (steps > 0) {
                int x = n + m;
                result.push_back(x);
                helper(m, x, steps - 1);
            }
        };

        helper(n, m, steps - 2);
        return result;
    },
    py::arg<"n"> = 0,
    py::arg<"m"> = 1,
    py::arg<"steps"> = 10
);




std::vector<int> fibonacci4(int n = 0, int m = 1, int steps = 10) {
    std::vector<int> result = {n, m};

    std::function<void(int, int, int)> helper = [&](int n, int m, int steps) {
        if (steps > 0) {
            int x = n + m;
            result.push_back(x);
            helper(m, x, steps - 1);
        }
    };

    helper(n, m, steps - 2);
    return result;
}





void run() {
    static const auto demo_py = py::import<"demo_py">();
    static const auto fibonacci1 = demo_py.attr<"fibonacci1">();
    static const auto fibonacci2 = script({{"name", "world"}})["fibonacci2"];

    py::print(fibonacci1());
    py::print(fibonacci2());
    py::print(fibonacci3());
    py::print(fibonacci4());
}





BERTRAND_MODULE(demo_cpp, m) {
    m.def("run", "run the demo code from Python", run);
    m.attr<"fibonacci3">() = fibonacci3;
    m.def("fibonacci4", "A pure C++ function", fibonacci4);
}
