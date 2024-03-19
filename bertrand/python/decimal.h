#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_DECIMAL_H
#define BERTRAND_PYTHON_DECIMAL_H

#include "common.h"


// TODO: note that Decimal uses C-style division and modulo operations.  This would
// to be accounted for in math.h, which would need to handle decimal objects separately.

// TODO: Decimal:: should also be used to get and set the context flags.

// NOTE: this is going to be a lot more complicated than it would seem at first.


namespace bertrand {
namespace py {




}  // namespace py
}  // namespace bertrand


#endif // BERTRAND_PYTHON_DECIMAL_H
