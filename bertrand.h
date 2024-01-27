#ifndef BERTRAND_H
#define BERTRAND_H

/* NOTE: this file basically acts like a Python-style __init__.py file, but for C++
 * headers within the `bertrand::` namespace.  These will be automatically installed
 * alongside the python package when `pip install bertrand` is run, and this header
 * will serve as an entry point into the C++ API.  Users can access this API via
 * `#include <bertrand.h>`, which grants access to all of the C++ data structures
 * and algorithms from the `bertrand::` namespace.
 */

#include "bertrand/structs/linked.h"


#endif  // BERTRAND_H
