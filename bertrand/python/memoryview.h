#ifndef BERTRAND_PYTHON_MEMORYVIEW_H
#define BERTRAND_PYTHON_MEMORYVIEW_H

#include "common.h"


namespace py {

/* TODO: memoryview interface is the main way to access the buffer protocol.  They can
 * be built from any python object by calling PyObject_GetBuffer().
 *
 * Check out PyMemoryView API, plus C API docs for buffer protocol:
 * https://docs.python.org/3/c-api/buffer.html
 *
 * tobytes(order="C")
 * hex([sep[, bytes_per_sep]])
 * tolist()
 * toreadonly()
 * release()
 * cast(format[, shape])
 * obj
 * nbytes
 * readonly
 * format
 * itemsize
 * ndim
 * shape
 * strides
 * suboffsets
 * c_contiguous
 * f_contiguous
 * contiguous
 */



}  // namespace py


#endif  // BERTRAND_PYTHON_MEMORYVIEW_H
