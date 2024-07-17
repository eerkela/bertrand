module;

#define BERTRAND_PYTHON_MODULE_GUARD
#include "__init__.h"
#undef BERTRAND_PYTHON_MODULE_GUARD

export module bertrand.python;


export namespace py {
    using bertrand::py::Object;
    using bertrand::py::print;
}
