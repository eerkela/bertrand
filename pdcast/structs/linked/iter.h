// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_ITER_H
#define BERTRAND_STRUCTS_CORE_ITER_H

#include <stack>  // std::stack
#include <Python.h>  // CPython API
#include <type_traits>
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
protected:
    std::stack<NodeType*> stack;
    _Iterator() {}
    _Iterator(std::stack<NodeType*>&& stack) : stack(std::move(stack)) {}
    _Iterator(const _Iterator& other) : stack(other.stack) {}
    _Iterator(_Iterator&& other) : stack(std::move(other.stack)) {}
};


/* An iterator that traverses a list and keeps track of each node's neighbors. */
template <typename ViewType, Direction dir>
class Iterator :
    public _Iterator<
        typename ViewType::Node,
        dir == Direction::backward && !NodeTraits<typename ViewType::Node>::has_prev
    >
{
public:
    using View = ViewType;
    using Node = typename View::Node;
    using Value = typename Node::Value;
    static constexpr Direction direction = dir;

private:
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
        if constexpr (direction == Direction::backward) {
            view.link(_curr, node, _next);
        } else {
            view.link(_prev, node, _curr);
        }

        // update iterator
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

    /* Remove the node at the current position. */
    inline Node* drop() {
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

    /* Copy constructor from the other direction. */
    template <Direction T>
    Iterator(const Iterator<View, T>& other) :
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

    /* Move constructor from the other direction. */
    template <Direction T>
    Iterator(Iterator<View, T>&& other) :
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
    friend View;
    View& view;
    Node* _prev;
    Node* _curr;
    Node* _next;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

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
};


/* NOTE: Bidirectional<> is a type-erased iterator wrapper that can contain either a
 * forward or backward iterator over a linked list.  This allows us to write bare-metal
 * loops if the iteration direction is known at compile time, while also allowing for
 * dynamic traversal based on runtime conditions.  This is useful for implementing
 * slices, which can be iterated over in either direction depending on the step size
 * and singly- vs. doubly-linked nature of the list.
 * 
 * Bidirectional iterators have a small overhead compared to statically-typed iterators,
 * but this is minimized as much as possible through the use of tagged unions and
 * constexpr branches to eliminate conditionals.  If a list is doubly-linked, these
 * optimizations mean that we only add a single if statement on a constant boolean
 * discriminator to the body of the loop to determine the iterator direction.  If a
 * list is singly-linked, then the Bidirectional iterator is functionally equivalent to
 * a statically-typed forward iterator, and there are no unnecessary branches at all.
 */


/* Conditionally-compiled base class for Bidirectional iterators that respects the
reversability of the associated view. */
template <template <Direction> class Iterator, bool doubly_linked = false>
class _Bidirectional {
public:
    Direction direction = Direction::forward;

protected:
    union {
        Iterator<Direction::forward> forward;
    };

    /* Initialize the union with a forward iterator. */
    _Bidirectional(const Iterator<Direction::forward>& iter) {
        new (&forward) Iterator<Direction::forward>(iter);
    }

    /* Copy constructor. */
    _Bidirectional(const _Bidirectional& other) : direction(other.direction) {
        new (&forward) Iterator<Direction::forward>(other.forward);
    }

    /* Move constructor. */
    _Bidirectional(_Bidirectional&& other) noexcept : direction(other.direction) {
        new (&forward) Iterator<Direction::forward>(std::move(other.forward));
    }

    /* Copy assignment operator. */
    _Bidirectional& operator=(const _Bidirectional& other) {
        direction = other.direction;
        forward = other.forward;
        return *this;
    }

    /* Move assignment operator. */
    _Bidirectional& operator=(_Bidirectional&& other) {
        direction = other.direction;
        forward = std::move(other.forward);
        return *this;
    }

    /* Call the contained type's destructor. */
    ~_Bidirectional() { forward.~Iterator(); }
};


/* Specialization for doubly-linked lists. */
template <template <Direction> class Iterator>
class _Bidirectional<Iterator, true> {
public:
    Direction direction;

protected:
    union {
        Iterator<Direction::forward> forward;
        Iterator<Direction::backward> backward;
    };

    /* Initialize the union with a forward iterator. */
    _Bidirectional(const Iterator<Direction::forward>& iter) :
        direction(Direction::forward)
    {
        new (&forward) Iterator<Direction::forward>(iter);
    }

    /* Initialize the union with a backward iterator. */
    _Bidirectional(const Iterator<Direction::backward>& iter) :
        direction(Direction::backward)
    {
        new (&backward) Iterator<Direction::backward>(iter);
    }

    /* Copy constructor. */
    _Bidirectional(const _Bidirectional& other) : direction(other.direction) {
        switch (other.direction) {
            case Direction::backward:
                new (&backward) Iterator<Direction::backward>(other.backward);
                break;
            case Direction::forward:
                new (&forward) Iterator<Direction::forward>(other.forward);
                break;
        }
    }

    /* Move constructor. */
    _Bidirectional(_Bidirectional&& other) noexcept : direction(other.direction) {
        switch (other.direction) {
            case Direction::backward:
                new (&backward) Iterator<Direction::backward>(std::move(other.backward));
                break;
            case Direction::forward:
                new (&forward) Iterator<Direction::forward>(std::move(other.forward));
                break;
        }
    }

    /* Copy assignment operator. */
    _Bidirectional& operator=(const _Bidirectional& other) {
        direction = other.direction;
        switch (other.direction) {
            case Direction::backward:
                backward = other.backward;
                break;
            case Direction::forward:
                forward = other.forward;
                break;
        }
        return *this;
    }

    /* Move assignment operator. */
    _Bidirectional& operator=(_Bidirectional&& other) {
        direction = other.direction;
        switch (other.direction) {
            case Direction::backward:
                backward = std::move(other.backward);
                break;
            case Direction::forward:
                forward = std::move(other.forward);
                break;
        }
        return *this;
    }

    /* Call the contained type's destructor. */
    ~_Bidirectional() {
        switch (direction) {
            case Direction::backward:
                backward.~Iterator();
                break;
            case Direction::forward:
                forward.~Iterator();
                break;
        }
    }
};


/* A type-erased iterator that can contain either a forward or backward iterator. */
template <template <Direction> class Iterator>
class Bidirectional : public _Bidirectional<
    Iterator,
    Iterator<Direction::forward>::Node::doubly_linked
> {
public:
    using ForwardIterator = Iterator<Direction::forward>;
    using Node = typename ForwardIterator::Node;
    using Base = _Bidirectional<Iterator, Node::doubly_linked>;

    // iterator tags for std::iterator_traits
    using iterator_category     = typename ForwardIterator::iterator_category;
    using difference_type       = typename ForwardIterator::difference_type;
    using value_type            = typename ForwardIterator::value_type;
    using pointer               = typename ForwardIterator::pointer;
    using reference             = typename ForwardIterator::reference;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Initialize the union using an existing iterator. */
    template <Direction dir>
    explicit Bidirectional(const Iterator<dir>& it) : Base(it) {}

    /* Copy constructor. */
    Bidirectional(const Bidirectional& other) : Base(other) {}

    /* Move constructor. */
    Bidirectional(Bidirectional&& other) noexcept : Base(std::move(other)) {}

    // destructor is automatically called by base class

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* Dereference the iterator to get the node at the current position. */
    inline value_type operator*() const {
        /*
         * HACK: we rely on a special property of the templated iterators: that both
         * forward and backward iterators use the same implementation of the
         * dereference operator.  This, coupled with the fact that unions occupy the
         * same space in memory, means that we can safely dereference the iterators
         * using only the forward operator, even when the data we access is taken from
         * the backward iterator.  This avoids the need for any extra branches.
         *
         * If this specific implementation detail ever changes, then this hack should
         * be reconsidered in favor of a solution more like the one below.
         */
        return *(this->forward);

        /* 
         * if constexpr (Node::doubly_linked) {
         *     if (this->direction == Direction::backward) {
         *         return *(this->backward);
         *     }
         * }
         * return *(this->forward);
         */
    }

    /* Prefix increment to advance the iterator to the next node in the slice. */
    inline Bidirectional& operator++() {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                ++(this->backward);
                return *this;
            }
        }
        ++(this->forward);
        return *this;
    }

    /* Inequality comparison to terminate the slice. */
    inline bool operator!=(const Bidirectional& other) const {
        /*
         * HACK: We rely on a special property of the templated iterators: that both
         * forward and backward iterators can be safely compared using the same operator
         * implementation between them.  This, coupled with the fact that unions occupy
         * the same space in memory, means that we can directly compare the iterators
         * without any extra branches, regardless of which type is currently active.
         *
         * In practice, what this solution does is always use the forward-to-forward
         * comparison operator, but using data from the backward iterator if it is
         * currently active.  This is safe becase the backward-to-backward comparison
         * is exactly the same as the forward-to-forward comparison, as are all the
         * other possible combinations.  In other words, the directionality of the
         * iterator is irrelevant to the comparison.
         *
         * If these specific implementation details ever change, then this hack should
         * be reconsidered in favor of a solution more like the one below.
         */
        return this->forward != other.forward;

        /*
         * using OtherNode = typename std::decay_t<decltype(other)>::Node;
         *
         * if constexpr (Node::doubly_linked) {
         *     if (this->direction == Direction::backward) {
         *         if constexpr (OtherNode::doubly_linked) {
         *             if (other.direction == Direction::backward) {
         *                 return this->backward != other.backward;
         *             }
         *         }
         *         return this->backward != other.forward;
         *     }
         * }
         *
         * if constexpr (OtherNode::doubly_linked) {
         *     if (other.direction == Direction::backward) {
         *         return this->forward != other.backward;
         *     }
         * }
         * return this->forward != other.forward;
         */
    }

    ///////////////////////////////////
    ////    CONDITIONAL METHODS    ////
    ///////////////////////////////////

    // NOTE: this uses SFINAE to detect the presence of these methods on the template
    // Iterator.  If the Iterator does not implement the named method, then it will not
    // be compiled, and users will get compile-time errors if they try to access it.
    // This avoids the need to manually extend the Bidirectional interface to match
    // that of the Iterator.  See https://en.cppreference.com/w/cpp/language/sfinae
    // for more information.

    template <typename T = ForwardIterator>
    inline auto prev() const -> decltype(std::declval<T>().prev()) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.prev();
            }
        }
        return this->forward.prev();
    }

    template <typename T = ForwardIterator>
    inline auto curr() const -> decltype(std::declval<T>().curr()) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.curr();
            }
        }
        return this->forward.curr();
    }

    template <typename T = ForwardIterator>
    inline auto next() const -> decltype(std::declval<T>().next()) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.next();
            }
        }
        return this->forward.next();
    }

    /* Insert a node at the current position. */
    template <typename T = ForwardIterator>
    inline auto insert(value_type value) -> decltype(std::declval<T>().insert(value)) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.insert(value);
            }
        }
        return this->forward.insert(value);
    }

    /* Remove the node at the current position. */
    template <typename T = ForwardIterator>
    inline auto remove() -> decltype(std::declval<T>().remove()) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.remove();
            }
        }
        return this->forward.remove();
    }

    /* Replace the node at the current position. */
    template <typename T = ForwardIterator>
    inline auto replace(value_type value) -> decltype(std::declval<T>().replace(value)) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.replace(value);
            }
        }
        return this->forward.replace(value);
    }

    /* Get the index of the current position. */
    template <typename T = ForwardIterator>
    inline auto index() const -> decltype(std::declval<T>().index()) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.index();
            }
        }
        return this->forward.index();
    }

    /* Check whether the iterator direction is consistent with a slice's step size. */
    template <typename T = ForwardIterator>
    inline auto idx() const -> decltype(std::declval<T>().idx()) {
        if constexpr (Node::doubly_linked) {
            if (this->direction == Direction::backward) {
                return this->backward.idx();
            }
        }
        return this->forward.idx();
    }

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_CORE_ITER_H include guard
