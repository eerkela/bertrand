
// include guard prevents multiple inclusion
#ifndef VIEW_H
#define VIEW_H

#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <limits>  // for std::numeric_limits
#include <Python.h>  // for CPython API
#include "node.h"  // for Hashed<T>, Mapped<T>



/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* MAX_SIZE_T is used to signal errors in indexing operations where NULL would
not be a valid return value, and 0 is likely to be valid output. */
const size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();
const std::pair<size_t, size_t> MAX_SIZE_T_PAIR = (
    std::make_pair(MAX_SIZE_T, MAX_SIZE_T)
);


/* Some Views use hash tables for fast access to each element. */
const size_t INITIAL_TABLE_CAPACITY = 16;  // initial size of hash table
const float MAX_LOAD_FACTOR = 0.7;  // grow if load factor exceeds threshold
const float MIN_LOAD_FACTOR = 0.2;  // shrink if load factor drops below threshold
const float MAX_TOMBSTONES = 0.2;  // clear tombstones if threshold is exceeded
const size_t PRIMES[29] = {  // prime numbers to use for double hashing
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


/////////////////////////////
////    INDEX HELPERS    ////
/////////////////////////////


// NOTE: We may not always be able to efficiently iterate through a linked list
// in reverse order.  As a result, we can't always guarantee that we iterate
// over a slice in the same direction as the step size would normally indicate.
// For instance, if we have a singly-linked list and we want to iterate over a
// slice with a negative step size, we'll have to start from the head and
// traverse over it backwards.  We can compensate for this by manually
// reversing the slice again as we extract each node, which counteracts the
// previous effect and produces the intended result.

// In the case of doubly-linked lists, we can use this trick to minimize total
// iterations and avoid backtracking.  Since we're free to start from either
// end of the list, we always choose whichever one that is closer to a slice
// boundary, and then reflect the results to match the intended output.

// This changes the way we have to approach our slice indices.  Python slices
// are normally asymmetric and half-open at the stop index, but this presents a
// problem for our optimization strategy.  Because we might be iterating from
// the stop index to the start index rather than the other way around, we need
// to be able to treat the slice symmetrically in both directions.  To
// facilitate this, we convert the slice into a closed interval by rounding the
// stop index to the nearest included step.  This means that both the start and
// stop indices are always included in the slice, allowing us to iterate
// equally in either direction.


/* A modulo operator (%) that matches Python's behavior with respect to
negative numbers. */
template <typename T>
inline T py_modulo(T a, T b) {
    // NOTE: Python's `%` operator is defined such that the result has the same
    // sign as the divisor (b).  This differs from C, where the result has the
    // same sign as the dividend (a).  This function uses the Python version.
    return (a % b + b) % b;
}


/* Adjust the stop index in a slice to make it closed on both ends. */
template <typename T>
inline T closed_interval(T start, T stop, T step) {
    T remainder = py_modulo((stop - start), step);
    if (remainder == 0) {
        return stop - step; // decrement stop by 1 full step
    }
    return stop - remainder;  // decrement stop to nearest multiple of step
}


/* Allow Python-style negative indexing with wraparound and boundschecking. */
template <typename T>
size_t normalize_index(T index, size_t size, bool truncate) {
    bool index_lt_zero = index < 0;

    // wraparound negative indices
    if (index_lt_zero) {
        index += size;
        index_lt_zero = index < 0;
    }

    // boundscheck
    if (index_lt_zero || index >= static_cast<T>(size)) {
        if (truncate) {
            if (index_lt_zero) {
                return 0;
            }
            return size - 1;
        }
        PyErr_SetString(PyExc_IndexError, "list index out of range");
        return MAX_SIZE_T;
    }

    // return as size_t
    return (size_t)index;
}


/* A specialized version of normalize_index() for use with Python integers. */
template <>
size_t normalize_index(PyObject* index, size_t size, bool truncate) {
    // NOTE: this is the same algorithm as _normalize_index() except that it
    // accepts Python integers and handles the associated reference counting.
    if (!PyLong_Check(index)) {
        PyErr_SetString(PyExc_TypeError, "Index must be a Python integer");
        return MAX_SIZE_T;
    }

    // comparisons are kept at the python level until we're ready to return
    PyObject* py_zero = PyLong_FromSize_t(0);  // new reference
    PyObject* py_size = PyLong_FromSize_t(size);  // new reference
    int index_lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);

    // wraparound negative indices
    bool release_index = false;
    if (index_lt_zero) {
        index = PyNumber_Add(index, py_size);  // new reference
        index_lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);
        release_index = true;  // remember to free index later
    }

    // boundscheck
    if (index_lt_zero || PyObject_RichCompareBool(index, py_size, Py_GE)) {
        // clean up references
        Py_DECREF(py_zero);
        Py_DECREF(py_size);
        if (release_index) {
            Py_DECREF(index);
        }

        // apply truncation if directed
        if (truncate) {
            if (index_lt_zero) {
                return 0;
            }
            return size - 1;
        }

        // raise IndexError
        PyErr_SetString(PyExc_IndexError, "list index out of range");
        return MAX_SIZE_T;
    }

    // value is good - convert to size_t
    size_t result = PyLong_AsSize_t(index);

    // clean up references
    Py_DECREF(py_zero);
    Py_DECREF(py_size);
    if (release_index) {
        Py_DECREF(index);
    }

    return result;
}


