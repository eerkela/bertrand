// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_POSITION_H
#define BERTRAND_STRUCTS_ALGORITHMS_POSITION_H

#include <cstddef>  // size_t
#include <stdexcept>  // std::out_of_range
#include <sstream>  // std::ostringstream
#include <Python.h>  // CPython API
#include "../../util/base.h"  // is_pyobject<>
#include "../../util/except.h"  // type_error()
#include "../../util/python.h"  // lt(), ge(), plus()
#include "../core/iter.h"  // Direction, Bidirectional
#include "../core/view.h"  // ViewTraits


// TODO: ElementProxy.insert() should be deleted.  The problem is that allocating a
// new node can cause the table to grow, which invalidates the iterator.  This
// causes an interminable conflict, so we should just disallow it.  What we should do
// instead is reserve space for the new node ahead of time, then generate an iterator
// and do the insertion manually in the linked::insert() method.


namespace bertrand {
namespace structs {
namespace linked {


    /* forward declaration */
    template <typename View>
    class ElementProxy;


    /* Normalize a numeric index, applying Python-style wraparound and bounds
    checking. */
    template <typename Index>
    size_t normalize_index(Index index, size_t size, bool truncate) {
        // wraparound negative indices
        bool lt_zero = index < 0;
        if (lt_zero) {
            index += size;
            lt_zero = index < 0;
        }

        // boundscheck
        if (lt_zero || index >= static_cast<Index>(size)) {
            if (truncate) {
                return lt_zero ? 0 : size - 1;
            }
            throw std::out_of_range("list index out of range");
        }

        // return as size_t
        return static_cast<size_t>(index);
    }


    /* Normalize a Python integer for use as an index to a list. */
    size_t normalize_index(PyObject* index, size_t size, bool truncate) {
        // check that index is a Python integer
        if (!PyLong_Check(index)) {
            throw util::type_error("index must be a Python integer");
        }

        // comparisons are kept at the python level until we're ready to return
        PyObject* py_zero = PyLong_FromSize_t(0);  // new ref
        PyObject* py_size = PyLong_FromSize_t(size);  // new ref
        int lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);

        // wraparound negative indices
        bool release_index = false;
        if (lt_zero) {
            index = PyNumber_Add(index, py_size);  // new ref
            lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);
            release_index = true;  // remember to DECREF index later
        }

        // boundscheck - value is bad
        if (lt_zero || PyObject_RichCompareBool(index, py_size, Py_GE)) {
            Py_DECREF(py_zero);
            Py_DECREF(py_size);
            if (release_index) Py_DECREF(index);

            // truncate if directed
            if (truncate) {
                return lt_zero ? 0 : size - 1;
            }
            throw std::out_of_range("list index out of range");
        }

        // value is good - cast to size_t
        size_t result = PyLong_AsSize_t(index);

        // clean up references
        Py_DECREF(py_zero);
        Py_DECREF(py_size);
        if (release_index) Py_DECREF(index);
        return result;
    }


    /* Get a proxy for a value at a particular index of the list. */
    template <
        typename View,
        typename Index
    >
    auto position(View& view, Index index)
        -> std::enable_if_t<ViewTraits<View>::listlike, ElementProxy<View>>
    {
        // normalize index
        size_t norm_index = normalize_index(index, view.size(), false);

        // get iterator to index
        if constexpr (NodeTraits<typename View::Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.rbegin();
                for (size_t i = view.size() - 1; i > norm_index; --i) ++it;
                return ElementProxy<View>(view, std::move(it));
            }
        }

        // forward traversal
        auto it = view.begin();
        for (size_t i = 0; i < norm_index; ++i) ++it;
        return ElementProxy<View>(view, std::move(it));
    }


    /* A proxy for an element at a particular index of the list, as returned by the []
    operator. */
    template <typename View>
    class ElementProxy {
        using Node = typename View::Node;
        using Value = typename View::Value;

        template <Direction dir>
        using Iterator = typename View::template Iterator<dir>;

    public:

        /* Get the value at the current index. */
        inline Value get() const {
            if constexpr (util::is_pyobject<Value>) {
                return Py_NewRef(*iter);
            } else {
                return *iter;
            }
        }

        /* Set the value at the current index. */
        inline void set(const Value value) {
            Node* node = view.node(value);
            view.recycle(iter.replace(node));
        }

        /* Delete the value at the current index. */
        inline void del() {
            view.recycle(iter.drop());
        }

        /* Implicitly convert the proxy to the value where applicable.

        This is syntactic sugar for the get() method, such that `Value value = list[i]`
        is equivalent to `Value value = list[i].get()`.  The same implicit conversion
        is also applied if the proxy is passed to a function that expects a value,
        unless that function is marked as `explicit`. */
        inline operator Value() const {
            return get();
        }

        /* Assign the value at the current index.

        This is syntactic sugar for the set() method, such that `list[i] = value` is
        equivalent to `list[i].set(value)`. */
        inline ElementProxy& operator=(const Value& value) {
            set(value);
            return *this;
        }

        /* Disallow ElementProxies from being stored as lvalues. */
        ElementProxy(const ElementProxy&) = delete;
        ElementProxy(ElementProxy&&) = delete;
        ElementProxy& operator=(const ElementProxy&) = delete;
        ElementProxy& operator=(ElementProxy&&) = delete;

    private:
        View& view;
        Bidirectional<Iterator> iter;

        template <typename _View, typename _Index>
        friend auto position(_View& view, _Index index)
            -> std::enable_if_t<ViewTraits<_View>::listlike, ElementProxy<_View>>;

        template <Direction dir>
        ElementProxy(View& view, Iterator<dir>&& iter) :
            view(view), iter(std::move(iter))
        {}
    };


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_APPEND_H include guard
