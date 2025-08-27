#ifndef BERTRAND_ITER_SPLIT_H
#define BERTRAND_ITER_SPLIT_H

#include "bertrand/common.h"
#include "bertrand/iter/range.h"


namespace bertrand {


/// TODO: split{n/func/mask/mptr} becomes an equivalent to group_by in most other
/// languages, where `n` is an integer count to split on, `func` is a key function
/// that accepts an element as input, `mask` is a boolean mask that gets split into
/// two groups (true and false), and `mptr` is a member pointer that gets converted
/// into an equivalent `func`.
/// -> There also needs to be a way to split on a particular separator, which currently
/// conflicts with the `n` version of `split{n}`.  There would have to be some
/// workaround for that, which may also permit `peek{}` to be implemented in a similar
/// way.


}


#endif  // BERTRAND_ITER_SPLIT_H