/* Create a bounded interval over a subset of a list, for use in index(), count(),
etc. */
template <typename T>
std::pair<size_t, size_t> normalize_bounds(
    T start,
    T stop,
    size_t size,
    bool truncate
) {
    // pass both start and stop through normalize_index()
    size_t norm_start = normalize_index(start, size, truncate);
    size_t norm_stop = normalize_index(stop, size, truncate);

    // check for errors
    if ((norm_start == MAX_SIZE_T || norm_stop == MAX_SIZE_T) && PyErr_Occurred()) {
        return std::make_pair(MAX_SIZE_T, MAX_SIZE_T);  // propagate error
    }

    // check bounds are in order
    if (norm_start > norm_stop) {
        PyErr_SetString(
            PyExc_ValueError,
            "start index must be less than or equal to stop index"
        );
        return std::make_pair(MAX_SIZE_T, MAX_SIZE_T);
    }

    // return normalized bounds
    return std::make_pair(norm_start, norm_stop);
}


/* Get the direction in which to traverse a slice according to the structure of
the list. */
template <template <typename> class ViewType, typename NodeType>
std::pair<size_t, size_t> normalize_slice(
    ViewType<NodeType>* view,
    Py_ssize_t start,
    Py_ssize_t stop,
    Py_ssize_t step
) {
    // NOTE: the input to this function is assumed to be the output of
    // slice.indices(), which handles negative indices and 0 step size step
    // size.  Its behavior is undefined if these conditions are not met.
    using Node = typename ViewType<NodeType>::Node;

    // convert from half-open to closed interval
    stop = closed_interval(start, stop, step);

    // check if slice is not a valid interval
    if ((step > 0 && start > stop) || (step < 0 && start < stop)) {
        // NOTE: even though the inputs are never negative, the result of
        // closed_interval() may cause `stop` to run off the end of the list.
        // This branch catches that case, in addition to unbounded slices.
        PyErr_SetString(PyExc_ValueError, "invalid slice");
        return std::make_pair(MAX_SIZE_T, MAX_SIZE_T);
    }

    // convert to size_t
    size_t norm_start = (size_t)start;
    size_t norm_stop = (size_t)stop;
    size_t begin, end;

    // get begin and end indices
    if constexpr (is_doubly_linked<Node>::value) {
        // NOTE: if the list is doubly-linked, then we can iterate from either
        // direction.  We therefore choose the direction that's closest to its
        // respective slice boundary.
        if (
            (step > 0 && norm_start <= view->size - norm_stop) ||
            (step < 0 && view->size - norm_start <= norm_stop)
        ) {  // iterate normally
            begin = norm_start;
            end = norm_stop;
        } else {  // reverse
            begin = norm_stop;
            end = norm_start;
        }
    } else {
        // NOTE: if the list is singly-linked, then we can only iterate in one
        // direction, even when the step size is negative.
        if (step > 0) {  // iterate normally
            begin = norm_start;
            end = norm_stop;
        } else {  // reverse
            begin = norm_stop;
            end = norm_start;
        }
    }

    // NOTE: comparing the begin and end indices reveals the direction of
    // traversal for the slice.  If begin < end, then we iterate from the head
    // of the list.  If begin > end, then we iterate from the tail.
    return std::make_pair(begin, end);
}


/////////////////////
////    TABLE    ////
/////////////////////


/* HashTables allow O(1) lookup for nodes within SetViews and DictViews. */
template <typename Node>
class HashTable {
private:
    Node** table;               // array of pointers to nodes
    Node* tombstone;            // sentinel value for deleted nodes
    size_t capacity;            // size of table
    size_t occupied;            // number of occupied slots (incl. tombstones)
    size_t tombstones;          // number of tombstones
    unsigned char exponent;     // log2(capacity) - log2(INITIAL_TABLE_SIZE)
    size_t prime;               // prime number used for double hashing

    /* Resize the hash table and replace its contents. */
    void resize(unsigned char new_exponent) {
        Node** old_table = table;
        size_t old_capacity = capacity;
        size_t new_capacity = 1 << new_exponent;

        if constexpr (DEBUG) {
            printf("    -> malloc: HashTable(%lu)\n", new_capacity);
        }

        // allocate new table
        table = static_cast<Node**>(calloc(new_capacity, sizeof(Node*)));
        if (table == nullptr) {
            PyErr_NoMemory();
            return;  // propagate error
        }

        // update table parameters
        capacity = new_capacity;
        exponent = new_exponent;
        prime = PRIMES[new_exponent];

        size_t new_index, step;
        Node* lookup;

        // rehash old table and clear tombstones
        for (size_t old_index = 0; old_index < old_capacity; old_index++) {
            lookup = old_table[old_index];
            if (lookup != nullptr && lookup != tombstone) {  // insert into new table
                // NOTE: we don't need to check for errors because we already
                // know that the old table is valid.
                new_index = lookup->hash % new_capacity;
                step = prime - (lookup->hash % prime);
                while (table[new_index] != nullptr) {
                    new_index = (new_index + step) % new_capacity;
                }
                table[new_index] = lookup;
            }
        }

        // reset tombstone count
        occupied -= tombstones;
        tombstones = 0;

        // free old table
        if constexpr (DEBUG) {
            printf("    -> free: HashTable(%lu)\n", old_capacity);
        }
        free(old_table);
    }

public:

    /* Disabled copy/move constructors.  These are dangerous because we're
    managing memory manually. */
    HashTable(const HashTable& other) = delete;         // copy constructor
    HashTable& operator=(const HashTable&) = delete;    // copy assignment
    HashTable(HashTable&&) = delete;                    // move constructor
    HashTable& operator=(HashTable&&) = delete;         // move assignment

    /* Construct an empty HashTable. */
    HashTable() {
        if constexpr (DEBUG) {
            printf("    -> malloc: HashTable(%lu)\n", INITIAL_TABLE_CAPACITY);
        }

        // initialize hash table
        table = static_cast<Node**>(calloc(INITIAL_TABLE_CAPACITY, sizeof(Node*)));
        if (table == nullptr) {
            throw std::bad_alloc();  // we have to use C++ exceptions here
        }

        // initialize tombstone
        tombstone = static_cast<Node*>(malloc(sizeof(Node)));
        if (tombstone == nullptr) {
            free(table);  // clean up staged table
            throw std::bad_alloc();
        }

        // initialize table parameters
        capacity = INITIAL_TABLE_CAPACITY;
        occupied = 0;
        tombstones = 0;
        exponent = 0;
        prime = PRIMES[exponent];
    }

