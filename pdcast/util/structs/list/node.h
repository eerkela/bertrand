// include guard prevents multiple inclusion
#ifndef NODE_H
#define NODE_H

#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <Python.h>  // for CPython API


// TODO: SetView/DictView.stage() do not check for blacklists.  Instead, when
// we add value to the list, we check if `link()` raises an error.  If it does,
// we throw away all the values we've added so far.  This is the most efficient
// way to do it, it just requires extra bookkeeping on our end.

// extend() and extendleft() are easy.  We just maintain a reference to the
// previous head/tail of the list.  If an error is encountered, we just
// retrace our steps back to the original configuration.


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* DEBUG = TRUE adds print statements for memory allocation/deallocation to help
identify memory leaks. */
const bool DEBUG = true;


/* For efficient memory management, every View maintains its own freelist of
deallocated nodes that can be reused for fast allocation. */
const unsigned int FREELIST_SIZE = 32;


/* Some ListViews use hash tables for fast access to each element.  These
parameters control the growth and hashing behavior of each table. */
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


/////////////////////
////    NODES    ////
/////////////////////


struct SingleNode {
    PyObject* value;
    SingleNode* next;

    /* Freelist constructor. */
    inline static SingleNode* allocate(
        std::queue<SingleNode*>& freelist,
        PyObject* value
    ) {
        SingleNode* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (SingleNode*)malloc(sizeof(SingleNode));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        initialize(node, value);
        return node;
    }

    /* Initialize a newly-allocated node. */
    inline static void initialize(SingleNode* node, PyObject* value) {
        Py_INCREF(value);
        node->value = value;
        node->next = NULL;
    }

    /* Free a node and add it to the freelist. */
    inline static void deallocate(
        std::queue<SingleNode*>& freelist,
        SingleNode* node
    ) {
        Py_DECREF(node->value);
        if (freelist.size() <= FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free(node);
        }
    }

    /* Copy constructor. */
    inline static SingleNode* copy(
        std::queue<SingleNode*>& freelist,
        SingleNode* node
    ) {
        return allocate(freelist, node->value);
    }

