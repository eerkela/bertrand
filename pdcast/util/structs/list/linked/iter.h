// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_ITER_H
#define BERTRAND_STRUCTS_CORE_ITER_H

#include <stack>  // std::stack
#include <Python.h>  // CPython API
#include <type_traits>
#include "../util/coupled_iter.h"  // CoupledIterator<>


namespace bertrand {
namespace structs {
namespace linked {


/* enum to make iterator direction hints more readable. */
enum class Direction {
    forward,
    backward
};


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
        return util::CoupledIterator<Iterator<Direction::forward>>(begin(), end());
    }

    /* Return a coupled iterator from the tail of the list. */
    inline auto reverse() const {
        return util::CoupledIterator<Iterator<Direction::backward>>(rbegin(), rend());
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
