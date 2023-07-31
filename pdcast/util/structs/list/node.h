// include guard prevents multiple inclusion
#ifndef NODE_H
#define NODE_H

#include <cstddef>  // for size_t
#include <utility>  // for std::pair
#include <queue>  // for std::queue
#include <Python.h>  // for CPython API


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* DEBUG = TRUE adds print statements for memory allocation/deallocation to help
identify memory leaks. */
const bool DEBUG = true;


/* For efficient memory management, every ListView maintains its own freelist
of deallocated nodes, which can be reused for repeated allocation. */
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


struct Node {
    PyObject* value;

    /* Hash the underlying value. */
    inline static Py_hash_t hash(Node node) {
        // C API equivalent of the hash() function
        return PyObject_Hash(value);
    }

}


struct SingleNode : public Node {
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

        // initialize node
        Py_INCREF(value);
        node->value = value;
        node->next = NULL;
        return node;
    }

    /* Freelist destructor. */
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

};


struct DoubleNode : public Node {
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

        // initialize node
        Py_INCREF(value);
        node->value = value;
        node->next = NULL;
        node->prev = NULL;
        return node;
    }

    /* Freelist destructor. */
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
};


struct HashNode : public DoubleNode {
    Py_hash_t _hash;

    /* Freelist constructor. */
    inline static HashNode* allocate(
        std::queue<HashNode*>& freelist,
        PyObject* value
    ) {        
        // C API equivalent of the hash() function
        Py_hash_t hash_value = PyObject_Hash(value);
        if (hash_value == -1 && PyErr_Occurred()) {
            return NULL;
        }

        HashNode* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (HashNode*)malloc(sizeof(HashNode));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        // initialize node
        Py_INCREF(value);
        node->value = value;
        node->_hash = hash_value;
        node->next = NULL;
        node->prev = NULL;
        return node;
    }

    /* Copy constructor. */
    inline static HashNode* copy(
        std::queue<HashNode*>& freelist,
        HashNode* node
    ) {
        // reuse the old node's hash value
        HashNode* new_node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            new_node = (HashNode*)malloc(sizeof(HashNode));
            if (new_node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            new_node = freelist.front();
            freelist.pop();
        }

        // initialize node
        Py_INCREF(node->value);
        new_node->value = node->value;
        new_node->_hash = node->_hash;  // re-use the pre-computed hash
        new_node->next = NULL;
        new_node->prev = NULL;
        return new_node;
    }

    /* Return the pre-computed hash. */
    inline static Py_hash_t hash(HashNode* node) {
        return node->_hash;
    }

};


/////////////////////
////    VIEWS    ////
/////////////////////


template <typename T>
class ListView {
private:
    std::queue<T*> freelist;

    /* Allow Python-style negative indexing with wraparound. */
    inline size_t normalize_index(long long index) {
        // wraparound
        if (index < 0) {
            index += size;
        }

        // boundscheck
        if (index < 0 || index >= (long long)size) {
            throw std::out_of_range("list index out of range");
        }

        return (size_t)index;
    }

public:
    T* head;
    T* tail;
    size_t size;

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
        PyObject* python_repr;
        const char* c_repr;

        // free all nodes
        while (curr != NULL) {
            // print deallocation message if DEBUG=TRUE
            if (DEBUG) {
                python_repr = PyObject_Repr(curr->value);
                c_repr = PyUnicode_AsUTF8(python_repr);
                Py_DECREF(python_repr);
                printf("    -> free: %s\n", c_repr);
            }

            // destroy node
            next = curr->next;
            Py_DECREF(curr->value);
            free(curr);
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
            next = curr->next;
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
        T* old_node = head;
        T* new_node = NULL;
        T* prev = NULL;

        // copy each node in list
        while (old_node != NULL) {
            new_node = T::copy(freelist, old_node);
            T::link(prev, new_node, NULL);
            if (copied->head == NULL) {
                copied->head = new_node;
            }
            prev = new_node;
            old_node = old_node->next;
        }

        copied->tail = new_node;  // last node in copied list
        copied->size = size;  // reuse old size
        return copied;
    }

