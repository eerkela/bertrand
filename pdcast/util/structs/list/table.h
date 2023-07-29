#include <cstddef>  // for size_t
#include <Python.h>  // for CPython API access


const size_t INITIAL_TABLE_SIZE = 16;  // initial size of hash table
const float MAX_LOAD_FACTOR = 0.7;  // resize when load factor exceeds this
const float MIN_LOAD_FACTOR = 0.2;  // resize when load factor drops below this
const float MAX_TOMBSTONES = 0.2;  // clear tombstones when this is exceeded
const size_t PRIMES[29] = {
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


/*ListTables allow O(1) lookup for elements within LinkedLists.  They should
not be used in any other context, and do not manage memory for their elements.

Note that this is a template class.  It must be instantiated with the type of
node that is stored within the list.
*/
template <typename T>
class ListTable {
private:
    T** table;          // array of pointers to nodes
    T* tombstone;       // sentinel value for deleted nodes
    size_t size;        // size of table
    size_t occupied;    // number of occupied slots (incl. tombstones)
    size_t tombstones;  // number of tombstones
    unsigned char exponent;    // log2(size) - log2(INITIAL_TABLE_SIZE)
    size_t prime;       // prime number used for double hashing

public:
    /*Constructor.

    This is called with during Cython `__cinit__()`, and throws a
    `std::bad_alloc` exception if memory allocation fails.
    */
    ListTable() {
        // if DEBUG
        printf("    -> malloc: ListTable(%lu)\n", INITIAL_TABLE_SIZE);
        table = (T**)calloc(INITIAL_TABLE_SIZE, sizeof(T*));
        if (!table) {
            throw std::bad_alloc();  // C++ equivalent of MemoryError
        }
        tombstone = (T*)malloc(sizeof(T));
        if (!tombstone) {
            throw std::bad_alloc();  // C++ equivalent of MemoryError
        }
        size = INITIAL_TABLE_SIZE;
        occupied = 0;
        tombstones = 0;
        exponent = 0;
        prime = PRIMES[exponent];
    };

    /*Destructor.  Always call this during Cython `__dealloc__()`.*/
    ~ListTable() {
        // if DEBUG
        printf("    -> free: ListTable(%lu)\n", size);
        free(table);
        free(tombstone);
    };

    /*copy/move constructors.  These are unnecessary and will never be used
    from Cython.
    */
    ListTable(const ListTable& other) = delete;         // copy constructor
    ListTable& operator=(const ListTable&) = delete;    // copy assignment
    ListTable(ListTable&&) = delete;                    // move constructor
    ListTable& operator=(ListTable&&) = delete;         // move assignment

    /*Add a node to the hash map for direct access.

    This normally exits with a return value of 0. If an error occurs during
    comparison, then -1 is returned and a Python exception is set.
    */
    int remember(T* node) {
        // resize if necessary
        if (occupied > size * MAX_LOAD_FACTOR) {
            resize(exponent + 1);
        }

        // get index and step for double hashing
        size_t index = node->hash % size;
        size_t step = prime - (node->hash % prime);
        T* curr = table[index];
        int comp;

        // search table
        while (curr != NULL) {
            if (curr != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(curr->value, node->value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return -1;
                } else if (comp == 1) {  // value already present
                    PyErr_SetString(PyExc_ValueError, "Value already present");
                    return -1;
                }
            }

            // advance to next slot
            index = (index + step) % size;
            curr = table[index];
        }

        // insert value
        table[index] = node;
        occupied++;
        return 0;
    };

    /*Remove a node from the hash map.

    This normally exits with a return value of 0. If an error occurs during
    comparison, -1 is returned and a Python exception is set.
    */
    int forget(T* node) {
        // get index and step for double hashing
        size_t index = node->hash % size;
        size_t step = prime - (node->hash % prime);
        T* curr = table[index];
        int comp;

        // search table
        while (curr != NULL) {
            if (curr != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(curr->value, node->value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return -1;
                } else if (comp == 1) {  // value found
                    table[index] = tombstone;
                    tombstones++;
                    if (exponent > 0 && occupied - tombstones < size * MIN_LOAD_FACTOR) {
                        resize(exponent - 1);
                    } else if (tombstones > size * MAX_TOMBSTONES) {
                        clear_tombstones();
                    }
                    return 0;
                }
            }

            // advance to next slot
            index = (index + step) % size;
            curr = table[index];
        }

        // value not found
        PyErr_SetString(PyExc_ValueError, "Value not found");
        return -1;
    }

    /*Clear the hash table and reset it to its initial state.

    This method throws a `std::bad_alloc` exception if memory allocation fails.
    */
    void clear() {
        // if DEBUG
        printf("    -> free: ListTable(%lu)\n", size);
        free(table);
        // if DEBUG
        printf("    -> malloc: ListTable(%lu)\n", INITIAL_TABLE_SIZE);
        table = (T**)calloc(INITIAL_TABLE_SIZE, sizeof(T*));
        if (!table) {
            throw std::bad_alloc();  // C++ equivalent of MemoryError
        }
        size = INITIAL_TABLE_SIZE;
        occupied = 0;
        tombstones = 0;
        exponent = 0;
        prime = PRIMES[exponent];
    }

    /*Search for a node in the hash map by its value.

    This returns NULL if the value is not found OR an error occurs during
    `hash()` or `==`.  If NULL is returned, the caller should always check for
    a Python exception using PyErr_Occurred().
    */
    T* search(PyObject* value) {
        // hash the search value
        Py_hash_t hash = PyObject_Hash(value);
        if (hash == -1 && PyErr_Occurred()) {  // error occurred during hash()
            return NULL;
        }

        // get index and step for double hashing
        size_t index = hash % size;
        size_t step = prime - (hash % prime);
        T* curr = table[index];
        int comp;

        // search table
        while (curr != NULL) {
            if (curr != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(curr->value, value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return NULL;
                } else if (comp == 1) {  // value found
                    return curr;
                }
            }

            // advance to next slot
            index = (index + step) % size;
            curr = table[index];
        }

        // value not found
        return NULL;
    }

    /*Search for a node directly.

    This is identical to search() except that it reuses a node's pre-computed
    hash for efficiency.  Users should still check for `PyErr_Occurred()`
    after calling this method.
    */
    T* search_node(T* node) {
        // get index and step for double hashing
        size_t index = node->hash % size;
        size_t step = prime - (node->hash % prime);
        T* curr = table[index];
        int comp;

        // search table
        while (curr != NULL) {
            if (curr != tombstone) {
                // CPython API equivalent of == operator
                comp = PyObject_RichCompareBool(curr->value, node->value, Py_EQ);
                if (comp == -1) {  // error occurred during ==
                    return NULL;
                } else if (comp == 1) {  // value found
                    return curr;
                }
            }

            // advance to next slot
            index = (index + step) % size;
            curr = table[index];
        }

        // value not found
        return NULL;
    }

    /*Resize the hash table and rehash its contents.

    This method throws a `std::bad_alloc` exception if memory allocation fails.
    */
    void resize(unsigned char new_exponent) {
        T** old_table = table;
        size_t old_size = size;
        size_t new_size = 1 << new_exponent;

        // if DEBUG
        printf("    -> malloc: ListTable(%lu)\n", new_size);

        // allocate new hash table
        table = (T**)calloc(new_size, sizeof(T*));
        if (!table) {
            throw std::bad_alloc();  // C++ equivalent of MemoryError
        }

        // update table parameters
        size = new_size;
        exponent = new_exponent;
        prime = PRIMES[new_exponent];

        size_t new_index, step;
        T* curr;

        // rehash old table and remove tombstones
        for (size_t i = 0; i < old_size; i++) {
            curr = old_table[i];
            if (curr != NULL && curr != tombstone) {
                // NOTE: we don't need to check for errors because we already
                // know that the old table is valid.
                new_index = curr->hash % new_size;
                step = prime - (curr->hash % prime);
                while (table[new_index] != NULL) {
                    new_index = (new_index + step) % new_size;
                }
                table[new_index] = curr;
            }
        }

        // reset tombstone count
        occupied -= tombstones;
        tombstones = 0;

        // free old table
        // if DEBUG
        printf("    -> free: ListTable(%lu)\n", old_size);
        free(old_table);
    }

    /*Clear tombstones from the hash table.

    This method throws a `std::bad_alloc` exception if memory allocation fails.
    */
    void clear_tombstones() {
        T** old_table = table;

        // if DEBUG
        printf("    -> malloc: ListTable(%lu)\n", size);

        // allocate new hash table
        table = (T**)calloc(size, sizeof(T*));
        if (!table) {
            throw std::bad_alloc();  // C++ equivalent of MemoryError
        }

        size_t new_index, step;
        T* curr;

        // rehash old table and remove tombstones
        for (size_t i = 0; i < size; i++) {
            curr = old_table[i];
            if (curr != NULL && curr != tombstone) {
                // NOTE: we don't need to check for errors because we already
                // know that the old table is valid.
                new_index = curr->hash % size;
                step = prime - (curr->hash % prime);
                while (table[new_index] != NULL) {
                    new_index = (new_index + step) % size;
                }
                table[new_index] = curr;
            }
        }

        // reset tombstone count
        occupied -= tombstones;
        tombstones = 0;

        // free old table
        // if DEBUG
        printf("    -> free: ListTable(%lu)\n", size);
        free(old_table);
    }

    /*Get the total amount of memory consumed by the hash table.*/
    size_t nbytes() {
        size_t total = sizeof(table);
        total += sizeof(tombstone);
        total += sizeof(size);
        total += sizeof(occupied);
        total += sizeof(tombstones);
        total += sizeof(exponent);
        total += sizeof(prime);
        return total;
    }

};
