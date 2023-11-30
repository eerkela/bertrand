// include guard: BERTRAND_STRUCTS_LINKED_CORE_VIEW_H
#ifndef BERTRAND_STRUCTS_LINKED_CORE_VIEW_H
#define BERTRAND_STRUCTS_LINKED_CORE_VIEW_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <stack>  // std::stack
#include <Python.h>  // CPython API
#include "node.h"  // Hashed<>, Mapped<>
#include "allocate.h"  // allocators
#include "iter.h"  // Iterator, Direction
#include "../../util/iter.h"  // iter()
#include "../../util/ops.h"  // len()


namespace bertrand {
namespace structs {
namespace linked {


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a low-level view for a linked data structure.

NOTE: this class is inherited by all views, and can be used for easy SFINAE checks via
std::is_base_of, without requiring any foreknowledge of template parameters. */
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
class template in the allocate.h header.  They may add additional functionality as
specified in the extensions to BaseView listed below. */
template <typename Derived, typename AllocatorType>
class BaseView : public ViewTag {
public:
    using Allocator = AllocatorType;
    using Node = typename Allocator::Node;
    using Value = typename Node::Value;
    using MemGuard = typename Allocator::MemGuard;

    template <Direction dir>
    using Iterator = linked::Iterator<BaseView, dir>;
    template <Direction dir>
    using ConstIterator = linked::Iterator<const BaseView, dir>;

    static constexpr unsigned int FLAGS = Allocator::FLAGS;
    static constexpr bool SINGLY_LINKED = Allocator::SINGLY_LINKED;
    static constexpr bool DOUBLY_LINKED = Allocator::DOUBLY_LINKED;
    static constexpr bool XOR = Allocator::XOR;
    static constexpr bool DYNAMIC = Allocator::DYNAMIC;
    static constexpr bool FIXED_SIZE = Allocator::FIXED_SIZE;
    static constexpr bool PACKED = Allocator::PACKED;
    static constexpr bool STRICTLY_TYPED = Allocator::STRICTLY_TYPED;

    // low-level memory management
    mutable Allocator allocator;  // use at your own risk!

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty view. */
    BaseView(std::optional<size_t> capacity, PyObject* spec) :
        allocator(capacity, spec)
    {}

