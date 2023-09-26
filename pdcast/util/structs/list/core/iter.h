// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_ITER_H
#define BERTRAND_STRUCTS_CORE_ITER_H

#include <stack>  // std::stack
#include <Python.h>  // CPython API
#include <type_traits>
#include "util.h"  // CoupledIterator<>


/* An IteratorFactory is a functor (function object) that produces iterators over a
 * linked list, set, or dictionary.  It is used by View classes to provide a uniform
 * interface for iterating over their elements, with support for both forward and
 * reverse iteration, as well as the simplified CoupledIterator interface.
 * 
 * The iterators that are returned by the factory can be subclassed in other functors
 * to avoid code duplication.  The implementation that is provided here is barebones,
 * and only keeps track of the neighboring nodes at the current position.
 */


/* A functor that produces unidirectional iterators over the templated view. */
template <typename ViewType, bool allow_stack = true>
class IteratorFactory {
    using View = ViewType;
    using Node = typename View::Node;

    friend View;
    View& view;

    /* Construct an iterator factory around a particular view. */
    IteratorFactory(View& view) : view(view) {}

    /* Specialization for forward iterators or reverse iterators over doubly-linked
    lists. */
    template <bool has_stack = false, typename = void>
    class BaseIterator {};

    /* Specialization for reverse iterators over singly-linked lists. */
    template <typename _>
    class BaseIterator<true, _> {
    protected:
        std::stack<Node*> stack;

        /* Constructors to initialize conditional stack. */
        BaseIterator() {}
        BaseIterator(std::stack<Node*>&& stack) : stack(std::move(stack)) {}
        BaseIterator(const BaseIterator& other) : stack(other.stack) {}
        BaseIterator(BaseIterator&& other) : stack(std::move(other.stack)) {}
    };

public:

    //////////////////////////////
    ////    ITERATOR TYPES    ////
    //////////////////////////////

    /* An iterator that traverses a list and keeps track of each node's neighbors. */
    template <
        Direction dir,
        bool has_stack = (dir == Direction::backward && !Node::doubly_linked)
    >
    class Iterator : public BaseIterator<has_stack> {
        using Base = BaseIterator<has_stack>;

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
                if (prev != nullptr) {
                    if constexpr (has_stack) {
                        if (this->stack.empty()) {
                            prev = nullptr;
                        } else {
                            prev = this->stack.top();
                            this->stack.pop();
                        }
                    } else {
                        prev = prev->prev();
                    }
                }
            } else {
                prev = curr;
                curr = next;
                if (next != nullptr) {
                    next = next->next();
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

            // update iterator
            if constexpr (dir == Direction::backward) {
                if constexpr (has_stack) {
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
            // unlink current node
            Node* removed = curr;
            view.unlink(prev, curr, next);

            // update iterator
            if constexpr (dir == Direction::backward) {
                curr = prev;
                if (prev != nullptr) {
                    if constexpr (has_stack) {
                        if (this->stack.empty()) {
                            prev = nullptr;
                        } else {
                            prev = this->stack.top();
                            this->stack.pop();
                        }
                    } else {
                        prev = prev->prev();
                    }
                }
            } else {
                curr = next;
                if (next != nullptr) {
                    next = next->next();
                }
            }

            // return removed node
            return removed;
        }

        /* Replace the node at the current position. */
        inline void replace(Node* node) {
            // swap current node
            view.unlink(prev, curr, next);
            view.link(prev, node, next);

            // update iterator
            view.recycle(curr);
            curr = node;
        }

        /* Copy constructor. */
        Iterator(const Iterator& other) :
            Base(other), prev(other.prev), curr(other.curr), next(other.next),
            view(other.view)
        {}

        /* Copy constructor from a different direction. */
        template <Direction T>
        Iterator(const Iterator<T>& other) :
            Base(other), prev(other.prev), curr(other.curr), next(other.next),
            view(other.view)
        {}

        /* Move constructor. */
        Iterator(Iterator&& other) :
            Base(std::move(other)), prev(other.prev), curr(other.curr),
            next(other.next), view(other.view)
        {
            other.prev = nullptr;
            other.curr = nullptr;
            other.next = nullptr;
        }

        /* Move constructor from a different direction. */
        template <Direction T>
        Iterator(Iterator<T>&& other) :
            Base(std::move(other)), prev(other.prev), curr(other.curr),
            next(other.next), view(other.view)
        {
            other.prev = nullptr;
            other.curr = nullptr;
            other.next = nullptr;
        }

        /* Assignment operators deleted for simplicity. */
        Iterator& operator=(const Iterator&) = delete;
        Iterator& operator=(Iterator&&) = delete;

    protected:
        friend IteratorFactory;
        View& view;

        ////////////////////////////
        ////    CONSTRUCTORS    ////
        ////////////////////////////

        /* Initialize an empty iterator. */
        Iterator(View& view) :
            prev(nullptr), curr(nullptr), next(nullptr), view(view)
        {}

        /* Initialize an iterator around a particular linkage within the list. */
        Iterator(View& view, Node* prev, Node* curr, Node* next) :
            prev(prev), curr(curr), next(next), view(view)
        {}

        /* Initialize an iterator with a stack that allows reverse iteration over a
        singly-linked list. */
        template <bool cond = has_stack>
        Iterator(
            std::enable_if_t<cond, View&> view,
            std::stack<Node*>&& prev,
            Node* curr,
            Node* next
        ) :
            Base(std::move(prev)), prev(this->stack.top()), curr(curr), next(next),
            view(view)
        {
            this->stack.pop();  // always has at least one element (nullptr)
        }

    };

    ///////////////////////
    ////    METHODS    ////
    ///////////////////////

    /* Return a coupled iterator from the head of the list. */
    inline auto operator()() const {
        return CoupledIterator<Iterator<Direction::forward>>(begin(), end());
    }

    /* Return a coupled iterator from the tail of the list. */
    inline auto reverse() const {
        return CoupledIterator<Iterator<Direction::backward>>(rbegin(), rend());
    }

    /* Return a forward iterator to the head of the list. */
    inline auto begin() const {
        // short-circuit if list is empty
        if (view.head() == nullptr) {
            return end();
        }

        Node* next = view.head()->next();
        return Iterator<Direction::forward>(view, nullptr, view.head(), next);
    }

    /* Return an empty forward iterator to terminate the sequence. */
    inline auto end() const {
        return Iterator<Direction::forward>(view);
    }

    /* Return a backward iterator to the tail of the list. */
    inline auto rbegin() const {
        // sanity check
        static_assert(
            Node::doubly_linked || allow_stack,
            "Cannot iterate over a singly-linked list in reverse without using a "
            "temporary stack."
        );

        // short-circuit if list is empty
        if (view.tail() == nullptr) {
            return rend();
        }

        // if list is doubly-linked, we can just use the prev pointer to get neighbors
        if constexpr (Node::doubly_linked) {
            Node* prev = view.tail()->prev();
            return Iterator<Direction::backward>(view, prev, view.tail(), nullptr);

        // Otherwise, we have to build a stack of prev pointers
        } else {
            std::stack<Node*> prev;
            prev.push(nullptr);
            Node* temp = view.head();
            while (temp != view.tail()) {
                prev.push(temp);
                temp = temp->next();
            }
            return Iterator<Direction::backward>(
                view, std::move(prev), view.tail(), nullptr
            );
        }
    }

    /* Return an empty backward iterator to terminate the sequence. */
    inline auto rend() const {
        return Iterator<Direction::backward>(view);
    }

};


#endif  // BERTRAND_STRUCTS_CORE_ITER_H include guard
