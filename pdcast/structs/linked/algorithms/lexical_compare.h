// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_LEXICAL_COMPARE_H
#define BERTRAND_STRUCTS_ALGORITHMS_LEXICAL_COMPARE_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/iter.h"  // iter()
#include "../../util/python.h"  // std::less<>, std::greater<>, std::equal_to<>, etc.
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Compare a view to an */
    template <typename View, typename Container>
    auto lexical_lt(const View& lhs, const Container& rhs)
        -> std::enable_if_t<ViewTraits<View>::listlike, bool>
    {
        using Node = typename View::Node;
        auto it_lhs = util::iter(lhs).forward();
        auto it_rhs = util::iter(rhs).forward();

        // compare until one of the sequences is exhausted
        while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
            Node* val = *it_lhs;
            auto comp = *it_rhs;
            if (val < comp) return true;
            if (comp < val) return false;
            ++it_lhs;
            ++it_rhs;
        }

        // check if lhs is shorter than rhs
        return (it_lhs == it_lhs.end() && it_rhs != it_rhs.end());
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_COMPARE_H include guard
