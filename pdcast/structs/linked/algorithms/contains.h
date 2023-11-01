// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H
#define BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H

#include <Python.h>  // CPython API
#include <type_traits>  // std::enable_if_t<>
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    /* Check if an item is contained within a linked list. */
    template <typename View>
    inline auto contains(View& view, PyObject* item)
        -> std::enable_if_t<ViewTraits<View>::listlike, bool>
    {
        for (auto it = view.begin(), end = view.end(); it != end; ++it) {
            if (it.curr()->eq(item)) return true;
        }
        return false;
    }


    /* Check if an item is contained within a linked set or dictionary. */
    template <typename View>
    inline auto contains(View& view, PyObject* item)
        -> std::enable_if_t<ViewTraits<View>::setlike, bool>
    {
        return view.search(item) != nullptr;
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_CONTAINS_H include guard
