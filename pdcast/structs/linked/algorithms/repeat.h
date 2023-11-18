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
        using Node = typename View::Node;

        // trivial case: empty repetition
        if (repetitions < 0 || view.size() == 0) {
            return View(view.capacity(), view.dynamic(), view.specialization());
        }

        // preallocate exact size
        size_t reps = static_cast<size_t>(repetitions);
        View copy(view.size() * reps, view.dynamic(), view.specialization());

        // repeatedly copy nodes from original view
        for (size_t i = 0; i < reps; ++i) {
            for (auto it = view.begin(), end = view.end(); it != end; ++it) {
                Node* node = copy.node(*(it.curr()));
                copy.link(copy.tail(), node, nullptr);
            }
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

        // reserve exact size
        size_t reps = static_cast<size_t>(repetitions);
        view.reserve(view.size() * reps);

        // NOTE: If we're careful, we can do this without copying the view.  This is
        // done by recording the original tail and repeatedly iterating through the
        // beginning portion while extending the list.

        // copy nodes in-place
        Node* tail = view.tail();
        for (size_t i = 1; i < reps; ++i) {
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