    /* Link the node to its neighbors to form a singly-linked list. */
    inline static void link(
        SingleNode* prev,
        SingleNode* curr,
        SingleNode* next
    ) {
        if (prev != NULL) {
            prev->next = curr;
        }
        curr->next = next;
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(
        SingleNode* prev,
        SingleNode* curr,
        SingleNode* next
    ) {
        if (prev != NULL) {
            prev->next = next;
        }
        curr->next = NULL;
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(SingleNode* prev, SingleNode* curr) {
        if (prev != NULL) {
            prev->next = NULL;
        }
    }

    /* Join the list at the specified nodes. */
    inline static void join(SingleNode* prev, SingleNode* curr) {
        if (prev != NULL) {
            prev->next = curr;
        }
    }

};


struct DoubleNode {
    PyObject* value;
    DoubleNode* next;
    DoubleNode* prev;

    /* Freelist constructor. */
    inline static DoubleNode* allocate(
        std::queue<DoubleNode*>& freelist,
        PyObject* value
    ) {
        DoubleNode* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (DoubleNode*)malloc(sizeof(DoubleNode));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        initialize(node, value);
        return node;
    }

    /* Initialize a newly-allocated node. */
    inline static void initialize(DoubleNode* node, PyObject* value) {
        Py_INCREF(value);
        node->value = value;
        node->next = NULL;
        node->prev = NULL;
    }

    /* Free a node and add it to the freelist. */
    inline static void deallocate(
        std::queue<DoubleNode*>& freelist,
        DoubleNode* node
    ) {
        Py_DECREF(node->value);
        if (freelist.size() <= FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free(node);
        }
    }

    /* Copy constructor. */
    inline static DoubleNode* copy(
        std::queue<DoubleNode*>& freelist,
        DoubleNode* node
    ) {
        return allocate(freelist, node->value);
    }

    /* Link the node to its neighbors to form a doubly-linked list. */
    inline static void link(
        DoubleNode* prev,
        DoubleNode* curr,
        DoubleNode* next
    ) {
        if (prev != NULL) {
            prev->next = curr;
        }
        curr->prev = prev;
        curr->next = next;
        if (next != NULL) {
            next->prev = curr;
        }
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(
        DoubleNode* prev,
        DoubleNode* curr,
        DoubleNode* next
    ) {
        if (prev != NULL) {
            prev->next = next;
        }
        if (next != NULL) {
            next->prev = prev;
        }
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(DoubleNode* prev, DoubleNode* curr) {
        if (prev != NULL) {
            prev->next = NULL;
        }
        if (curr != NULL) {
            curr->prev = NULL;
        }
    }

    /* Join the list at the specified nodes. */
    inline static void join(DoubleNode* prev, DoubleNode* curr) {
        if (prev != NULL) {
            prev->next = curr;
        }
        if (curr != NULL) {
            curr->prev = prev;
        }
    }
};


template <typename NodeType>
struct Hashed : public NodeType {
    Py_hash_t hash;

    /* Freelist constructor. */
    inline static Hashed<NodeType>* allocate(
        std::queue<Hashed<NodeType>*>& freelist,
        PyObject* value
    ) {
        Hashed<NodeType>* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (Hashed<NodeType>*)malloc(sizeof(Hashed<NodeType>));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        initialize(node, value);
        return node;
    }

    /* Initialize a newly-allocated node. */
    inline static void initialize(Hashed<NodeType>* node, PyObject* value) {
        NodeType::initialize(node, value);
        node->hash = PyObject_Hash(value);
    }

    /* Free a node and add it to the freelist. */
    inline static void deallocate(
        std::queue<Hashed<NodeType>*>& freelist,
        Hashed<NodeType>* node
    ) {
        Py_DECREF(node->value);
        if (freelist.size() <= FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free(node);
        }
    }

    /* Copy constructor. */
    inline static Hashed<NodeType>* copy(
        std::queue<Hashed<NodeType>*>& freelist,
        Hashed<NodeType>* node
    ) {
        // reuse the old node's hash value
        Hashed<NodeType>* new_node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            new_node = (Hashed<NodeType>*)malloc(sizeof(Hashed<NodeType>));
            if (new_node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            new_node = freelist.front();
            freelist.pop();
        }

        // initialize node
        NodeType::initialize(new_node, node->value);
        new_node->hash = node->hash;  // reuse the pre-computed hash
        return new_node;
    }

};


template <typename NodeType>
struct Mapped : public NodeType {
    Py_hash_t hash;
    PyObject* mapped;

    /* Freelist constructor. */
    inline static Mapped<NodeType>* allocate(
        std::queue<Mapped<NodeType>*>& freelist,
        PyObject* value,
        PyObject* mapped
    ) {
        Mapped<NodeType>* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (Mapped<NodeType>*)malloc(sizeof(Mapped<NodeType>));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        initialize(node, value, mapped);
        return node;
    }

    /* Initialize a newly-allocated node. */
    inline static void initialize(
        Mapped<NodeType>* node,
        PyObject* value,
        PyObject* mapped
    ) {
        NodeType::initialize(node, value);
        node->hash = PyObject_Hash(value);
        Py_INCREF(mapped);
        node->mapped = mapped;
    }

    /* Free a node and add it to the freelist. */
    inline static void deallocate(
        std::queue<Mapped<NodeType>*>& freelist,
        Mapped<NodeType>* node
    ) {
        Py_DECREF(node->value);
        Py_DECREF(node->mapped);
        if (freelist.size() <= FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free(node);
        }
    }

    /* Copy constructor. */
    inline static Mapped<NodeType>* copy(
        std::queue<Mapped<NodeType>*>& freelist,
        Mapped<NodeType>* node
    ) {
        // reuse the old node's hash value
        Mapped<NodeType>* new_node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            new_node = (Mapped<NodeType>*)malloc(sizeof(Mapped<NodeType>));
            if (new_node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            new_node = freelist.front();
            freelist.pop();
        }

        // initialize node
        NodeType::initialize(new_node, node->value);
        new_node->hash = node->hash;  // reuse the pre-computed hash
        Py_INCREF(node->mapped);
        new_node->mapped = node->mapped;  // copy mapped value
        return new_node;
    }

};


/////////////////////
////    TABLE    ////
/////////////////////


/* HashTables allow O(1) lookup for elements within SetViews and DictViews. */
template <typename T>
class HashTable {
private:
    T* table;               // array of pointers to nodes
    T tombstone;            // sentinel value for deleted nodes
    size_t capacity;        // size of table
    size_t occupied;        // number of occupied slots (incl. tombstones)
    size_t tombstones;      // number of tombstones
    unsigned char exponent; // log2(capacity) - log2(INITIAL_TABLE_SIZE)
    size_t prime;           // prime number used for double hashing

    /* Resize the hash table and replace its contents. */
    void resize(unsigned char new_exponent) {
        T* old_table = table;
        size_t old_capacity = capacity;
        size_t new_capacity = 1 << new_exponent;

        if (DEBUG) {
            printf("    -> malloc: HashTable(%lu)\n", new_capacity);
        }

        // allocate new table
        table = (T*)calloc(new_capacity, sizeof(T));
        if (table == NULL) {
            throw std::bad_alloc();
        }

        // update table parameters
        capacity = new_capacity;
        exponent = new_exponent;
        prime = PRIMES[new_exponent];

        size_t new_index, step;
        T lookup;

        // rehash old table and clear tombstones
        for (size_t old_index = 0; old_index < old_capacity; old_index++) {
            lookup = old_table[old_index];
            if (lookup != NULL && lookup != tombstone) {  // insert into new table
                // NOTE: we don't need to check for errors because we already
                // know that the old table is valid.
                new_index = lookup->hash % new_capacity;
                step = prime - (lookup->hash % prime);
                while (table[new_index] != NULL) {
                    new_index = (new_index + step) % new_capacity;
                }
                table[new_index] = lookup;
            }
        }

        // reset tombstone count
        occupied -= tombstones;
        tombstones = 0;

        // free old table
        if (DEBUG) {
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

    /* Constructor. */
    HashTable() {
        if (DEBUG) {
            printf("    -> malloc: HashTable(%lu)\n", INITIAL_TABLE_CAPACITY);
        }

        // initialize hash table
        table = (T*)calloc(INITIAL_TABLE_CAPACITY, sizeof(T));
        if (table == NULL) {
            throw std::bad_alloc();
        }

        // initialize tombstone
        tombstone = (T)malloc(sizeof(T));
        if (tombstone == NULL) {
            free(table);
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
        if (DEBUG) {
            printf("    -> free: HashTable(%lu)\n", capacity);
        }
        free(table);
        free(tombstone);
    }

    /* Add a node to the hash map for direct access. */
    void remember(T node) {
        // resize if necessary
        if (occupied > capacity * MAX_LOAD_FACTOR) {
            resize(exponent + 1);
        }

        // get index and step for double hashing
        size_t index = node->hash % capacity;
        size_t step = prime - (node->hash % prime);
        T lookup = table[index];
        int comp;

        // search table
        while (lookup != NULL) {
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
    void forget(T node) {
        // get index and step for double hashing
        size_t index = node->hash % capacity;
        size_t step = prime - (node->hash % prime);
        T lookup = table[index];
        int comp;
        size_t n = occupied - tombstones;

        // search table
        while (lookup != NULL) {
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
        // free old table
        if (DEBUG) {
            printf("    -> free: HashTable(%lu)\n", capacity);
        }
        free(table);

        // allocate new table
        if (DEBUG) {
            printf("    -> malloc: HashTable(%lu)\n", INITIAL_TABLE_CAPACITY);
        }
        table = (T*)calloc(INITIAL_TABLE_CAPACITY, sizeof(T));
        if (table == NULL) {
            throw std::bad_alloc();
        }

        // reset table parameters
        capacity = INITIAL_TABLE_CAPACITY;
        occupied = 0;
        tombstones = 0;
        exponent = 0;
        prime = PRIMES[exponent];
    }

    /* Search for a node in the hash map by value. */
    T search(PyObject* value) {
        // CPython API equivalent of hash(value)
        Py_hash_t hash = PyObject_Hash(value);
        if (hash == -1 && PyErr_Occurred()) {  // error occurred during hash()
            return NULL;
        }

        // get index and step for double hashing
        size_t index = hash % capacity;
        size_t step = prime - (hash % prime);
        T lookup = table[index];
        int comp;

        // search table
        while (lookup != NULL) {
            if (lookup != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(lookup->value, value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return NULL;
                } else if (comp == 1) {  // value found
                    return lookup;
                }
            }

            // advance to next slot
            index = (index + step) % capacity;
            lookup = table[index];
        }

        // value not found
        return NULL;
    }

    /* Search for a node directly. */
    T search(T value) {
        // reuse the node's pre-computed hash
        size_t index = value->hash % capacity;
        size_t step = prime - (value->hash % prime);
        T lookup = table[index];
        int comp;

        // search table
        while (lookup != NULL) {
            if (lookup != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(lookup->value, value->value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return NULL;
                } else if (comp == 1) {  // value found
                    return lookup;
                }
            }

            // advance to next slot
            index = (index + step) % capacity;
            lookup = table[index];
        }

        // value was not found
        return NULL;
    }

    /* Clear tombstones from the hash table. */
    void clear_tombstones() {
        T* old_table = table;

        if (DEBUG) {
            printf("    -> malloc: HashTable(%lu)\n", capacity);
        }

        // allocate new hash table
        table = (T*)calloc(capacity, sizeof(T));
        if (table == NULL) {
            throw std::bad_alloc();
        }

        size_t new_index, step;
        T lookup;

        // rehash old table and remove tombstones
        for (size_t old_index = 0; old_index < capacity; old_index++) {
            lookup = old_table[old_index];
            if (lookup != NULL && lookup != tombstone) {
                // NOTE: we don't need to check for errors because we already
                // know that the old table is valid.
                new_index = lookup->hash % capacity;
                step = prime - (lookup->hash % prime);
                while (table[new_index] != NULL) {
                    new_index = (new_index + step) % capacity;
                }
                table[new_index] = lookup;
            }
        }

        // reset tombstone count
        occupied -= tombstones;
        tombstones = 0;

        // free old table
        if (DEBUG) {
            printf("    -> free: HashTable(%lu)\n", capacity);
        }
        free(old_table);
    }

    /*Get the total amount of memory consumed by the hash table.*/
    inline size_t nbytes() {
        return sizeof(HashTable<T>);
    }

};


/////////////////////
////    VIEWS    ////
/////////////////////


template <typename T>
class ListView {
private:
    std::queue<T*> freelist;

public:
    T* head;
    T* tail;
    size_t size;

    /* Disabled copy/move constructors.  These are dangerous because we're
    managing memory manually. */
    ListView(const ListView& other) = delete;       // copy constructor
    ListView& operator=(const ListView&) = delete;  // copy assignment
    ListView(ListView&&) = delete;                  // move constructor
    ListView& operator=(ListView&&) = delete;       // move assignment

    /* Construct an empty ListView. */
    ListView() {
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<T*>();
    }

    /* Destroy a ListView and free all its nodes. */
    ~ListView() {
        T* curr = head;
        T* next;

        // free all nodes
        while (curr != NULL) {
            next = (T*)curr->next;
            deallocate(curr);
            curr = next;
        }
    }

    /* Allocate a new node for the list. */
    inline T* allocate(PyObject* value) {
        PyObject* python_repr;
        const char* c_repr;

        // print allocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> malloc: %s\n", c_repr);
        }

        // delegate to node-specific allocator
        return T::allocate(freelist, value);
    }

    /* Free a node. */
    inline void deallocate(T* node) {
        PyObject* python_repr;
        const char* c_repr;

        // print deallocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(node->value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> free: %s\n", c_repr);
        }

        // delegate to node-specific deallocater
        T::deallocate(freelist, node);
    }

    /* Return the size of the freelist. */
    inline unsigned char freelist_size() {
        return freelist.size();
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(T* prev, T* curr, T* next) {
        T::link(prev, curr, next);
        if (prev == NULL) {
            head = curr;
        }
        if (next == NULL) {
            tail = curr;
        }
        size++;
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(T* prev, T* curr, T* next) {
        T::unlink(prev, curr, next);
        if (prev == NULL) {
            head = next;
        }
        if (next == NULL) {
            tail = prev;
        }
        size--;
    }

    /* Clear the list. */
    inline void clear() {
        T* curr = head;
        T* next;
        while (curr != NULL) {
            next = (T*)curr->next;
            deallocate(curr);
            curr = next;
        }
        head = NULL;
        tail = NULL;
        size = 0;
    }

    /* Make a shallow copy of the list. */
    inline ListView<T>* copy() {
        ListView<T>* copied = new ListView<T>();
        if (copied == NULL) {
            throw std::bad_alloc();
        }

        T* old_node = head;
        T* new_node = NULL;
        T* prev = NULL;
        PyObject* python_repr;
        const char* c_repr;

        // copy each node in list
        while (old_node != NULL) {
            // print allocation message if DEBUG=TRUE
            if (DEBUG) {
                python_repr = PyObject_Repr(old_node->value);
                c_repr = PyUnicode_AsUTF8(python_repr);
                Py_DECREF(python_repr);
                printf("    -> malloc: %s\n", c_repr);
            }

            new_node = T::copy(freelist, old_node);
            copied->link(prev, new_node, NULL);
            prev = new_node;
            old_node = (T*)old_node->next;
        }

        copied->tail = new_node;  // last node in copied list
        return copied;
    }

    /* Stage a new ListView of nodes from an input iterable. */
    static ListView<T>* stage(PyObject* iterable, bool reverse = false) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == NULL) {
            return NULL;
        }

        ListView<T>* staged = new ListView<T>();
        if (staged == NULL) {
            Py_DECREF(iterator);
            PyErr_NoMemory();
            return NULL;
        }

        T* node;
        PyObject* item;

        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    Py_DECREF(item);
                    Py_DECREF(iterator);
                    delete staged;
                    return NULL;  // raise exception
                }
                break;
            }

            // allocate a new node
            node = staged->allocate(item);

            // link the node to the staged list
            if (reverse) {
                staged->link(NULL, node, staged->head);
            } else {
                staged->link(staged->tail, node, NULL);
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);

        // return the staged View
        return staged;
    }

    /* Get the total memory consumed by the ListView (in bytes).

    NOTE: this is a lower bound and does not include the control structure of
    the `std::queue` freelist.  The actual memory usage is always slightly
    higher than is reported here.
    */
    inline size_t nbytes() {
        size_t total = sizeof(ListView<T>);  // ListView object
        total += size * sizeof(T); // contents of list
        total += sizeof(freelist);  // freelist queue
        total += freelist.size() * (sizeof(T) + sizeof(T*));  // contents of freelist
        return total;
    }

};


template <typename T>
class SetView {
private:
    std::queue<Hashed<T>*> freelist;
    HashTable<Hashed<T>*>* table;

public:
    Hashed<T>* head;
    Hashed<T>* tail;
    size_t size;

    /* Disabled copy/move constructors.  These are dangerous because we're
    managing memory manually. */
    SetView(const SetView& other) = delete;       // copy constructor
    SetView& operator=(const SetView&) = delete;  // copy assignment
    SetView(SetView&&) = delete;                  // move constructor
    SetView& operator=(SetView&&) = delete;       // move assignment

    /* Construct an empty SetView. */
    SetView() {
        // initialize list
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<Hashed<T>*>();

        // initialize hash table
        table = new HashTable<Hashed<T>*>();
        if (table == NULL) {
            throw std::bad_alloc();
        }
    }

    /* Destroy a SetView and free all its resources. */
    ~SetView() {
        // free all nodes
        Hashed<T>* curr = head;
        Hashed<T>* next;
        while (curr != NULL) {
            next = (Hashed<T>*)curr->next;
            deallocate(curr);
            curr = next;
        }

        // free hash table
        delete table;
    }

    /* Allocate a new node for the list. */
    inline Hashed<T>* allocate(PyObject* value) {
        PyObject* python_repr;
        const char* c_repr;

        // print allocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> malloc: %s\n", c_repr);
        }

        // delegate to node-specific allocator
        return Hashed<T>::allocate(freelist, value);
    }

    /* Free a node. */
    inline void deallocate(Hashed<T>* node) {
        PyObject* python_repr;
        const char* c_repr;

        // print deallocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(node->value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> free: %s\n", c_repr);
        }

        // delegate to node-specific deallocater
        Hashed<T>::deallocate(freelist, node);
    }

    /* Return the size of the freelist. */
    inline unsigned char freelist_size() {
        return freelist.size();
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(Hashed<T>* prev, Hashed<T>* curr, Hashed<T>* next) {
        // add node to hash table
        table->remember(curr);
        if (PyErr_Occurred()) {
            return;
        }

        // link node to neighbors
        Hashed<T>::link(prev, curr, next);

        // update head/tail pointers
        if (prev == NULL) {
            head = curr;
        }
        if (next == NULL) {
            tail = curr;
        }

        // increment size
        size++;
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Hashed<T>* prev, Hashed<T>* curr, Hashed<T>* next) {
        // remove node from hash table
        table->forget(curr);
        if (PyErr_Occurred()) {
            return;
        }

        // unlink node from neighbors
        Hashed<T>::unlink(prev, curr, next);

        // update head/tail pointers
        if (prev == NULL) {
            head = next;
        }
        if (next == NULL) {
            tail = prev;
        }

        // decrement size
        size--;
    }

    /* Clear the list and reset the associated hash table. */
    inline void clear() {
        table->clear();  // clear hash table
        ListView<T>::clear();  // clear list
    }

    /* Make a shallow copy of the list. */
    inline SetView<T>* copy() {
        SetView<T>* copied = new SetView<T>();
        Hashed<T>* old_node = head;
        Hashed<T>* new_node = NULL;
        Hashed<T>* prev = NULL;
        PyObject* python_repr;
        const char* c_repr;

        // copy each node in list
        while (old_node != NULL) {
            // print allocation message if DEBUG=TRUE
            if (DEBUG) {
                python_repr = PyObject_Repr(old_node->value);
                c_repr = PyUnicode_AsUTF8(python_repr);
                Py_DECREF(python_repr);
                printf("    -> malloc: %s\n", c_repr);
            }

            new_node = Hashed<T>::copy(freelist, old_node);
            copied->link(prev, new_node, NULL);
            prev = new_node;
            old_node = (Hashed<T>*)old_node->next;
        }

        copied->tail = new_node;  // last node in copied list
        return copied;
    }

    /* Stage a new SetView of nodes from an input iterable. */
    static SetView<T>* stage(PyObject* iterable, bool reverse = false) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == NULL) {
            return NULL;
        }

        SetView<T>* staged = new SetView<T>();
        if (staged == NULL) {
            Py_DECREF(iterator);
            PyErr_NoMemory();
            return NULL;
        }

        Hashed<T>* node;
        PyObject* item;

        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    Py_DECREF(item);
                    Py_DECREF(iterator);
                    delete staged;
                    return NULL;  // raise exception
                }
                break;
            }

            // allocate a new node
            node = staged->allocate(item);

            // link the node to the staged list
            if (reverse) {
                staged->link(NULL, node, staged->head);
            } else {
                staged->link(staged->tail, node, NULL);
            }
            if (PyErr_Occurred()) {
                Py_DECREF(item);
                Py_DECREF(iterator);
                delete staged;
                return NULL;  // raise exception
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);

        // return the staged View
        return staged;
    }

    /* Search for a node by its value. */
    inline Hashed<T>* search(PyObject* value) {
        return table->search(value);
    }

    /* Search for a node by its value. */
    inline Hashed<T>* search(Hashed<T>* value) {
        return table->search(value);
    }

    /* Clear all tombstones from the hash table. */
    inline void clear_tombstones() {
        table->clear_tombstones();
    }

    /* Get the total amount of memory consumed by the hash table.

    NOTE: this is a lower bound and does not include the control structure of
    the `std::queue` freelist.  The actual memory usage is always slightly
    higher than is reported here. */
    inline size_t nbytes() {
        size_t total = sizeof(SetView<T>);  // SetView object
        total += table->nbytes();  // hash table
        total += size * sizeof(Hashed<T>);  // contents of set
        total += sizeof(freelist);  // freelist queue
        total += freelist.size() * (sizeof(Hashed<T>) + sizeof(Hashed<T>*));
        return total;
    }

};


template <typename T>
class DictView {
private:
    std::queue<Mapped<T>*> freelist;
    HashTable<Mapped<T>*>* table;

public:
    Mapped<T>* head;
    Mapped<T>* tail;
    size_t size;

