
// include guard prevents multiple inclusion
#ifndef VIEW_H
#define VIEW_H

#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <limits>  // for std::numeric_limits
#include <Python.h>  // for CPython API
#include <node.h>  // for Hashed<T>, Mapped<T>


// TODO: search.h should be a separate header file that implements __getitem__
// and __setitem__ for dictionary lookup.  This handles the reference counting
// and error handling, and it includes LRU functionality that moves the node
// whenever it is searched.  It has a __setitem__ equivalent that always links
// to the head of the list.  It also implements a purge() function that removes
// any stagnant nodes at the C++ level.  __setitem__() calls purge() internally.


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* MAX_SIZE_T is used to signal errors in indexing operations where NULL would
not be a valid return value, and 0 is likely to be valid output. */
const size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();


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


/////////////////////////
////    FUNCTIONS    ////
/////////////////////////


/* Allow Python-style negative indexing with wraparound and boundschecking. */
inline size_t normalize_index(
    PyObject* index,
    size_t size,
    bool truncate
) {
    // check that index is a Python integer
    if (!PyLong_Check(index)) {
        PyErr_SetString(PyExc_TypeError, "Index must be a Python integer");
        return MAX_SIZE_T;
    }

    PyObject* pylong_zero = PyLong_FromSize_t(0);
    PyObject* pylong_size = PyLong_FromSize_t(size);
    int index_lt_zero = PyObject_RichCompareBool(index, pylong_zero, Py_LT);

    // wraparound negative indices
    // if index < 0:
    //     index += size
    bool release_index = false;
    if (index_lt_zero) {
        index = PyNumber_Add(index, pylong_size);
        index_lt_zero = PyObject_RichCompareBool(index, pylong_zero, Py_LT);
        release_index = true;
    }

    // boundscheck
    // if index < 0 or index >= size:
    //     if truncate:
    //         if index < 0:
    //             return 0
    //         return size - 1
    //    raise IndexError("list index out of range")
    if (index_lt_zero || PyObject_RichCompareBool(index, pylong_size, Py_GE)) {
        Py_DECREF(pylong_zero);
        Py_DECREF(pylong_size);
        if (release_index) {
            Py_DECREF(index);  // index reference was created by PyNumber_Add()
        }
        if (truncate) {
            if (index_lt_zero) {
                return 0;
            }
            return size - 1;
        }
        PyErr_SetString(PyExc_IndexError, "list index out of range");
        return MAX_SIZE_T;
    }

    // release references
    Py_DECREF(pylong_zero);
    Py_DECREF(pylong_size);
    if (release_index) {
        Py_DECREF(index);  // index reference was created by PyNumber_Add()
    }

    // return as size_t
    return PyLong_AsSize_t(index);
}


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
            PyErr_NoMemory();
            return;  // propagate error
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

    /* Construct an empty HashTable. */
    HashTable() {
        if (DEBUG) {
            printf("    -> malloc: HashTable(%lu)\n", INITIAL_TABLE_CAPACITY);
        }

        // initialize hash table
        table = (T*)calloc(INITIAL_TABLE_CAPACITY, sizeof(T));
        if (table == NULL) {
            throw std::bad_alloc();  // we have to use C++ exceptions here
        }

        // initialize tombstone
        tombstone = (T)malloc(sizeof(T));
        if (tombstone == NULL) {
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
            if (PyErr_Occurred()) {  // error occurred during resize()
                return;
            }
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
        if (DEBUG) {
            printf("    -> free: HashTable(%lu)\n", capacity);
            printf("    -> malloc: HashTable(%lu)\n", INITIAL_TABLE_CAPACITY);
        }

        // free old table
        free(table);

        // allocate new table
        table = (T*)calloc(INITIAL_TABLE_CAPACITY, sizeof(T));
        if (table == NULL) {  // this should pretty much never happen, but just in case
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
            PyErr_NoMemory();
            return;  // propagate error
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


template <typename NodeType>
class ListView {
public:
    using Node = NodeType;
    Node* head;
    Node* tail;
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
        freelist = std::queue<Node*>();
    }

    /* Construct a ListView from an input iterable. */
    ListView(PyObject* iterable, bool reverse = false) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == NULL) {  // TypeError()
            throw std::invalid_argument("Value is not iterable");
        }

        // init empty ListView
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<Node*>();

        // unpack iterator into ListView
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
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
        Allocater<Node>::discard_list(head);
        Allocater<Node>::discard_freelist(freelist);
        // NOTE: head, tail, size, and queue are automatically destroyed
    }

    /* Construct a new node for the list. */
    inline Node* node(PyObject* value) {
        return Allocater<Node>::create(freelist, value);
    }

    /* Release a node, pushing it into the freelist. */
    inline void recycle(Node* node) {
        Allocater<Node>::recycle(freelist, node);
    }

    /* Copy a node in the list. */
    inline Node* copy(Node* node) {
        return Allocater<Node>::copy(freelist, node);
    }

    /* Make a shallow copy of the entire list. */
    inline ListView<NodeType>* copy() {
        ListView<NodeType>* copied = new ListView<NodeType>();
        Node* old_node = head;
        Node* new_node = NULL;
        Node* new_prev = NULL;

        // copy each node in list
        while (old_node != NULL) {
            new_node = copy(old_node);  // copy node
            if (new_node == NULL) {  // error during copy()
                delete copied;  // discard staged list
                return NULL;
            }

            // link to tail of copied list
            copied->link(new_prev, new_node, NULL);

            // advance to next node
            new_prev = new_node;
            old_node = (Node*)old_node->next;
        }

        // return copied list
        return copied;
    }

    /* Clear the list. */
    inline void clear() {
        Node* curr = head;  // store temporary reference to head

        // reset list parameters
        head = NULL;
        tail = NULL;
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
        if (prev == NULL) {
            head = curr;
        }
        if (next == NULL) {
            tail = curr;
        }
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        // delegate to node-specific unlink() helper
        Node::unlink(prev, curr, next);

        // update list parameters
        size--;
        if (prev == NULL) {
            head = next;
        }
        if (next == NULL) {
            tail = prev;
        }
    }

    /* Get the total memory consumed by the ListView (in bytes).

    NOTE: this is a lower bound and does not include the control structure of
    the `std::queue` freelist.  The actual memory usage is always slightly
    higher than is reported here.
    */
    inline size_t nbytes() {
        size_t total = sizeof(ListView<NodeType>);  // ListView object
        total += size * sizeof(Node); // nodes
        total += sizeof(freelist);  // freelist queue
        total += freelist.size() * (sizeof(Node) + sizeof(Node*));  // freelist
        return total;
    }

private:
    std::queue<Node*> freelist;

    /* Allocate a new node for the item and append it to the list, discarding
    it in the event of an error. */
    inline void stage(PyObject* item, bool reverse) {
        // allocate a new node
        Node* curr = node(item);
        if (curr == NULL) {  // error during node initialization
            return;
        }

        // link the node to the staged list
        // NOTE: this will never cause an error for ListViews, so we can can
        // omit the error-handling logic.
        if (reverse) {
            link(NULL, curr, head);
        } else {
            link(tail, curr, NULL);
        }
    }

};


