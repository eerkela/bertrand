// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_REPEAT_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_REPEAT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_REPEAT_H

#include <type_traits>  // std::enable_if_t<>
#include "../../util/except.h"  // catch_python<>, TypeError
#include "../../util/python.h"  // le(), eq()
#include "../core/view.h"  // ViewTraits
#include "extend.h"  // extend()


namespace bertrand {
namespace structs {
namespace linked {


    /* Repeat the elements in a linked data structure a specified number of times. */
    template <typename View>
    auto repeat(const View& view, long long repetitions)
        -> std::enable_if_t<ViewTraits<View>::listlike, View>
    {
        // trivial case: empty repetition
        if (repetitions < 0 || view.size() == 0) {
            return View(view.max_size(), view.specialization());  // empty view
        }

        // copy existing view and preallocate space for repetitions
        View copy(view);
        copy.reserve(view.size() * repetitions);
        for (long long i = 1; i < repetitions; ++i) {
            extend(copy, view, false);
        }
        return copy;
    }


    /* Repeat the elements in a linked data structure a specified number of times. */
    template <typename View>
    auto repeat(const View& view, PyObject* repetitions)
        -> std::enable_if_t<ViewTraits<View>::listlike, View>
    {
        // check if repetitions is an integer
        if (!PyLong_Check(repetitions)) {
            std::ostringstream msg;
            msg << "can't multiply sequence by non-int of type '";
            msg << repetitions->ob_type->tp_name << "'";
            throw util::TypeError(msg.str());
        }

        // delegate to C++ overload
        long long n = PyLong_AsLongLong(repetitions);
        if (n == -1 && PyErr_Occurred()) {
            throw util::catch_python<util::TypeError>();
        }
        return repeat(view, n);
    }


    /* Repeat the elements of a linked data structure in-place the specified number of
    times. */
    template <typename View>
    auto repeat_inplace(View& view, long long repetitions)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        using Node = typename View::Node;

        // trivial case: empty repetition
        if (repetitions < 0 || view.size() == 0) {
            view.clear();
            return;
        }

        // NOTE: If we're careful, we can do this without copying the view.  This is
        // done by recording the original tail of the view and then repeatedly
        // iterating through the beginning portion while extending the list.  If we
        // preallocate the space for each repetition, then we don't even need to grow
        // the allocator array.

        // copy nodes in-place
        view.reserve(view.size() * static_cast<size_t>(repetitions));
        Node* tail = view.tail();
        for (long long i = 1; i < repetitions; ++i) {
            auto it = view.begin();
            for (; it.curr() != tail; ++it) {
                Node* copy = view.node(*it.curr());  // copy constructor
                view.link(view.tail(), copy, nullptr);
            }
            Node* copy = view.node(*it.curr());  // account for last item
            view.link(view.tail(), copy, nullptr);
        }
    }


    /* Repeat the elements of a linked data structure in-place the specified number of
    times. */
    template <typename View>
    auto repeat_inplace(View& view, PyObject* repetitions)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        // check if repetitions is an integer
        if (!PyLong_Check(repetitions)) {
            std::ostringstream msg;
            msg << "can't multiply sequence by non-int of type '";
            msg << repetitions->ob_type->tp_name << "'";
            throw util::TypeError(msg.str());
        }

        // delegate to C++ overload
        long long n = PyLong_AsLongLong(repetitions);
        if (n == -1 && PyErr_Occurred()) {
            throw util::catch_python<util::TypeError>();
        }
        repeat_inplace(view, n);
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_REPEAT_H