    /* Disabled copy/move constructors.  These are dangerous because we're
    managing memory manually. */
    DictView(const DictView& other) = delete;       // copy constructor
    DictView& operator=(const DictView&) = delete;  // copy assignment
    DictView(DictView&&) = delete;                  // move constructor
    DictView& operator=(DictView&&) = delete;       // move assignment

    /* Construct an empty DictView. */
    DictView() {
        // initialize list
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<Mapped<T>*>();

        // initialize hash table
        table = new HashTable<Mapped<T>*>();
        if (table == NULL) {
            throw std::bad_alloc();
        }
    }

    /* Destroy a DictView and free all its resources. */
    ~DictView() {
        // free all nodes
        Mapped<T>* curr = head;
        Mapped<T>* next;
        while (curr != NULL) {
            next = (Mapped<T>*)curr->next;
            deallocate(curr);
            curr = next;
        }

        // free hash table
        delete table;
    }

    /* Allocate a new node for the dictionary. */
    inline Mapped<T>* allocate(PyObject* value, PyObject* mapped) {
        PyObject* python_repr;
        const char* c_repr;

        // print allocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> malloc: %s\n", c_repr);
        }

        // delegate to node-specific allocator
        return Mapped<T>::allocate(freelist, value, mapped);
    }

    /* Allocate a new node from a key-value pair. */
    inline Mapped<T>* allocate(PyObject* value) {
        // Check that the item is a tuple of size 2 (key-value pair)
        if (!PyTuple_Check(value) || PyTuple_Size(value) != 2) {
            PyErr_Format(PyExc_TypeError, "Expected tuple of size 2, got %R", value);
            return NULL;  // raise exception
        }

        // extract key and value and allocate a new node
        PyObject* key = PyTuple_GetItem(value, 0);
        value = PyTuple_GetItem(value, 1);
        allocate(key, value);  // pass to 2-argument overload
    }