    /* Destructor.*/
    ~HashTable() {
        if constexpr (DEBUG) {
            printf("    -> free: HashTable(%lu)\n", capacity);
        }
        free(table);
        free(tombstone);
    }

    /* Add a node to the hash map for direct access. */
    void remember(Node* node) {
        // resize if necessary
        if (occupied > capacity * MAX_LOAD_FACTOR) {
            resize(exponent + 1);
            if (PyErr_Occurred()) {  // error occurred during resize()
                return;
            }
        }

        // get index and step for double hashing
        size_t index = node->hash % capacity;
        size_t step = prime - (node->hash % prime);
        Node* lookup = table[index];
        int comp;

        // search table
        while (lookup != nullptr) {
            if (lookup != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(lookup->value, node->value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return;
                } else if (comp == 1) {  // value already present
                    PyErr_SetString(PyExc_ValueError, "Value already present");
                    return;
                }
            }

            // advance to next slot
            index = (index + step) % capacity;
            lookup = table[index];
        }

        // insert value
        table[index] = node;
        occupied++;
    }

    /* Remove a node from the hash map. */
    void forget(Node* node) {
        // get index and step for double hashing
        size_t index = node->hash % capacity;
        size_t step = prime - (node->hash % prime);
        Node* lookup = table[index];
        int comp;
        size_t n = occupied - tombstones;

        // search table
        while (lookup != nullptr) {
            if (lookup != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(lookup->value, node->value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return;
                } else if (comp == 1) {  // value found
                    table[index] = tombstone;
                    tombstones++;
                    n--;
                    if (exponent > 0 && n < capacity * MIN_LOAD_FACTOR) {
                        resize(exponent - 1);
                    } else if (tombstones > capacity * MAX_TOMBSTONES) {
                        clear_tombstones();
                    }
                    return;
                }
            }

            // advance to next slot
            index = (index + step) % capacity;
            lookup = table[index];
        }

        // value not found
        PyErr_Format(PyExc_ValueError, "Value not found: %R", node->value);
    }

    /* Clear the hash table and reset it to its initial state. */
    void clear() {
        if constexpr (DEBUG) {
            printf("    -> free: HashTable(%lu)\n", capacity);
            printf("    -> malloc: HashTable(%lu)\n", INITIAL_TABLE_CAPACITY);
        }

        // free old table
        free(table);

        // allocate new table
        table = static_cast<Node**>(calloc(INITIAL_TABLE_CAPACITY, sizeof(Node*)));
        if (table == nullptr) {  // this should never happen, but just in case
            PyErr_NoMemory();
            return;  // propagate error
        }

        // reset table parameters
        capacity = INITIAL_TABLE_CAPACITY;
        occupied = 0;
        tombstones = 0;
        exponent = 0;
        prime = PRIMES[exponent];
    }

    /* Search for a node in the hash map by value. */
    Node* search(PyObject* value) const {
        // CPython API equivalent of hash(value)
        Py_hash_t hash = PyObject_Hash(value);
        if (hash == -1 && PyErr_Occurred()) {  // error occurred during hash()
            return nullptr;
        }

        // get index and step for double hashing
        size_t index = hash % capacity;
        size_t step = prime - (hash % prime);
        Node* lookup = table[index];
        int comp;

        // search table
        while (lookup != nullptr) {
            if (lookup != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(lookup->value, value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return nullptr;
                } else if (comp == 1) {  // value found
                    return lookup;
                }
            }

            // advance to next slot
            index = (index + step) % capacity;
            lookup = table[index];
        }

        // value not found
        return nullptr;
    }

    /* Search for a node directly. */
    Node* search(Node* value) const {
        // reuse the node's pre-computed hash
        size_t index = value->hash % capacity;
        size_t step = prime - (value->hash % prime);
        Node* lookup = table[index];
        int comp;

        // search table
        while (lookup != nullptr) {
            if (lookup != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(lookup->value, value->value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return nullptr;
                } else if (comp == 1) {  // value found
                    return lookup;
                }
            }

            // advance to next slot
            index = (index + step) % capacity;
            lookup = table[index];
        }

        // value was not found
        return nullptr;
    }

    /* Clear tombstones from the hash table. */
    void clear_tombstones() {
        Node** old_table = table;

        if constexpr (DEBUG) {
            printf("    -> malloc: HashTable(%lu)\n", capacity);
        }

        // allocate new hash table
        table = static_cast<Node**>(calloc(capacity, sizeof(Node*)));
        if (table == nullptr) {
            PyErr_NoMemory();
            return;  // propagate error
        }

        size_t new_index, step;
        Node* lookup;

        // rehash old table and remove tombstones
        for (size_t old_index = 0; old_index < capacity; old_index++) {
            lookup = old_table[old_index];
            if (lookup != nullptr && lookup != tombstone) {
                // NOTE: we don't need to check for errors because we already
                // know that the old table is valid.
                new_index = lookup->hash % capacity;
                step = prime - (lookup->hash % prime);
                while (table[new_index] != nullptr) {
                    new_index = (new_index + step) % capacity;
                }
                table[new_index] = lookup;
            }
        }

        // reset tombstone count
        occupied -= tombstones;
        tombstones = 0;

        // free old table
        if constexpr (DEBUG) {
            printf("    -> free: HashTable(%lu)\n", capacity);
        }
        free(old_table);
    }

