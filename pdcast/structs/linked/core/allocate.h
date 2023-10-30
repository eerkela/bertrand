// include guard: BERTRAND_STRUCTS_LINKED_ALLOCATE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALLOCATE_H
#define BERTRAND_STRUCTS_LINKED_ALLOCATE_H

#include <cstddef>  // size_t
#include <cstdlib>  // malloc(), calloc(), free()
#include <functional>  // std::hash, std::equal_to
#include <iostream>  // std::cout, std::endl
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <stdexcept>  // std::invalid_argument
#include <Python.h>  // CPython API
#include "../util/except.h"  // catch_python(), type_error()
#include "../util/math.h"  // next_power_of_two()
#include "../util/python.h"  // std::hash<PyObject*>, std::equal_to<PyObject*>
#include "../util/repr.h"  // repr()
#include "node.h"  // NodeTraits


namespace bertrand {
namespace structs {
namespace linked {


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* DEBUG=TRUE adds print statements for every call to malloc()/free() in order
to help catch memory leaks. */
const bool DEBUG = true;


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a node allocator for a linked data structure.

NOTE: this class is inherited by all allocators, and can be used for easy SFINAE
checks via std::is_base_of, without requiring any foreknowledge of template
parameters. */
class AllocatorTag {};


/* Base class that implements shared functionality for all allocators and provides the
minimum necessary attributes for compatibility with higher-level views. */
template <typename NodeType>
class BaseAllocator : public AllocatorTag {
public:
    using Node = NodeType;

protected:

    /* Allocate a temporary node for use in algorithms. */
    inline static Node* allocate_temp() {
        Node* result = static_cast<Node*>(malloc(sizeof(Node)));
        if (result == nullptr) {
            throw std::bad_alloc();
        }
        return result;
    }

    /* Allocate a contiguous block of uninitialized nodes with the specified size. */
    inline static Node* allocate_array(size_t capacity) {
        Node* result = static_cast<Node*>(malloc(capacity * sizeof(Node)));
        if (result == nullptr) {
            throw std::bad_alloc();
        }
        return result;
    }

    /* Initialize an uninitialized node for use in the list. */
    template <typename... Args>
    inline void init_node(Node* node, Args&&... args) {
        using util::repr;

        // variadic dispatch to node constructor
        new (node) Node(std::forward<Args>(args)...);

        // check python specialization if enabled
        if (specialization != nullptr && !node->typecheck(specialization)) {
            std::ostringstream msg;
            msg << repr(node->value()) << " is not of type ";
            msg << repr(specialization);
            node->~Node();  // in-place destructor
            throw util::type_error(msg.str());
        }

        if constexpr (DEBUG) {
            std::cout << "    -> create: " << repr(node->value()) << std::endl;
        }
    }

    /* Destroy all nodes contained in the list. */
    inline void destroy_list() noexcept {
        using util::repr;

        Node* curr = head;
        while (curr != nullptr) {
            Node* next = curr->next();
            if constexpr (DEBUG) {
                std::cout << "    -> recycle: " << repr(curr->value()) << std::endl;
            }
            curr->~Node();  // in-place destructor
            curr = next;
        }
    }

public:
    Node* head;  // head of the list
    Node* tail;  // tail of the list
    Node* temp;  // temporary node for use in insertions/custom algorithms
    size_t capacity;  // number of nodes in the array
    size_t occupied;  // number of nodes currently in use - equivalent to list.size()
    bool frozen;  // indicates if the array is frozen at its current capacity
    PyObject* specialization;  // type specialization for PyObject* values

    /* Create an allocator with an optional fixed size. */
    BaseAllocator(
        std::optional<size_t> capacity,
        size_t default_capacity,
        PyObject* specialization
    ) :
        head(nullptr),
        tail(nullptr),
        temp(allocate_temp()),
        capacity(capacity.value_or(default_capacity)),
        occupied(0),
        frozen(capacity.has_value()),
        specialization(specialization)
    {
        
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << this->capacity << " nodes" << std::endl;
        }
        if (specialization != nullptr) {
            Py_INCREF(specialization);
        }
    }

