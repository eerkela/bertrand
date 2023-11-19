// include guard: BERTRAND_STRUCTS_LINKED_CORE_VIEW_H
#ifndef BERTRAND_STRUCTS_LINKED_CORE_VIEW_H
#define BERTRAND_STRUCTS_LINKED_CORE_VIEW_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <stack>  // std::stack
#include <stdexcept>  // std::invalid_argument
#include <Python.h>  // CPython API
#include "node.h"  // Hashed<>, Mapped<>
#include "allocate.h"  // Allocator
#include "iter.h"  // Iterator, Direction
#include "../../util/iter.h"  // iter()
#include "../../util/math.h"  // next_power_of_two()
#include "../../util/python.h"  // len()


namespace bertrand {
namespace structs {
namespace linked {


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a low-level view for a linked data structure.

NOTE: this class is inherited by all views, and can be used for easy SFINAE checks via
std::is_base_of, without requiring any foreknowledge of template parameters. */
class ViewTag {};


/* Base class representing the low-level core of a linked data structure.

Views are responsible for managing the memory of and links between the underlying
nodes, iteration over them, and references to the head/tail of the list as well as its
current size.  They are lightweight wrappers around a raw memory allocator that exposes
the following interface:

template <typename NodeType>
class Allocator : public BaseAllocator<NodeType> {
public:
    Node* head;
    Node* tail;
    size_t capacity;  // total number of nodes in existence
    size_t occupied;  // equivalent to view.size()
    PyObject* specialization;

    Node* create(Args...);  // forward to Node constructor
    void recycle(Node* node);
    void reserve(size_t capacity);
    void defragment();
    void clear();
    void specialize(PyObject* spec);
    size_t nbytes();
};

To this, they add a number of convenience methods for high-level list manipulations,
which are then used in the various non-member method headers listed in the algorithms/
directory.  This setup allows for ultimate configurability and code reuse, since
methods can be mixed, matched, and specialized alongside views to produce a variety of
flexible and highly-optimized data structures, without worrying about low-level
implementation details.
*/
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

    // low-level memory management
    mutable Allocator allocator;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty view. */
    BaseView(size_t capacity, bool dynamic, PyObject* spec) :
        allocator(capacity, dynamic, spec)
    {}

    // TODO: union.h and update.h require a simple constructor for view

    /* Construct a view from an input iterable. */
    template <typename Container>
    BaseView(
        Container&& iterable,
        size_t capacity,
        bool dynamic,
        PyObject* spec,
        bool reverse
    ) : allocator(
            init_size(iterable, capacity, dynamic),
            dynamic,
            spec
        )
    {
        for (auto item : util::iter(iterable)) {
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
        size_t capacity,
        bool dynamic,
        PyObject* spec,
        bool reverse
    ) : allocator(capacity, dynamic, spec)
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
        if (node != nullptr && !allocator.owns(node)) {
            throw std::invalid_argument("node must be owned by this allocator");
        }
        allocator.head = node;
    }

    /* Get the tail of the list. */
    inline Node* tail() const noexcept {
        return allocator.tail;
    }
 