template <typename NodeType>
class SetView {
public:
    using Node = Hashed<NodeType>;
    Node* head;
    Node* tail;
    size_t size;

    /* Disabled copy/move constructors.  These are dangerous because we're
    managing memory manually. */
    SetView(const SetView& other) = delete;       // copy constructor
    SetView& operator=(const SetView&) = delete;  // copy assignment
    SetView(SetView&&) = delete;                  // move constructor
    SetView& operator=(SetView&&) = delete;       // move assignment

    /* Construct an empty SetView. */
    SetView() {
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<Node*>();
        table = new HashTable<Node*>();
    }

    /* Construct a SetView from an input iterable. */
    SetView(PyObject* iterable, bool reverse = false) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == NULL) {
            throw std::invalid_argument("Value is not iterable");
        }

        // init empty SetView
        try {
            head = NULL;
            tail = NULL;
            size = 0;
            freelist = std::queue<Node*>();
            table = new HashTable<Node*>();  // can throw std::bad_alloc
        } catch (const std::bad_alloc& err) {
            Py_DECREF(iterator);
            throw err;
        }

        // unpack iterator into SetView
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    Py_DECREF(iterator);
                    Allocater<Node>::discard_list(head);
                    delete table;
                    throw std::runtime_error("could not get item from iterator");
                }
                break;
            }

            // allocate a new node and link it to the list
            stage(item, reverse);
            if (PyErr_Occurred()) {
                Py_DECREF(iterator);
                Allocater<Node>::discard_list(head);
                delete table;
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
        Allocater<Node>::discard_list(head);
        Allocater<Node>::discard_freelist(freelist);
        delete table;
        // NOTE: head, tail, size, and queue are automatically destroyed
    }

    /* Construct a new node for the list. */
    inline Node* node(PyObject* value) {
        return Allocater<Node>::create(freelist, value);
    }

    /* Release a node, pushing it into the freelist. */
    inline void recycle(Node* node) {
        Allocater<Node>::recycle(freelist, node);
    }

    /* Copy a node in the list. */
    inline Node* copy(Node* node) {
        return Allocater<Node>::copy(freelist, node);
    }

    /* Make a shallow copy of the entire list. */
    inline SetView<NodeType>* copy() {
        SetView<NodeType>* copied = new SetView<NodeType>();
        Node* old_node = head;
        Node* new_node = NULL;
        Node* new_prev = NULL;

        // copy each node in list
        while (old_node != NULL) {
            new_node = copy(old_node);  // copy node
            if (new_node == NULL) {  // error during copy()
                delete copied;  // discard staged list
                return NULL;
            }

            // link to tail of copied list
            copied->link(new_prev, new_node, NULL);
            if (PyErr_Occurred()) {  // error during link()
                delete copied;  // discard staged list
                return NULL;
            }

            // advance to next node
            new_prev = new_node;
            old_node = (Node*)old_node->next;
        }

        // return copied view
        return copied;
    }

    /* Clear the list and reset the associated hash table. */
    inline void clear() {
        Node* curr = head;  // store temporary reference to head

        // reset list parameters
        head = NULL;
        tail = NULL;
        size = 0;

        // reset hash table to initial size
        table->clear();

        // recycle all nodes, filling up the freelist
        Allocater<Node>::recycle_list(freelist, curr);
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(Node* prev, Node* curr, Node* next) {
        // add node to hash table
        table->remember(curr);
        if (PyErr_Occurred()) {  // node is already present in table
            return;
        }

        // delegate to node-specific link() helper
        Node::link(prev, curr, next);

        // update list parameters
        size++;
        if (prev == NULL) {
            head = curr;
        }
        if (next == NULL) {
            tail = curr;
        }
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        // remove node from hash table
        table->forget(curr);
        if (PyErr_Occurred()) {  // node is not present in table
            return;
        }

        // delegate to node-specific unlink() helper
        Node::unlink(prev, curr, next);

        // update list parameters
        size--;
        if (prev == NULL) {
            head = next;
        }
        if (next == NULL) {
            tail = prev;
        }
    }

    /* Search for a node by its value. */
    inline Node* search(PyObject* value) {
        return table->search(value);
    }

    /* Search for a node by its value. */
    inline Node* search(Node* value) {
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
        size_t total = sizeof(SetView<NodeType>);  // SetView object
        total += table->nbytes();  // hash table
        total += size * sizeof(Node);  // nodes
        total += sizeof(freelist);  // freelist queue
        total += freelist.size() * (sizeof(Node) + sizeof(Node*));  // freelist
        return total;
    }

private:
    std::queue<Node*> freelist;
    HashTable<Node*>* table;

    /* Allocate a new node for the item and append it to the list, discarding
    it in the event of an error. */
    inline void stage(PyObject* item, bool reverse) {
        // allocate a new node
        Node* curr = node(item);
        if (curr == NULL) {  // error during node initialization
            if (DEBUG) {
                // QoL - nothing has been allocated, so we don't actually free anything
                printf("    -> free: %s\n", repr(item));
            }
            return;  // propagate error
        }

        // link the node to the staged list
        if (reverse) {
            link(NULL, curr, head);
        } else {
            link(tail, curr, NULL);
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
    using Node = Mapped<NodeType>;
    Node* head;
    Node* tail;
    size_t size;

    /* Disabled copy/move constructors.  These are dangerous because we're
    managing memory manually. */
    DictView(const DictView& other) = delete;       // copy constructor
    DictView& operator=(const DictView&) = delete;  // copy assignment
    DictView(DictView&&) = delete;                  // move constructor
    DictView& operator=(DictView&&) = delete;       // move assignment

    /* Construct an empty DictView. */
    DictView() {
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<Node*>();
        table = new HashTable<Node*>();
    }

    /* Construct a DictView from an input iterable. */
    DictView(PyObject* iterable, bool reverse = false) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == NULL) {
            throw std::invalid_argument("Value is not iterable");
        }

        // init empty DictView
        try {
            head = NULL;
            tail = NULL;
            size = 0;
            freelist = std::queue<Node*>();
            table = new HashTable<Node*>();  // can throw std::bad_alloc
        } catch (const std::bad_alloc& err) {
            Py_DECREF(iterator);
            throw err;
        }

        // unpack iterator into DictView
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    Py_DECREF(iterator);
                    Allocater<Node>::discard_list(head);
                    delete table;
                    throw std::runtime_error("could not get item from iterator");
                }
                break;  // end of iterator
            }

            // allocate a new node and link it to the list
            stage(item, reverse);
            if (PyErr_Occurred()) {  // error during stage()
                Py_DECREF(iterator);
                Allocater<Node>::discard_list(head);
                delete table;
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
        Allocater<Node>::discard_list(head);
        Allocater<Node>::discard_freelist(freelist);
        delete table;
        // NOTE: head, tail, size, and queue are automatically destroyed
    }

    /* Allocate a new node from a packed key-value pair. */
    inline Node* node(PyObject* value) {
        return Allocater<Node>::create(freelist, value);
    }

    /* Allocate a new node from an unpacked key-value pair. */
    inline Node* node(PyObject* value, PyObject* mapped) {
        return Allocater<Node>::create(freelist, value, mapped);
    }

    /* Free a node. */
    inline void recycle(Node* node) {
        Allocater<Node>::recycle(freelist, node);
    }

    /* Copy a single node in the list. */
    inline Node* copy(Node* node) {
        return Allocater<Node>::copy(freelist, node);
    }

    /* Make a shallow copy of the list. */
    inline DictView<NodeType>* copy() {
        DictView<NodeType>* copied = new DictView<NodeType>();
        Node* old_node = head;
        Node* new_node = NULL;
        Node* new_prev = NULL;

        // copy each node in list
        while (old_node != NULL) {
            new_node = copy(old_node);  // copy node
            if (new_node == NULL) {  // error during copy()
                delete copied;  // discard staged list
                return NULL;
            }

            // link to tail of copied list
            copied->link(new_prev, new_node, NULL);
            if (PyErr_Occurred()) {  // error during link()
                delete copied;  // discard staged list
                return NULL;
            }

            // advance to next node
            new_prev = new_node;
            old_node = (Node*)old_node->next;
        }

        // return copied view
        return copied;
    }

    /* Clear the list and reset the associated hash table. */
    inline void clear() {
        Node* curr = head;  // store temporary reference to head

        // reset list parameters
        head = NULL;
        tail = NULL;
        size = 0;

        // reset hash table to initial size
        table->clear();

        // recycle all nodes, filling up the freelist
        Allocater<Node>::recycle_list(freelist, curr);
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(Node* prev, Node* curr, Node* next) {
        // add node to hash table
        table->remember(curr);
        if (PyErr_Occurred()) {
            return;
        }

        // delegate to node-specific link() helper
        Node::link(prev, curr, next);

        // update list parameters
        size++;
        if (prev == NULL) {
            head = curr;
        }
        if (next == NULL) {
            tail = curr;
        }
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        // remove node from hash table
        table->forget(curr);
        if (PyErr_Occurred()) {
            return;
        }

        // delegate to node-specific unlink() helper
        Node::unlink(prev, curr, next);

        // update list parameters
        size--;
        if (prev == NULL) {
            head = next;
        }
        if (next == NULL) {
            tail = prev;
        }
    }

    /* Search for a node by its value. */
    inline Node* search(PyObject* value) {
        return table->search(value);
    }

    /* Search for a node by its value. */
    inline Node* search(Node* value) {
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
        size_t total = sizeof(DictView<NodeType>);  // SetView object
        total += table->nbytes();  // hash table
        total += size * sizeof(Node);  // contents of dictionary
        total += sizeof(freelist);  // freelist queue
        total += freelist.size() * (sizeof(Node) + sizeof(Node*));
        return total;
    }

private:
    std::queue<Node*> freelist;
    HashTable<Node*>* table;

    /* Allocate a new node for the item and append it to the list, discarding
    it in the event of an error. */
    inline void stage(PyObject* item, bool reverse) {
        // allocate a new node
        Node* curr = node(item);
        if (PyErr_Occurred()) {
            // QoL - nothing has been allocated, so we don't actually free anything
            if (DEBUG) {
                printf("    -> free: %s\n", repr(item));
            }
            return;
        }

        // link the node to the staged list
        if (reverse) {
            link(NULL, curr, head);
        } else {
            link(tail, curr, NULL);
        }
        if (PyErr_Occurred()) {
            Allocater<Node>::discard(curr);  // clean up staged node
            return;
        }
    }

};


#endif // VIEW_H include guard