    /* Copy constructor. */
    BaseAllocator(const BaseAllocator& other) :
        head(nullptr),
        tail(nullptr),
        temp(allocate_temp()),
        capacity(other.capacity),
        occupied(other.occupied),
        frozen(other.frozen),
        specialization(other.specialization)
    {
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << capacity << " nodes" << std::endl;
        }
        Py_XINCREF(specialization);
    }

    /* Move constructor. */
    BaseAllocator(BaseAllocator&& other) noexcept :
        head(other.head),
        tail(other.tail),
        temp(allocate_temp()),
        capacity(other.capacity),
        occupied(other.occupied),
        frozen(other.frozen),
        specialization(other.specialization)
    {
        other.head = nullptr;
        other.tail = nullptr;
        other.capacity = 0;
        other.occupied = 0;
        other.frozen = false;
        other.specialization = nullptr;
    }

    /* Copy assignment operator. */
    BaseAllocator& operator=(const BaseAllocator& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // destroy current
        Py_XDECREF(specialization);
        if (head != nullptr) {
            destroy_list();  // calls destructors
            head = nullptr;
            tail = nullptr;
        }
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << capacity << " nodes" << std::endl;
        }

        // copy from other
        capacity = other.capacity;
        occupied = other.occupied;
        frozen = other.frozen;
        specialization = Py_XNewRef(other.specialization);

        return *this;
    }

    /* Move assignment operator. */
    BaseAllocator& operator=(BaseAllocator&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // destroy current
        Py_XDECREF(specialization);
        if (head != nullptr) {
            destroy_list();  // calls destructors
        }
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << capacity << " nodes" << std::endl;
        }

        // transfer ownership
        head = other.head;
        tail = other.tail;
        capacity = other.capacity;
        occupied = other.occupied;
        frozen = other.frozen;
        specialization = other.specialization;

        // reset other
        other.head = nullptr;
        other.tail = nullptr;
        other.capacity = 0;
        other.occupied = 0;
        other.frozen = false;
        other.specialization = nullptr;
        return *this;
    }

    /* Destroy an allocator and release its resources. */
    ~BaseAllocator() noexcept {
        Py_XDECREF(specialization);
        free(temp);
    }

    /* Enforce strict type checking for python values within the list. */
    void specialize(PyObject* spec) {
        // handle null assignment
        if (spec == nullptr) {
            if (specialization != nullptr) {
                Py_DECREF(specialization);  // release old spec
                specialization = nullptr;
            }
            return;
        }

        // early return if new spec is same as old spec
        if (specialization != nullptr) {
            int comp = PyObject_RichCompareBool(spec, specialization, Py_EQ);
            if (comp == -1) {  // comparison raised an exception
                throw util::catch_python<util::type_error>();
            } else if (comp == 1) {
                return;
            }
        }

        // check the contents of the list
        Node* curr = head;
        while (curr != nullptr) {
            if (!curr->typecheck(spec)) {
                throw util::type_error("node type does not match specialization");
            }
            curr = curr->next();
        }

        // replace old specialization
        Py_INCREF(spec);
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
        specialization = spec;
    }

    /* Get the total amount of dynamic memory allocated by this allocator. */
    inline size_t nbytes() const {
        return sizeof(Node);  // temporary node
    }

};


////////////////////
////    LIST    ////
////////////////////


/* A custom allocator that uses a dynamic array to manage memory for each node. */
template <typename NodeType>
class ListAllocator : public BaseAllocator<NodeType> {
public:
    using Node = NodeType;

private:
    using Base = BaseAllocator<Node>;
    static constexpr size_t DEFAULT_CAPACITY = 8;  // minimum array size

    Node* array;  // dynamic array of nodes
    std::pair<Node*, Node*> free_list;  // singly-linked list of open nodes

    /* When we allocate new nodes, we fill the dynamic array from left to right.
    If a node is removed from the middle of the array, then we add it to the free
    list.  The next time a node is allocated, we check the free list and reuse a
    node if possible.  Otherwise, we initialize a new node at the end of the
    occupied section.  If this causes the array to exceed its capacity, then we
    allocate a new array with twice the length and copy all nodes in the same order
    as they appear in the list. */

