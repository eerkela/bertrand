// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_POSITION_H
#define BERTRAND_STRUCTS_ALGORITHMS_POSITION_H

#include <cstddef>  // size_t
#include <stdexcept>  // std::out_of_range
#include <sstream>  // std::ostringstream
#include <Python.h>  // CPython API
#include "../iter.h"  // Direction, Bidirectional
#include "../../util/except.h"  // type_error()


namespace bertrand {
namespace structs {
namespace linked {
namespace algorithms {


namespace list {

    template <typename View>
    class ElementProxy;  // forward declaration

    /* Normalize a numeric index, applying Python-style wraparound and bounds
    checking. */
    template <typename T>
    size_t normalize_index(T index, size_t size, bool truncate) {
        // wraparound negative indices
        bool lt_zero = index < 0;
        if (lt_zero) {
            index += size;
            lt_zero = index < 0;
        }

        // boundscheck
        if (lt_zero || index >= static_cast<T>(size)) {
            if (truncate) {
                if (lt_zero) {
                    return 0;
                }
                return size - 1;
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
        PyObject* py_zero = PyLong_FromSize_t(0);  // new reference
        PyObject* py_size = PyLong_FromSize_t(size);  // new reference
        int lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);

        // wraparound negative indices
        bool release_index = false;
        if (lt_zero) {
            index = PyNumber_Add(index, py_size);  // new reference
            lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);
            release_index = true;  // remember to DECREF index later
        }

        // boundscheck - value is bad
        if (lt_zero || PyObject_RichCompareBool(index, py_size, Py_GE)) {
            Py_DECREF(py_zero);
            Py_DECREF(py_size);
            if (release_index) {
                Py_DECREF(index);
            }

            // apply truncation if directed
            if (truncate) {
                if (lt_zero) {
                    return 0;
                }
                return size - 1;
            }

            // raise IndexError
            throw std::out_of_range("list index out of range");
        }

        // value is good - cast to size_t
        size_t result = PyLong_AsSize_t(index);

        // clean up references
        Py_DECREF(py_zero);
        Py_DECREF(py_size);
        if (release_index) {
            Py_DECREF(index);
        }

        return result;
    }

    /* Get a proxy for a value at a particular index of the list. */
    template <
        typename View,
        typename T,
        template <Direction> class Iterator = View::template Iterator
    >
    ElementProxy<View> position(View& view, T index) {
        using Node = typename View::Node;

        // normalize index
        size_t norm_index = normalize_index(index, view.size(), false);

        // get iterator to index
        if constexpr (Node::doubly_linked) {
            size_t threshold = (view.size() - (view.size() > 0)) / 2;
            if (norm_index > threshold) {  // backward traversal
                Iterator<Direction::backward> iter = view.rbegin();
                for (size_t i = view.size() - 1; i > norm_index; --i) {
                    ++iter;
                }
                return ElementProxy<View>(view, iter);
            }
        }

        // forward traversal
        Iterator<Direction::forward> iter = view.begin();
        for (size_t i = 0; i < norm_index; ++i) {
            ++iter;
        }
        return ElementProxy<View>(view, iter);
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
            return (*iter)->value();
        }

        /* Set the value at the current index. */
        inline void set(const Value value) {
            Node* node = view.node(value);
            iter.replace(node);
        }

        /* Insert a value at the current index. */
        inline void insert(const Value value) {
            Node* node = view.node(value);
            iter.insert(node);
        }

        /* Delete the value at the current index. */
        inline void del() {
            iter.remove();
        }

        /* Remove the node at the current index and return its value. */
        inline Value pop() {
            Node* node = iter.remove();
            Value result = node->value();
            Py_INCREF(result);  // ensure value is not garbage collected during recycle()
            view.recycle(node);
            return result;
        }

        /* Implicitly convert the proxy to the value where applicable.

        This is syntactic sugar for get() such that `Value value = list[i]` is
        equivalent to `Value value = list[i].get()`.  The same implicit conversion
        is also applied if the proxy is passed to a function that expects a value,
        unless that function is marked as `explicit`. */
        inline operator Value() const {
            return get();
        }

        /* Assign the value at the current index.

        This is syntactic sugar for set() such that `list[i] = value` is equivalent to
        `list[i].set(value)`. */
        inline ElementProxy& operator=(const Value& value) {
            set(value);
            return *this;
        }

    private:
        template <typename _View, typename T, template <Direction> class _Iterator>
        friend ElementProxy<_View> position(_View& view, T index);

        View& view;
        Bidirectional<Iterator> iter;

        template <Direction dir>
        ElementProxy(View& view, Iterator<dir>& iter) : view(view), iter(iter) {}
    };

}  // namespace list


}  // namespace algorithms
}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_ALGORITHMS_APPEND_H include guard