    /* Construct a view from an input iterable. */
    template <typename Container>
    BaseView(
        Container&& iterable,
        std::optional<size_t> capacity,
        PyObject* spec,
        bool reverse
    ) : allocator(init_size(iterable, capacity), spec)
    {
        for (auto item : iter(iterable)) {
            Node* curr = node(item);
            if (reverse) {
                link(nullptr, curr, head());
            } else {
                link(tail(), curr, nullptr);
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
    ) : allocator(capacity, spec)
    {
        for (; begin != end; ++begin) {
            Node* curr = node(*begin);
            if (reverse) {
                link(nullptr, curr, head());
            } else {
                link(tail(), curr, nullptr);
            }
        }
    }

    /* Copy constructor. */
    BaseView(const BaseView& other) : allocator(other.allocator) {}

    /* Move constructor. */
    BaseView(BaseView&& other) noexcept : allocator(std::move(other.allocator)) {}

    /* Copy assignment operator. */
    BaseView& operator=(const BaseView& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // copy nodes
        allocator = other.allocator;
        return *this;
    }

    /* Move assignment operator. */
    BaseView& operator=(BaseView&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // transfer ownership of nodes
        allocator = std::move(other.allocator);
        return *this;
    }

    ///////////////////////////////
    ////    NODE MANAGEMENT    ////
    ///////////////////////////////

    /* Get the head of the list. */
    inline Node* head() const noexcept {
        return allocator.head;
    }

    /* Set the head of the list to another node. */
    inline void head(Node* node) {
        allocator.head = node;
    }

    /* Get the tail of the list. */
    inline Node* tail() const noexcept {
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
        if (prev == nullptr) allocator.head = curr;
        if (next == nullptr) allocator.tail = curr;
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        Node::unlink(prev, curr, next);  // as defined by Node
        if (prev == nullptr) allocator.head = next;
        if (next == nullptr) allocator.tail = prev;
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
    until it falls out of scope.  This prevents the view from dynamically resizing and
    guarantees that the node addresses remain stable over the intervening context,
    which is necessary for any algorithm that iterates over the list while modifying
    it.  The guard also attempts to shrink the view back to a reasonable size upon
    destruction if the load factor is below its minimum threshold (as defined by the
    allocator).  This allows users to preallocate more memory than they need and
    automatically release it later to prevent memory bloat (with hysteresis to avoid
    thrashing).  Lastly, guards can be nested if and only if none of the inner guards
    cause the view to grow beyond its current capacity. */
    inline MemGuard reserve(std::optional<size_t> capacity = std::nullopt) const {
        return allocator.reserve(capacity.value_or(size()));
    }

    /* Optionally reserve memory for a given number of nodes.  Produces an empty
    MemGuard if the input is null.

    NOTE: empty MemGuards are essentially no-ops that do not resize the view or prevent
    it from growing dynamically.  This is useful if the new capacity is not always
    known ahead of time, but may be under certain conditions. */
    inline MemGuard try_reserve(std::optional<size_t> capacity) const {
        return allocator.try_reserve(capacity);
    }

    /* Attempt to reserve memory to hold all the elements of a given container if it
    implements a `size()` method or is a Python object with a `__len__()` attribute.
    Otherwise, produce an empty MemGuard. */
    template <typename Container>
    inline MemGuard try_reserve(Container& container) const {
        return allocator.try_reserve(container);
    }

    /* Rearrange the nodes in memory to optimize performance. */
    inline void defragment() {
        allocator.defragment();
    }

    /* Check whether the allocator is currently frozen for memory stability. */
    inline bool frozen() const noexcept {
        return allocator.frozen();
    }

    /* Get the total amount of dynamic memory consumed by the list.  This does not
    include any stack memory used by the list itself, which must be accounted for
    separately by the caller. */
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

    /* NOTE: these methods are called automatically by the `iter()` utility function
     * when traversing a linked data structure.  Users should never need to call them
     * directly - `iter()` should always be preferred for compatibility with Python and
     * other C++ containers, as well as its more streamlined/intuitive interface.
     */

    /* Return a mutable forward iterator to the head of a view. */
    Iterator<Direction::forward> begin() {
        if (head() == nullptr) {
            return end();
        }
        Node* next = head()->next();
        return Iterator<Direction::forward>(*this, nullptr, head(), next);
    }

    /* Return a mutable forward iterator to terminate a view. */
    Iterator<Direction::forward> end() {
        return Iterator<Direction::forward>(*this);
    }

    /* Return a mutable reverse iterator to the tail of a view. */
    Iterator<Direction::backward> rbegin() {
        if (tail() == nullptr) {
            return rend();
        }

        // if list is doubly-linked, we can just use the prev pointer to get neighbors
        if constexpr (NodeTraits<Node>::has_prev) {
            Node* prev = tail()->prev();
            return Iterator<Direction::backward>(*this, prev, tail(), nullptr);

        // Otherwise, we have to build a temporary stack of prev pointers
        } else {
            std::stack<Node*> prev;
            prev.push(nullptr);  // stack always has at least one element (nullptr)
            Node* temp = head();
            while (temp != tail()) {
                prev.push(temp);
                temp = temp->next();
            }
            return Iterator<Direction::backward>(
                *this, std::move(prev), tail(), nullptr
            );
        }
    }

    /* Return a mutable reverse iterator to terminate a view. */
    Iterator<Direction::backward> rend() {
        return Iterator<Direction::backward>(*this);
    }

    /* Return a set of iterators over a const view. */
    ConstIterator<Direction::forward> begin() const { return cbegin(); }
    ConstIterator<Direction::forward> end() const { return cend(); }
    ConstIterator<Direction::backward> rbegin() const { return crbegin(); }
    ConstIterator<Direction::backward> rend() const { return crend(); }

    /* Return a const forward iterator to the head of a view. */
    ConstIterator<Direction::forward> cbegin() const {
        if (head() == nullptr) {
            return cend();
        }
        Node* next = head()->next();
        return ConstIterator<Direction::forward>(*this, nullptr, head(), next);
    }

    /* Return a const forward iterator to terminate thae view. */
    ConstIterator<Direction::forward> cend() const {
        return ConstIterator<Direction::forward>(*this);
    }

    /* Return a const reverse iterator to the tail of a view. */
    ConstIterator<Direction::backward> crbegin() const {
        if (tail() == nullptr) {
            return crend();
        }

        // if list is doubly-linked, we can just use the prev pointer to get neighbors
        if constexpr (NodeTraits<Node>::has_prev) {
            Node* prev = tail()->prev();
            return ConstIterator<Direction::backward>(*this, prev, tail(), nullptr);

        // Otherwise, we have to build a temporary stack of prev pointers
        } else {
            std::stack<Node*> prev;
            prev.push(nullptr);  // stack always has at least one element (nullptr)
            Node* temp = head();
            while (temp != tail()) {
                prev.push(temp);
                temp = temp->next();
            }
            return ConstIterator<Direction::backward>(
                *this, std::move(prev), tail(), nullptr
            );
        }
    }

    /* Return a const reverse iterator to terminate a view. */
    ConstIterator<Direction::backward> crend() const {
        return ConstIterator<Direction::backward>(*this);
    }

    ////////////////////////
    ////    INTERNAL    ////
    ////////////////////////

    /* Get the allocator's temporary node. */
    inline Node* temp() const noexcept {
        return allocator.temp();
    }

    /* Check whether a given index is closer to the tail of the list than it is to the
    head.
    
    NOTE: this is used to optimize certain operations for doubly-linked lists, which
    can be traversed in either direction.  If the index is closer to the tail, then we
    can save time by traversing backward rather than forward from the head. */
    inline bool closer_to_tail(size_t index) const noexcept {
        return index > (size() + 1) / 2;
    }

protected:

    /* Get the size at which to initialize a list based on a given iterable and
    optional fixed size parameter. */
    template <typename Container>
    static std::optional<size_t> init_size(
        Container&& container,
        std::optional<size_t> capacity
    ) {
        // if dynamic, get length of container and compare with specified capacity
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

        // otherwise, use capacity directly
        return capacity;
    }

};


/* An extension of BaseView that adds behavior specific to allocators that hash their
contents. */
template <typename Derived, typename Allocator>
class HashView : public BaseView<Derived, Allocator> {
    using Base = BaseView<Derived, Allocator>;

public:
    using Node = typename Base::Node;
    using Value = typename Base::Value;

    // inherit constructors
    using Base::Base;
    using Base::operator=;

    /* Construct a hashed view from an input iterable. */
    template <typename Container>
    HashView(
        Container&& iterable,
        std::optional<size_t> capacity,
        PyObject* spec,
        bool reverse
    ) : Base(capacity, spec)
    {
        for (auto item : iter(iterable)) {
            if (reverse) {
                node<Allocator::EXIST_OK | Allocator::INSERT_HEAD>(item);
            } else {
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
        for (; begin != end; ++begin) {
            if (reverse) {
                node<Allocator::EXIST_OK | Allocator::INSERT_HEAD>(*begin);
            } else {
                node<Allocator::EXIST_OK | Allocator::INSERT_TAIL>(*begin);
            }
        }
    }

    /* Construct a new node for the set using an optional template configuration.

    Any number of the following options can be combined via bitwise OR to modify the
    behavior of this method (available under the Allocator:: namespace):
        EXIST_OK - do not throw an exception if the node already exists
        EVICT_HEAD - evict the head node to make room if the set is full
        EVICT_TAIL - evict the tail node to make room if the set is full
        INSERT_HEAD - if the node is not already in the set, insert it at the head
        INSERT_TAIL - if the node is not already in the set, insert it at the tail
        MOVE_HEAD - if the node is already in the set, move it to the head
        MOVE_TAIL - if the node is already in the set, move it to the tail
    */
    template <unsigned int flags = Allocator::DEFAULT, typename... Args>
    inline Node* node(Args&&... args) const {
        return this->allocator.template create<flags, Args&&...>(
            std::forward<Args>(args)...
        );
    }

    /* Release a node, returning it to the allocator.

    Any number of the following options can be combined via bitwise OR to modify the
    behavior of this method (available under the Allocator:: namespace):
        NOEXIST_OK - do not throw an exception if the node is not contained in the set
        UNLINK - unlink the node from its neighbors before returning it to the allocator
        RETURN_MAPPED - return the node's mapped value (void otherwise)
    */
    template <unsigned int flags = Allocator::DEFAULT, typename... Args>
    inline void recycle(Args&&... args) const {
        this->allocator.template recycle<flags>(std::forward<Args>(args)...);
    }

    /* Search the set for a particular node/value.

    Any number of the following options can be combined via bitwise OR to modify the
    behavior of this method (available under the Allocator:: namespace):
        MOVE_HEAD - if the node is found, move it to the head of the set
        MOVE_TAIL - if the node is found, move it to the tail of the set
    */
    template <unsigned int flags = Allocator::DEFAULT, typename... Args>
    inline Node* search(Args&&... args) const {
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
    template <unsigned int NewFlags>
    using Reconfigure = ListView<NodeType, NewFlags>;

    static constexpr bool listlike = true;

    // inherit constructors
    using Base::Base;
    using Base::operator=;

};


///////////////////////
////    SETVIEW    ////
///////////////////////


/* A linked data structure that uses a hash table to allocate and store nodes. */
template <typename NodeType, unsigned int Flags>
class SetView : public HashView<
    SetView<NodeType, Flags>, HashAllocator<Hashed<NodeType>, Flags>
> {
    using Base = HashView<
        SetView<NodeType, Flags>, HashAllocator<Hashed<NodeType>, Flags>
    >;

public:
    template <unsigned int NewFlags>
    using Reconfigure = SetView<NodeType, NewFlags>;

    static constexpr bool setlike = true;

    // inherit constructors
    using Base::Base;
    using Base::operator=;

};


////////////////////////
////    DICTVIEW    ////
////////////////////////


/* A linked data structure that uses a hash table to store nodes as key-value pairs. */
template <typename NodeType, unsigned int Flags>
class DictView : public HashView<
    DictView<NodeType, Flags>, HashAllocator<Hashed<NodeType>, Flags>
> {
    using Base = HashView<
        DictView<NodeType, Flags>, HashAllocator<Hashed<NodeType>, Flags>
    >;

public:
    template <unsigned int NewFlags>
    using Reconfigure = DictView<NodeType, NewFlags>;

    static constexpr bool dictlike = true;

    // inherit constructors
    using Base::Base;
    using Base::operator=;

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

    // configuration flags
    static constexpr unsigned int FLAGS = ViewType::FLAGS;
    static constexpr bool SINGLY_LINKED = ViewType::SINGLY_LINKED;
    static constexpr bool DOUBLY_LINKED = ViewType::DOUBLY_LINKED;
    static constexpr bool XOR = ViewType::XOR;
    static constexpr bool DYNAMIC = ViewType::DYNAMIC;
    static constexpr bool FIXED_SIZE = ViewType::FIXED_SIZE;
    static constexpr bool PACKED = ViewType::PACKED;
    static constexpr bool STRICTLY_TYPED = ViewType::STRICTLY_TYPED;

    // reconfigure view with different flags
    struct As {
        using SinglyLinked = typename ViewType::template Reconfigure<
            (FLAGS & ~(Config::DOUBLY_LINKED | Config::XOR)) | Config::SINGLY_LINKED
        >;
        using DoublyLinked = typename ViewType::template Reconfigure<
            (FLAGS & ~(Config::SINGLY_LINKED | Config::XOR)) | Config::DOUBLY_LINKED
        >;
        using Xor = typename ViewType::template Reconfigure<
            (FLAGS & ~(Config::SINGLY_LINKED | Config::DOUBLY_LINKED)) | Config::XOR
        >;
        using Dynamic = typename ViewType::template Reconfigure<
            (FLAGS & ~Config::FIXED_SIZE) | Config::DYNAMIC
        >;
        using FixedSize = typename ViewType::template Reconfigure<
            (FLAGS & ~Config::DYNAMIC) | Config::FIXED_SIZE
        >;
        using Packed = typename ViewType::template Reconfigure<
            FLAGS | Config::PACKED
        >;
        using Unpacked = typename ViewType::template Reconfigure<
            FLAGS & ~Config::PACKED
        >;
        using StrictlyTyped = typename ViewType::template Reconfigure<
            FLAGS | Config::STRICTLY_TYPED
        >;
        using LooselyTyped = typename ViewType::template Reconfigure<
            FLAGS & ~Config::STRICTLY_TYPED
        >;
    };

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_CORE_VIEW_H
