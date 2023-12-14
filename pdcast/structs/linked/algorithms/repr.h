#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_STRING_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_STRING_H

#include <sstream>  // std::ostringstream
#include <stack>  // std::stack
#include <string>  // std::string
#include <type_traits>  // std::enable_if_t<>
#include "../../util/container.h"  // python::Slice
#include "../../util/ops.h"  // bertrand::repr()
#include "../core/node.h"  // NodeTraits
#include "../core/view.h"  // ViewTraits, Yield


namespace bertrand {
namespace linked {


    /* Get a comma-separated, possibly abbreviated string representing the contents
    of the list, for use in repr()-style string formatting. */
    template <Yield yield = Yield::KEY, typename View>
    auto build_repr(
        const View& view,
        const char* prefix,
        const char* lbracket,
        const char* rbracket,
        size_t max_entries,
        const char* item_prefix = "",
        const char* item_separator = ": ",
        const char* item_suffix = ""
    ) -> std::enable_if_t<ViewTraits<View>::linked, std::string>
    {
        std::ostringstream stream;

        // append prefix, specialization if given
        stream << prefix;
        if (view.specialization() != nullptr) {
            PyObject* spec = view.specialization();
            stream << "[";
            if constexpr (ViewTraits<View>::dictlike) {
                if (PySlice_Check(spec)) {
                    python::Slice<python::Ref::BORROW> slice(spec);
                    if constexpr (yield == Yield::KEY) {
                        stream << repr(slice.start());
                    } else if constexpr (yield == Yield::VALUE) {
                        stream << repr(slice.stop());
                    } else {
                        stream << repr(slice.start()) << " : ";
                        stream << repr(slice.stop());
                    }
                } else {
                    stream << repr(spec);
                }
            } else {
                stream << repr(spec);
            }
            stream << "]";
        }

        // append left bracket
        stream << "(" << lbracket;

        // Helper for generating a token from a single element of the data structure
        auto token = [&](std::ostringstream& stream, auto it) {
            if constexpr (ViewTraits<View>::dictlike && yield == Yield::ITEM) {
                auto item = *it;
                stream << item_prefix << repr(item.first) << item_separator;
                stream << repr(item.second) << item_suffix;
            } else {
                stream << repr(*it);
            }
        };

        // append first element
        auto it = view.template cbegin<yield>();
        auto end = view.template cend<yield>();
        if (it != end) {
            token(stream, it);
            ++it;
        }

        // abbreviate to avoid spamming the console
        if (view.size() > max_entries) {
            size_t count = 1;
            size_t threshold = max_entries / 2;
            for (; it != end && count < threshold; ++it, ++count) {
                stream << ", ";
                token(stream, it);
            }

            stream << ", ...";

            // NOTE: if doubly-linked, skip to the end and iterate backwards
            if constexpr (NodeTraits<typename View::Node>::has_prev) {
                std::stack<std::string> stack;
                auto r_it = view.template crbegin<yield>();
                auto r_end = view.template crend<yield>();
                for (; r_it != r_end && count < max_entries; ++r_it, ++count) {
                    std::ostringstream ss;
                    token(ss, r_it);
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
                while (it != end) {
                    stream << ", ";
                    token(stream, it);
                    ++it;
                }
            }

        } else {
            while (it != end) {
                stream << ", ";
                token(stream, it);
                ++it;
            }
        }

        // append right bracket
        stream << rbracket << ")";
        return stream.str();
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_STRING_H