    /*Get the total amount of memory consumed by the hash table.*/
    inline size_t nbytes() const {
        return sizeof(HashTable<Node>);
    }

};


/////////////////////
////    VIEWS    ////
/////////////////////


template <typename NodeType>
class ListView {
public:
    using Node = NodeType;
    Node* head;
    Node* tail;
    size_t size;

    /* Disabled copy/move constructors.  These are dangerous because we're
    manually managing memory for each node. */
    ListView(const ListView& other) = delete;       // copy constructor
    ListView& operator=(const ListView&) = delete;  // copy assignment
    ListView(ListView&&) = delete;                  // move constructor
    ListView& operator=(ListView&&) = delete;       // move assignment

    /* Construct an empty ListView. */
    ListView() {
        head = nullptr;
        tail = nullptr;
        size = 0;
        specialization = nullptr;
    }

    /* Construct a ListView from an input iterable. */
    ListView(PyObject* iterable, bool reverse = false, PyObject* spec = nullptr) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == nullptr) {  // TypeError()
            throw std::invalid_argument("Value is not iterable");
        }

        // init empty ListView
        head = nullptr;
        tail = nullptr;
        size = 0;
        specialization = spec;
        if (spec != nullptr) {
            Py_INCREF(spec);  // hold reference to specialization
        }

        // unpack iterator into ListView
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == nullptr) { // end of iterator or error
                if (PyErr_Occurred()) {
                    Py_DECREF(iterator);
                    Allocater<Node>::discard_list(head);
                    throw std::runtime_error("could not get item from iterator");
                }
                break;
            }

            // allocate a new node and link it to the list
            stage(item, reverse);
            if (PyErr_Occurred()) {  // error during stage()
                Py_DECREF(iterator);
                Allocater<Node>::discard_list(head);
                throw std::runtime_error("could not stage item");
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);
    }

    /* Destroy a ListView and free all its nodes. */
    ~ListView() {
        // NOTE: head, tail, size, and queue are automatically destroyed
        Allocater<Node>::discard_list(head);
        clear_freelist();
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
    }

    /* Construct a new node for the list. */
    template <typename... Args>
    inline Node* node(PyObject* value, Args... args) const {
        // variadic dispatch to Node::init()
        Node* result = Allocater<Node>::create(freelist, value, args...);
        if (specialization != nullptr && result != nullptr) {
            if (!Node::typecheck(result, specialization)) {
                recycle(result);  // clean up allocated node
                return nullptr;  // propagate TypeError()
            }
        }

        return result;
    }

    /* Release a node, pushing it into the freelist. */
    inline void recycle(Node* node) const {
        Allocater<Node>::recycle(freelist, node);
    }

    /* Copy a node in the list. */
    inline Node* copy(Node* node) const {
        return Allocater<Node>::copy(freelist, node);
    }

    /* Make a shallow copy of the entire list. */
    inline ListView<NodeType>* copy() const {
        ListView<NodeType>* copied = new ListView<NodeType>();
        Node* old_node = head;
        Node* new_node = nullptr;
        Node* new_prev = nullptr;

        // copy each node in list
        while (old_node != nullptr) {
            new_node = copy(old_node);  // copy node
            if (new_node == nullptr) {  // error during copy()
                delete copied;  // discard staged list
                return nullptr;
            }

            // link to tail of copied list
            copied->link(new_prev, new_node, nullptr);

            // advance to next node
            new_prev = new_node;
            old_node = static_cast<Node*>(old_node->next);
        }

        // return copied list
        return copied;
    }

    /* Clear the list. */
    inline void clear() {
        Node* curr = head;  // store temporary reference to head

        // reset list parameters
        head = nullptr;
        tail = nullptr;
        size = 0;

        // recycle all nodes, filling up the freelist
        Allocater<Node>::recycle_list(freelist, curr);
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(Node* prev, Node* curr, Node* next) {
        // delegate to node-specific link() helper
        Node::link(prev, curr, next);

        // update list parameters
        size++;
        if (prev == nullptr) {
            head = curr;
        }
        if (next == nullptr) {
            tail = curr;
        }
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        // delegate to node-specific unlink() helper
        Node::unlink(prev, curr, next);

        // update list parameters
        size--;
        if (prev == nullptr) {
            head = next;
        }
        if (next == nullptr) {
            tail = prev;
        }
    }

    /* Enforce strict type checking for elements of this list. */
    void specialize(PyObject* spec) {
        // check the contents of the list
        if (spec != nullptr) {
            Node* curr = head;
            for (size_t i = 0; i < size; i++) {
                if (!Node::typecheck(curr, spec)) {
                    return;  // propagate TypeError()
                }
                curr = static_cast<Node*>(curr->next);
            }
            Py_INCREF(spec);
        }

        // replace old specialization
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
        specialization = spec;
    }

    /* Get the type specialization for elements of this list. */
    inline PyObject* get_specialization() const {
        if (specialization != nullptr) {
            Py_INCREF(specialization);
        }
        return specialization;  // return a new reference or NULL
    }

    /* Clear the internal freelist to remove dead nodes. */
    inline void clear_freelist() {
        Allocater<Node>::discard_freelist(freelist);
    }

    /* Get the total memory consumed by the list (in bytes). */
    inline size_t nbytes() const {
        size_t total = sizeof(ListView<NodeType>);  // ListView object
        total += size * sizeof(Node); // nodes
        total += sizeof(freelist);  // freelist queue
        total += freelist.size() * (sizeof(Node) + sizeof(Node*));  // freelist
        return total;
    }

private:
    PyObject* specialization;  // specialized type for elements of this list
    mutable std::queue<Node*> freelist;

    /* Allocate a new node for the item and append it to the list, discarding
    it in the event of an error. */
    inline void stage(PyObject* item, bool reverse) {
        // allocate a new node
        Node* curr = node(item);
        if (curr == nullptr) {  // error during node initialization
            return;
        }

        // link the node to the staged list
        // NOTE: this will never cause an error for ListViews, so we can can
        // omit the error-handling logic.
        if (reverse) {
            link(nullptr, curr, head);
        } else {
            link(tail, curr, nullptr);
        }
    }

};


