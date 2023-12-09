#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_REPEAT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_REPEAT_H

#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>
#include "../../util/except.h"  // catch_python, TypeError
#include "../../util/ops.h"  // le(), eq()
#include "../core/view.h"  // ViewTraits
#include "extend.h"  // extend()


// TODO: work in Yield::VALUE/ITEM for dictlike views.


namespace bertrand {
namespace linked {


    /* Repeat the elements in a linked data structure a specified number of times. */
    template <typename View>
    auto repeat(const View& view, long long repetitions)
        -> std::enable_if_t<ViewTraits<View>::listlike, View>
    {
        using Node = typename View::Node;
        if (repetitions < 0 || view.size() == 0) {
            return View(view.capacity(), view.specialization());  // empty view
        }

        size_t reps = static_cast<size_t>(repetitions);
        View copy(view.size() * reps, view.specialization());

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
        if (!PyLong_Check(repetitions)) {
            std::ostringstream msg;
            msg << "can't multiply sequence by non-int of type '";
            msg << repetitions->ob_type->tp_name << "'";
            throw TypeError(msg.str());
        }

        // error can still occur if python int is too large to fit in long long
        long long n = PyLong_AsLongLong(repetitions);
        if (n == -1 && PyErr_Occurred()) {
            throw catch_python();
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
        if (repetitions < 0 || view.size() == 0) {
            view.clear();
            return;
        }

        // NOTE: If we're careful, we can do this without copying the view.  This is
        // done by recording the original tail and repeatedly iterating through the
        // beginning portion while extending the list.

        size_t reps = static_cast<size_t>(repetitions);
        view.reserve(view.size() * reps);
        Node* tail = view.tail();

        for (size_t i = 1; i < reps; ++i) {
            auto it = view.begin();
            for (; it.curr() != tail; ++it) {
                Node* copy = view.node(*it.curr());
                view.link(view.tail(), copy, nullptr);
            }
            Node* copy = view.node(*it.curr());
            view.link(view.tail(), copy, nullptr);
        }
    }


    /* Repeat the elements of a linked data structure in-place the specified number of
    times. */
    template <typename View>
    auto repeat_inplace(View& view, PyObject* repetitions)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        if (!PyLong_Check(repetitions)) {
            std::ostringstream msg;
            msg << "can't multiply sequence by non-int of type '";
            msg << repetitions->ob_type->tp_name << "'";
            throw TypeError(msg.str());
        }

        // error can still occur if python int is too large to fit in long long
        long long n = PyLong_AsLongLong(repetitions);
        if (n == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        repeat_inplace(view, n);
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_REPEAT_H
