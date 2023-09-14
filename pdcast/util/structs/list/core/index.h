// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_INDEX_H
#define BERTRAND_STRUCTS_CORE_INDEX_H

#include <optional>  // std::optional
#include <type_traits>  // std::enable_if_t
#include <Python.h>  // CPython API
#include "node.h"  // has_prev<>
#include "iter.h"  // IteratorFactory
#include "util.h"  // CoupledIterator


////////////////////////
////    FUNCTORS    ////
////////////////////////


/*
NOTE: IndexFactory is a functor (function object) that produces iterators to a specific
index of a linked list, set, or dictionary.  It is used by view classes to provide a
uniform interface for accessing elements by their index, with the same semantics for
index normalization and bounds checking as Python lists.
*/


/* A functor that produces unidirectional iterators to a specific index of the
templated view. */
template <typename ViewType>
class IndexFactory {
public:
    using View = ViewType;
    using Node = typename View::Node;
    inline static constexpr bool doubly_linked = has_prev<Node>::value;

    // NOTE: Reverse iterators are only compiled for doubly-linked lists.

    template <
        Direction dir = Direction::forward,
        typename = std::enable_if_t<dir == Direction::forward || doubly_linked>
    >
    class Iterator;

    template <
        Direction dir = Direction::forward,
        typename = std::enable_if_t<dir == Direction::forward || doubly_linked>
    >
    using IteratorPair = CoupledIterator<Iterator<dir>>;

    IndexFactory(View& view) : view(view) {}

    /////////////////////////
    ////    ITERATORS    ////
    /////////////////////////

    /* Return a bidirectional iterator at an arbitrary index of a linked list. */
    template <typename T>
    std::optional<Bidirectional<Iterator>> operator()(
        T index,
        bool truncate = false
    ) const {
        // normalize index
        auto opt_index = normalize(index, truncate);
        if (!opt_index.has_value()) {
            return std::nullopt;
        }

        // get iterator to index
        size_t norm_index = opt_index.value();
        if constexpr (doubly_linked) {
            if (norm_index > (view.size - (view.size > 0)) / 2) {  // backward traversal
                Iterator<Direction::backward> it(view, view.tail, view.size - 1);
                for (size_t i = view.size - 1; i > norm_index; --i) {
                    ++it;
                }
                return std::make_optional(Bidirectional(it));
            }
        }

        // forward traversal
        Iterator<Direction::forward> it(view, view.head, 0);
        for (size_t i = 0; i < norm_index; ++i) {
            ++it;
        }
        return std::make_optional(Bidirectional(it));
    }

    /* Return a forward iterator at an arbitrary index of a linked list. */
    template <typename T>
    std::optional<Iterator<Direction::forward>> forward(
        T index,
        bool truncate = false
    ) const {
        return _directional<Direction::forward>(index, truncate);
    }

    /* Return a backward iterator at an arbitrary index of a linked list. */
    template <typename T, typename = std::enable_if<doubly_linked>>
    auto backward(T index, bool truncate = false) const {
        return _directional<Direction::backward>(index, truncate);
    }

    /////////////////////////////////
    ////    NAMESPACE METHODS    ////
    /////////////////////////////////

    /* Normalize a numeric index, applying Python-style wraparound and bounds
    checking. */
    template <typename T>
    std::optional<size_t> normalize(T index, bool truncate = false) const {
        bool index_lt_zero = index < 0;

        // wraparound negative indices
        if (index_lt_zero) {
            index += view.size;
            index_lt_zero = index < 0;
        }

        // boundscheck
        if (index_lt_zero || index >= static_cast<T>(view.size)) {
            if (truncate) {
                if (index_lt_zero) {
                    return 0;
                }
                return view.size - 1;
            }
            PyErr_SetString(PyExc_IndexError, "list index out of range");
            return std::nullopt;
        }

        // return as size_t
        return std::make_optional(static_cast<size_t>(index));
    }

