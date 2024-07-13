#ifndef BERTRAND_H
#define BERTRAND_H


// TODO: at some point, this should be translated into an actual __init__.cpp file.
// However, there has to be some way of disambiguating the C++ API from the Python API.
// during Python-level imports.  This is non-trivial, since I will have to detect the
// presence of an equivalent __init__.py file and add some identifier to avoid confusing
// the two.
// -> If __init__.py is present, then importing the module at the Python level will
// invoke the Python API.  The Python API can then import the C++ API and forward what
// it needs.
// -> If __init__.py is not present, then an equivalent shared library will be created
// using the C++ API.

// Basically, this involves tuning the target name such that it does not collide with
// __init__.py.  Perhaps __init__cpp.suffix.so?  __init__.py can then import it as
// from .__init__cpp import ...

// Or perhaps I ditch special treatment for __init__.cpp and just find a way to allow
// the module name to differ from the file name.  Maybe the source files can be
// anywhere, but the build products will always be placed into a Python-style
// source tree so that they can be correctly imported?




/* NOTE: this file basically acts like a Python-style __init__.py file, but for C++
 * headers within the bertrand:: namespace.  These headers will be automatically
 * installed alongside the python package when `pip install bertrand` is executed.
 * Assuming the include directories have been properly set up, users can access the C++
 * API with `#include <bertrand.h>`, which brings the full Bertrand C++ API into scope.
 * Specific submodules can be accessed via `#include <bertrand/{module}.h>`, which has
 * the same semantics as Python `from bertrand.module import *`.
 *
 * Setting up include paths:
 *
 *   1. For Python extensions listed in setup.py, add the following to the global
 *      setup() command:
 * 
 *          from bertrand import Extension, build_ext
 *
 *          EXTENSIONS = [
 *              Extension(
 *                  ...
 *              ),
 *              ...
 *          ]
 *
 *          setup(
 *              ...
 *              ext_modules=EXTENSIONS,
 *              cmdclass={"build_ext": build_ext},
 *              ...
 *          )
 *
 *   2. For standalone C++ projects, add the following to the compilation options:
 *
 *          $ c++ foo.cpp -o foo.out ... $(python3 -m bertrand -I) ...
 *
 * Both of these will make <bertrand.h> headers available to the compiler with no
 * further qualifications.
 *
 * Note that all bertrand-related functionality is hidden behind the bertrand::
 * namespace in order to avoid conflicts with other libraries.  As such, users may find
 * the following directives useful:
 *
 *      #include <bertrand/bertrand.h>
 *
 *      namespace py = bertrand::py;
 *      namespace np = bertrand::np;
 *      using namespace py::literals;  // brings pybind11::literals + _python literal into scope
 */

// #include "structs/linked.h"
#include "python.h"
#include "regex.h"


#endif  // BERTRAND_H