    /* Stage a new View of nodes to be added to the list. */
    static ListView<T>* stage(PyObject* iterable, bool reverse = false) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == NULL) {
            return NULL;
        }

        ListView<T>* staged = new ListView<T>();

        T* node;
        PyObject* item;

        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    Py_DECREF(item);
                    Py_DECREF(iterator);
                    while (staged->head != NULL) {  // clean up staged nodes
                        node = staged->head;
                        staged->head = staged->head->next;
                        Py_DECREF(node->value);
                        free(node);
                    }
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

    /* Get the total memory consumed by the ListView (in bytes). */
    inline size_t nbytes() {
        size_t total = sizeof(ListView<T>);
        total += freelist.size() * (sizeof(T) + sizeof(T*));
        total += size * sizeof(T);
        return total;
    }

};


template <typename T>
class SetView : public ListView<T> {
private:
    T** table;                  // hash table
    T* tombstone;               // sentinel value for removed nodes
    size_t capacity;            // total number of slots in the table
    size_t occupied;            // number of occupied slots (incl. tombstones)
    size_t tombstones;          // number of tombstones
    unsigned char exponent;     // log2(capacity) - log2(INITIAL_TABLE_CAPACITY)
    size_t prime;               // prime number used for double hashing

public:
    /* Construct an empty SetView. */
    SetView() {
        // initialize list
        this->head = NULL;
        this->tail = NULL;
        this->size = 0;
        this->freelist = std::queue<T*>();

        if (DEBUG) {
            printf("    -> malloc: HashTable(%lu)\n", INITIAL_TABLE_CAPACITY);
        }

        // initialize hash table
        this->table = (T**)calloc(INITIAL_TABLE_CAPACITY, sizeof(T*));
        if (table == NULL) {
            throw std::bad_alloc();
        }

        // initialize tombstone
        this->tombstone = (T*)malloc(sizeof(T));
        if (tombstone == NULL) {
            free(table);
            throw std::bad_alloc();
        }

        // initialize table parameters
        this->capacity = INITIAL_TABLE_CAPACITY;
        this->occupied = 0;
        this->tombstones = 0;
        this->exponent = 0;
        this->prime = PRIMES[this->exponent];
    }

    /* Destroy a SetView and free all its resources. */
    ~SetView() {
        // free all nodes
        T* curr = this->head;
        T* next;
        while (curr != NULL) {
            next = curr->next;
            Py_DECREF(curr->value);
            Py_DECREF(curr->mapped);  // extra DECREF for mapped value
            free(curr);
            curr = next;
        }

        if (DEBUG) {
            printf("    -> free: HashTable(%lu)\n", this->capacity);
        }

        // free hash table
        free(this->table);
        free(this->tombstone);
    }

    /* Disabled copy/move constructors.  These are dangerous because we're
    managing memory manually in the constructor/destructor. */
    SetView(const SetView& other) = delete;         // copy constructor
    SetView& operator=(const SetView&) = delete;    // copy assignment
    SetView(SetView&&) = delete;                    // move constructor
    SetView& operator=(SetView&&) = delete;         // move assignment

    /* Link a node to its neighbors to form a linked list. */
    inline void link(T* prev, T* curr, T* next) {
        // resize if necessary
        if (occupied > size * MAX_LOAD_FACTOR) {
            resize(exponent + 1);
        }

        // get index and step for double hashing
        Py_hash_t hash_val = curr->hash();
        size_t index = hash_val % size;
        size_t step = prime - (hash_val % prime);
        T* lookup = table[index];
        int comp;

        // search table
        while (lookup != NULL) {
            if (lookup != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(lookup->value, node->value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return -1;
                } else if (comp == 1) {  // value already present
                    PyErr_SetString(PyExc_ValueError, "Value already present");
                    return -1;
                }
            }

            // advance to next slot
            index = (index + step) % size;
            lookup = table[index];
        }

        // insert value
        table[index] = curr;
        occupied++;

        // link node to neighbors
        T::link(prev, curr, next);

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


    // TODO: the only new method that this class adds is search() and maybe
    // clear_tombstones().

    // search(T* node) {}
    // search(PyObject* value) {}

    // TODO: have to override clear() to reset the size of the hash table,
    // stage() to account for non-unique values in the iterable.





};


#endif // NODE_H include guard