    /* Set the tail of the list to another node. */
    inline void tail(Node* node) {
        if (node != nullptr && !allocator.owns(node)) {
            throw std::invalid_argument("node must be owned by this allocator");
        }
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
        Derived result(capacity(), dynamic(), specialization());
        for (auto it = util::iter(*this).forward(); it != it.end(); ++it) {
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
            head(curr);
        }
        if (next == nullptr) {
            tail(curr);
        }
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        Node::unlink(prev, curr, next);  // as defined by Node
        if (prev == nullptr) {
            head(next);
        }
        if (next == nullptr) {
            tail(prev);
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
        return dynamic() ? std::nullopt : std::make_optional(allocator.capacity);
    }

    /* Check whether the allocator supports dynamic resizing. */
    inline bool dynamic() const noexcept {
        return allocator.dynamic();
    }

    /* Check whether the allocator is currently frozen for memory stability. */
    inline bool frozen() const noexcept {
        return allocator.frozen();
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
    static size_t init_size(Container&& container, size_t capacity, bool dynamic) {
        // trivial case: capacity is fixed
        if (!dynamic) return capacity;

        // get size of container if possible
        std::optional<size_t> size = util::len(container);
        if (size.has_value()) {  // use max of container size and specified capacity
            size_t val = size.value();
            return val < capacity ? capacity : val;
        }

        // otherwise, use the specified capacity
        return capacity;
    }

};


////////////////////////
////    LISTVIEW    ////
////////////////////////


/* A linked data structure that uses a dynamic array to store nodes in sequential
order.

ListViews use a similar memory layout to std::vector and the built-in Python list.
They initially allocate a small array of nodes (8 in this case), and advance an
internal pointer whenever a new node is constructed.  When the array is full, the view
allocates a new array with twice the size and moves the old nodes into the new array.
This strategy is significantly more efficient than allocating nodes individually on the
heap, which is typical for linked lists and incurs a large overhead for each
allocation.  This sequential memory layout also avoids heap fragmentation, improving
cache locality and overall performance as a result.  This gives the list amortized O(1)
insertion time comparable to std::vector or its Python equivalent, and much faster in
general than std::list.

Whenever a node is removed from the list, its memory is returned to the allocator array
and inserted into a linked list of recycled nodes.  This list is composed directly from
the removed nodes themselves, avoiding auxiliary data structures and associated memory
overhead.  When a new node is constructed, the free list is checked to see if there are
any available nodes.  If there are, the node is removed from the free list and reused.
Otherwise, a new node is allocated from the array, causing it to grow if necessary.
Conversely, if a linked list's size drops to below 1/4 its capacity, the allocator
array will shrink to half its current size, providing dynamic sizing.

As an additional optimization, linked lists prefer to store their nodes in strictly
sequential order, which ensures optimal alignment at the hardware level.  This is
accomplished by transferring nodes in their current list order whenever we grow or
shrink the internal array.  This automatically defragments the list periodically as
elements are added and removed, requiring no additional overhead beyond an ordinary
copy.  If desired, this process can also be triggered manually via the defragment()
method for on-demand optimization.
*/
template <typename NodeType = DoubleNode<PyObject*>>
class ListView : public BaseView<ListView<NodeType>, ListAllocator<NodeType>> {
    using Base = BaseView<ListView<NodeType>, ListAllocator<NodeType>>;

public:
    // inherit constructors
    using Base::Base;
    using Base::operator=;

    /* NOTE: we don't need any further implementation since ListView is the minimal
     * valid view configuration.  Other than an optimized memory management strategy,
     * it does not add any additional functionality, and does not assume any specific
     * capabilities of the underlying nodes.
     */
};


///////////////////////
////    SETVIEW    ////
///////////////////////


/* A linked data structure that uses a hash table to allocate and store nodes.

SetViews use a similar memory layout to std::unordered_set and the built-in Python set.
They begin by allocating a small array of nodes (16 in this case), and placing nodes
into the array using std::hash to determine their position.  If two nodes hash to the
same index, then the collision is resolved using open addressing via double hashing.
This strategy prevents clustering and ensures that items are relatively evenly
distributed across the array, which is important for optimal performance.  Additionally,
the maximum load factor before growing the array is 0.5, limiting collisions and
ensuring fast lookup times in the average case.  The minimum load factor before
shrinking the array is 0.125, providing hysteresis to prevent thrashing when the array
is near its minimum/maximum size.

Whenever a node is removed from the set, its corresponding index is marked as a
tombstone in an auxiliary bit set, indicating that it is available for reuse without
interfering with normal collision resolution.  These tombstones are skipped over during
searches and removals, and are automatically cleared when the set is resized or
defragmented, or if their concentration exceeds 1/8th of the table size.  Tombstones
can also be gradually replaced if they are encountered during insertion, delaying the
need for a full rehash.

Combining the allocator with a hash table like this provides a number of advantages
over using a separate data structure.  First, it limits memory overhead to just 2 extra
bits per node (for the required bit flags), which is significantly less than retaining
a separate hash table in addition to the nodes themselves.  Second, it guarantees (at
an algorithmic level) that the set invariants are never violated at any point.
Non-hashable items will simply fail to compile, and duplicate items will be rejected by
the allocator at runtime.  This is in contrast to maintaining a separate hash table,
which requires manual synchronization to ensure that the data structures remain
consistent with one another.  Finally, it removes an extra layer of indirection during
lookups, which improves cache locality and overall performance. */
template <typename NodeType = DoubleNode<PyObject*>>
class SetView : public BaseView<SetView<NodeType>, HashAllocator<Hashed<NodeType>>> {
    using Base = BaseView<SetView<NodeType>, HashAllocator<Hashed<NodeType>>>;

public:
    using Node = Hashed<NodeType>;

    // inherit constructors
    using Base::Base;
    using Base::operator=;

    /* Construct a SetView from an input iterable. */
    template <typename Container>
    SetView(
        Container&& iterable,
        size_t capacity,
        bool dynamic,
        PyObject* spec,
        bool reverse
    ) : Base::Allocator(
            Base::init_size(iterable, capacity, dynamic),
            dynamic,
            spec
        )
    {
        for (auto item : util::iter(iterable)) {
            Node* curr = node<true>(item);  // exist_ok = True
            if (curr->next() == nullptr && curr != this->tail()) {
                if (reverse) {
                    Base::link(nullptr, curr, Base::head());
                } else {
                    Base::link(Base::tail(), curr, nullptr);
                }
            }
        }
    }

    /* Construct a SetView from an iterator range. */
    template <typename Iterator>
    SetView(
        Iterator&& begin,
        Iterator&& end,
        size_t capacity,
        bool dynamic,
        PyObject* spec,
        bool reverse
    ) : Base::Allocator(capacity, dynamic, spec)
    {
        for (; begin != end; ++begin) {
            Node* curr = node<true>(*begin);  // exist_ok = True
            if (curr->next() == nullptr && curr != this->tail()) {
                if (reverse) {
                    Base::link(nullptr, curr, Base::head());
                } else {
                    Base::link(Base::tail(), curr, nullptr);
                }
            }
        }
    }

    /* Construct a new node for the set.

    NOTE: this accepts an optional `exist_ok` template parameter that controls whether
    or not the allocator will throw an exception if the value already exists in the set.
    The default is false, meaning that an exception will be thrown if a duplicate node
    is requested. */
    template <bool exist_ok = false, typename... Args>
    inline Node* node(Args&&... args) const {
        return this->allocator.template create<exist_ok, Args&&...>(
            std::forward<Args>(args)...
        );
    }

    /* Search the set for a particular node/value. */
    template <typename T>
    inline Node* search(T* key) const {
        return this->allocator.search(key);
    }

};


////////////////////////
////    DICTVIEW    ////
////////////////////////


/* A linked data structure that uses a hash table to store nodes as key-value pairs.

DictViews are identical to SetViews except that their nodes are specialized to store
key-value pairs rather than single values.  This allows them to be used as a mapping
type, similar to std::unordered_map or the built-in Python dict.

DictViews (and the associated LinkedDicts) are especially useful as high-performance
caches, particularly when they are doubly-linked.  In this case, we can move nodes
within the list at the same time as we perform a search, allowing us to efficiently
implement a variety of caching strategies.  The most basic is a Least-Recently-Used
(LRU) cache, which simply moves the searched node to the head of the list in a single
operation. */
template <typename NodeType = DoubleNode<PyObject*>, typename MappedType = PyObject*>
class DictView : public BaseView<
    DictView<NodeType, MappedType>,
    HashAllocator<Mapped<Hashed<NodeType>, MappedType>>
> {
    using Base = BaseView<
        DictView<NodeType, MappedType>,
        HashAllocator<Mapped<Hashed<NodeType>, MappedType>>
    >;

public:
    using Node = Mapped<Hashed<NodeType>, MappedType>;

    // inherit constructors
    using Base::Base;
    using Base::operator=;

    /* Construct a DictView from an input iterable. */
    template <typename Container>
    DictView(
        Container&& iterable,
        size_t capacity,
        bool dynamic,
        PyObject* spec,
        bool reverse
    ) : Base::Allocator(
            Base::init_size(iterable, capacity, dynamic),
            dynamic,
            spec
        )
    {
        for (auto item : util::iter(iterable)) {
            Node* curr = node<true>(item);  // exist_ok = True
            if (curr->next() == nullptr && curr != this->tail()) {
                if (reverse) {
                    Base::link(nullptr, curr, Base::head());
                } else {
                    Base::link(Base::tail(), curr, nullptr);
                }
            }
        }
    }

    /* Construct a SetView from an iterator range. */
    template <typename Iterator>
    DictView(
        Iterator&& begin,
        Iterator&& end,
        size_t capacity,
        bool dynamic,
        PyObject* spec,
        bool reverse
    ) : Base::Allocator(capacity, dynamic, spec)
    {
        for (; begin != end; ++begin) {
            Node* curr = node<true>(*begin);  // exist_ok = True
            if (curr->next() == nullptr && curr != this->tail()) {
                if (reverse) {
                    Base::link(nullptr, curr, Base::head());
                } else {
                    Base::link(Base::tail(), curr, nullptr);
                }
            }
        }
    }

    /* Construct a new node for the set.

    NOTE: this accepts an optional `exist_ok` template parameter that controls whether
    or not the allocator will throw an exception if the value already exists in the set.
    The default is false, meaning that an exception will be thrown if a duplicate node
    is requested. */
    template <bool exist_ok = false, typename... Args>
    inline Node* node(Args&&... args) const {
        return this->allocator.template create<exist_ok, Args&&...>(
            std::forward<Args>(args)...
        );
    }

    /* Search the set for a particular node/value. */
    template <typename T>
    inline Node* search(T* key) const {
        return this->allocator.search(key);
    }

};


///////////////////////////
////    VIEW TRAITS    ////
///////////////////////////


/* A collection of SFINAE traits for inspecting view types at compile time. */
template <typename ViewType>
class ViewTraits {

    /* Detects whether the templated type has a search(Value&) method, indicating
    set-like behavior. */
    struct _setlike {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(
            t->search(std::declval<typename T::Value>()),
            std::true_type()
        );
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<ViewType>(nullptr))::value;
    };

    /* Detects whether the templated type has a lookup(Value&) method, indicating
    dict-like behavior. */
    struct _dictlike {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(
            t->lookup(std::declval<typename T::Value>()),
            std::true_type()
        );
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<ViewType>(nullptr))::value;
    };

public:
    static constexpr bool listlike = std::is_base_of_v<ViewTag, ViewType>;
    static constexpr bool setlike = listlike && _setlike::value;
    static constexpr bool dictlike = listlike && _dictlike::value;
};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_CORE_VIEW_H