template <typename NodeType>
class SetView {
public:
    /* A node decorator that computes the hash of the underlying PyObject* and
    caches it alongside the node's original fields. */
    struct Node : public NodeType {
        Py_hash_t hash;

        /* Initialize a newly-allocated node. */
        inline static Node* init(Node* node, PyObject* value) {
            node = static_cast<Node*>(NodeType::init(node, value));
            if (node == nullptr) {  // Error during decorated init()
                return nullptr;  // propagate
            }

            // compute hash
            node->hash = PyObject_Hash(value);
            if (node->hash == -1 && PyErr_Occurred()) {
                NodeType::teardown(node);  // free any resources allocated during init()
                return nullptr;  // propagate TypeError()
            }

            // return initialized node
            return node;
        }

        /* Initialize a copied node. */
        inline static Node* init_copy(Node* new_node, Node* old_node) {
            new_node = static_cast<Node*>(NodeType::init_copy(new_node, old_node));
            if (new_node == nullptr) {  // Error during decorated init_copy()
                return nullptr;  // propagate
            }

            // reuse the pre-computed hash
            new_node->hash = old_node->hash;
            return new_node;
        }

    };

    Node* head;
    Node* tail;
    size_t size;

    /* Disabled copy/move constructors.  These are dangerous because we're
    manually managing memory for each node. */
    SetView(const SetView& other) = delete;       // copy constructor
    SetView& operator=(const SetView&) = delete;  // copy assignment
    SetView(SetView&&) = delete;                  // move constructor
    SetView& operator=(SetView&&) = delete;       // move assignment

    /* Construct an empty SetView. */
    SetView() {
        head = nullptr;
        tail = nullptr;
        size = 0;
        specialization = nullptr;
    }

    /* Construct a SetView from an input iterable. */
    SetView(PyObject* iterable, bool reverse = false, PyObject* spec = nullptr) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == nullptr) {
            throw std::invalid_argument("Value is not iterable");
        }

        // init empty SetView
        head = nullptr;
        tail = nullptr;
        size = 0;
        specialization = spec;
        if (spec != nullptr) {
            Py_INCREF(spec);  // hold reference to specialization
        }

        // unpack iterator into SetView
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == nullptr) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    Py_DECREF(iterator);
                    Allocater<Node>::discard_list(head);
                    throw std::runtime_error("could not get item from iterator");
                }
                break;
            }

            // allocate a new node and link it to the list
            stage(item, reverse);
            if (PyErr_Occurred()) {
                Py_DECREF(iterator);
                Allocater<Node>::discard_list(head);
                throw std::runtime_error("could not stage item");
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);
    }

    /* Destroy a SetView and free all its resources. */
    ~SetView() {
        // NOTE: head, tail, size, queue, and table are automatically destroyed.
        Allocater<Node>::discard_list(head);
        clear_freelist();
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
    }

    /* Construct a new node for the list. */
    template <typename... Args>
    inline Node* node(PyObject* value, Args... args) const {
        // variadic dispatch to Node::init()
        Node* result = Allocater<Node>::create(freelist, value, args...);
        if (specialization != nullptr && result != nullptr) {
            if (!Node::typecheck(result, specialization)) {
                recycle(result);  // clean up allocated node
                return nullptr;  // propagate TypeError()
            }
        }

        return result;
    }

    /* Release a node, pushing it into the freelist. */
    inline void recycle(Node* node) const {
        Allocater<Node>::recycle(freelist, node);
    }

    /* Copy a node in the list. */
    inline Node* copy(Node* node) const {
        return Allocater<Node>::copy(freelist, node);
    }

    /* Make a shallow copy of the entire list. */
    inline SetView<NodeType>* copy() const {
        SetView<NodeType>* copied = new SetView<NodeType>();
        Node* old_node = head;
        Node* new_node = nullptr;
        Node* new_prev = nullptr;

        // copy each node in list
        while (old_node != nullptr) {
            new_node = copy(old_node);  // copy node
            if (new_node == nullptr) {  // error during copy()
                delete copied;  // discard staged list
                return nullptr;
            }

            // link to tail of copied list
            copied->link(new_prev, new_node, nullptr);
            if (PyErr_Occurred()) {  // error during link()
                delete copied;  // discard staged list
                return nullptr;
            }

            // advance to next node
            new_prev = new_node;
            old_node = static_cast<Node*>(old_node->next);
        }

        // return copied view
        return copied;
    }

    /* Clear the list and reset the associated hash table. */
    inline void clear() {
        Node* curr = head;  // store temporary reference to head

        // reset list parameters
        head = nullptr;
        tail = nullptr;
        size = 0;

        // reset hash table to initial size
        table.clear();

        // recycle all nodes, filling up the freelist
        Allocater<Node>::recycle_list(freelist, curr);
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(Node* prev, Node* curr, Node* next) {
        // add node to hash table
        table.remember(curr);
        if (PyErr_Occurred()) {  // node is already present in table
            return;
        }

        // delegate to node-specific link() helper
        Node::link(prev, curr, next);

        // update list parameters
        size++;
        if (prev == nullptr) {
            head = curr;
        }
        if (next == nullptr) {
            tail = curr;
        }
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        // remove node from hash table
        table.forget(curr);
        if (PyErr_Occurred()) {  // node is not present in table
            return;
        }

        // delegate to node-specific unlink() helper
        Node::unlink(prev, curr, next);

        // update list parameters
        size--;
        if (prev == nullptr) {
            head = next;
        }
        if (next == nullptr) {
            tail = prev;
        }
    }

    /* Enforce strict type checking for elements of this list. */
    void specialize(PyObject* spec) {
        // check the contents of the list
        if (spec != nullptr) {
            Node* curr = head;
            for (size_t i = 0; i < size; i++) {
                if (!Node::typecheck(curr, spec)) {
                    return;  // propagate TypeError()
                }
                curr = static_cast<Node*>(curr->next);
            }
            Py_INCREF(spec);
        }

        // replace old specialization
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
        specialization = spec;
    }

    /* Get the type specialization for elements of this list. */
    inline PyObject* get_specialization() const {
        if (specialization != nullptr) {
            Py_INCREF(specialization);
        }
        return specialization;  // return a new reference or NULL
    }

    /* Search for a node by its value. */
    inline Node* search(PyObject* value) const {
        return table.search(value);
    }

    /* Search for a node by its value. */
    inline Node* search(Node* value) const {
        return table.search(value);
    }

    /* Clear all tombstones from the hash table. */
    inline void clear_tombstones() {
        table.clear_tombstones();
    }

    /* Clear the internal freelist to remove dead nodes. */
    inline void clear_freelist() {
        Allocater<Node>::discard_freelist(freelist);
    }

    /* Get the total amount of memory consumed by the set (in bytes).  */
    inline size_t nbytes() const {
        size_t total = sizeof(SetView<NodeType>);  // SetView object
        total += table.nbytes();  // hash table
        total += size * sizeof(Node);  // nodes
        total += sizeof(freelist);  // freelist queue
        total += freelist.size() * (sizeof(Node) + sizeof(Node*));  // freelist
        return total;
    }

private:
    PyObject* specialization;  // specialized type for elements of this list
    mutable std::queue<Node*> freelist;  // stack allocated
    HashTable<Node> table;  // stack allocated

    /* Allocate a new node for the item and append it to the list, discarding
    it in the event of an error. */
    void stage(PyObject* item, bool reverse) {
        // allocate a new node
        Node* curr = node(item);
        if (curr == nullptr) {  // error during node initialization
            if constexpr (DEBUG) {
                // QoL - nothing has been allocated, so we don't actually free anything
                printf("    -> free: %s\n", repr(item));
            }
            return;  // propagate error
        }

        // link the node to the staged list
        if (reverse) {
            link(nullptr, curr, head);
        } else {
            link(tail, curr, nullptr);
        }
        if (PyErr_Occurred()) {  // error during link()
            Allocater<Node>::discard(curr);  // clean up staged node
            return;
        }
    }

};


