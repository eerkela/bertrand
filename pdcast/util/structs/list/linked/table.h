// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_TABLE_H
#define BERTRAND_STRUCTS_CORE_TABLE_H

#include <cstddef>  // size_t
#include <cstdlib>  // calloc, free
#include <stdexcept>  // std::bad_alloc
#include <Python.h>  // CPython API
#include "allocate.h"  // DEBUG
#include "node.h"  // has_hash<>


// TODO: table should have move constructors, but not copy constructors.

// TODO: table and tombstone should be stack-allocated


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* These constants control the resizing and open addressing of elements within
a HashTable. */
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
////    TABLE    ////
/////////////////////


// NOTE: This is a custom implementation of a hash table using open addressing
// and double hashing.  It is specifically designed to store pointers to nodes
// in a `SetView` or `DictView` so that we can perform O(1) lookups from
// Python.  In this regard, it is extremely performant, but it should not be
// used for any other purpose.


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

    /* Look up a value in the hash table by providing an explicit hash. */
    Node* lookup(Py_hash_t hash, PyObject* value) const {
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

public:

    /* Copy constructors. These are disabled for the sake of efficiency,
    preventing us from unintentionally copying data. */
    HashTable(const HashTable& other) = delete;         // copy constructor
    HashTable& operator=(const HashTable&) = delete;    // copy assignment

    /* Construct an empty HashTable. */
    HashTable() :
        capacity(INITIAL_TABLE_CAPACITY), occupied(0), tombstones(0), exponent(0),
        prime(PRIMES[0])
    {
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
    }

    /* Move ownership from one HashTable to another (move constructor). */
    HashTable(HashTable&& other) :
        table(other.table), tombstone(other.tombstone), capacity(other.capacity),
        occupied(other.occupied), tombstones(other.tombstones),
        exponent(other.exponent), prime(other.prime)
    {
        // reset other table
        other.table = nullptr;
        other.tombstone = nullptr;
        other.capacity = 0;
        other.occupied = 0;
        other.tombstones = 0;
        other.exponent = 0;
        other.prime = PRIMES[0];
    }

    /* Move ownership from one HashTable to another (move assignment). */
    HashTable& operator=(HashTable&& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // free old table/tombstone
        if constexpr (DEBUG) {
            printf("    -> free: HashTable(%lu)\n", capacity);
        }
        free(table);
        free(tombstone);

        // transfer ownership of table/tombstone
        table = other.table;
        tombstone = other.tombstone;
        capacity = other.capacity;
        occupied = other.occupied;
        tombstones = other.tombstones;
        exponent = other.exponent;
        prime = other.prime;

        // reset other table
        other.table = nullptr;
        other.tombstone = nullptr;
        other.capacity = 0;
        other.occupied = 0;
        other.tombstones = 0;
        other.exponent = 0;
        other.prime = PRIMES[0];

        return *this;
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
    void reset() {
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

    /* Search for a node by reusing attributes from another node. */
    template <typename T>
    Node* search(T* key) const {
        Py_hash_t hash_val;
        if constexpr (has_hash<T>::value) {
            // if input node has a pre-computed hash, then we can reuse it to
            // speed up lookups
            hash_val = key->hash;
            return lookup(hash_val, key->value);
        }

        // otherwise, we have to compute the hash ourselves
        return search(key->value);
    }

    /* Search for a node by value. */
    Node* search(PyObject* key) const {
        Py_hash_t hash_val = PyObject_Hash(key);
        if (hash_val == -1 && PyErr_Occurred()) {
            return nullptr;
        }

        // delegate to shared lookup() method
        return lookup(hash_val, key);
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
        return sizeof(*this);
    }

};


#endif  // BERTRAND_STRUCTS_CORE_TABLE_H include guard
