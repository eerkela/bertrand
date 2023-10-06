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

        /////////////////////////////////
        ////    ITERATOR PROTOCOL    ////
        /////////////////////////////////

        /* Dereference the iterator to get the node at the current position. */
        inline Node* operator*() const noexcept {
            return _curr;
        }

        /* Prefix increment to advance the iterator to the next node in the slice. */
        inline Iterator& operator++() noexcept {
            if constexpr (dir == Direction::backward) {
                _next = _curr;
                _curr = _prev;
                if (_prev != nullptr) {
                    if constexpr (has_stack) {
                        if (this->stack.empty()) {
                            _prev = nullptr;
                        } else {
                            _prev = this->stack.top();
                            this->stack.pop();
                        }
                    } else {
                        _prev = _prev->prev();
                    }
                }
            } else {
                _prev = _curr;
                _curr = _next;
                if (_next != nullptr) {
                    _next = _next->next();
                }
            }
            return *this;
        }

        /* Inequality comparison to terminate the slice. */
        template <Direction T>
        inline bool operator!=(const Iterator<T>& other) const noexcept {
            return _curr != other._curr;
        }

        //////////////////////////////
        ////    HELPER METHODS    ////
        //////////////////////////////

        /* Get the previous node in the list. */
        inline Node* prev() const noexcept {
            return _prev;
        }

        /* Get the current node in the list. */
        inline Node* curr() const noexcept {
            return _curr;
        }

        /* Get the next node in the list. */
        inline Node* next() const noexcept {
            return _next;
        }

        /* Insert a node at the current position. */
        inline void insert(Node* node) {
            // link new node
            if constexpr (dir == Direction::backward) {
                view.link(_curr, node, _next);
            } else {
                view.link(_prev, node, _curr);
            }

            // update iterator
            if constexpr (dir == Direction::backward) {
                if constexpr (has_stack) {
                    this->stack.push(_prev);
                }
                _prev = _curr;
            } else {
                _next = _curr;
            }
            _curr = node;
        }

        /* Remove the node at the current position. */
        inline Node* remove() {
            // unlink current node
            Node* removed = _curr;
            view.unlink(_prev, _curr, _next);

            // update iterator
            if constexpr (dir == Direction::backward) {
                _curr = _prev;
                if (_prev != nullptr) {
                    if constexpr (has_stack) {
                        if (this->stack.empty()) {
                            _prev = nullptr;
                        } else {
                            _prev = this->stack.top();
                            this->stack.pop();
                        }
                    } else {
                        _prev = _prev->prev();
                    }
                }
            } else {
                _curr = _next;
                if (_next != nullptr) {
                    _next = _next->next();
                }
            }

            // return removed node
            return removed;
        }

        /* Replace the node at the current position. */
        inline void replace(Node* node) {
            // swap current node
            view.unlink(_prev, _curr, _next);
            view.link(_prev, node, _next);

            // update iterator
            view.recycle(_curr);
            _curr = node;
        }

        /* Copy constructor. */
        Iterator(const Iterator& other) :
            Base(other), view(other.view), _prev(other._prev), _curr(other._curr),
            _next(other._next)
        {}

        /* Copy constructor from a different direction. */
        template <Direction T>
        Iterator(const Iterator<T>& other) :
            Base(other), view(other.view), _prev(other._prev), _curr(other._curr),
            _next(other._next)
        {}

        /* Move constructor. */
        Iterator(Iterator&& other) :
            Base(std::move(other)), view(other.view), _prev(other._prev),
            _curr(other._curr), _next(other._next)
        {
            other._prev = nullptr;
            other._curr = nullptr;
            other._next = nullptr;
        }

        /* Move constructor from a different direction. */
        template <Direction T>
        Iterator(Iterator<T>&& other) :
            Base(std::move(other)), view(other.view), _prev(other._prev),
            _curr(other._curr), _next(other._next)
        {
            other._prev = nullptr;
            other._curr = nullptr;
            other._next = nullptr;
        }

        /* Assignment operators deleted for simplicity. */
        Iterator& operator=(const Iterator&) = delete;
        Iterator& operator=(Iterator&&) = delete;

    protected:
        friend IteratorFactory;
        View& view;
        Node* _prev;
        Node* _curr;
        Node* _next;

        ////////////////////////////
        ////    CONSTRUCTORS    ////
        ////////////////////////////

        /* Initialize an empty iterator. */
        Iterator(View& view) :
            view(view), _prev(nullptr), _curr(nullptr), _next(nullptr)
        {}

        /* Initialize an iterator around a particular linkage within the list. */
        Iterator(View& view, Node* prev, Node* curr, Node* next) :
            view(view), _prev(prev), _curr(curr), _next(next)
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
            Base(std::move(prev)), view(view), _prev(this->stack.top()), _curr(curr),
            _next(next)
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