    /* Copy/move the nodes from this allocator into the given array. */
    template <bool move>
    std::pair<Node*, Node*> transfer(Node* other) {
        Node* new_head = nullptr;
        Node* new_tail = nullptr;

        // copy over existing nodes
        Node* curr = this->head;
        size_t idx = 0;
        while (curr != nullptr) {
            // remember next node in original list
            Node* next = curr->next();

            // initialize new node in array
            Node* other_curr = &other[idx++];
            if constexpr (move) {
                new (other_curr) Node(std::move(*curr));
            } else {
                new (other_curr) Node(*curr);
            }

            // link to previous node in array
            if (curr == this->head) {
                new_head = other_curr;
            } else {
                Node::join(new_tail, other_curr);
            }
            new_tail = other_curr;
            curr = next;
        }

        // return head/tail pointers for new list
        return std::make_pair(new_head, new_tail);
    }

    /* Allocate a new array of a given size and transfer the contents of the list. */
    void resize(size_t new_capacity) {
        // allocate new array
        Node* new_array = Base::allocate_array(new_capacity);
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << new_capacity << " nodes" << std::endl;
        }

        // move nodes into new array
        std::pair<Node*, Node*> bounds(transfer<true>(new_array));
        this->head = bounds.first;
        this->tail = bounds.second;

        // replace old array
        free(array);  // bypasses destructors
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << this->capacity << " nodes" << std::endl;
        }
        array = new_array;
        this->capacity = new_capacity;
        free_list.first = nullptr;
        free_list.second = nullptr;
    }