template <typename NodeType>
class DictView {
public:
    /* A node decorator that hashes the underlying object and adds a second
    PyObject* reference, allowing the list to act as a dictionary. */
    struct Node : public NodeType {
        Py_hash_t hash;
        PyObject* mapped;

        /* Initialize a newly-allocated node (1-argument version). */
        inline static Node* init(Node* node, PyObject* value) {
            // Check that item is a tuple of size 2 (key-value pair)
            if (!PyTuple_Check(value) || PyTuple_Size(value) != 2) {
                PyErr_Format(
                    PyExc_TypeError,
                    "Expected tuple of size 2 (key, value), not: %R",
                    value
                );
                return nullptr;  // propagate TypeError()
            }

            // unpack tuple and pass to 2-argument version
            PyObject* key = PyTuple_GetItem(value, 0);
            PyObject* mapped = PyTuple_GetItem(value, 1);
            return init(node, key, mapped);
        }

        /* Initialize a newly-allocated node (2-argument version). */
        inline static Node* init(Node* node, PyObject* value, PyObject* mapped) {
            node = static_cast<Node*>(NodeType::init(node, value));
            if (node == nullptr) {  // Error during decorated init()
                return nullptr;  // propagate
            }

            // compute hash
            node->hash = PyObject_Hash(value);
            if (node->hash == -1 && PyErr_Occurred()) {
                NodeType::teardown(node);  // free any resources allocated during init()
                return nullptr;  // propagate TypeError()
            }

            // store a reference to the mapped value
            Py_INCREF(mapped);
            node->mapped = mapped;

            // return initialized node
            return node;
        }

        /* Initialize a copied node. */
        inline static Node* init_copy(Node* new_node, Node* old_node) {
            new_node = static_cast<Node*>(NodeType::init_copy(new_node, old_node));
            if (new_node == nullptr) {  // Error during decrated init_copy()
                return nullptr;  // propagate
            }

            // reuse the pre-computed hash
            new_node->hash = old_node->hash;

            // store a new reference to mapped value
            Py_INCREF(old_node->mapped);
            new_node->mapped = old_node->mapped;

            // return initialized node
            return new_node;
        }

        /* Tear down a node before freeing it. */
        inline static void teardown(Node* node) {
            Py_DECREF(node->mapped);  // release mapped value
            NodeType::teardown(node);
        }

    };

    Node* head;
    Node* tail;
    size_t size;

    /* Disabled copy/move constructors.  These are dangerous because we're
    manually managing memory for each node. */
    DictView(const DictView& other) = delete;       // copy constructor
    DictView& operator=(const DictView&) = delete;  // copy assignment
    DictView(DictView&&) = delete;                  // move constructor
    DictView& operator=(DictView&&) = delete;       // move assignment

    /* Construct an empty DictView. */
    DictView() {
        head = nullptr;
        tail = nullptr;
        size = 0;
        specialization = nullptr;
    }

