// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_LEXICAL_COMPARE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_LEXICAL_COMPARE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_LEXICAL_COMPARE_H

#include "../../util/iter.h"  // iter()
#include "../../util/python.h"  // lt()


namespace bertrand {
namespace structs {
namespace linked {


    /* Compare elements of two containers lexicographically, checking whether all
    elements of the left operand are less than or equal to their counterparts from the
    right operand, with ties broken if the left operand is shorter than the right. */
    template <typename LHS, typename RHS>
    bool lexical_lt(const LHS& lhs, const RHS& rhs) {
        auto it_lhs = util::iter(lhs).forward();
        auto it_rhs = util::iter(rhs).forward();

        // compare until one of the sequences is exhausted
        while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
            auto x = *it_lhs;
            auto y = *it_rhs;
            if (util::lt(x, y)) return true;
            if (util::lt(y, x)) return false;
            ++it_lhs;
            ++it_rhs;
        }

        // check if lhs is shorter than rhs
        return (!(it_lhs != it_lhs.end()) && it_rhs != it_rhs.end());
    }


    /* Compare elements of two containers lexicographically, checking whether all
    elements of the left operand are less than or equal to their counterparts from the
    right operand, with ties broken if the left operand is the same size or shorter
    than the right. */
    template <typename LHS, typename RHS>
    bool lexical_le(const LHS& lhs, const RHS& rhs) {
        auto it_lhs = util::iter(lhs).forward();
        auto it_rhs = util::iter(rhs).forward();

        // compare until one of the sequences is exhausted
        while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
            auto x = *it_lhs;
            auto y = *it_rhs;
            if (util::lt(x, y)) return true;
            if (util::lt(y, x)) return false;
            ++it_lhs;
            ++it_rhs;
        }

        // check if lhs is shorter than or equal to rhs
        return !(it_lhs != it_lhs.end());
    }


    /* Compare elements of two containers lexicographically, checking whether all
    elements of the left operand are stricly equal to their counterparts from the right
    operand, and that the left operand is the same length as the right. */
    template <typename LHS, typename RHS>
    bool lexical_eq(const LHS& lhs, const RHS& rhs) {
        auto it_lhs = util::iter(lhs).forward();
        auto it_rhs = util::iter(rhs).forward();

        // compare until one of the sequences is exhausted
        while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
            if (util::ne(*it_lhs, *it_rhs)) return false;
            ++it_lhs;
            ++it_rhs;
        }

        // check if both sequences are the same length
        return (!(it_lhs != it_lhs.end()) && !(it_rhs != it_rhs.end()));
    }


    /* Compare elements of two containers lexicographically, checking whether all
    elements of the left operand are greater than or equal to their counterparts from
    the right operand, with ties broken if the left operand is the same size or longer
    than the right. */
    template <typename LHS, typename RHS>
    bool lexical_ge(const LHS& lhs, const RHS& rhs) {
        auto it_lhs = util::iter(lhs).forward();
        auto it_rhs = util::iter(rhs).forward();

        // compare until one of the sequences is exhausted
        while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
            auto x = *it_lhs;
            auto y = *it_rhs;
            if (util::lt(y, x)) return true;
            if (util::lt(x, y)) return false;
            ++it_lhs;
            ++it_rhs;
        }

        // check if lhs is longer than or equal to rhs
        return !(it_rhs != it_rhs.end());
    }


    /* Compare elements of two containers lexicographically, checking whether all
    elements of the left operand are greater than or equal to their counterparts from
    the right operand, with ties broken if the left operand is longer than the right. */
    template <typename LHS, typename RHS>
    bool lexical_gt(const LHS& lhs, const RHS& rhs) {
        auto it_lhs = util::iter(lhs).forward();
        auto it_rhs = util::iter(rhs).forward();

        // compare until one of the sequences is exhausted
        while (it_lhs != it_lhs.end() && it_rhs != it_rhs.end()) {
            auto x = *it_lhs;
            auto y = *it_rhs;
            if (util::lt(y, x)) return true;
            if (util::lt(x, y)) return false;
            ++it_lhs;
            ++it_rhs;
        }

        // check if lhs is longer than rhs
        return (!(it_rhs != it_rhs.end()) && it_lhs != it_lhs.end());
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_LEXICAL_COMPARE_H
