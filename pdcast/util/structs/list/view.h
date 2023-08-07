
// include guard prevents multiple inclusion
#ifndef VIEW_H
#define VIEW_H

#include <cstddef>  // for size_t
#include <queue>  // for std::queue
#include <limits>  // for std::numeric_limits
#include <Python.h>  // for CPython API
#include <node.h>  // for Hashed<T>, Mapped<T>


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* DEBUG = TRUE adds print statements for memory allocation/deallocation to help
identify memory leaks. */
const bool DEBUG = true;


/* MAX_SIZE_T is used to signal errors in indexing operations where NULL would
not be a valid return value, and 0 is likely to be valid output. */
const size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();


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

    /* Initialize an empty ListView. */
    void init_empty() {
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<T*>();
    }

    /* Delete the contents of the linked list. */
    void delete_list() {
        tail = NULL;
        size = 0;

        // free all nodes
        T* next;
        while (head != NULL) {
            next = (T*)head->next;
            deallocate(head);
            head = next;
        }
    }

    /* Delete all the resources that are being consumed by this view. */
    void delete_view() {
        // free linked list
        delete_list();

        // free the contents of the freelist
        T* node;
        while (!freelist.empty()) {
            node = freelist.front();
            freelist.pop();
            free(node);
        }
    }

    /* Abort the construction of a ListView and free its resources. */
    void abort_init(PyObject* iterator, PyObject* item = NULL) {
        Py_DECREF(iterator);  // release reference on iterator
        if (item != NULL) {  // release reference on item
            Py_DECREF(item);
        }
        delete_view();  // free the view's manually-allocated resources
    }

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
        init_empty();
    }

    /* Construct a ListView from an input iterable. */
    ListView(PyObject* iterable, bool reverse = false) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == NULL) {  // TypeError()
            throw std::invalid_argument("Value is not iterable");
        }

        // init empty ListView
        init_empty();

        // unpack iterator into ListView
        T* node;
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    abort_init(iterator);
                    throw std::runtime_error("could not get item from iterator");
                }
                break;
            }

            // allocate a new node
            try {
                node = allocate(item);
            } catch (const std::bad_alloc& err) {
                abort_init(iterator, item);
                throw err;
            }

            // link the node to the staged list
            if (reverse) {
                link(NULL, node, head);
            } else {
                link(tail, node, NULL);
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);
    }

    /* Destroy a ListView and free all its nodes. */
    ~ListView() {
        delete_view();
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
        // NOTE: this does not empty the freelist
        delete_list();  // free linked list
    }

    /* Make a shallow copy of the list. */
    inline ListView<T>* copy() {
        ListView<T>* copied = new ListView<T>();
        T* old_node = head;
        T* new_node = NULL;
        T* new_prev = NULL;

        // copy each node in list
        while (old_node != NULL) {
            try {
                new_node = copy(old_node);  // copy node
            } catch (const std::bad_alloc& err) {  // memory error during copy()
                delete copied;
                throw err;
            }

            // link to tail of copied list
            copied->link(new_prev, new_node, NULL);

            // advance to next node
            new_prev = new_node;
            old_node = (T*)old_node->next;
        }

        // return copied list
        return copied;
    }

    /* Copy a single node in the list. */
    inline T* copy(T* node) {
        PyObject* python_repr;
        const char* c_repr;

        // print allocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(node->value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> malloc: %s\n", c_repr);
        }

        // delegate to node-specific copy function
        return T::copy(freelist, node);
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

    /* Initialize an empty SetView. */
    void init_empty() {
        // initialize list
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<Hashed<T>*>();

        // initialize hash table
        table = new HashTable<Hashed<T>*>();
    }

    /* Delete the contents of the linked list. */
    void delete_list() {
        tail = NULL;
        size = 0;

        // free all nodes
        Hashed<T>* next;
        while (head != NULL) {
            next = (Hashed<T>*)head->next;
            deallocate(head);
            head = next;
        }
    }

    /* Delete all the resources that are being consumed by this view. */
    void delete_view() {
        // free linked list
        delete_list();

        // free the contents of the freelist
        Hashed<T>* node;
        while (!freelist.empty()) {
            node = freelist.front();
            freelist.pop();
            free(node);
        }

        // free the hash table
        delete table;
        table = NULL;  // avoid dangling pointer
    }

    /* Abort the construction of a SetView and free its resources. */
    void abort_init(PyObject* iterator, PyObject* item = NULL) {
        Py_DECREF(iterator);  // release reference on iterator
        if (item != NULL) {  // release reference on item
            Py_DECREF(item);
        }
        delete_view();  // free the view's manually-allocated resources
    }

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
        init_empty();
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
            init_empty();
        } catch (const std::bad_alloc& err) {
            Py_DECREF(iterator);
            throw err;
        }

        // unpack iterator into SetView
        Hashed<T>* node;
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    abort_init(iterator);
                    throw std::runtime_error("could not get item from iterator");
                }
                break;
            }

            // allocate a new node
            try {
                node = allocate(item);
            } catch (const std::bad_alloc& err) {
                abort_init(iterator, item);
                throw err;
            }
            if (PyErr_Occurred()) {  // TypeError(): value is not hashable
                // NOTE: this print statement is for QoL in debugging.  Nothing
                // has actually been allocated yet, so we don't really free.
                if (DEBUG) {
                    PyObject* python_repr = PyObject_Repr(item);
                    const char* c_repr = PyUnicode_AsUTF8(python_repr);
                    Py_DECREF(python_repr);
                    printf("    -> free: %s\n", c_repr);
                }
                abort_init(iterator, item);
                throw std::runtime_error("value is not hashable");
            }

            // link the node to the staged list
            try {
                if (reverse) {
                    link(NULL, node, head);
                } else {
                    link(tail, node, NULL);
                }
            } catch (const std::bad_alloc& err) {  // memory error during resize()
                deallocate(node);  // NOTE: if DEBUG, prints messages before aborting
                abort_init(iterator, item);
                throw err;
            }
            if (PyErr_Occurred()) {  // ValueError(): item is already contained in set
                deallocate(node);
                abort_init(iterator, item);
                throw std::runtime_error("item is already contained in set");
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);
    }

    /* Destroy a SetView and free all its resources. */
    ~SetView() {
        delete_view();
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
        // NOTE: this does not empty the freelist
        delete_list();  // free linked list
        table->clear();  // reset hash table to initial size
    }

    /* Make a shallow copy of the entire list. */
    inline SetView<T>* copy() {
        SetView<T>* copied = new SetView<T>();
        Hashed<T>* old_node = head;
        Hashed<T>* new_node = NULL;
        Hashed<T>* new_prev = NULL;

        // copy each node in list
        while (old_node != NULL) {
            try {
                new_node = copy(old_node);  // copy node
            } catch (const std::bad_alloc& err) {  // memory error during copy()
                delete copied;
                throw err;
            }

            // link to tail of copied list
            try {
                copied->link(new_prev, new_node, NULL);
            } catch (const std::bad_alloc& err) {  // error during resize()
                delete copied;
                throw err;
            }

            // advance to next node
            new_prev = new_node;
            old_node = (Hashed<T>*)old_node->next;
        }

        // return copied view
        return copied;
    }

    /* Copy a single node in the list. */
    inline Hashed<T>* copy(Hashed<T>* node) {
        PyObject* python_repr;
        const char* c_repr;

        // print allocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(node->value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> malloc: %s\n", c_repr);
        }

        // delegate to node-specific copy function
        return Hashed<T>::copy(freelist, node);
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

    /* Initialize an empty DictView. */
    void init_empty() {
        // initialize list
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<Mapped<T>*>();

        // initialize hash table
        table = new HashTable<Mapped<T>*>();
    }

    /* Delete the contents of the linked list. */
    void delete_list() {
        tail = NULL;
        size = 0;

        // free all nodes
        Mapped<T>* next;
        while (head != NULL) {
            next = (Mapped<T>*)head->next;
            deallocate(head);
            head = next;
        }
    }

    /* Delete all the resources that are being consumed by this view. */
    void delete_view() {
        // free linked list
        delete_list();

        // free the contents of the freelist
        Mapped<T>* node;
        while (!freelist.empty()) {
            node = freelist.front();
            freelist.pop();
            free(node);
        }

        // free the hash table
        delete table;
        table = NULL;  // avoid dangling pointer
    }

    /* Abort the construction of a DictView and free its resources. */
    void abort_init(PyObject* iterator, PyObject* item = NULL) {
        Py_DECREF(iterator);  // release reference on iterator
        if (item != NULL) {  // release reference on item
            Py_DECREF(item);
        }
        delete_view();  // free the view's manually-allocated resources
    }

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
        init_empty();
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
            init_empty();
        } catch (const std::bad_alloc& err) {
            Py_DECREF(iterator);
            throw err;
        }

        // unpack iterator into DictView
        Mapped<T>* node;
        PyObject* item;
        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    abort_init(iterator);
                    throw std::runtime_error("could not get item from iterator");
                }
                break;  // end of iterator
            }

            // allocate a new node
            try {
                node = allocate(item);
            } catch (const std::bad_alloc& err) {
                abort_init(iterator, item);
                throw err;
            }
            if (PyErr_Occurred()) {  // TypeError(): not hashable or tuple of size 2
                // NOTE: this print statement is for QoL in debugging.  Nothing
                // has actually been allocated yet, so we don't really free.
                if (DEBUG) {
                    PyObject* python_repr = PyObject_Repr(item);
                    const char* c_repr = PyUnicode_AsUTF8(python_repr);
                    Py_DECREF(python_repr);
                    printf("    -> free: %s\n", c_repr);
                }
                abort_init(iterator, item);
                throw std::runtime_error("value is not hashable or tuple of size 2");
            }

            // link the node to the staged list
            try {
                if (reverse) {
                    link(NULL, node, head);
                } else {
                    link(tail, node, NULL);
                }
            } catch (const std::bad_alloc& err) {  // memory error during resize()
                deallocate(node);  // NOTE: if DEBUG, prints messages before aborting
                abort_init(iterator, item);
                throw err;
            }
            if (PyErr_Occurred()) {  // ValueError(): item is already contained in dict
                deallocate(node);
                abort_init(iterator, item);
                throw std::runtime_error("item is already contained in dictionary");
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);
    }

    /* Destroy a DictView and free all its resources. */
    ~DictView() {
        delete_list();
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
            PyErr_Format(
                PyExc_TypeError,
                "Expected tuple of size 2 (key, value), not: %R",
                value
            );
            return NULL;  // raise exception
        }

        // extract key and value and allocate a new node
        PyObject* key = PyTuple_GetItem(value, 0);
        value = PyTuple_GetItem(value, 1);
        return allocate(key, value);  // pass to 2-argument overload
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
        delete_list();  // clear linked list
        table->clear();  // clear hash table
    }

    /* Make a shallow copy of the list. */
    inline DictView<T>* copy() {
        DictView<T>* copied = new DictView<T>();
        Mapped<T>* old_node = head;
        Mapped<T>* new_node = NULL;
        Mapped<T>* new_prev = NULL;

        // copy each node in list
        while (old_node != NULL) {
            try {
                new_node = copy(old_node);  // copy node
            } catch (const std::bad_alloc& err) {  // memory error during copy()
                delete copied;
                throw err;
            }

            // link to tail of copied list
            try {
                copied->link(new_prev, new_node, NULL);
            } catch (const std::bad_alloc& err) {  // error during resize()
                delete copied;
                throw err;
            }

            // advance to next node
            new_prev = new_node;
            old_node = (Mapped<T>*)old_node->next;
        }

        // return copied view
        return copied;
    }

    /* Copy a single node in the list. */
    inline Mapped<T>* copy(Mapped<T>* node) {
        PyObject* python_repr;
        const char* c_repr;

        // print allocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(node->value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> malloc: %s\n", c_repr);
        }

        // delegate to node-specific copy function
        return Mapped<T>::copy(freelist, node);
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


#endif // VIEW_H include guard