public:

    /* Create an allocator with an optional fixed size. */
    ListAllocator(std::optional<size_t> capacity, PyObject* specialization) :
        Base(capacity, DEFAULT_CAPACITY, specialization),
        array(Base::allocate_array(this->capacity)),
        free_list(std::make_pair(nullptr, nullptr))
    {}

    /* Copy constructor. */
    ListAllocator(const ListAllocator& other) :
        Base(other),
        array(Base::allocate_array(this->capacity)),
        free_list(std::make_pair(nullptr, nullptr))
    {
        // copy over existing nodes in correct list order (head -> tail)
        if (this->occupied != 0) {
            std::pair<Node*, Node*> bounds(other.transfer<false>(array));
            this->head = bounds.first;
            this->tail = bounds.second;
        }
    }

    /* Move constructor. */
    ListAllocator(ListAllocator&& other) noexcept :
        Base(std::move(other)),
        array(other.array),
        free_list(std::move(other.free_list))
    {
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
    }

    /* Copy assignment operator. */
    ListAllocator& operator=(const ListAllocator& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // invoke parent
        Base::operator=(other);

        // destroy array
        free_list.first = nullptr;
        free_list.second = nullptr;
        if (array != nullptr) {
            free(array);
        }

        // copy array
        array = Base::allocate_array(this->capacity);
        if (this->occupied != 0) {
            std::pair<Node*, Node*> bounds(other.transfer<false>(array));
            this->head = bounds.first;
            this->tail = bounds.second;
        } else {
            this->head = nullptr;
            this->tail = nullptr;
        }
        return *this;
    }

    /* Move assignment operator. */
    ListAllocator& operator=(ListAllocator&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // invoke parent
        Base::operator=(std::move(other));

        // destroy array
        if (array != nullptr) {
            free(array);
        }

        // transfer ownership
        array = other.array;
        free_list.first = other.free_list.first;
        free_list.second = other.free_list.second;

        // reset other
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
        return *this;
    }

    /* Destroy an allocator and release its resources. */
    ~ListAllocator() noexcept {
        if (this->head != nullptr) {
            Base::destroy_list();  // calls destructors
        }
        if (array != nullptr) {
            free(array);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << this->capacity << " nodes" << std::endl;
            }
        }
    }

    /* Construct a new node for the list. */
    template <typename... Args>
    Node* create(Args&&... args) {
        // check free list
        if (free_list.first != nullptr) {
            Node* node = free_list.first;
            Node* temp = node->next();
            try {
                Base::init_node(node, std::forward<Args>(args)...);
            } catch (...) {
                node->next(temp);  // restore free list
                throw;  // propagate
            }
            free_list.first = temp;
            if (temp == nullptr) {
                free_list.second = nullptr;
            }
            ++this->occupied;
            return node;
        }

        // check if we need to grow the array
        if (this->occupied == this->capacity) {
            if (this->frozen) {
                std::ostringstream msg;
                msg << "array cannot grow beyond size " << this->capacity;
                throw std::runtime_error(msg.str());
            }
            resize(this->capacity * 2);
        }

        // append to end of allocated section
        Node* node = &array[this->occupied++];
        Base::init_node(node, std::forward<Args>(args)...);
        return node;
    }

    /* Release a node from the list. */
    void recycle(Node* node) {
        // manually call destructor
        if constexpr (DEBUG) {
            std::cout << "    -> recycle: " << util::repr(node->value()) << std::endl;
        }
        node->~Node();

        // check if we need to shrink the array
        if (!this->frozen &&
            this->capacity != DEFAULT_CAPACITY &&
            this->occupied == this->capacity / 4
        ) {
            resize(this->capacity / 2);
        } else {
            if (free_list.first == nullptr) {
                free_list.first = node;
                free_list.second = node;
            } else {
                free_list.second->next(node);
                free_list.second = node;
            }
        }
        --this->occupied;
    }

    /* Remove all elements from the list. */
    void clear() noexcept {
        // destroy all nodes
        Base::destroy_list();

        // reset list parameters
        this->head = nullptr;
        this->tail = nullptr;
        this->occupied = 0;
        free_list.first = nullptr;
        free_list.second = nullptr;
        if (!this->frozen) {
            this->capacity = DEFAULT_CAPACITY;
            free(array);
            array = Base::allocate_array(this->capacity);
        }
    }

    /* Resize the array to store a specific number of nodes. */
    void reserve(size_t new_size) {
        // ensure new capacity is large enough to store all nodes
        if (new_size < this->occupied) {
            throw std::invalid_argument(
                "new capacity must not be smaller than current size"
            );
        }

        // handle frozen arrays
        if (this->frozen) {
            if (new_size <= this->capacity) {
                return;  // do nothing
            }
            std::ostringstream msg;
            msg << "array cannot grow beyond size " << this->capacity;
            throw std::runtime_error(msg.str());
        }

        // ensure array does not shrink below default capacity
        if (new_size <= DEFAULT_CAPACITY) {
            if (this->capacity != DEFAULT_CAPACITY) {
                resize(DEFAULT_CAPACITY);
            }
            return;
        }

        // resize to the next power of two
        size_t rounded = util::next_power_of_two(new_size);
        if (rounded != this->capacity) {
            resize(rounded);
        }
    }

    /* Consolidate the nodes within the array, arranging them in the same order as
    they appear within the list. */
    inline void consolidate() {
        resize(this->capacity);  // in-place resize
    }

    /* Check whether the referenced node is being managed by this allocator. */
    inline bool owns(Node* node) const noexcept {
        return node >= array && node < array + this->capacity;  // pointer arithmetic
    }

    /* Get the total amount of dynamic memory allocated by this allocator. */
    inline size_t nbytes() const {
        return Base::nbytes() + (this->capacity * sizeof(Node));
    }

};


//////////////////////////////
////    SET/DICTIONARY    ////
//////////////////////////////