    /* Free a node. */
    inline void deallocate(Mapped<T>* node) {
        PyObject* python_repr;
        const char* c_repr;

        // print deallocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(node->value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> free: %s\n", c_repr);
        }

        // delegate to node-specific deallocater
        Mapped<T>::deallocate(freelist, node);
    }

    /* Return the size of the freelist. */
    inline unsigned char freelist_size() {
        return freelist.size();
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(Mapped<T>* prev, Mapped<T>* curr, Mapped<T>* next) {
        // add node to hash table
        table->remember(curr);
        if (PyErr_Occurred()) {
            return;
        }

        // link node to neighbors
        Mapped<T>::link(prev, curr, next);

        // update head/tail pointers
        if (prev == NULL) {
            head = curr;
        }
        if (next == NULL) {
            tail = curr;
        }

        // increment size
        size++;
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Mapped<T>* prev, Mapped<T>* curr, Mapped<T>* next) {
        // remove node from hash table
        table->forget(curr);
        if (PyErr_Occurred()) {
            return;
        }

        // unlink node from neighbors
        Mapped<T>::unlink(prev, curr, next);

        // update head/tail pointers
        if (prev == NULL) {
            head = next;
        }
        if (next == NULL) {
            tail = prev;
        }

        // decrement size
        size--;
    }

    /* Clear the list and reset the associated hash table. */
    inline void clear() {
        table->clear();  // clear hash table
        ListView<T>::clear();  // clear list
    }

    /* Make a shallow copy of the list. */
    inline DictView<T>* copy() {
        DictView<T>* copied = new DictView<T>();
        Mapped<T>* old_node = head;
        Mapped<T>* new_node = NULL;
        Mapped<T>* prev = NULL;
        PyObject* python_repr;
        const char* c_repr;

        // copy each node in list
        while (old_node != NULL) {
            // print allocation message if DEBUG=TRUE
            if (DEBUG) {
                python_repr = PyObject_Repr(old_node->value);
                c_repr = PyUnicode_AsUTF8(python_repr);
                Py_DECREF(python_repr);
                printf("    -> malloc: %s\n", c_repr);
            }

            new_node = Mapped<T>::copy(freelist, old_node);
            copied->link(prev, new_node, NULL);
            prev = new_node;
            old_node = (Mapped<T>*)old_node->next;
        }

        copied->tail = new_node;  // last node in copied list
        return copied;
    }

    /* Stage a new DictView of nodes from an input iterable. */
    static DictView<T>* stage(PyObject* iterable, bool reverse = false) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == NULL) {
            return NULL;
        }

        DictView<T>* staged = new DictView<T>();
        if (staged == NULL) {
            Py_DECREF(iterator);
            PyErr_NoMemory();
            return NULL;
        }

        Mapped<T>* node;
        PyObject* item;
        PyObject* key;
        PyObject* value;

        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    Py_DECREF(item);
                    Py_DECREF(iterator);
                    delete staged;
                    return NULL;  // raise exception
                }
                break;  // end of iterator
            }

            // Check that the item is a tuple of size 2 (key-value pair)
            if (!PyTuple_Check(item) || PyTuple_Size(item) != 2) {
                PyErr_Format(
                    PyExc_TypeError, "Expected tuple of size 2, got %R", item
                );
                Py_DECREF(item);
                Py_DECREF(iterator);
                delete staged;
                return NULL;  // raise exception
            }

            // extract key and value and allocate a new node
            key = PyTuple_GetItem(item, 0);
            value = PyTuple_GetItem(item, 1);
            node = staged->allocate(key, value);

            // link the node to the staged list
            if (reverse) {
                staged->link(NULL, node, staged->head);
            } else {
                staged->link(staged->tail, node, NULL);
            }
            if (PyErr_Occurred()) {
                Py_DECREF(item);
                Py_DECREF(iterator);
                delete staged;
                return NULL;  // raise exception
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);

        // return the staged View
        return staged;
    }

    /* Search for a node by its value. */
    inline Mapped<T>* search(PyObject* value) {
        return table->search(value);
    }

    /* Search for a node by its value. */
    inline Mapped<T>* search(Mapped<T>* value) {
        return table->search(value);
    }

    /* Clear all tombstones from the hash table. */
    inline void clear_tombstones() {
        table->clear_tombstones();
    }

    /* Get the total amount of memory consumed by the hash table.

    NOTE: this is a lower bound and does not include the control structure of
    the `std::queue` freelist.  The actual memory usage is always slightly
    higher than is reported here. */
    inline size_t nbytes() {
        size_t total = sizeof(SetView<T>);  // SetView object
        total += table->nbytes();  // hash table
        total += size * sizeof(Mapped<T>);  // contents of dictionary
        total += sizeof(freelist);  // freelist queue
        total += freelist.size() * (sizeof(Mapped<T>) + sizeof(Mapped<T>*));
        return total;
    }

};


#endif // NODE_H include guard