    /* Normalize a Python integer for use as an index to the list. */
    std::optional<size_t> normalize(PyObject* index, bool truncate = false) const {
        // check that index is a Python integer
        if (!PyLong_Check(index)) {
            PyErr_SetString(PyExc_TypeError, "index must be a Python integer");
            return std::nullopt;
        }

        // comparisons are kept at the python level until we're ready to return
        PyObject* py_zero = PyLong_FromSize_t(0);  // new reference
        PyObject* py_size = PyLong_FromSize_t(view.size);  // new reference
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
                    return std::make_optional(static_cast<size_t>(0));
                }
                return std::make_optional(view.size - 1);
            }

            // raise IndexError
            PyErr_SetString(PyExc_IndexError, "list index out of range");
            return std::nullopt;
        }

        // value is good - cast to size_t
        size_t result = PyLong_AsSize_t(index);

        // clean up references
        Py_DECREF(py_zero);
        Py_DECREF(py_size);
        if (release_index) {
            Py_DECREF(index);
        }

        return std::make_optional(result);
    }

    /////////////////////////////
    ////    INNER CLASSES    ////
    /////////////////////////////

    template <Direction dir>
    using BaseIterator = typename IteratorFactory<View>::template Iterator<dir>;

    template <Direction dir, typename>
    class Iterator : public BaseIterator<dir> {
    public:
        using Base = BaseIterator<dir>;

        /* prefix increment to advance iterator and update index. */
        inline Iterator& operator++() {
            if constexpr (dir == Direction::backward) {
                --idx;
            } else {
                ++idx;
            }
            Base::operator++();
            return *this;
        }

        /* Inequality comparison to terminate the slice. */
        template <Direction T>
        inline bool operator!=(const Iterator<T>& other) const {
            return idx != other.idx;
        }

        /* Get the current index of the iterator within the list. */
        inline size_t index() const {
            return idx;
        }

        /* Copy constructor. */
        Iterator(const Iterator& other) : Base(other), idx(other.idx) {}

        /* Copy constructor from a different direction. */
        template <Direction T>
        Iterator(const Iterator<T>& other) : Base(other), idx(other.idx) {}

        /* Move constructor. */
        Iterator(Iterator&& other) : Base(std::move(other)), idx(other.idx) {}

        /* Move constructor from a different direction. */
        template <Direction T>
        Iterator(Iterator<T>&& other) : Base(std::move(other)), idx(other.idx) {}

    protected:
        friend IndexFactory;
        size_t idx;

        /* Construct an iterator at a given index. */
        Iterator(View& view, Node* node, size_t idx) :
            Base(view, node), idx(idx)
        {}

        /* Empty iterator. */
        Iterator(View& view, size_t idx) : Base(view), idx(idx) {}

    };

private:
    View& view;

    /* Return a unidirectional iterator at the specified index. */
    template <Direction dir, typename T>
    std::optional<Iterator<dir>> _directional(
        T index,
        bool truncate = false
    ) const {
        // normalize index
        auto opt_index = normalize(index, truncate);
        if (!opt_index.has_value()) {
            return std::nullopt;
        }

        // NOTE: we still choose the closest side to iterate from, but rather than
        // returning a bidirectional iterator, we move the result into an explicit
        // forward or reverse iterator and return that instead.

        // get iterator to index
        size_t norm_index = opt_index.value();
        if constexpr (doubly_linked) {
            if (norm_index > (view.size - (view.size > 0)) / 2) {  // backward traversal
                Iterator<Direction::backward> it(view, view.tail, view.size - 1);
                for (size_t i = view.size - 1; i > norm_index; --i) {
                    ++it;
                }
                return std::make_optional(Iterator<dir>(std::move(it)));
            }
        }

        // forward traversal
        Iterator<Direction::forward> it(view, view.head, 0);
        for (size_t i = 0; i < norm_index; ++i) {
            ++it;
        }
        return std::make_optional(Iterator<dir>(std::move(it)));
    }

};


#endif  // BERTRAND_STRUCTS_CORE_INDEX_H include guard