/* A custom allocator that uses a hash table to manage memory for each node. */
template <typename NodeType>
class HashAllocator : public BaseAllocator<NodeType> {
public:
    using Node = NodeType;

private:
    using Base = BaseAllocator<NodeType>;
    using Value = typename Node::Value;
    static constexpr size_t DEFAULT_CAPACITY = 16;  // minimum array size
    static constexpr unsigned char DEFAULT_EXPONENT = 4;  // log2(DEFAULT_CAPACITY)
    static constexpr float MAX_LOAD_FACTOR = 0.5;  // grow if load factor exceeds threshold
    static constexpr float MIN_LOAD_FACTOR = 0.125;  // shrink if load factor drops below threshold
    static constexpr float MAX_TOMBSTONES = 0.125;  // clear tombstones if threshold is exceeded
    static constexpr size_t PRIMES[29] = {  // prime numbers to use for double hashing
        // HASH PRIME   // TABLE SIZE               // AI AUTOCOMPLETE
        13,             // 16 (2**4)                13
        23,             // 32 (2**5)                23
        47,             // 64 (2**6)                53
        97,             // 128 (2**7)               97
        181,            // 256 (2**8)               193
        359,            // 512 (2**9)               389
        719,            // 1024 (2**10)             769
        1439,           // 2048 (2**11)             1543
        2879,           // 4096 (2**12)             3079
        5737,           // 8192 (2**13)             6151
        11471,          // 16384 (2**14)            12289
        22943,          // 32768 (2**15)            24593
        45887,          // 65536 (2**16)            49157
        91753,          // 131072 (2**17)           98317
        183503,         // 262144 (2**18)           196613
        367007,         // 524288 (2**19)           393241
        734017,         // 1048576 (2**20)          786433
        1468079,        // 2097152 (2**21)          1572869
        2936023,        // 4194304 (2**22)          3145739
        5872033,        // 8388608 (2**23)          6291469
        11744063,       // 16777216 (2**24)         12582917
        23488103,       // 33554432 (2**25)         25165843
        46976221,       // 67108864 (2**26)         50331653
        93952427,       // 134217728 (2**27)        100663319
        187904861,      // 268435456 (2**28)        201326611
        375809639,      // 536870912 (2**29)        402653189
        751619321,      // 1073741824 (2**30)       805306457
        1503238603,     // 2147483648 (2**31)       1610612741
        3006477127,     // 4294967296 (2**32)       3221225473
        // NOTE: HASH PRIME is the first prime number larger than 0.7 * TABLE_SIZE
    };

    Node* array;  // dynamic array of nodes
    uint32_t* flags;  // bit array indicating whether a node is occupied or deleted
    size_t tombstones;  // number of nodes marked for deletion
    size_t prime;  // prime number used for double hashing
    unsigned char exponent;  // log2(capacity) - log2(DEFAULT_CAPACITY)

    /* Adjust the input to a constructor's `capacity` argument to account for double
    hashing, maximum load factor, and strict power of two table sizes. */
    inline static std::optional<size_t> adjust_size(std::optional<size_t> capacity) {
        // ignore null
        if (!capacity.has_value()) {
            return std::nullopt;
        }

        // check if capacity is a power of two
        size_t value = capacity.value();
        if (!util::is_power_of_two(value)) {
            std::ostringstream msg;
            msg << "capacity must be a power of two, got " << value;
            throw std::invalid_argument(msg.str());
        }

        // double capacity to ensure load factor is at most 0.5
        return std::make_optional(value * 2);
    }

    /* Allocate an auxiliary bit array indicating whether a given index is currently
    occupied or is marked as a tombstone. */
    inline static uint32_t* allocate_flags(const size_t capacity) {
        size_t num_flags = (capacity - 1) / 16;
        uint32_t* result = static_cast<uint32_t*>(calloc(num_flags, sizeof(uint32_t)));
        if (result == nullptr) {
            throw std::bad_alloc();
        }
        return result;
    }

    /* Copy/move the nodes from this allocator into the given array. */
    template <bool move>
    std::pair<Node*, Node*> transfer(Node* other, uint32_t* other_flags) {
        Node* new_head = nullptr;
        Node* new_tail = nullptr;

        // move nodes into new array
        Node* curr = this->head;
        std::equal_to<Value> eq;
        while (curr != nullptr) {
            // rehash node
            size_t hash;
            if constexpr (NodeTraits<Node>::has_hash) {
                hash = curr->hash();
            } else {
                hash = std::hash<Value>()(curr->value());
            }

            // get index in new array
            size_t idx = hash % this->capacity;
            size_t step = (hash % prime) | 1;
            Node* lookup = &other[idx];
            size_t div = idx / 16;
            size_t mod = idx % 16;
            uint32_t bits = other_flags[div] >> mod;

            // handle collisions (NOTE: no need to check for tombstones)
            while (bits & 0b10) {  // node is constructed
                idx = (idx + step) % this->capacity;
                lookup = &other[idx];
                div = idx / 16;
                mod = idx % 16;
                bits = other_flags[div] >> mod;
            }

            // transfer node into new array
            Node* next = curr->next();
            if constexpr (move) {
                new (lookup) Node(std::move(*curr));
            } else {
                new (lookup) Node(*curr);
            }
            other_flags[div] |= 0b10 << mod;  // mark as constructed

            // update head/tail pointers
            if (curr == this->head) {
                new_head = lookup;
            } else {
                Node::join(new_tail, lookup);
            }
            new_tail = lookup;
            curr = next;
        }

        // return head/tail pointers for new list
        return std::make_pair(new_head, new_tail);
    }

