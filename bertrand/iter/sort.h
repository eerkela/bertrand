#ifndef BERTRAND_ITER_SORT_H
#define BERTRAND_ITER_SORT_H

#include "bertrand/common.h"
#include "bertrand/iter/range.h"


namespace bertrand {


/// TODO: copy over the sorting logic from before, and turn it into a range adaptor
/// with as little overhead as possible.  Sorting an immutable input means sorting an
/// array of `impl::ref` objects to its contents, which gets allocated and populated
/// at the same time as the insertion sort pass.


}


#endif  // BERTRAND_ITER_SORT_H