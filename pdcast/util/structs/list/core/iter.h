// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_ITER_H
#define BERTRAND_STRUCTS_CORE_ITER_H

#include <type_traits>  // std::enable_if_t<>
#include <Python.h>  // CPython API
#include "util.h"  // CoupledIterator<>


////////////////////////
////    FUNCTORS    ////
////////////////////////


/*
IteratorFactory is a functor (function object) that produces iterators over a linked
list, set, or dictionary.  It is used by the view classes to provide a uniform
interface for iterating over their elements, with support for both forward and
reverse iteration, as well as the simplified CoupledIterator interface.

The iterators that are returned by the factory can be subclassed in other functors to
avoid code duplication.  The implementation that is provided here is barebones, and
only keeps track of the neighboring nodes at the current position.
*/


/* A functor that produces unidirectional iterators over the templated view. */
template <typename ViewType>
class IteratorFactory {
public:
    using View = ViewType;
    using Node = typename View::Node;

    // NOTE: Reverse iterators are only compiled for doubly-linked lists.

    template <
        Direction dir = Direction::forward,
        typename = std::enable_if_t<dir == Direction::forward || has_prev<Node>::value>
    >
    class Iterator;

    template <
        Direction dir = Direction::forward,
        typename = std::enable_if_t<dir == Direction::forward || has_prev<Node>::value>
    >
    using IteratorPair = CoupledIterator<Iterator<dir>>;

    /* Return a coupled iterator for clearer access to the iterator's interface. */
    template <Direction dir = Direction::forward>
    inline IteratorPair<dir> operator()() const {
        return IteratorPair(begin<dir>(), end<dir>());
    }

    /* Return an iterator to the head/tail of a list based on the reverse parameter. */
    template <Direction dir = Direction::forward>
    inline Iterator<dir> begin() const {
        Node* origin;
        if constexpr (dir == Direction::backward) {
            origin = view.tail;
        } else {
            origin = view.head;
        }
        return Iterator<dir>(view, origin);
    }

    /* Return a null iterator to terminate the sequence. */
    template <Direction dir = Direction::forward>
    inline Iterator<dir> end() const {
        return Iterator<dir>(view);
    }

    /* An iterator that traverses a list and keeps track of each node's neighbors. */
    template <Direction dir, typename>
    class Iterator {
    public:
        using View = ViewType;
        using Node = typename View::Node;

        // iterator tags for std::iterator_traits
        using iterator_category     = std::forward_iterator_tag;
        using difference_type       = std::ptrdiff_t;
        using value_type            = Node*;
        using pointer               = Node**;
        using reference             = Node*&;

        // neighboring nodes at the current position
        Node* prev;
        Node* curr;
        Node* next;

        /////////////////////////////////
        ////    ITERATOR PROTOCOL    ////
        /////////////////////////////////

        /* Dereference the iterator to get the node at the current position. */
        inline Node* operator*() const {
            return curr;
        }

        /* Prefix increment to advance the iterator to the next node in the slice. */
        inline Iterator& operator++() {
            if constexpr (dir == Direction::backward) {
                next = curr;
                curr = prev;
                prev = static_cast<Node*>(prev->prev);
            } else {
                prev = curr;
                curr = next;
                next = static_cast<Node*>(next->next);
            }
            return *this;
        }

        /* Inequality comparison to terminate the slice. */
        template <Direction T>
        inline bool operator!=(const Iterator<T>& other) const {
            return curr != other.curr;
        }

        //////////////////////////////
        ////    HELPER METHODS    ////
        //////////////////////////////

        /* Insert a node at the current position. */
        inline void insert(Node* node) {
            if constexpr (dir == Direction::backward) {
                view.link(curr, node, next);
            } else {
                view.link(prev, node, curr);
            }
            if (PyErr_Occurred()) {
                return;  // propagate
            }

            if constexpr (dir == Direction::backward) {
                prev = curr;
            } else {
                next = curr;
            }
            curr = node;
        }

        /* Remove the node at the current position. */
        inline Node* remove() {
            Node* removed = curr;
            view.unlink(prev, curr, next);
            if constexpr (dir == Direction::backward) {
                curr = prev;
                if (prev != nullptr) {
                    prev = static_cast<Node*>(prev->prev);
                }
            } else {
                curr = next;
                if (next != nullptr) {
                    next = static_cast<Node*>(next->next);
                }
            }
            return removed;
        }

        /* Replace the node at the current position. */
        inline void replace(Node* node) {
            // remove current node
            Node* removed = curr;
            view.unlink(prev, curr, next);

            // insert new node
            view.link(prev, node, next);
            if (PyErr_Occurred()) {
                view.link(prev, removed, next);  // restore old node
                return;  // propagate
            }

            // recycle old node + update iterator
            view.recycle(removed);
            curr = node;
        }

    protected:
        friend IteratorFactory;
        View& view;

        ////////////////////////////
        ////    CONSTRUCTORS    ////
        ////////////////////////////

        /* Get an iterator to the start or end of the list. */
        Iterator(View& view, Node* node) :
            prev(nullptr), curr(node), next(nullptr), view(view)
        {
            if (curr != nullptr) {
                if constexpr (dir == Direction::backward) {
                    prev = static_cast<Node*>(curr->prev);
                } else {
                    next = static_cast<Node*>(curr->next);
                }
            }
        }

        /* Get a null iterator to the end of the list. */
        Iterator(View& view) :
            prev(nullptr), curr(nullptr), next(nullptr), view(view)
        {}

    };

private:
    friend View;
    View& view;

    IteratorFactory(View& view) : view(view) {}
};


#endif  // BERTRAND_STRUCTS_CORE_ITER_H include guard
