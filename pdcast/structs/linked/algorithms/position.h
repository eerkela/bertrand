#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_POSITION_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_POSITION_H

#include <cstddef>  // size_t
#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>
#include <Python.h>  // CPython API
#include "../../util/base.h"  // is_pyobject<>
#include "../../util/except.h"  // TypeError
#include "../../util/ops.h"  // lt(), ge(), plus()
#include "../core/iter.h"  // Direction
#include "../core/view.h"  // ViewTraits


namespace bertrand {
namespace structs {
namespace linked {


    ///////////////////////////////////
    ////    INDEX NORMALIZATION    ////
    ///////////////////////////////////


    /* Normalize a numeric index, applying Python-style wraparound and bounds
    checking. */
    size_t normalize_index(long long index, size_t size, bool truncate) {
        // wraparound
        bool lt_zero = index < 0;
        if (lt_zero) {
            index += size;
            lt_zero = index < 0;
        }

        // boundscheck
        if (lt_zero || index >= static_cast<long long>(size)) {
            if (truncate) {
                return lt_zero ? 0 : size - 1;
            }
            throw IndexError("list index out of range");
        }

        return static_cast<size_t>(index);
    }


    /* Normalize a Python integer for use as an index to a list. */
    size_t normalize_index(PyObject* index, size_t size, bool truncate) {
        if (!PyLong_Check(index)) {
            throw TypeError("index must be a Python integer");
        }

        // error can still occur if python int is too large to fit in long long
        long long value = PyLong_AsLongLong(index);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return normalize_index(value, size, truncate);
    }


    /////////////////////
    ////    PROXY    ////
    /////////////////////


    /* A proxy for an element at a particular index of the list, as returned by the []
    operator. */
    template <typename View>
    class ElementProxy {
        using Node = typename View::Node;
        using Value = typename View::Value;

        template <Direction dir>
        using ViewIter = typename View::template Iterator<dir>;

        View& view;
        bool is_fwd;
        union {
            ViewIter<Direction::forward> fwd;
            ViewIter<Direction::backward> bwd;
        };

        template <typename _View>
        friend auto position(_View& view, long long index)
            -> std::enable_if_t<ViewTraits<_View>::linked, ElementProxy<_View>>;

        template <Direction dir>
        ElementProxy(View& view, ViewIter<dir>&& it) : view(view) {
            if constexpr (dir == Direction::forward) {
                is_fwd = true;
                new (&fwd) ViewIter<Direction::forward>(std::move(it));
            } else {
                is_fwd = false;
                new (&bwd) ViewIter<Direction::backward>(std::move(it));
            }
        }

    public:
        /* Disallow ElementProxies from being stored as lvalues. */
        ElementProxy(const ElementProxy&) = delete;
        ElementProxy(ElementProxy&&) = delete;
        ElementProxy& operator=(const ElementProxy&) = delete;
        ElementProxy& operator=(ElementProxy&&) = delete;

        /* Clean up the union iterator when the proxy is destroyed. */
        ~ElementProxy() {
            if (is_fwd) {
                fwd.~ViewIter<Direction::forward>();
            } else {
                bwd.~ViewIter<Direction::backward>();
            }
        }

        /* Get the value at the current index. */
        inline Value get() const {
            if constexpr (is_pyobject<Value>) {
                return Py_NewRef(is_fwd ? *fwd : *bwd);
            } else {
                return is_fwd ? *fwd : *bwd;
            }
        }

        /* Set the value at the current index. */
        inline void set(const Value value) {
            Node* node = view.node(value);
            view.recycle(is_fwd ? fwd.replace(node) : bwd.replace(node));
        }

        /* Delete the value at the current index. */
        inline void del() {
            view.recycle(is_fwd ? fwd.drop() : bwd.drop());
        }

        /* Implicitly convert the proxy to the value where applicable.  This is
        syntactic sugar for the get() method, such that `Value value = list[i]` is
        equivalent to `Value value = list[i].get()`. */
        inline operator Value() const {
            return get();
        }

        /* Assign the value at the current index.  This is syntactic sugar for the
        set() method, such that `list[i] = value` is equivalent to
        `list[i].set(value)`. */
        inline ElementProxy& operator=(const Value& value) {
            set(value);
            return *this;
        }

    };


    /* Get a proxy for a value at a particular index of the list. */
    template <typename View>
    auto position(View& view, long long index)
        -> std::enable_if_t<ViewTraits<View>::linked, ElementProxy<View>>
    {
        size_t norm_index = normalize_index(index, view.size(), false);

        // maybe get backward iterator if closer to tail
        if constexpr (NodeTraits<typename View::Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size() - 1; i > norm_index; --i, ++it);
                return ElementProxy<View>(view, std::move(it));
            }
        }

        // otherwise, use forward iterator
        auto it = view.begin();
        for (size_t i = 0; i < norm_index; ++i, ++it);
        return ElementProxy<View>(view, std::move(it));
    }


    /* Get a proxy for a value at a particular index of the list. */
    template <typename View>
    auto position(const View& view, long long index)
        -> std::enable_if_t<ViewTraits<View>::linked, const ElementProxy<View>>
    {
        size_t norm_index = normalize_index(index, view.size(), false);

        // maybe get backward iterator if closer to tail
        if constexpr (NodeTraits<typename View::Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size() - 1; i > norm_index; --i, ++it);
                return ElementProxy<View>(view, std::move(it));
            }
        }

        // otherwise, use forward iterator
        auto it = view.begin();
        for (size_t i = 0; i < norm_index; ++i, ++it);
        return ElementProxy<View>(view, std::move(it));
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_POSITION_H
