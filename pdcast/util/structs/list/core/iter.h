// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_ITER_H
#define BERTRAND_STRUCTS_CORE_ITER_H

#include <stack>  // std::stack
#include <Python.h>  // CPython API
#include "util.h"  // CoupledIterator<>


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
template <typename ViewType, bool allow_stack = true>
class IteratorFactory {
public:
    using View = ViewType;
    using Node = typename View::Node;
    inline static constexpr bool doubly_linked = has_prev<Node>::value;

    template <Direction dir>
    class Iterator;

    template <Direction dir>
    using IteratorPair = CoupledIterator<Iterator<dir>>;

    /////////////////////////
    ////    ITERATORS    ////
    /////////////////////////

    // NOTE: reverse iterators may not be supported if the list is singly-linked and
    // allow_stack is set to false

    /* Return a coupled iterator from the head of the list. */
    inline auto operator()() const {
        return IteratorPair<Direction::forward>(begin(), end());
    }

    /* Return a coupled iterator from the tail of the list. */
    inline auto reverse() const {
        static_assert(
            doubly_linked || allow_stack,
            "Cannot iterate over a singly-linked list in reverse without a temporary "
            "stack."
        );
        return IteratorPair<Direction::backward>(rbegin(), rend());
    }

    /* Return a forward iterator to the head of the list. */
    inline auto begin() const {
        return Iterator<Direction::forward>(view, view.head);
    }

    /* Return an empty forward iterator to terminate the sequence. */
    inline auto end() const {
        return Iterator<Direction::forward>(view);
    }

    /* Return a backward iterator to the tail of the list. */
    inline auto rbegin() const {
        static_assert(
            doubly_linked || allow_stack,
            "Cannot iterate over a singly-linked list in reverse without a temporary "
            "stack."
        );
        return Iterator<Direction::backward>(view, view.tail);
    }

    /* Return an empty backward iterator to terminate the sequence. */
    inline auto rend() const {
        static_assert(
            doubly_linked || allow_stack,
            "Cannot iterate over a singly-linked list in reverse without a temporary "
            "stack."
        );
        return Iterator<Direction::backward>(view);
    }

    /////////////////////////////
    ////    INNER CLASSES    ////
    /////////////////////////////

    /* Specialization for forward iterators or reverse iterators over doubly-linked
    lists. */
    template <bool stack_reverse = false, typename = void>
    class BaseIterator {};

    /* Specialization for reverse iterators over singly-linked lists. */
    template <typename _>
    class BaseIterator<true, _> {
    protected:
        std::stack<Node*> stack;
    };

    /* An iterator that traverses a list and keeps track of each node's neighbors. */
    template <Direction dir>
    class Iterator : public BaseIterator<dir == Direction::backward && !doubly_linked> {
    public:
        using View = ViewType;
        using Node = typename View::Node;
        inline static bool constexpr stack_reverse = (
            dir == Direction::backward && !doubly_linked
        );

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
                if (prev != nullptr) {
                    if constexpr (stack_reverse) {
                        if (this->stack.empty()) {
                            prev = nullptr;
                        } else {
                            prev = this->stack.top();
                            this->stack.pop();
                        }
                    } else {
                        prev = static_cast<Node*>(prev->prev);
                    }
                }
            } else {
                prev = curr;
                curr = next;
                if (next != nullptr) {
                    next = static_cast<Node*>(next->next);
                }
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
            // link new node
            if constexpr (dir == Direction::backward) {
                view.link(curr, node, next);
            } else {
                view.link(prev, node, curr);
            }
            if (PyErr_Occurred()) {
                return;  // propagate
            }

            // update iterator parameters
            if constexpr (dir == Direction::backward) {
                if constexpr (stack_reverse) {
                    this->stack.push(prev);
                }
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
                    if constexpr (stack_reverse) {
                        if (this->stack.empty()) {
                            prev = nullptr;
                        } else {
                            prev = this->stack.top();
                            this->stack.pop();
                        }
                    } else {
                        prev = static_cast<Node*>(prev->prev);
                    }
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

        /* Copy constructor. */
        Iterator(const Iterator& other) :
            prev(other.prev), curr(other.curr), next(other.next), view(other.view)
        {}

        /* Copy constructor from a different direction. */
        template <Direction T>
        Iterator(const Iterator<T>& other) :
            prev(other.prev), curr(other.curr), next(other.next), view(other.view)
        {}

        /* Move constructor. */
        Iterator(Iterator&& other) :
            prev(other.prev), curr(other.curr), next(other.next), view(other.view)
        {
            other.prev = nullptr;
            other.curr = nullptr;
            other.next = nullptr;
        }

        /* Move constructor from a different direction. */
        template <Direction T>
        Iterator(Iterator<T>&& other) :
            prev(other.prev), curr(other.curr), next(other.next), view(other.view)
        {
            other.prev = nullptr;
            other.curr = nullptr;
            other.next = nullptr;
        }

        /* Assignment operators deleted for consistency with Bidirectional. */
        Iterator& operator=(const Iterator&) = delete;
        Iterator& operator=(Iterator&&) = delete;

    protected:
        friend IteratorFactory;
        View& view;

        ////////////////////////////
        ////    CONSTRUCTORS    ////
        ////////////////////////////

        /* Construct an iterator to the start or end of the list. */
        Iterator(View& view, Node* node) :
            prev(nullptr), curr(node), next(nullptr), view(view)
        {
            if (curr != nullptr) {
                if constexpr (dir == Direction::backward) {
                    if constexpr (stack_reverse) {
                        Node* temp = view.head;
                        while (temp != node) {
                            this->stack.push(temp);
                            temp = static_cast<Node*>(temp->next);
                        }
                        prev = this->stack.top();
                        this->stack.pop();
                    } else {
                        prev = static_cast<Node*>(curr->prev);
                    }
                } else {
                    next = static_cast<Node*>(curr->next);
                }
            }
        }

        /* Empty iterator. */
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