    /* Construct a DictView from an input iterable. */
    DictView(PyObject* iterable, bool reverse = false, PyObject* spec = nullptr) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == nullptr) {
            throw std::invalid_argument("Value is not iterable");
        }

        // init empty DictView
        head = nullptr;
        tail = nullptr;
        size = 0;
        specialization = spec;
        if (spec != nullptr) {
            Py_INCREF(spec);  // hold reference to specialization
        }

        // unpack iterator into DictView
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == nullptr) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    Py_DECREF(iterator);
                    Allocater<Node>::discard_list(head);
                    throw std::runtime_error("could not get item from iterator");
                }
                break;  // end of iterator
            }

            // allocate a new node and link it to the list
            stage(item, reverse);
            if (PyErr_Occurred()) {  // error during stage()
                Py_DECREF(iterator);
                Allocater<Node>::discard_list(head);
                throw std::runtime_error("could not stage item");
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);
    }

    /* Destroy a DictView and free all its resources. */
    ~DictView() {
        // NOTE: head, tail, size, queue, and table are automatically destroyed.
        Allocater<Node>::discard_list(head);
        clear_freelist();
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
    }

    /* Construct a new node for the list. */
    template <typename... Args>
    inline Node* node(PyObject* value, Args... args) const {
        // variadic dispatch to Node::init()
        Node* result = Allocater<Node>::create(freelist, value, args...);
        if (specialization != nullptr && result != nullptr) {
            if (!Node::typecheck(result, specialization)) {
                recycle(result);  // clean up allocated node
                return nullptr;  // propagate TypeError()
            }
        }

        return result;
    }

    /* Free a node, pushing it into the freelist if possible. */
    inline void recycle(Node* node) const {
        Allocater<Node>::recycle(freelist, node);
    }

    /* Copy a single node in the list. */
    inline Node* copy(Node* node) const {
        return Allocater<Node>::copy(freelist, node);
    }

    /* Make a shallow copy of the list. */
    inline DictView<NodeType>* copy() const {
        DictView<NodeType>* copied = new DictView<NodeType>();
        Node* old_node = head;
        Node* new_node = nullptr;
        Node* new_prev = nullptr;

        // copy each node in list
        while (old_node != nullptr) {
            new_node = copy(old_node);  // copy node
            if (new_node == nullptr) {  // error during copy()
                delete copied;  // discard staged list
                return nullptr;
            }

            // link to tail of copied list
            copied->link(new_prev, new_node, nullptr);
            if (PyErr_Occurred()) {  // error during link()
                delete copied;  // discard staged list
                return nullptr;
            }

            // advance to next node
            new_prev = new_node;
            old_node = static_cast<Node*>(old_node->next);
        }

        // return copied view
        return copied;
    }

    /* Clear the list and reset the associated hash table. */
    inline void clear() {
        Node* curr = head;  // store temporary reference to head

        // reset list parameters
        head = nullptr;
        tail = nullptr;
        size = 0;

        // reset hash table to initial size
        table.clear();

        // recycle all nodes, filling up the freelist
        Allocater<Node>::recycle_list(freelist, curr);
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(Node* prev, Node* curr, Node* next) {
        // add node to hash table
        table.remember(curr);
        if (PyErr_Occurred()) {
            return;
        }

        // delegate to node-specific link() helper
        Node::link(prev, curr, next);

        // update list parameters
        size++;
        if (prev == nullptr) {
            head = curr;
        }
        if (next == nullptr) {
            tail = curr;
        }
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        // remove node from hash table
        table.forget(curr);
        if (PyErr_Occurred()) {
            return;
        }

        // delegate to node-specific unlink() helper
        Node::unlink(prev, curr, next);

        // update list parameters
        size--;
        if (prev == nullptr) {
            head = next;
        }
        if (next == nullptr) {
            tail = prev;
        }
    }

    /* Enforce strict type checking for elements of this list. */
    void specialize(PyObject* spec) {
        // check the contents of the list
        if (spec != nullptr) {
            Node* curr = head;
            for (size_t i = 0; i < size; i++) {
                if (!Node::typecheck(curr, spec)) {
                    return;  // propagate TypeError()
                }
                curr = static_cast<Node*>(curr->next);
            }
            Py_INCREF(spec);
        }

        // replace old specialization
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
        specialization = spec;
    }

    /* Get the type specialization for elements of this list. */
    inline PyObject* get_specialization() const {
        if (specialization != nullptr) {
            Py_INCREF(specialization);
        }
        return specialization;  // return a new reference or NULL
    }

    /* Search for a node by its value. */
    inline Node* search(PyObject* value) const {
        return table.search(value);
    }

    /* Search for a node by its value. */
    inline Node* search(Node* value) const {
        return table.search(value);
    }

    /* Search for a node and move it to the front of the list at the same time. */
    inline Node* lru_search(PyObject* value) {
        // move node to head of list
        Node* curr = table.search(value);
        if (curr != nullptr && curr != head) {
            if (curr == tail) {
                tail = static_cast<Node*>(curr->prev);
            }
            Node* prev = static_cast<Node*>(curr->prev);
            Node* next = static_cast<Node*>(curr->next);
            Node::unlink(prev, curr, next);
            Node::link(nullptr, curr, head);
            head = curr;
        }

        return curr;
    }

    /* Clear all tombstones from the hash table. */
    inline void clear_tombstones() {
        table.clear_tombstones();
    }

    /* Clear the internal freelist to remove dead nodes. */
    inline void clear_freelist() {
        Allocater<Node>::discard_freelist(freelist);
    }

    /* Get the total amount of memory consumed by the dictionary (in bytes). */
    inline size_t nbytes() const {
        size_t total = sizeof(DictView<NodeType>);  // SetView object
        total += table.nbytes();  // hash table
        total += size * sizeof(Node);  // contents of dictionary
        total += sizeof(freelist);  // freelist queue
        total += freelist.size() * (sizeof(Node) + sizeof(Node*));
        return total;
    }

private:
    PyObject* specialization;  // specialized type for elements of this list
    mutable std::queue<Node*> freelist;  // stack allocated
    HashTable<Node> table;  // stack allocated

    /* Allocate a new node for the item and append it to the list, discarding
    it in the event of an error. */
    void stage(PyObject* item, bool reverse) {
        // allocate a new node
        Node* curr = node(item);
        if (PyErr_Occurred()) {
            // QoL - nothing has been allocated, so we don't actually free anything
            if constexpr (DEBUG) {
                printf("    -> free: %s\n", repr(item));
            }
            return;
        }

        // link the node to the staged list
        if (reverse) {
            link(nullptr, curr, head);
        } else {
            link(tail, curr, nullptr);
        }
        if (PyErr_Occurred()) {
            Allocater<Node>::discard(curr);  // clean up staged node
            return;
        }
    }

};


