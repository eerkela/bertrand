#ifndef BERTRAND_STRUCTS_LINKED_CORE_VIEW_H
#define BERTRAND_STRUCTS_LINKED_CORE_VIEW_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <stack>  // std::stack
#include <type_traits>  // std::conditional_t<>
#include <Python.h>  // CPython API
#include "node.h"  // Hashed<>, Mapped<>
#include "allocate.h"  // allocators
#include "../../util/container.h"  // PyDict
#include "../../util/ops.h"  // len()


namespace bertrand {
namespace linked {


/////////////////////////
////    ITERATORS    ////
/////////////////////////


/* Enum to make iterator direction hints more readable. */
enum class Direction {
    FORWARD,
    BACKWARD
};


/* Enum holding iterator customizations for dictlike views.  */
enum class Yield {
    KEY,
    VALUE,
    ITEM
};


/* Base class for forward iterators and reverse iterators over doubly-linked lists. */
template <typename NodeType, bool stack = false>
struct BaseIterator {
    static constexpr bool HAS_STACK = false;
};


/* Base class for reverse iterators over singly-linked lists.  NOTE: these use an
auxiliary stack to allow reverse iteration even when no `prev` pointer is available.
This requires an additional iteration over the list to populate the stack, but is only
applied in the special case of a reverse iterator over a singly-linked list.  The
upside is that we can use the same interface for both singly- and doubly-linked
lists. */
template <typename NodeType>
class BaseIterator<NodeType, true> {
    using Node = std::conditional_t<
        std::is_const_v<NodeType>,
        const NodeType,
        NodeType
    >;

protected:
    std::stack<Node*> stack;
    BaseIterator() {}
    BaseIterator(std::stack<Node*>&& stack) : stack(std::move(stack)) {}
    BaseIterator(const BaseIterator& other) : stack(other.stack) {}
    BaseIterator(BaseIterator&& other) : stack(std::move(other.stack)) {}

public:
    static constexpr bool HAS_STACK = true;

};


/* An iterator over the templated view, with a customizable direction and dereference
type.

Iterator directions are specified using the Direction:: enum listed above.  Note that
backward iteration over a singly-linked view requires the creation of a temporary
stack, which is handled implicitly by the iterator.

The Yield:: enum allows users to customize the type of value that is returned by the
dereference operator.  This is only applicable for dictlike views, and defaults to
Yield::KEY in all other cases.  If set to Yield::VALUE or Yield::ITEM, the iterator
will return the mapped value or a (key, value) pair, respectively.  These are used to
implement the LinkedDict.keys(), LinkedDict.values(), and LinkedDict.items() methods
in a simple and efficient manner.

If the iterator is templated on a const view, then it is suitable for the const
overloads of `begin()` and `end()`, as well as the corresponding `cbegin()` and
`cend()` factory methods. */
template <typename View, Direction dir, Yield yield>
class Iterator :
    public BaseIterator<
        std::conditional_t<
            std::is_const_v<View>,
            const typename View::Node,
            typename View::Node
        >,
        dir == Direction::BACKWARD && !NodeTraits<typename View::Node>::has_prev
    >
{
    using Node = std::conditional_t<
        std::is_const_v<View>,
        const typename View::Node,
        typename View::Node
    >;
    using Base = BaseIterator<
        Node, dir == Direction::BACKWARD && !NodeTraits<Node>::has_prev
    >;

    static_assert(
        yield == Yield::KEY || NodeTraits<Node>::has_mapped,
        "Yield::VALUE and Yield::ITEM can only be used on dictlike views."
    );

    /* Infer dereference type based on `yield` parameter. */
    template <Yield Y = Yield::KEY, typename Dummy = void>
    struct DerefType {
        using type = typename Node::Value;
    };
    template <typename Dummy>
    struct DerefType<Yield::VALUE, Dummy> {
        using type = typename Node::MappedValue;
    };
    template <typename Dummy>
    struct DerefType<Yield::ITEM, Dummy> {
        using type = std::pair<typename Node::Value, typename Node::MappedValue>;
    };

public:
    static constexpr bool CONSTANT          = std::is_const_v<View>;
    static constexpr Direction DIRECTION    = dir;
    static constexpr bool HAS_STACK         = Base::HAS_STACK;
    static constexpr Yield YIELD            = yield;

    using Deref = std::conditional_t<
        CONSTANT,
        const typename DerefType<YIELD>::type,
        typename DerefType<YIELD>::type
    >;

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    using iterator_category     = std::forward_iterator_tag;
    using difference_type       = std::ptrdiff_t;
    using value_type            = std::remove_reference_t<Deref>;
    using pointer               = value_type*;
    using reference             = value_type&;

    /* Dereference the iterator to get the value at the current position. */
    inline Deref operator*() const noexcept {
        if constexpr (YIELD == Yield::KEY) {
            return _curr->value();
        } else if constexpr (YIELD == Yield::VALUE) {
            return _curr->mapped();
        } else {
            return std::make_pair(_curr->value(), _curr->mapped());
        }
    }

    /* Prefix increment to advance the iterator to the next node in the view. */
    inline Iterator& operator++() noexcept {
        if constexpr (DIRECTION == Direction::BACKWARD) {
            _next = _curr;
            _curr = _prev;
            if (_prev != nullptr) {
                if constexpr (HAS_STACK) {
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
    template <Direction T, Yield Y>
    inline bool operator!=(const Iterator<View, T, Y>& other) const noexcept {
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

    /* NOTE: View iterators also provide the following convenience methods for mutating
     * the list.  These are not part of the standard iterator protocol, but are useful
     * for implementing algorithms that need to modify the list while iterating over
     * it, such as index-based insertions, removals, etc.  Here's a basic diagram
     * showing how they work.  The current position is denoted by the caret (^):
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
     * NOTE: these methods are only available for iterators over mutable containers,
     * and care must be taken to ensure that the iterator is not invalidated by the
     * operation.  This can happen if the operation triggers a reallocation of the
     * underlying data, such as a dynamic growth or shrink.  In this case, the Node
     * pointers stored in the iterator will no longer be valid, and accessing them can
     * lead to undefined behavior.
     */

    /* Insert a node between the previous and current nodes, implicitly rewinding the
    iterator. */
    template <bool cond = !CONSTANT>
    inline auto insert(Node* node) -> std::enable_if_t<cond, void> {
        if constexpr (DIRECTION == Direction::BACKWARD) {
            view.link(_curr, node, _next);
        } else {
            view.link(_prev, node, _curr);
        }

        // move current node to subsequent node, new node replaces curr
        if constexpr (DIRECTION == Direction::BACKWARD) {
            if constexpr (HAS_STACK) {
                this->stack.push(_prev);
            }
            _prev = _curr;
        } else {
            _next = _curr;
        }
        _curr = node;
    }

    /* Remove the node at the current position, implicitly advancing the iterator. */
    template <bool cond = !CONSTANT>
    inline auto drop() -> std::enable_if_t<cond, Node*> {
        Node* removed = _curr;
        view.unlink(_prev, _curr, _next);

        // move subsequent node to current node, then get following node
        if constexpr (DIRECTION == Direction::BACKWARD) {
            _curr = _prev;
            if (_prev != nullptr) {
                if constexpr (HAS_STACK) {
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

        return removed;
    }

    /* Replace the node at the current position without advancing the iterator. */
    template <bool cond = !CONSTANT>
    inline auto replace(Node* node) -> std::enable_if_t<cond, Node*> {
        view.unlink(_prev, _curr, _next);
        view.link(_prev, node, _next);
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
    template <bool cond = HAS_STACK, std::enable_if_t<cond, int> = 0>
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

    /* Move constructor. */
    Iterator(Iterator&& other) :
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
    View& view;
    Node* _prev;
    Node* _curr;
    Node* _next;
};


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a low-level view for a linked data structure.  This class is
inherited by all views, and can be used for easy SFINAE checks via std::is_base_of,
without requiring any foreknowledge of template parameters. */
struct ViewTag {
    static constexpr bool listlike = false;
    static constexpr bool setlike = false;
    static constexpr bool dictlike = false;
};


/* Base class representing the low-level core of a linked data structure.

Views are essentially lightweight wrappers around a raw memory allocator that expose a
high-level interface for manipulating the state of the data structure.  They are always
passed as a generic first argument to the non-member methods defined in the algorithms/
directory, which can accept any kind of view as long as it is compatible with the
algorithm in question.  These non-member methods are then packaged into the data
structure's final public interface as normal member methods.  This allows us to mix and
match the algorithms that are used simply by including the appropriate headers, without
regard to the underlying implementation details.

Each allocator must at minimum conform to the interface found in the BaseAllocator
class template in allocate.h.  They may add additional functionality as specified in
the extensions to BaseView listed below. */
template <typename Derived, typename AllocatorType>
class BaseView : public ViewTag {
public:
    using Allocator = AllocatorType;
    using Node = typename Allocator::Node;
    using Value = typename Node::Value;
    using MemGuard = typename Allocator::MemGuard;

    template <Direction dir, Yield yield = Yield::KEY>
    using Iterator = linked::Iterator<Derived, dir, yield>;
    template <Direction dir, Yield yield = Yield::KEY>
    using ConstIterator = linked::Iterator<const Derived, dir, yield>;

    static constexpr unsigned int FLAGS = Allocator::FLAGS;
    static constexpr bool SINGLY_LINKED = Allocator::SINGLY_LINKED;
    static constexpr bool DOUBLY_LINKED = Allocator::DOUBLY_LINKED;
    static constexpr bool XOR = Allocator::XOR;
    static constexpr bool DYNAMIC = Allocator::DYNAMIC;
    static constexpr bool FIXED_SIZE = Allocator::FIXED_SIZE;
    static constexpr bool PACKED = Allocator::PACKED;
    static constexpr bool STRICTLY_TYPED = Allocator::STRICTLY_TYPED;

    // low-level node allocation/deallocation
    mutable Allocator allocator;  // use at your own risk!

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty view. */
    BaseView(std::optional<size_t> capacity, PyObject* spec) :
        allocator(capacity, spec == Py_None ? nullptr : spec)
    {}

    /* Construct a view from an input container. */
    template <typename Container>
    BaseView(
        Container&& iterable,
        std::optional<size_t> capacity,
        PyObject* spec,
        bool reverse
    ) : allocator(init_size(iterable, capacity), spec == Py_None ? nullptr : spec)
    {
        if (reverse) {
            for (const auto& item : iter(iterable)) {
                link(nullptr, node(item), head());
            }
        } else {
            for (const auto& item : iter(iterable)) {
                link(tail(), node(item), nullptr);
            }
        }
    }

    /* Construct a view from an iterator range. */
    template <typename Iterator>
    BaseView(
        Iterator&& begin,
        Iterator&& end,
        std::optional<size_t> capacity,
        PyObject* spec,
        bool reverse
    ) : allocator(capacity, spec == Py_None ? nullptr : spec)
    {
        if (reverse) {
            while (begin != end) {
                link(nullptr, node(*begin), head());
                ++begin;
            }
        } else {
            while (begin != end) {
                link(tail(), node(*begin), nullptr);
                ++begin;
            }
        }
    }

    /* Copy constructor. */
    BaseView(const BaseView& other) : allocator(other.allocator) {}

    /* Move constructor. */
    BaseView(BaseView&& other) noexcept : allocator(std::move(other.allocator)) {}

    /* Copy assignment operator. */
    BaseView& operator=(const BaseView& other) {
        if (this == &other) {
            return *this;
        }
        allocator = other.allocator;
        return *this;
    }

    /* Move assignment operator. */
    BaseView& operator=(BaseView&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        allocator = std::move(other.allocator);
        return *this;
    }

    ///////////////////////////////
    ////    NODE MANAGEMENT    ////
    ///////////////////////////////

    /* Get the node at the head of the list. */
    inline Node* head() noexcept {
        return allocator.head;
    }

    /* Get the node at the head of a const list. */
    inline const Node* head() const noexcept {
        return allocator.head;
    }

    /* Set the head of the list to another node. */
    inline void head(Node* node) {
        allocator.head = node;
    }

    /* Get the node at the tail of the list. */
    inline Node* tail() noexcept {
        return allocator.tail;
    }

    /* Get the node at the tail of a const list. */
    inline const Node* tail() const noexcept {
        return allocator.tail;
    }

    /* Set the tail of the list to another node. */
    inline void tail(Node* node) {
        allocator.tail = node;
    }

    /* Construct a new node for the list. */
    template <typename... Args>
    inline Node* node(Args&&... args) const {
        return allocator.create(std::forward<Args>(args)...);
    }

    /* Release a node, returning it to the allocator. */
    inline void recycle(Node* node) const {
        allocator.recycle(node);
    }

    /* Make a shallow copy of the entire list. */
    inline Derived copy() const {
        Derived result(capacity(), specialization());
        for (auto it = this->begin(), end = this->end(); it != end; ++it) {
            Node* copied = result.node(*it.curr());
            result.link(result.tail(), copied, nullptr);
        }
        return result;
    }

    /* Remove all elements from a list. */
    inline void clear() noexcept {
        allocator.clear();
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(Node* prev, Node* curr, Node* next) {
        Node::link(prev, curr, next);  // as defined by Node
        if (prev == nullptr) {
            allocator.head = curr;
        }
        if (next == nullptr) {
            allocator.tail = curr;
        }
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        Node::unlink(prev, curr, next);  // as defined by Node
        if (prev == nullptr) {
            allocator.head = next;
        }
        if (next == nullptr) {
            allocator.tail = prev;
        }
    }

    /* Get the current size of the list. */
    inline size_t size() const noexcept {
        return allocator.occupied;
    }

    /* Get the current capacity of the allocator array. */
    inline size_t capacity() const noexcept {
        return allocator.capacity;
    }

    /* Get the maximum size of the list. */
    inline std::optional<size_t> max_size() const noexcept {
        return allocator.max_size();
    }

    /* Reserve memory for a given number of nodes ahead of time.

    NOTE: this method produces a MemGuard object that holds the view at the new capacity
    until it falls out of scope.  This prevents the view from being reallocated as long
    as the guard is active, and ensures that the node addresses remain stable over that
    time.  This is necessary for any algorithm that modifies the list while iterating
    over it.  Upon destruction, the guard also attempts to shrink the allocator back to
    a reasonable size if the load factor is below its minimum threshold (as defined by
    the allocator).  This allows callers of this method to preallocate more memory than
    they need and automatically release it later to prevent bloat (with some hysteresis
    to avoid thrashing).  Lastly, guards can be nested in the same context if and only
    if none of the inner guards cause the view to grow beyond its current capacity. */
    inline MemGuard reserve(std::optional<size_t> capacity = std::nullopt) const {
        return allocator.reserve(capacity.value_or(size()));
    }

    /* Optionally reserve memory for a given number of nodes.  Produces an empty
    MemGuard if the input is null.

    NOTE: empty MemGuards are essentially no-ops that do not resize the view or prevent
    it from dynamically growing or shrinking.  They can be identified by checking their
    `active()` status. */
    inline MemGuard try_reserve(std::optional<size_t> capacity) const {
        return allocator.try_reserve(capacity);
    }

    /* Attempt to reserve memory to hold all the elements of a given container if it
    implements a `size()` method or is a Python object with a corresponding `__len__()`
    attribute.  Otherwise, produce an empty MemGuard. */
    template <typename Container>
    inline MemGuard try_reserve(const Container& container) const {
        return allocator.try_reserve(container);
    }

    /* Rearrange the nodes in memory to reduce fragmentation and improve performance. */
    inline void defragment() {
        allocator.defragment();
    }

    /* Check whether the allocator is currently frozen for memory stability.  This is
    true if and only if an active MemGuard exists for the view, preventing it from
    being reallocated. */
    inline bool frozen() const noexcept {
        return allocator.frozen();
    }

    /* Get the total amount of dynamic memory consumed by the list.  This does not
    include any stack space held by the list itself (i.e. the size of its control
    structures), which must be accounted for separately by the caller. */
    inline size_t nbytes() const noexcept {
        return allocator.nbytes();
    }

    /* Get the current specialization for Python objects within the list. */
    inline PyObject* specialization() const {
        return allocator.specialization;
    }

    /* Enforce strict type checking for elements of this list. */
    inline void specialize(PyObject* spec) {
        static_assert(
            std::is_convertible_v<Value, PyObject*>,
            "Cannot specialize a list that does not contain Python objects."
        );
        allocator.specialize(spec);
    }

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* NOTE: Views can be iterated over just like any other STL container.  They can
     * thus be used equivalently in range-based for loops, STL algorithms, or the
     * universal iter() function defined in utils/iter.h.
     * 
     * Each of the following methods maps to a corresponding Iterator template with the
     * expected semantics.  Users should note that reverse iteration over a
     * singly-linked list requires the creation of a temporary stack, which is
     * conditionally compiled for this specific case.
     *
     * The `yield` template parameter determines the return value of the iterator's
     * dereference operator.  It can only be used on dictlike views, and defaults to
     * Yield::KEY in all other cases.  See the Iterator definition above for more
     * details.
     */

    template <Yield yield = Yield::KEY>
    Iterator<Direction::FORWARD, yield> begin() {
        if (head() == nullptr) {
            return end<yield>();
        }
        Node* next = head()->next();
        Derived* self = static_cast<Derived*>(this);
        return Iterator<Direction::FORWARD, yield>(*self, nullptr, head(), next);
    }

    template <Yield yield = Yield::KEY>
    ConstIterator<Direction::FORWARD, yield> begin() const {
        return cbegin<yield>();
    }

    template <Yield yield = Yield::KEY>
    ConstIterator<Direction::FORWARD, yield> cbegin() const {
        if (head() == nullptr) {
            return cend<yield>();
        }
        Node* next = head()->next();
        const Derived* self = static_cast<const Derived*>(this);
        return ConstIterator<Direction::FORWARD, yield>(*self, nullptr, head(), next);
    }

    template <Yield yield = Yield::KEY>
    Iterator<Direction::FORWARD, yield> end() {
        Derived* self = static_cast<Derived*>(this);
        return Iterator<Direction::FORWARD, yield>(*self);
    }

    template <Yield yield = Yield::KEY>
    ConstIterator<Direction::FORWARD, yield> end() const {
        return cend<yield>();
    }

    template <Yield yield = Yield::KEY>
    ConstIterator<Direction::FORWARD, yield> cend() const {
        const Derived* self = static_cast<const Derived*>(this);
        return ConstIterator<Direction::FORWARD, yield>(*self);
    }

    template <Yield yield = Yield::KEY>
    Iterator<Direction::BACKWARD, yield> rbegin() {
        if (tail() == nullptr) {
            return rend<yield>();
        }

        Derived* self = static_cast<Derived*>(this);

        // if list is doubly-linked, we can use prev to get neighbors
        if constexpr (NodeTraits<Node>::has_prev) {
            Node* prev = tail()->prev();
            return Iterator<Direction::BACKWARD, yield>(
                *self, prev, tail(), nullptr
            );

        // Otherwise, build a temporary stack of prev pointers
        } else {
            std::stack<Node*> prev;
            prev.push(nullptr);  // stack always has at least one element (nullptr)
            Node* temp = head();
            while (temp != tail()) {
                prev.push(temp);
                temp = temp->next();
            }
            return Iterator<Direction::BACKWARD, yield>(
                *self, std::move(prev), tail(), nullptr
            );
        }
    }

    template <Yield yield = Yield::KEY>
    ConstIterator<Direction::BACKWARD, yield> rbegin() const {
        return crbegin<yield>();
    }

    template <Yield yield = Yield::KEY>
    ConstIterator<Direction::BACKWARD, yield> crbegin() const {
        if (tail() == nullptr) {
            return crend<yield>();
        }

        const Derived* self = static_cast<const Derived*>(this);

        // if list is doubly-linked, we can use prev to get neighbors
        if constexpr (NodeTraits<Node>::has_prev) {
            const Node* prev = tail()->prev();
            return ConstIterator<Direction::BACKWARD, yield>(
                *self, prev, tail(), nullptr
            );

        // Otherwise, build a temporary stack of prev pointers
        } else {
            std::stack<const Node*> prev;
            prev.push(nullptr);  // stack always has at least one element (nullptr)
            const Node* temp = head();
            while (temp != tail()) {
                prev.push(temp);
                temp = temp->next();
            }
            return ConstIterator<Direction::BACKWARD, yield>(
                *self, std::move(prev), tail(), nullptr
            );
        }
    }

    template <Yield yield = Yield::KEY>
    Iterator<Direction::BACKWARD, yield> rend() {
        Derived* self = static_cast<Derived*>(this);
        return Iterator<Direction::BACKWARD, yield>(*self);
    }

    template <Yield yield = Yield::KEY>
    ConstIterator<Direction::BACKWARD, yield> rend() const {
        return crend<yield>();
    }

    template <Yield yield = Yield::KEY>
    ConstIterator<Direction::BACKWARD, yield> crend() const {
        const Derived* self = static_cast<const Derived*>(this);
        return ConstIterator<Direction::BACKWARD, yield>(*self);
    }

    ////////////////////////
    ////    INTERNAL    ////
    ////////////////////////

    /* Get a reference to a temporary node held by the allocator for internal use.
    Users should not access this node directly, as it may be overwritten at any time. */
    inline Node* temp() const noexcept {
        return allocator.temp();
    }

    /* Check whether a given index is closer to the tail of the list than it is to the
    head. */
    inline bool closer_to_tail(size_t index) const noexcept {
        return index > (size() + 1) / 2;
    }

protected:

    /* Get the size at which to initialize a list based on an input container and
    optional fixed size. */
    template <typename Container>
    static std::optional<size_t> init_size(
        Container&& container,
        std::optional<size_t> capacity
    ) {
        if constexpr (DYNAMIC) {
            std::optional<size_t> size = len(container);

            // use max of container size and specified capacity
            if (size.has_value()) {
                if (capacity.has_value()) {
                    size_t x = capacity.value();
                    size_t y = size.value();
                    return std::make_optional(x < y ? y : x);
                }
                return size;
            }
        }

        return capacity;
    }

};


/* An extension of BaseView that adds behavior specific to HashAllocator-based
views. */
template <typename Derived, typename Allocator>
class HashView : public BaseView<Derived, Allocator> {
    using Base = BaseView<Derived, Allocator>;

public:
    using Node = typename Base::Node;
    using Value = typename Base::Value;

    using Base::Base;
    using Base::operator=;

    /* Construct a hashed view from an input container. */
    template <typename Container>
    HashView(
        Container&& iterable,
        std::optional<size_t> capacity,
        PyObject* spec,
        bool reverse
    ) : Base(capacity, spec)
    {
        if (reverse) {
            for (const auto& item : iter(iterable)) {
                node<Allocator::EXIST_OK | Allocator::INSERT_HEAD>(item);
            }
        } else {
            for (const auto& item : iter(iterable)) {
                node<Allocator::EXIST_OK | Allocator::INSERT_TAIL>(item);
            }
        }
    }

    /* Construct a hashed view from an iterator range. */
    template <typename Iterator>
    HashView(
        Iterator&& begin,
        Iterator&& end,
        std::optional<size_t> capacity,
        PyObject* spec,
        bool reverse
    ) : Base(capacity, spec)
    {
        if (reverse) {
            while (begin != end) {
                node<Allocator::EXIST_OK | Allocator::INSERT_HEAD>(*begin);
                ++begin;
            }
        } else {
            while (begin != end) {
                node<Allocator::EXIST_OK | Allocator::INSERT_TAIL>(*begin);
                ++begin;
            }
        }
    }

    /* NOTE: Hashed views offer additional options for customizing the behavior of the
     * create(), recycle(), and search() methods to avoid repeated lookups.  Each
     * option and its meaning is documented in the HashAllocator class under
     * allocate.h.  By default, the node() and recycle() methods behave identically to
     * their BaseView equivalents.  The search() method, however, is new to HashView,
     * and can be used to perform a setlike search of the view's keys.
     */

    /* Construct a new hashed node using an optional template configuration to resolve
    collisions. */
    template <unsigned int flags = Allocator::DEFAULT, typename... Args>
    inline Node* node(Args&&... args) {
        return this->allocator.template create<flags, Args&&...>(
            std::forward<Args>(args)...
        );
    }

    /* Release a hashed node, returning it to the allocator.  Uses a similar (optional)
    template configuration to avoid repeated lookups. */
    template <unsigned int flags = Allocator::DEFAULT, typename... Args>
    inline auto recycle(Args&&... args) {
        return this->allocator.template recycle<flags>(std::forward<Args>(args)...);
    }

    /* Search the set for a particular node/value.  Supports template configuration
    just like node() and recycle(). */
    template <unsigned int flags = Allocator::DEFAULT, typename... Args>
    inline Node* search(Args&&... args) {
        return this->allocator.template search<flags>(std::forward<Args>(args)...);
    }

    /* Search a const set for a particular node/value. */
    template <unsigned int flags = Allocator::DEFAULT, typename... Args>
    inline const Node* search(Args&&... args) const {
        return this->allocator.template search<flags>(std::forward<Args>(args)...);
    }

};


////////////////////////
////    LISTVIEW    ////
////////////////////////


/* A linked data structure that uses a dynamic array to store nodes in sequential
order. */
template <typename NodeType, unsigned int Flags>
class ListView : public BaseView<
    ListView<NodeType, Flags>, ListAllocator<NodeType, Flags>
> {
    using Base = BaseView<ListView<NodeType, Flags>, ListAllocator<NodeType, Flags>>;

public:
    static constexpr bool listlike = true;
    using Base::Base;
    using Base::operator=;

    template <unsigned int NewFlags>
    using Reconfigure = ListView<NodeType, NewFlags>;

};


///////////////////////
////    SETVIEW    ////
///////////////////////


/* A linked data structure that uses a hash table to allocate and store nodes in a
setlike fashion. */
template <typename NodeType, unsigned int Flags>
class SetView : public HashView<
    SetView<NodeType, Flags>, HashAllocator<Hashed<NodeType>, Flags>
> {
    using Base = HashView<
        SetView<NodeType, Flags>, HashAllocator<Hashed<NodeType>, Flags>
    >;

public:
    static constexpr bool setlike = true;
    using Base::Base;
    using Base::operator=;

    template <unsigned int NewFlags>
    using Reconfigure = SetView<NodeType, NewFlags>;

};


////////////////////////
////    DICTVIEW    ////
////////////////////////


/* A linked data structure that uses a hash table to allocate and store nodes as
key-value pairs. */
template <typename NodeType, unsigned int Flags>
class DictView : public HashView<
    DictView<NodeType, Flags>, HashAllocator<Hashed<NodeType>, Flags>
> {
    using Base = HashView<
        DictView<NodeType, Flags>, HashAllocator<Hashed<NodeType>, Flags>
    >;

    template <typename View>
    friend class ViewTraits;

    template <unsigned int NewFlags>
    using Reconfigure = DictView<NodeType, NewFlags>;

public:
    static constexpr bool dictlike = true;
    using MappedValue = typename Base::Node::MappedValue;
    using Allocator = typename Base::Allocator;
    using Base::Base;
    using Base::operator=;

    /* Construct a DictView from an input container. */
    template <typename Container>
    DictView(
        Container&& iterable,
        std::optional<size_t> capacity,
        PyObject* spec,
        bool reverse
    ) : Base(capacity, spec)
    {
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED
        );

        if constexpr (is_pyobject<Container>) {
            if (PyDict_Check(iterable)) {
                if (reverse) {
                    for (const auto& item : PyDict(iterable)) {
                        Base::template node<flags | Allocator::INSERT_HEAD>(item);
                    }
                } else {
                    for (const auto& item : PyDict(iterable)) {
                        Base::template node<flags | Allocator::INSERT_TAIL>(item);
                    }
                }
            } else {
                if (reverse) {
                    for (const auto& item : iter(iterable)) {
                        Base::template node<flags | Allocator::INSERT_HEAD>(item);
                    }
                } else {
                    for (const auto& item : iter(iterable)) {
                        Base::template node<flags | Allocator::INSERT_TAIL>(item);
                    }
                }
            }

        } else {
            if (reverse) {
                for (const auto& item : iter(iterable)) {
                    Base::template node<flags | Allocator::INSERT_HEAD>(item);
                }
            } else {
                for (const auto& item : iter(iterable)) {
                    Base::template node<flags | Allocator::INSERT_TAIL>(item);
                }
            }
        }
    }

    /* Construct a DictView from an iterator range. */
    template <typename Iterator>
    DictView(
        Iterator&& begin,
        Iterator&& end,
        std::optional<size_t> capacity,
        PyObject* spec,
        bool reverse
    ) : Base(capacity, spec)
    {
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED
        );

        if (reverse) {
            while (begin != end) {
                Base::template node<flags | Allocator::INSERT_HEAD>(*begin);
                ++begin;
            }
        } else {
            while (begin != end) {
                Base::template node<flags | Allocator::INSERT_TAIL>(*begin);
                ++begin;
            }
        }
    }

};


//////////////////////
////    TRAITS    ////
//////////////////////


/* A collection of SFINAE traits for inspecting view types at compile time and
dispatching non-member methods accordingly. */
template <typename ViewType>
struct ViewTraits {

    // dispatch hooks for non-member algorithms
    static constexpr bool linked = std::is_base_of_v<ViewTag, ViewType>;
    static constexpr bool listlike = linked && ViewType::listlike;
    static constexpr bool setlike = linked && ViewType::setlike;
    static constexpr bool dictlike = linked && ViewType::dictlike;
    static constexpr bool hashed = setlike || dictlike;

    // template introspection
    static constexpr unsigned int FLAGS = ViewType::FLAGS;
    static constexpr bool SINGLY_LINKED = ViewType::SINGLY_LINKED;
    static constexpr bool DOUBLY_LINKED = ViewType::DOUBLY_LINKED;
    static constexpr bool XOR = ViewType::XOR;
    static constexpr bool DYNAMIC = ViewType::DYNAMIC;
    static constexpr bool FIXED_SIZE = ViewType::FIXED_SIZE;
    static constexpr bool PACKED = ViewType::PACKED;
    static constexpr bool STRICTLY_TYPED = ViewType::STRICTLY_TYPED;

    // reconfigure with different flags
    class As {
        using Node = typename ViewType::Node;

        template <Yield yield, bool as_pytuple, bool dictlike = false>
        struct AsList {
            using Value = typename ViewType::Value;
            using RootNode = typename NodeTraits<Node>::Root;
            using type = ListView<RootNode, FLAGS>;
        };

        template <Yield yield, bool as_pytuple>
        struct AsList<yield, as_pytuple, true> {
            using Key = typename ViewType::Value;
            using Value = typename ViewType::MappedValue;
            using NewValue = std::conditional_t<
                yield == Yield::KEY,
                Key,
                std::conditional_t<
                    yield == Yield::VALUE,
                    Value,
                    std::conditional_t<
                        as_pytuple,
                        PyObject*,
                        std::pair<Key, Value>
                    >
                >
            >;
            using RootNode = typename NodeTraits<Node>::Root;
            using ListNode = typename NodeTraits<RootNode>::template Reconfigure<NewValue>;
            using type = ListView<ListNode, FLAGS>;
        };

        template <Yield yield, bool as_pytuple, bool dictlike = false>
        struct AsSet {
            using Value = typename ViewType::Value;
            using RootNode = typename NodeTraits<Node>::Root;
            using type = SetView<RootNode, FLAGS>;
        };

        template <Yield yield, bool as_pytuple>
        struct AsSet<yield, as_pytuple, dictlike> {
            using Key = typename ViewType::Value;
            using Value = typename ViewType::MappedValue;
            using NewValue = std::conditional_t<
                yield == Yield::KEY,
                Key,
                std::conditional_t<
                    yield == Yield::VALUE,
                    Value,
                    std::conditional_t<
                        as_pytuple,
                        PyObject*,
                        std::pair<Key, Value>
                    >
                >
            >;
            using RootNode = typename NodeTraits<Node>::Root;
            using SetNode = typename NodeTraits<RootNode>::template Reconfigure<NewValue>;
            using type = SetView<SetNode, FLAGS>;
        };

    public:

        template <Yield yield = Yield::KEY, bool as_pytuple = false>
        using List = typename AsList<yield, as_pytuple, dictlike>::type;

        template <Yield yield = Yield::KEY, bool as_pytuple = false>
        using Set = typename AsSet<yield, as_pytuple, dictlike>::type;

        using SINGLY_LINKED = typename ViewTraits::template Reconfigure<
            (FLAGS & ~(Config::DOUBLY_LINKED | Config::XOR)) | Config::SINGLY_LINKED
        >;
        using DOUBLY_LINKED = typename ViewType::template Reconfigure<
            (FLAGS & ~(Config::SINGLY_LINKED | Config::XOR)) | Config::DOUBLY_LINKED
        >;
        using XOR = typename ViewType::template Reconfigure<
            (FLAGS & ~(Config::SINGLY_LINKED | Config::DOUBLY_LINKED)) | Config::XOR
        >;
        using DYNAMIC = typename ViewType::template Reconfigure<
            (FLAGS & ~Config::FIXED_SIZE) | Config::DYNAMIC
        >;
        using FIXED_SIZE = typename ViewType::template Reconfigure<
            (FLAGS & ~Config::DYNAMIC) | Config::FIXED_SIZE
        >;
        using PACKED = typename ViewType::template Reconfigure<
            FLAGS | Config::PACKED
        >;
        using UNPACKED = typename ViewType::template Reconfigure<
            FLAGS & ~Config::PACKED
        >;
        using STRICTLY_TYPED = typename ViewType::template Reconfigure<
            FLAGS | Config::STRICTLY_TYPED
        >;
        using LOOSELY_TYPED = typename ViewType::template Reconfigure<
            FLAGS & ~Config::STRICTLY_TYPED
        >;

    };

    template <unsigned int Config>
    using Reconfigure = typename ViewType::template Reconfigure<Config>;

    // assertion helpers
    class Assert {

        template <bool dictlike = false, typename Dummy = void>
        struct _Assert {

            template <bool value, Yield yield>
            static constexpr bool as_pytuple = !value;

        };

        template <typename Dummy>
        struct _Assert<true, Dummy> {

            template <bool value, Yield yield>
            static constexpr bool as_pytuple = !value || (
                yield == Yield::ITEM &&
                std::is_same_v<typename ViewType::Value, PyObject*> &&
                std::is_same_v<typename ViewType::MappedValue, PyObject*>
            );

        };

    public:

        template <bool value, Yield yield>
        static constexpr bool as_pytuple = (
            _Assert<dictlike>::template as_pytuple<value, yield>
        );

    };

};


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_CORE_VIEW_H