    /* Allocate a new array of a given size and transfer the contents of the list. */
    void resize(const unsigned char new_exponent) {
        // allocate new array
        size_t new_capacity = 1 << (new_exponent + DEFAULT_EXPONENT);
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << new_capacity << " nodes" << std::endl;
        }
        Node* new_array = Base::allocate_array(new_capacity);
        uint32_t* new_flags = allocate_flags(new_capacity);

        // update table parameters
        size_t old_capacity = this->capacity;
        this->capacity = new_capacity;
        tombstones = 0;
        prime = PRIMES[new_exponent];
        exponent = new_exponent;

        // move nodes into new array
        std::pair<Node*, Node*> bounds(transfer<true>(new_array, new_flags));
        this->head = bounds.first;
        this->tail = bounds.second;

        // replace arrays
        free(array);
        free(flags);
        array = new_array;
        flags = new_flags;
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << old_capacity << " nodes" << std::endl;
        }
    }

    /* Look up a value in the hash table by providing an explicit hash. */
    Node* _search(const size_t hash, const Value& value) const {
        // get index and step for double hashing
        size_t step = (hash % prime) | 1;
        size_t idx = hash % this->capacity;
        Node& lookup = array[idx];
        uint32_t& bits = flags[idx / 16];
        unsigned char flag = (bits >> (idx % 16)) & 0b11;

        // NOTE: first bit flag indicates whether bucket is occupied by a valid node.
        // Second indicates whether it is a tombstone.  Both are mutually exclusive.

        // handle collisions
        std::equal_to<Value> eq;
        while (flag > 0b00) {
            if (flag == 0b10 && eq(lookup->value(), value)) return &lookup;

            // advance to next slot
            idx = (idx + step) % this->capacity;
            lookup = array[idx];
            bits = flags[idx / 16];
            flag = (bits >> (idx % 16)) & 0b11;
        }

        // value not found
        return nullptr;
    }

