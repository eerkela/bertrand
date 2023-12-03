// include guard: BERTRAND_STRUCTS_LINKED_CORE_ITER_H
#ifndef BERTRAND_STRUCTS_LINKED_CORE_ITER_H
#define BERTRAND_STRUCTS_LINKED_CORE_ITER_H

#include <stack>  // std::stack
#include <Python.h>  // CPython API
#include <type_traits>  // std::conditional_t<>
#include "node.h"  // NodeTraits


namespace bertrand {
namespace structs {
namespace linked {


/* enum to make iterator direction hints more readable. */
enum class Direction {
    forward,
    backward
};


/* Base class for forward iterators and reverse iterators over doubly-linked lists. */
template <typename NodeType, bool has_stack = false>
class _Iterator {};


/* Base class for reverse iterators over singly-linked lists.

NOTE: these use an auxiliary stack to allow reverse iteration even when no `prev`
pointer is available.  This requires an additional iteration over the list to populate
the stack, but this is only applied in the special case of a reverse iterator over a
singly-linked list.  The upside is that we can use the same interface for both singly-
and doubly-linked lists. */
template <typename NodeType>
class _Iterator<NodeType, true> {
    static constexpr bool constant = std::is_const_v<NodeType>;
    using Node = std::conditional_t<constant, const NodeType, NodeType>;

protected:
    std::stack<Node*> stack;
    _Iterator() {}
    _Iterator(std::stack<Node*>&& stack) : stack(std::move(stack)) {}
    _Iterator(const _Iterator& other) : stack(other.stack) {}
    _Iterator(_Iterator&& other) : stack(std::move(other.stack)) {}
};


/* An optionally const iterator over the templated view, with the given direction. */
template <typename ViewType, Direction dir>
class Iterator :
    public _Iterator<
        std::conditional_t<
            std::is_const_v<ViewType>,
            const typename ViewType::Node,
            typename ViewType::Node
        >,
        dir == Direction::backward && !NodeTraits<typename ViewType::Node>::has_prev
    >
{
    static constexpr bool constant = std::is_const_v<ViewType>;

public:
    using View = ViewType;
    using Node = std::conditional_t<constant, const typename View::Node, typename View::Node>;
    using Value = std::conditional_t<constant, const typename Node::Value, typename Node::Value>;
    static constexpr Direction direction = dir;

protected:
    /* Conditional compilation of reversal stack for singly-linked lists. */
    static constexpr bool has_stack = (
        direction == Direction::backward && !NodeTraits<Node>::has_prev
    );
    using Base = _Iterator<Node, has_stack>;

public:

    // iterator tags for std::iterator_traits
    using iterator_category     = std::forward_iterator_tag;
    using difference_type       = std::ptrdiff_t;
    using value_type            = std::remove_reference_t<Value>;
    using pointer               = value_type*;
    using reference             = value_type&;

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* Dereference the iterator to get the value at the current position. */
    inline Value operator*() const noexcept {
        return _curr->value();
    }

    /* Prefix increment to advance the iterator to the next node in the view. */
    inline Iterator& operator++() noexcept {
        if constexpr (direction == Direction::backward) {
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

    /* Inequality comparison to terminate the sequence. */
    template <Direction T>
    inline bool operator!=(const Iterator<View, T>& other) const noexcept {
        return _curr != other.curr();
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

    /* NOTE: View iterators also provide the following convenience methods for
     * efficiently changing the state of the list.  These are not part of the standard
     * iterator protocol, but they are useful for implementing algorithms that modify
     * the list while iterating over it, such as index-based insertions, removals, etc.
     * Here's a basic diagram showing how they work.  The current iterator position is
     * denoted by the caret (^):
     *
     * original:    a -> b -> c
     *                   ^
     * insert(d):   a -> d -> b -> c
     *                   ^
     * drop():      a -> b -> c             (returns dropped node)
     *                   ^
     * replace(e):  a -> e -> c             (returns replaced node)
     *                   ^
     *
     * NOTE: these methods are only available on mutable iterators, and care must be
     * taken to ensure that the iterator is not invalidated by the operation.  This can
     * happen if the operation triggers a reallocation of the underlying data (via the
     * allocator's resize() method).  In this case, the Node pointers stored in the
     * iterator will no longer be valid, and accessing them can lead to undefined
     * behavior.
     */

    /* Insert a node in between the previous and current nodes, implicitly rewinding
    the iterator. */
    template <bool cond = !constant>
    inline std::enable_if_t<cond, void> insert(Node* node) {
        // link new node
        if constexpr (direction == Direction::backward) {
            view.link(_curr, node, _next);
        } else {
            view.link(_prev, node, _curr);
        }

        // move current node to subsequent node, new node replaces curr
        if constexpr (direction == Direction::backward) {
            if constexpr (has_stack) {
                this->stack.push(_prev);
            }
            _prev = _curr;
        } else {
            _next = _curr;
        }
        _curr = node;
    }

    /* Remove the node at the current position, implicitly advancing the iterator. */
    template <bool cond = !constant>
    inline std::enable_if_t<cond, Node*> drop() {
        // unlink current node
        Node* removed = _curr;
        view.unlink(_prev, _curr, _next);

        // move subsequent node to current node, then get following node
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

    /* Replace the node at the current position without advancing the iterator. */
    template <bool cond = !constant>
    inline std::enable_if_t<cond, Node*> replace(Node* node) {
        // swap current node
        view.unlink(_prev, _curr, _next);
        view.link(_prev, node, _next);

        // update iterator
        Node* result = _curr;
        _curr = node;
        return result;
    }

    /* Create an empty iterator. */
    Iterator(View& view) :
        view(view), _prev(nullptr), _curr(nullptr), _next(nullptr)
    {}

    /* Create an iterator around a particular linkage within the view. */
    Iterator(View& view, Node* prev, Node* curr, Node* next) :
        view(view), _prev(prev), _curr(curr), _next(next)
    {}

    /* Create a reverse iterator over a singly-linked view. */
    template <bool cond = has_stack, std::enable_if_t<cond, int> = 0>
    Iterator(View& view, std::stack<Node*>&& prev, Node* curr, Node* next) :
        Base(std::move(prev)), view(view), _prev(this->stack.top()), _curr(curr),
        _next(next)
    {
        this->stack.pop();  // always has at least one element (nullptr)
    }

    /* Copy constructor. */
    Iterator(const Iterator& other) :
        Base(other), view(other.view), _prev(other._prev), _curr(other._curr),
        _next(other._next)
    {}

    // TODO: converting constructors need to take the temporary stack into account.  When
    // creating a backward iterator from a forward iterator, we need to generate a copy
    // of the forward iterator, exhaust it, and add each node to the stack in reverse
    // order.

    // /* Copy constructor from the other direction. */
    // template <Direction T>
    // Iterator(const Iterator<View, T>& other) :
    //     Base(other), view(other.view), _prev(other._prev), _curr(other._curr),
    //     _next(other._next)
    // {}

    /* Move constructor. */
    Iterator(Iterator&& other) :
        Base(std::move(other)), view(other.view), _prev(other._prev),
        _curr(other._curr), _next(other._next)
    {
        other._prev = nullptr;
        other._curr = nullptr;
        other._next = nullptr;
    }

    // /* Move constructor from the other direction. */
    // template <Direction T>
    // Iterator(Iterator<View, T>&& other) :
    //     Base(std::move(other)), view(other.view), _prev(other._prev),
    //     _curr(other._curr), _next(other._next)
    // {
    //     other._prev = nullptr;
    //     other._curr = nullptr;
    //     other._next = nullptr;
    // }

    /* Assignment operators deleted for simplicity. */
    Iterator& operator=(const Iterator&) = delete;
    Iterator& operator=(Iterator&&) = delete;

protected:
    View& view;
    Node* _prev;
    Node* _curr;
    Node* _next;
};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_CORE_ITER_H
