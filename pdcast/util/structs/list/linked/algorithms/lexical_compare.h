// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_LEXICAL_COMPARE_H
#define BERTRAND_STRUCTS_ALGORITHMS_LEXICAL_COMPARE_H

#include <iterator>  // std::begin(), std::end()
#include <Python.h>  // CPython API


namespace bertrand {
namespace structs {
namespace linked {
namespace algorithms {


namespace list {

    /* Compare a view to an */
    template <typename View, typename T>
    bool lexical_lt(const View& lhs, const T rhs) {
        using Node = typename View::Node;

        // get coupled iterators
        auto iter_lhs = std::begin(lhs);
        auto end_lhs = std::end(lhs);
        auto iter_rhs = std::begin(rhs);
        auto end_rhs = std::end(rhs);

        // loop until one of the sequences is exhausted
        while (iter_lhs != end_lhs && iter_rhs != end_rhs) {
            Node* val = (*iter_lhs)->value();
            auto comp = *iter_rhs;
            if (val < comp) return true;
            if (comp < val) return false;
            ++iter_lhs;
            ++iter_rhs;
        }

        // check if lhs is shorter than rhs
        return (iter_lhs == end_lhs && iter_rhs != end_rhs);
    }

}  // namespace list


}  // namespace algorithms
}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H include guard