public:

    /* Create an allocator with an optional fixed size. */
    HashAllocator(std::optional<size_t> capacity, PyObject* specialization) :
        Base(adjust_size(capacity), DEFAULT_CAPACITY, specialization),
        array(Base::allocate_array(this->capacity)),
        flags(allocate_flags(this->capacity)),
        tombstones(0),
        prime(PRIMES[0]),
        exponent(0)
    {}

    /* Copy constructor. */
    HashAllocator(const HashAllocator& other) :
        Base(other),
        array(Base::allocate_array(this->capacity)),
        flags(allocate_flags(this->capacity)),
        tombstones(other.tombstones),
        prime(other.prime),
        exponent(other.exponent)
    {
        // copy over existing nodes in correct list order (head -> tail)
        if (this->occupied != 0) {
            std::pair<Node*, Node*> bounds(other.transfer<false>(array));
            this->head = bounds.first;
            this->tail = bounds.second;
        }
    }

    /* Move constructor. */
    HashAllocator(HashAllocator&& other) noexcept :
        Base(std::move(other)),
        array(other.array),
        flags(other.flags),
        tombstones(other.tombstones),
        prime(other.prime),
        exponent(other.exponent)
    {
        other.array = nullptr;
        other.flags = nullptr;
        other.tombstones = 0;
        other.prime = 0;
        other.exponent = 0;
    }

    /* Copy assignment operator. */
    HashAllocator& operator=(const HashAllocator& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // invoke parent
        Base::operator=(other);

        // destroy array
        if (array != nullptr) {
            free(array);
            free(flags);
        }

        // copy array
        array = Base::allocate_array(this->capacity);
        flags = allocate_flags(this->capacity);
        if (this->occupied != 0) {
            std::pair<Node*, Node*> bounds(other.transfer<false>(array));
            this->head = bounds.first;
            this->tail = bounds.second;
        } else {
            this->head = nullptr;
            this->tail = nullptr;
        }
        return *this;
    }

    /* Move assignment operator. */
    HashAllocator& operator=(HashAllocator&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // invoke parent
        Base::operator=(std::move(other));

        // destroy array
        if (array != nullptr) {
            free(array);
            free(flags);
        }

        // transfer ownership
        array = other.array;
        flags = other.flags;
        tombstones = other.tombstones;
        prime = other.prime;
        exponent = other.exponent;

        // reset other
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
        return *this;
    }

    /* Destroy an allocator and release its resources. */
    ~HashAllocator() noexcept {
        if (this->head != nullptr) {
            Base::destroy_list();  // calls destructors
        }
        if (array != nullptr) {
            free(array);
            free(flags);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << this->capacity << " nodes" << std::endl;
            }
        }
    }

    /* Construct a new node for the list. */
    template <typename... Args>
    Node* create(Args&&... args) {
        // allocate into temporary node
        Base::init_node(this->temp, std::forward<Args>(args)...);

        // check if we need to grow the array
        size_t total_load = this->occupied + tombstones;
        if (total_load >= this->capacity / 2) {
            if (this->frozen) {
                if (this->occupied == this->capacity / 2) {
                    std::ostringstream msg;
                    msg << "array cannot grow beyond size " << this->capacity;
                    throw std::runtime_error(msg.str());
                } else if (tombstones > this->capacity / 16) {
                    resize(exponent);  // clear tombstones
                }
                // NOTE: allow a small amount of hysteresis (load factor up to 0.5625)
                // to avoid pessimistic rehashing
            } else {
                resize(exponent + 1);  // grow array
            }
        }

        // search for node within hash table
        size_t hash = this->temp->hash();
        size_t step = (hash % prime) | 1;  // double hashing
        size_t idx = hash % this->capacity;
        Node& lookup = array[idx];
        size_t div = idx / 16;
        size_t mod = idx % 16;
        uint32_t& bits = flags[div];
        unsigned char flag = (bits >> mod) & 0b11;

        // NOTE: first bit flag indicates whether bucket is occupied by a valid node.
        // Second indicates whether it is a tombstone.  Both are mutually exclusive.

        // handle collisions, replacing tombstones if they are encountered
        while (flag == 0b10) {
            if (lookup.eq(this->temp->value())) {
                this->temp->~Node();  // in-place destructor
                std::ostringstream msg;
                msg << "duplicate key: " << util::repr(this->temp->value());
                throw std::invalid_argument(msg.str());
            }

            // advance to next index
            idx = (idx + step) % this->capacity;
            lookup = array[idx];
            div = idx / 16;
            mod = idx % 16;
            bits = flags[div];
            flag = (bits >> mod) & 0b11;
        }

        // move temp into empty bucket/tombstone
        new (&lookup) Node(std::move(*this->temp));
        bits |= (0b10 << mod);  // mark constructed flag
        if (flag == 0b01) {
            flag &= 0b10 << mod;  // clear tombstone flag
            --tombstones;
        }
        ++this->occupied;
        return &lookup;
    }

    /* Release a node from the list. */
    void recycle(Node* node) {
        if constexpr (DEBUG) {
            std::cout << "    -> recycle: " << util::repr(node->value()) << std::endl;
        }

        // look up node in hash table
        size_t hash = node->hash();
        size_t step = (hash % prime) | 1;  // double hashing
        size_t idx = hash % this->capacity;
        Node& lookup = array[idx];
        size_t div = idx / 16;
        size_t mod = idx % 16;
        uint32_t& bits = flags[div];
        unsigned char flag = (bits >> mod) & 0b11;

        // NOTE: first bit flag indicates whether bucket is occupied by a valid node.
        // Second indicates whether it is a tombstone.  Both are mutually exclusive.

        // handle collisions
        while (flag > 0b00) {
            if (flag == 0b10 && lookup.eq(node->value())) {
                // mark as tombstone
                lookup.~Node();  // in-place destructor
                bits &= (0b01 << mod);  // clear constructed flag
                bits |= (0b01 << mod);  // set tombstone flag
                ++tombstones;
                --this->occupied;

                // check whether to shrink array or clear out tombstones
                size_t threshold = this->capacity / 8;
                if (!this->frozen &&
                    this->capacity != DEFAULT_CAPACITY &&
                    this->occupied < threshold  // load factor < 0.125
                ) {
                    resize(exponent - 1);
                } else if (tombstones > threshold) {  // tombstones > 0.125
                    resize(exponent);
                }
                return;
            }

            // advance to next index
            idx = (idx + step) % this->capacity;
            lookup = array[idx];
            div = idx / 16;
            mod = idx % 16;
            bits = flags[div];
            flag = (bits >> mod) & 0b11;
        }

        // node not found
        std::ostringstream msg;
        msg << "key not found: " << util::repr(node->value());
        throw std::invalid_argument(msg.str());
    }

    /* Remove all elements from the list. */
    void clear() noexcept {
        // destroy all nodes
        Base::destroy_list();

        // reset list parameters
        this->head = nullptr;
        this->tail = nullptr;
        this->occupied = 0;
        tombstones = 0;
        prime = PRIMES[0];
        exponent = 0;
        if (!this->frozen) {
            this->capacity = DEFAULT_CAPACITY;
            free(array);
            free(flags);
            array = Base::allocate_array(this->capacity);
            flags = allocate_flags(this->capacity);
        }
    }

    /* Resize the array to store a specific number of nodes. */
    void reserve(size_t new_size) {
        // ensure new capacity is large enough to store all nodes
        if (new_size < this->occupied) {
            throw std::invalid_argument(
                "new capacity must not be smaller than current size"
            );
        }

        // handle frozen arrays
        if (this->frozen) {
            if (new_size <= this->capacity / 2) {
                return;  // do nothing
            }
            std::ostringstream msg;
            msg << "array cannot grow beyond size " << this->capacity;
            throw std::runtime_error(msg.str());
        }

        // ensure array does not shrink below default capacity
        if (new_size <= DEFAULT_CAPACITY / 2) {  // DEFAULT_CAPACITY * max load factor
            if (this->capacity != DEFAULT_CAPACITY) {
                resize(0);
            }
            return;
        }

        // resize to the next power of two
        size_t rounded = util::next_power_of_two(new_size);
        if (rounded != this->capacity) {
            // get exponent of new capacity
            unsigned char new_exponent = 0;
            while (rounded >>= 1) ++new_exponent;
            new_exponent += 1;  // rounded / max load factor
            resize(new_exponent - DEFAULT_EXPONENT);
        }
    }

    /* Rehash the nodes within the array, removing tombstones. */
    inline void defragment() {
        resize(exponent);  // in-place resize
    }

    /* Check whether the referenced node is being managed by this allocator. */
    inline bool owns(Node* node) const noexcept {
        return node >= array && node < array + this->capacity;  // pointer arithmetic
    }

    /* Search for a node by reusing a hash from another node. */
    template <typename Node>
    inline Node* search(Node* node) const {
        if constexpr (NodeTraits<Node>::has_hash) {
            return _search(node->hash(), node->value());
        } else {
            size_t hash = std::hash<Value>{}(node->value());
            return _search(hash, node->value());
        }
    }

    /* Search for a node by its value directly. */
    inline Node* search(Value& key) const {
        return _search(std::hash<Value>{}(key), key);
    }

    /* Get the total amount of dynamic memory being managed by this allocator. */
    inline size_t nbytes() const {
        return (
            Base::nbytes() +
            this->capacity * sizeof(Node) +
            this->capacity / 4  // flags take 2 bits per node => 4 nodes per byte
        );
    }

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALLOCATE_H
