// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_STRING_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_STRING_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_STRING_H

#include <sstream>  // std::ostringstream
#include <stack>  // std::stack
#include <string>  // std::string
#include <type_traits>  // std::enable_if_t<>
#include "../../util/repr.h"  // repr()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Get a comma-separated, possibly abbreviated string representing the contents
    of the list, for use in repr()-style string formatting. */
    template <typename View>
    auto repr(
        const View& view,
        const char* prefix,
        const char* lbracket,
        const char* rbracket,
        size_t max_entries
    )
        -> std::enable_if_t<ViewTraits<View>::listlike, std::string>
    {
        std::ostringstream stream;

        // append prefix + specialization if given
        stream << prefix;
        if (view.specialization() != nullptr) {
            stream << "[" << util::repr(view.specialization()) << "]";
        }

        // append left bracket
        stream << "(" << lbracket;

        // append first element
        auto it = view.cbegin();
        auto end = view.cend();
        if (it != end) {
            stream << util::repr(*it);
            ++it;
        }

        // abbreviate to avoid spamming the console with large lists
        if (view.size() > max_entries) {
            // append up to half the maximum number of entries
            size_t count = 1;
            size_t threshold = max_entries / 2;
            for (; it != end && count < threshold; ++it, ++count) {
                stream << ", " << util::repr(*it);
            }

            // ellipsis
            stream << ", ...";

            // append remaining elements from tail of list
            if constexpr (NodeTraits<typename View::Node>::has_prev) {
                // skip to the end and iterate backwards
                std::stack<std::string> stack;
                auto r_it = view.crbegin();
                auto r_end = view.crend();
                for (; r_it != r_end && count < max_entries; ++r_it, ++count) {
                    stack.push(util::repr(*r_it));
                }
                while (!stack.empty()) {
                    stream << ", " << stack.top();
                    stack.pop();
                }
            } else {
                // iterate forwards until we hit last remaining elements
                threshold = view.size() - (max_entries - threshold);
                for (size_t j = count; j < threshold; ++j, ++it);
                for (; it != end; ++it) {
                    stream << ", " << util::repr(*it);
                }
            }
        } else {
            for (; it != end; ++it) {
                stream << ", " << util::repr(*it);
            }
        }

        // append right bracket
        stream << rbracket << ")";

        // return as std::string
        return stream.str();
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_STRING_H
