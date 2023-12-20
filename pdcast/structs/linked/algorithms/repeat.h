#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_REPEAT_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_REPEAT_H

#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>
#include "../../util/except.h"  // catch_python, TypeError
#include "../../util/ops.h"  // le(), eq()
#include "../core/view.h"  // ViewTraits
#include "extend.h"  // extend()


namespace bertrand {
namespace linked {


    template <typename View>
    auto repeat(const View& view, long long repetitions)
        -> std::enable_if_t<
            ViewTraits<View>::listlike,
            typename ViewTraits<View>::As::DYNAMIC
        >
    {
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;

        if (repetitions < 0 || view.size() == 0) {
            return DynamicView(view.capacity(), view.specialization());
        }

        size_t reps = static_cast<size_t>(repetitions);
        DynamicView copy(view.size() * reps, view.specialization());

        for (size_t i = 0; i < reps; ++i) {
            for (auto it = view.begin(), end = view.end(); it != end; ++it) {
                copy.link(copy.tail(), copy.node(*(it.curr())), nullptr);
            }
        }
        return copy;
    }


    template <typename View>
    auto repeat(const View& view, PyObject* repetitions)
        -> std::enable_if_t<
            ViewTraits<View>::listlike,
            typename ViewTraits<View>::As::DYNAMIC
        >
    {
        PyObject* index = PyNumber_Index(repetitions);
        if (index == nullptr) {
            throw catch_python();
        }

        // error can still occur if python int is too large to fit in long long
        long long n = PyLong_AsLongLong(repetitions);
        Py_DECREF(index);
        if (n == -1 && PyErr_Occurred()) {
            throw catch_python();
        }

        return repeat(view, n);
    }


    template <Yield yield = Yield::KEY, bool as_pytuple = false, typename View>
    auto repeat(const View& view, long long repetitions)
        -> std::enable_if_t<
            ViewTraits<View>::dictlike,
            typename ViewTraits<
                typename ViewTraits<View>::As::DYNAMIC
            >::As::template List<>
        >
    {
        static_assert(
            yield != Yield::KEY,
            "cannot repeat dictionary keys: use setlike operators instead"
        );
        using DynamicView = typename ViewTraits<View>::As::DYNAMIC;
        using List = typename ViewTraits<DynamicView>::As::template List<>;

        if (repetitions < 0 || view.size() == 0) {
            return List(view.capacity(), view.specialization());  // empty view
        }

        size_t reps = static_cast<size_t>(repetitions);
        List list(view.size() * reps, view.specialization());

        for (size_t i = 0; i < reps; ++i) {
            auto it = view.template begin<yield, as_pytuple>();
            auto end = view.template end<yield, as_pytuple>();
            for (; it != end; ++it) {
                list.link(list.tail(), list.node(*it), nullptr);
            }
        }
        return list;
    }


    template <Yield yield = Yield::KEY, bool as_pytuple = false, typename View>
    auto repeat(const View& view, PyObject* repetitions)
        -> std::enable_if_t<
            ViewTraits<View>::dictlike,
            typename ViewTraits<
                typename ViewTraits<View>::As::DYNAMIC
            >::As::template List<>
        >
    {
        PyObject* index = PyNumber_Index(repetitions);
        if (index == nullptr) {
            throw catch_python();
        }

        // error can still occur if python int is too large to fit in long long
        long long n = PyLong_AsLongLong(repetitions);
        Py_DECREF(index);
        if (n == -1 && PyErr_Occurred()) {
            throw catch_python();
        }

        return repeat<yield, as_pytuple>(view, n);
    }


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


    template <typename View>
    auto repeat_inplace(View& view, PyObject* repetitions)
        -> std::enable_if_t<ViewTraits<View>::listlike, void>
    {
        PyObject* index = PyNumber_Index(repetitions);
        if (index == nullptr) {
            throw catch_python();
        }

        // error can still occur if python int is too large to fit in long long
        long long n = PyLong_AsLongLong(repetitions);
        Py_DECREF(index);
        if (n == -1 && PyErr_Occurred()) {
            throw catch_python();
        }

        repeat_inplace(view, n);
    }


}  // namespace linked
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALGORITHMS_REPEAT_H