///////////////////////////////
////    VIEW DECORATORS    ////
///////////////////////////////


// TODO: Sorted<> becomes a decorator for a view, not a node.  It automatically
// converts a view of any type into a sorted view, which stores its nodes in a
// skip list.  This makes the sortedness immutable, and blocks operations that
// would unsort the list.  Every node in the list is decorated with a key value
// that is supplied by the user.  This key is provided in the constructor, and
// is cached on the node itself under a universal `key` attribute.  The SortKey
// template parameter defines what is stored in this key, and under what
// circumstances it is modified.

// using MFUCache = typename Sorted<DictView<DoubleNode>, Frequency, Descending>;

// This would create a doubly-linked skip list where each node maintains a
// value, mapped value, frequency count, hash, and prev/next pointers.  The
// view itself would maintain a hash map for fast lookups.  If the default
// SortKey is used, then we can also make the the index() method run in log(n)
// by exploiting the skip list.  These can be specific overloads in the methods
// themselves.

// This decorator can be extended to any of the existing views.



// template <
//     template <typename> class ViewType,
//     typename NodeType,
//     typename SortKey = Value,
//     typename SortOrder = Ascending
// >
// class Sorted : public ViewType<NodeType> {
// public:
//     /* A node decorator that maintains vectors of next and prev pointers for use in
//     sorted, skip list-based data structures. */
//     struct Node : public ViewType::Node {
//         std::vector<Node*> skip_next;
//         std::vector<Node*> skip_prev;

//         /* Initialize a newly-allocated node. */
//         inline static Node* init(Node* node, PyObject* value) {
//             node = static_cast<Node*>(NodeType::init(node, value));
//             if (node == nullptr) {  // Error during decorated init()
//                 return nullptr;  // propagate
//             }

//             // NOTE: skip_next and skip_prev are stack-allocated, so there's no
//             // need to initialize them here.

//             // return initialized node
//             return node;
//         }

//         /* Initialize a copied node. */
//         inline static Node* init_copy(Node* new_node, Node* old_node) {
//             // delegate to templated init_copy() method
//             new_node = static_cast<Node*>(NodeType::init_copy(new_node, old_node));
//             if (new_node == nullptr) {  // Error during templated init_copy()
//                 return nullptr;  // propagate
//             }

//             // copy skip pointers
//             new_node->skip_next = old_node->skip_next;
//             new_node->skip_prev = old_node->skip_prev;
//             return new_node;
//         }

//         /* Tear down a node before freeing it. */
//         inline static void teardown(Node* node) {
//             node->skip_next.clear();  // reset skip pointers
//             node->skip_prev.clear();
//             NodeType::teardown(node);
//         }

//         // TODO: override link() and unlink() to update skip pointers and maintain
//         // sorted order
//     }
// }


// ////////////////////////
// ////    POLICIES    ////
// ////////////////////////


// // TODO: Value and Frequency should be decorators for nodes to give them full
// // type information.  They can even wrap


// /* A SortKey that stores a reference to a node's value in its key. */
// struct Value {
//     /* Decorate a freshly-initialized node. */
//     template <typename Node>
//     inline static void decorate(Node* node) {
//         node->key = node->value;
//     }

//     /* Clear a node's sort key. */
//     template <typename Node>
//     inline static void undecorate(Node* node) {
//         node->key = nullptr;
//     }
// };


// /* A SortKey that stores a frequency counter as a node's key. */
// struct Frequency {
//     /* Initialize a node's sort key. */
//     template <typename Node>
//     inline static void decorate(Node* node) {
//         node->key = 0;
//     }

//     /* Clear a node's sort key */

// };


// /* A Sorted<> policy that sorts nodes in ascending order based on key. */
// template <typename SortValue>
// struct Ascending {
//     /* Check whether two keys are in sorted order relative to one another. */
//     template <typename KeyValue>
//     inline static bool compare(KeyValue left, KeyValue right) {
//         return left <= right;
//     }

//     /* A specialization for compare to use with Python objects as keys. */
//     template <>
//     inline static bool compare(PyObject* left, PyObject* right) {
//         return PyObject_RichCompareBool(left, right, Py_LE);  // -1 signals error
//     }
// };


// /* A specialized version of Ascending that compares PyObject* references. */
// template <>
// struct Ascending<Value> {
    
// };


// /* A Sorted<> policy that sorts nodes in descending order based on key. */
// struct Descending {
//     /* Check whether two keys are in sorted order relative to one another. */
//     template <typename KeyValue>
//     inline static int compare(KeyValue left, KeyValue right) {
//         return left >= right;
//     }

//     /* A specialization for compare() to use with Python objects as keys. */
//     template <>
//     inline static int compare(PyObject* left, PyObject* right) {
//         return PyObject_RichCompareBool(left, right, Py_GE);  // -1 signals error
//     }
// };


#endif // VIEW_H include guard
