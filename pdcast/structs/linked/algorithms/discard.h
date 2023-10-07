// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_DISCARD_H
#define BERTRAND_STRUCTS_ALGORITHMS_DISCARD_H

#include <Python.h>  // CPython API
#include "../core/node.h"  // has_prev<>
#include "../core/view.h"  // views
#include "remove.h"  // _drop_setlike(), _drop_relative()


namespace Ops {

    /* Remove an item from a linked set or dictionary if it is present. */
    template <typename View>
    inline void discard(View* view, PyObject* item) {
        _drop_setlike(view, item, false);  // suppress errors
    }

    /* Remove an item from a linked set or dictionary immediately after the
    specified sentinel value. */
    template <typename View>
    inline void discard_relative(View* view, PyObject* sentinel, Py_ssize_t offset) {
        _drop_relative(view, sentinel, offset, false);  // suppress errors
    }

}


#endif  // BERTRAND_STRUCTS_ALGORITHMS_DISCARD_H include guard
