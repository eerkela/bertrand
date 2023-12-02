#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_STRING_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_STRING_H

#include <sstream>  // std::ostringstream
#include <stack>  // std::stack
#include <string>  // std::string
#include <type_traits>  // std::enable_if_t<>
#include "../../util/ops.h"  // bertrand::repr()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Get a comma-separated, possibly abbreviated string representing the contents
    of the list, for use in repr()-style string formatting. */
    template <typename View>
    auto build_repr(
        const View& view,
        const char* prefix,
        const char* lbracket,
        const char* rbracket,
        size_t max_entries
    ) -> std::enable_if_t<ViewTraits<View>::linked, std::string>
    {
        std::ostringstream stream;

        // append prefix, specialization if given
        stream << prefix;
        if (view.specialization() != nullptr) {
            stream << "[" << bertrand::repr(view.specialization()) << "]";
        }

        // append left bracket
        stream << "(" << lbracket;

        // append first element
        auto it = view.cbegin();
        auto end = view.cend();
        if (it != end) {
            stream << bertrand::repr(*it);
            ++it;
        }

        // Helper for generating a single token in the given data structure
        auto execute = [](std::ostringstream& stream, auto it) {
            if constexpr (ViewTraits<View>::dictlike) {
                stream << ", " << bertrand::repr(*it) << ": ";
                stream << bertrand::repr(it.curr()->mapped());
            } else {
                stream << ", " << bertrand::repr(*it);
            }
        };

        // abbreviate to avoid spamming the console
        if (view.size() > max_entries) {
            size_t count = 1;
            size_t threshold = max_entries / 2;
            for (; it != end && count < threshold; ++it, ++count) {
                execute(stream, it);
            }

            stream << ", ...";

            // NOTE: if doubly-linked, skip to the end and iterate backwards
            if constexpr (NodeTraits<typename View::Node>::has_prev) {
                std::stack<std::string> stack;
                auto r_it = view.crbegin();
                auto r_end = view.crend();
                for (; r_it != r_end && count < max_entries; ++r_it, ++count) {
                    std::ostringstream ss;
                    execute(ss, r_it);
                    stack.push(ss.str());
                }
                while (!stack.empty()) {
                    stream << ", " << stack.top();
                    stack.pop();
                }

            // otherwise, continue until we hit remaining elements
            } else {
                threshold = view.size() - (max_entries - threshold);
                for (size_t j = count; j < threshold; ++j, ++it);
                for (; it != end; ++it) {
                    execute(stream, it);
                }
            }

        } else {
            for (; it != end; ++it) {
                execute(stream, it);
            }
        }

        // append right bracket
        stream << rbracket << ")";
        return stream.str();
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_STRING_H
