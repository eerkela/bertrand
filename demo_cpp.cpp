#include <bertrand/bertrand.h>
namespace py = bertrand::py;
using namespace py::literals;


static const py::Code script = R"(
    def add(x: int, y: int) -> int:
        """Add 2 numbers in Python.

        Parameters
        ----------
        x : int
            left-hand operand
        y : int
            right-hand operand

        Returns
        -------
        int
            sum of x and y
        """
        return x + y
)"_python;


py::Int add(py::Int x, py::Int y) {
    return x + y;
}


void run() {
    py::Int a = py::import<"demo_py">().attr<"add">()(1, 2);
    py::Int b = script()["add"](1, 2);
    py::Int c = add(1, 2);
    py::Int d = 1 + 2;
    int e = d;

    py::print(a, b, c, d, e);
}


PYBIND11_MODULE(demo_cpp, m) {
    m.doc() = "Example bertrand plugin";
    m.def("run", &run, "Example function to demonstrate mixed Python/C++ code");
}
