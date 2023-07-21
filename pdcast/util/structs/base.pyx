"""This module contains pure C structs for use in other data structures.
"""
from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, free

cdef extern from "Python.h":
    int Py_EQ, Py_LT, Py_GT
    void Py_INCREF(PyObject* obj)
    void Py_DECREF(PyObject* obj)
    Py_hash_t PyObject_Hash(PyObject* obj)
    int PyObject_RichCompareBool(PyObject* obj1, PyObject* obj2, int opid)
    PyObject* PyErr_Occurred()


# TODO: store hash value in struct to avoid recomputing it every time we call
# resize() or clear_tombstones().  We could have separate structs for use with
# the hash table.

# cdef typedef fused ListStruct:
#     NodeStruct (doubly-linked list node)
#     HashStruct (same as ListStruct, but also stores hash value)
#     DictStruct (same as HashStruct, but also stores a mapped value)

# We can then compile different code paths by selecting a specialization.

# if ListStruct is NodeStruct:
#     ...
# if ListStruct is HashStruct:
#     ...
# if ListStruct is DictStruct:
#     ...


# We can even make specialized hash tables that use corresponding structs:

# cdef typedef fused ListTable:
#     HashTable (ListTable that uses HashStructs)
#     DictTable (ListTable that uses DictStructs)


#########################
####    CONSTANTS    ####
#########################


# DEBUG = TRUE adds print statements for memory allocation/deallocation to help
# identify memory leaks.
cdef bint DEBUG = True


cdef const size_t INITIAL_TABLE_SIZE = 16  # initial size of hash table
cdef const float MAX_LOAD_FACTOR = 0.7  # resize when load factor exceeds this
cdef const float MAX_TOMBSTONES = 0.2  # clear tombstones when this is exceeded
cdef const size_t[28] PRIMES = [
    # HASH PRIME    # TABLE SIZE                # AI AUTOCOMPLETE
    13,             # 16 (2**4)                 13
    23,             # 32 (2**5)                 23
    47,             # 64 (2**6)                 53
    97,             # 128 (2**7)                97
    181,            # 256 (2**8)                193
    359,            # 512 (2**9)                389
    719,            # 1024 (2**10)              769
    1439,           # 2048 (2**11)              1543
    2879,           # 4096 (2**12)              3079
    5737,           # 8192 (2**13)              6151
    11471,          # 16384 (2**14)             12289
    22943,          # 32768 (2**15)             24593
    45887,          # 65536 (2**16)             49157
    91753,          # 131072 (2**17)            98317
    183503,         # 262144 (2**18)            196613
    367007,         # 524288 (2**19)            393241
    734017,         # 1048576 (2**20)           786433
    1468079,        # 2097152 (2**21)           1572869
    2936023,        # 4194304 (2**22)           3145739
    5872033,        # 8388608 (2**23)           6291469
    11744063,       # 16777216 (2**24)          12582917
    23488103,       # 33554432 (2**25)          25165843
    46976221,       # 67108864 (2**26)          50331653
    93952427,       # 134217728 (2**27)         100663319
    187904861,      # 268435456 (2**28)         201326611
    375809639,      # 536870912 (2**29)         402653189
    751619321,      # 1073741824 (2**30)        805306457
    1503238603,     # 2147483648 (2**31)        1610612741
    3006477127,     # 4294967296 (2**32)        3221225473
    # NOTE: HASH PRIME is the first prime number larger than 0.7 * TABLE_SIZE
]


cdef void raise_exception() except *:
    """If the python interpreter is currently storing an exception, raise it.

    Notes
    -----
    Interacting with the Python C API can sometimes result in errors that are
    encoded in the function's output and checked by the `PyErr_Occurred()`
    interpreter flag.  This function can be called whenever this occurs in
    order to force that error to be raised as normal.
    """
    # Since we're using the except * Cython syntax, the error handler will be
    # invoked every time this function is called.  This means we don't have to
    # do anything here, just return void and let the built-in machinery do
    # all the work
    return


##################################
####    LISTSTRUCT METHODS    ####
##################################


cdef inline Node* allocate_node(PyObject* value):
    """Allocate a new ``Node`` to hold the referenced object.

    Parameters
    ----------
    value : PyObject*
        A reference to a Python object to use as the value of the returned
        node.  This method handles incrementing reference counters for the
        object.

    Returns
    -------
    Node*
        A reference to the allocated ``Node``.  This is a basic C struct that
        holds the minimum necessary information to construct a doubly-linked
        list.

    Raises
    ------
    MemoryError
        If ``malloc()`` fails to allocate a new block to hold the node.  This
        only happens if the system runs out of memory.

    Notes
    -----
    Every ``ListNode`` is allocated with a reference count of 1, indicating
    that it owns the underlying Python object.  The node will not be freed
    until this counter reaches 0, at which point it will decrement the refcount
    of the underlying object and expose it to garbage collection.

    A ``ListNode``'s reference counter is incremented whenever a
    :class:`NodeWrapper` is constructed around it and decremented whenever one
    is destroyed.  This is net neutral, and will never cause the node to be
    freed on its own.  However, if a node is removed from a list, then its
    reference counter will be decremented manually, and it will be freed when
    the counter reaches 0.  This might not occur immediately, but as soon as
    the last :class:`NodeWrapper` that references the node is collected by the
    ordinary Python garbage collector, then the underlying ``ListNode`` will be
    freed along with any other orphaned nodes that are connected to it.
    Whenever this occurs, the refcount of the referenced object(s) will also be
    decremented, allowing them to be garbage collected in turn.
    """
    if DEBUG:
        print(f"    -> malloc: {<object>value}")

    # allocate a new node
    cdef Node* node = <Node*>malloc(sizeof(Node))
    if node is NULL:  # malloc() failed to allocate new block
        raise MemoryError()

    # handle refcounters
    Py_INCREF(value)
    node.ref_count = 1

    # initialize
    node.value = value
    node.next = NULL
    node.prev = NULL

    # return reference to new node
    return node


cdef inline HashNode* allocate_hash_node(PyObject* value):
    """Allocate a new ``HashNode`` to hold the referenced object.

    Parameters
    ----------
    value : PyObject*
        A reference to a Python object to use as the value of the returned
        node.  This method handles incrementing reference counters for the
        object.

    Returns
    -------
    HashNode*
        A reference to the allocated ``HashNode``.  This is a specialized
        version of a standard ``Node`` that caches the hash value of the
        underlying Python object for use in hash tables.

    Raises
    ------
    MemoryError
        If ``malloc()`` fails to allocate a new block to hold the node.  This
        only happens if the system runs out of memory.

    Notes
    -----
    Every ``ListNode`` is allocated with a reference count of 1, indicating
    that it owns the underlying Python object.  The node will not be freed
    until this counter reaches 0, at which point it will decrement the refcount
    of the underlying object and expose it to garbage collection.

    A ``ListNode``'s reference counter is incremented whenever a
    :class:`NodeWrapper` is constructed around it and decremented whenever one
    is destroyed.  This is net neutral, and will never cause the node to be
    freed on its own.  However, if a node is removed from a list, then its
    reference counter will be decremented manually, and it will be freed when
    the counter reaches 0.  This might not occur immediately, but as soon as
    the last :class:`NodeWrapper` that references the node is collected by the
    ordinary Python garbage collector, then the underlying ``ListNode`` will be
    freed along with any other orphaned nodes that are connected to it.
    Whenever this occurs, the refcount of the referenced object(s) will also be
    decremented, allowing them to be garbage collected in turn.
    """
    if DEBUG:
        print(f"    -> malloc: {<object>value}")

    # TODO: figure out whether to hash the object here (before allocation) or
    # outside the function entirely.  We might just pass it in as an argument.

    # allocate a new node
    cdef HashNode* node = <HashNode*>malloc(sizeof(HashNode))
    if node is NULL:  # malloc() failed to allocate new block
        raise MemoryError()

    # handle refcounters
    Py_INCREF(value)
    node.ref_count = 1

    # initialize
    node.value = value
    node.next = NULL
    node.prev = NULL

    # return reference to new node
    return node


cdef inline DictNode* allocate_dict_node(PyObject* value, PyObject* mapped):
    """Allocate a new ``DictNode`` to hold the referenced object and a mapped
    value.

    Parameters
    ----------
    value : PyObject*
        A reference to a Python object to use as the value of the returned
        node.  This method handles incrementing reference counters for the
        object.
    mapped : PyObject*
        A reference to a Python object to use as the mapped value of the
        returned node.  This method handles incrementing reference counters for
        the object.

    Returns
    -------
    DictNode*
        A reference to the allocated ``DictNode``.  This is a specialized
        version of a standard ``HashNode`` that maps the underlying Python
        object to another Python object, allowing it to act as a dictionary
        item.

    Raises
    ------
    MemoryError
        If ``malloc()`` fails to allocate a new block to hold the node.  This
        only happens if the system runs out of memory.

    Notes
    -----
    Every ``ListNode`` is allocated with a reference count of 1, indicating
    that it owns the underlying Python object.  The node will not be freed
    until this counter reaches 0, at which point it will decrement the refcount
    of the underlying object and expose it to garbage collection.

    A ``ListNode``'s reference counter is incremented whenever a
    :class:`NodeWrapper` is constructed around it and decremented whenever one
    is destroyed.  This is net neutral, and will never cause the node to be
    freed on its own.  However, if a node is removed from a list, then its
    reference counter will be decremented manually, and it will be freed when
    the counter reaches 0.  This might not occur immediately, but as soon as
    the last :class:`NodeWrapper` that references the node is collected by the
    ordinary Python garbage collector, then the underlying ``ListNode`` will be
    freed along with any other orphaned nodes that are connected to it.
    Whenever this occurs, the refcount of the referenced object(s) will also be
    decremented, allowing them to be garbage collected in turn.
    """
    if DEBUG:
        print(f"    -> malloc: {<object>value}")

    # TODO: figure out whether to hash the object here (before allocation) or
    # outside the function entirely.  We might just pass it in as an argument.

    # allocate a new node
    cdef DictNode* node = <DictNode*>malloc(sizeof(DictNode))
    if node is NULL:  # malloc() failed to allocate new block
        raise MemoryError()

    # handle refcounters
    Py_INCREF(value)
    node.ref_count = 1

    # initialize
    node.value = value
    node.next = NULL
    node.prev = NULL
    node.mapped = mapped

    # return reference to new node
    return node


cdef inline void free_node(ListNode* node):
    """Delete a ``ListNode`` and decrement the reference counter of its
    underlying Python object.

    Parameters
    ----------
    node : ListNode*
        A pointer to the node to free.


    """
    if DEBUG:
        print(f"    -> free: {<object>node.value}")

    # nullify references to avoid dangling pointers
    if node.next is not NULL:
        node.next.prev = NULL
        node.next = NULL
    if node.prev is not NULL:
        node.prev.next = NULL
        node.prev = NULL

    Py_DECREF(node.value)  # decrement Python refcount
    free(node)  # free node



cdef inline void incref(ListNode* node):
    """Increment a node's reference counter, as well as that of the underlying
    Python object.

    Parameters
    ----------
    node : ListNode*
        A pointer to the node whose refcount will be incremented.

    Notes
    -----
    Every ``ListNode`` is allocated with a reference count of 1, indicating
    that it owns the underlying Python object.  The node will not be freed
    until this counter reaches 0, at which point it will decrement the refcount
    of the underlying object and expose it to garbage collection.

    A ``ListNode``'s reference counter is incremented whenever a
    :class:`NodeWrapper` is constructed around it and decremented whenever one
    is destroyed.  This is net neutral, and will never cause the node to be
    freed on its own.  However, if a node is removed from a list, then its
    reference counter will be decremented manually, and it will be freed when
    the counter reaches 0.  This might not occur immediately, but as soon as
    the last :class:`NodeWrapper` that references the node is collected by the
    ordinary Python garbage collector, then the underlying ``ListNode`` will be
    freed along with any other orphaned nodes that are connected to it.
    Whenever this occurs, the refcount of the referenced object(s) will also be
    decremented, allowing them to be garbage collected in turn.
    """
    if DEBUG:
        print(f"incref: {<object>node.value}")

    Py_INCREF(node.value)  # underlying Python object refcount
    node.ref_count += 1  # internal struct refcount


cdef inline void decref(ListNode* node):
    """Decrement a node's reference counter, as well as that of the underlying
    Python object.

    If the counter reaches zero, the node is freed.

    Parameters
    ----------
    node : ListNode*
        A pointer to the node to decrement.

    Notes
    -----
    Every ``ListNode`` is allocated with a reference count of 1, indicating
    that it owns the underlying Python object.  The node will not be freed
    until this counter reaches 0, at which point it will decrement the refcount
    of the underlying object and expose it to garbage collection.

    A ``ListNode``'s reference counter is incremented whenever a
    :class:`NodeWrapper` is constructed around it and decremented whenever one
    is destroyed.  This is net neutral, and will never cause the node to be
    freed on its own.  However, if a node is removed from a list, then its
    reference counter will be decremented manually, and it will be freed when
    the counter reaches 0.  This might not occur immediately, but as soon as
    the last :class:`NodeWrapper` that references the node is collected by the
    ordinary Python garbage collector, then the underlying ``ListNode`` will be
    freed along with any other orphaned nodes that are connected to it.
    Whenever this occurs, the refcount of the referenced object(s) will also be
    decremented, allowing them to be garbage collected in turn.
    """
    if DEBUG:
        print(f"decref: {<object>node.value}")

    # decrement internal reference counter
    node.ref_count -= 1

    # early return if not free()-able
    if node.ref_count != 0:
        return

    # free() node and decrement Python refcount
    cdef ListNode* forward = node.next
    cdef ListNode* backward = node.prev
    cdef ListNode* temp

    if DEBUG:
        print(f"    -> free: {<object>node.value}")

    # nullify references to avoid dangling pointers
    if node.next is not NULL:
        node.next.prev = NULL
        node.next = NULL
    if node.prev is not NULL:
        node.prev.next = NULL
        node.prev = NULL

    Py_DECREF(node.value)  # decrement Python refcount
    free(node)  # free node

    # NOTE: Whenever we remove a node, we implicitly remove a reference to
    # both of its neighbors.  This can lead to memory leaks if we remove a node
    # from the middle of the list, since the neighbors will become inaccessible
    # and cannot be freed.  To solve this, we emit a wave that propagates
    # outwards from the removed node in both directions, searching for a node
    # with refcount > 1 in that direction.  If one is found, we preserve all
    # the nodes in that direction, as they are still reachable via the
    # referenced node.  Otherwise, we can safely free them, as they would
    # become orphaned by the removal.

    cdef bint delete_forward = True
    cdef bint delete_backward = True

    # search forward
    temp = forward
    while temp is not NULL:
        if temp.ref_count > 1:
            delete_forward = False
            break
        temp = temp.next

    # search backward
    temp = backward
    while temp is not NULL:
        if temp.ref_count > 1:
            delete_backward = False
            break
        temp = temp.prev

    # delete orphaned nodes in the forward direction
    if delete_forward:
        while forward is not NULL:
            # remember next node
            temp = forward.next

            if DEBUG:
                print(f"    -> free: {<object>forward.value}")

            # NOTE: we don't need to nullify pointers here since we're
            # deleting all the way to the end of the list.

            # free node
            Py_DECREF(forward.value)
            free(forward)

            # advance to next node
            forward = temp

    # delete orphaned nodes in the backward direction
    if delete_backward:
        while backward is not NULL:
            # remember next node
            temp = backward.prev

            if DEBUG:
                print(f"    -> free: {<object>backward.value}")

            # NOTE: we don't need to nullify pointers here since we're
            # deleting all the way to the end of the list.

            # free node
            Py_DECREF(backward.value)
            free(backward)

            # advance to next node
            backward = temp

cdef inline void replace_value(ListNode* node, PyObject* value):
    """A helper function to replace the value that a node points to in-place.

    Parameters
    ----------
    node : ListNode*
        A pointer to the node to modify.
    value : PyObject*
        A reference to a Python object to use as the new value of the node.

    Notes
    -----
    This method handles incrementing/decrementing reference counters for both
    the current and new values.
    """
    Py_INCREF(value)  # increment new value's refcount
    Py_DECREF(node.value)  # decrement old value's refcount
    node.value = value  # assign new value


#################################
####    LISTTABLE METHODS    ####
#################################


cdef inline FlatTable* allocate_flat_table():
    """Allocate a new hash table.

    Returns
    -------
    FlatTable*
        A reference to an empty hash table that stores ``HashNode`` structs.

    Raises
    ------
    MemoryError
        If ``calloc()`` fails to allocate a new block to hold the table.  This
        only happens when the system runs out of memory.

    Notes
    -----
    The hash table is allocated with a fixed size, as determined by the
    ``INITIAL_TABLE_SIZE`` constant.  This defaults to 16, and will be doubled
    whenever the load factor exceeds ``MAX_LOAD_FACTOR``.

    Note that the table does not manage reference counters for the objects it
    contains.  This is always handled by the :class:`ListStruct` objects
    themselves.
    """
    if DEBUG:
        print(f"    -> malloc: FlatTable({INITIAL_TABLE_SIZE}})")

    # allocate memory for struct
    cdef FlatTable* table = <FlatTable*>malloc(sizeof(FlatTable))
    if table is NULL:  # malloc() failed to allocate new block
        raise MemoryError()

    # allocate memory for hash table
    table.lookup = <HashNode**>calloc(INITIAL_TABLE_SIZE, sizeof(HashNode*))
    if table.lookup is NULL:  # calloc() failed to allocate new block
        raise MemoryError()

    # allocate tombstone value
    table.tombstone = <HashNode*>malloc(sizeof(HashNode))
    table.tombstone_count = 0

    # initialize table
    table.size = INITIAL_TABLE_SIZE
    table.occupied = 0
    table.exponent = 4
    table.prime = PRIMES[0]
    return table


cdef inline DictTable* allocate_dict_table():
    """Allocate a new hash table.

    Returns
    -------
    DictTable*
        A reference to an empty hash table that stores ``HashNode`` structs.

    Raises
    ------
    MemoryError
        If ``calloc()`` fails to allocate a new block to hold the table.  This
        only happens when the system runs out of memory.

    Notes
    -----
    The hash table is allocated with a fixed size, as determined by the
    ``INITIAL_TABLE_SIZE`` constant.  This defaults to 16, and will be doubled
    whenever the load factor exceeds ``MAX_LOAD_FACTOR``.

    Note that the table does not manage reference counters for the objects it
    contains.  This is always handled by the :class:`ListNode` objects
    themselves.
    """
    if DEBUG:
        print(f"    -> malloc: DictTable({INITIAL_TABLE_SIZE}})")

    # allocate memory for struct
    cdef DictTable* table = <DictTable*>malloc(sizeof(DictTable))
    if table is NULL:  # malloc() failed to allocate new block
        raise MemoryError()

    # allocate memory for hash table
    table.lookup = <DictNode**>calloc(INITIAL_TABLE_SIZE, sizeof(DictNode*))
    if table.lookup is NULL:  # calloc() failed to allocate new block
        raise MemoryError()

    # allocate tombstone value
    table.tombstone = <DictNode*>malloc(sizeof(DictNode))
    table.tombstone_count = 0

    # initialize table
    table.size = INITIAL_TABLE_SIZE
    table.occupied = 0
    table.exponent = 4
    table.prime = PRIMES[0]
    return table


cdef inline void free_table(ListTable* table):
    """Free a ``ListTable``.

    Parameters
    ----------
    table : ListTable*
        A pointer to the table to free.

    Notes
    -----
    This method does not free the objects contained in the table.  This is
    always handled by the :class:`ListNode` objects themselves.
    """
    if DEBUG:
        print(f"    -> free: ListTable({table.size})")

    free(table.lookup)  # free hash table
    frro(table.tombstone)  # free tombstone value
    free(table)  # free node


cdef inline void resize(ListTable* table):
    """Grow a hash table and rehash its contents.

    Parameters
    ----------
    table : ListTable*
        A pointer to the table to resize.
    """
    # get old hash table
    cdef HashableNode** old_lookup = table.lookup
    cdef size_t old_size = table.size

    # update table parameters
    table.size *= 2
    table.exponent += 1
    table.prime = PRIMES[table.exponent]

    if DEBUG:
        print(f"    -> malloc: ListTable({table.size})")

    # allocate new hash table
    if ListTable is FlatTable:
        table.lookup = <HashNode**>calloc(table.size, sizeof(HashNode*))
    elif ListTable is DictTable:
        table.lookup = <DictNode**>calloc(table.size, sizeof(DictNode*))

    # check if calloc() failed to allocate new block
    if table.lookup is NULL:
        raise MemoryError()

    cdef size_t index, new_index, step
    cdef HashableNode* existing

    # rehash values and remove tombstones
    for index in range(old_size):
        existing = table.lookup[index]
        if existing is not NULL and existing is not table.tombstone:
            # NOTE: we don't need to handle error codes here since we know
            # each object was valid when we inserted it the first time.
            new_index = existing.hash % table.size
            step = table.prime - (existing.hash % table.prime)  # double hashing

            # find empty slot
            while table.lookup[new_index] is not NULL:
                new_index = (new_index + step) % table.size

            # insert value
            table.lookup[new_index] = existing

    # reset tombstone count
    table.occupied -= table.tombstone_count
    table.tombstone_count = 0

    if DEBUG:
        print(f"    -> free: ListTable({old_size})")

    # free old hash table
    free(old_lookup)


cdef inline void clear_tombstones(ListTable* table):
    """Clear tombstones from a hash table.

    Parameters
    ----------
    table : ListTable*
        A pointer to the table to clear.

    Notes
    -----
    Tombstones are inserted into the hash table whenever a value is removed.
    These cause lookups to skip over slots in the table, which can lead to
    performance degradation.  This method removes all tombstones from the table
    and rehashes its contents.
    """
    # get old hash table
    cdef HashableNode** old_lookup = table.lookup

    if DEBUG:
        print(f"    -> malloc: ListTable({table.size})    <- tombstones")

    # allocate new hash table
    if ListTable is FlatTable:
        table.lookup = <HashNode**>calloc(table.size, sizeof(HashNode*))
    elif ListTable is DictTable:
        table.lookup = <DictNode**>calloc(table.size, sizeof(DictNode*))

    # check if calloc() failed to allocate new block
    if table.lookup is NULL:  # calloc() failed to allocate new block
        raise MemoryError()

    # copy all non-tombstone values into new table
    cdef size_t index, new_index, step
    cdef HashableNode* existing

    for index in range(table.size):
        existing = old_lookup[index]
        if existing is not NULL and existing is not table.tombstone:
            # NOTE: we don't need to handle error codes here since we know
            # each object was valid when we inserted it the first time.
            new_index = existing.hash % table.size
            step = table.prime - (existing.hash % table.prime)  # double hashing

            # find empty slot
            while table.lookup[new_index] is not NULL:
                new_index = (new_index + step) % table.size

            # insert value
            table.lookup[new_index] = existing

    # reset tombstone count
    table.occupied -= table.tombstone_count
    table.tombstone_count = 0

    if DEBUG:
        print(f"    -> free: ListTable({old_size})    <- tombstones")

    # free old hash table
    free(old_lookup)


cdef inline void insert(ListTable* table, HashableNode* node):
    """Insert a node into a hash table.

    Parameters
    ----------
    table : ListTable*
        A pointer to the table to insert into.
    node : HashableNode*
        A pointer to the node to insert.  This function will use the node's
        ``value`` attribute as a key to the hash map.

    Notes
    -----
    This method does not increment the reference counter of the node.  This
    is always handled by the :class:`ListNode` objects themselves.
    """
    # grow table and rehash values if necessary
    if table.occupied > MAX_LOAD_FACTOR * table.size:
        resize(table)

    # get index in hash table
    cdef size_t index = node.hash % table.size
    cdef size_t step = table.prime - (node.hash % table.prime)  # double hashing
    cdef HashableNode* candidate = table.lookup[index]
    cdef int comp

    # find empty slot
    while candidate is not NULL:
        if candidate is not table.tombstone:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(candidate.value, node.value, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # replace node if equal
            elif comp == 1:
                table[index] = node
                return

        # advance to next slot
        index = (index + step) % table.size
        candidate = table.lookup[index]

    # insert node
    table.lookup[index] = node
    table.occupied += 1


cdef inline HashableNode* search(ListTable* table, PyObject* key):
    """Search a hash table for a node with a given key.

    Parameters
    ----------
    table : ListTable*
        A pointer to the table to search.
    key : PyObject*
        A reference to a Python object to use as the key to the hash map.

    Returns
    -------
    HashableNode*
        A pointer to the first node in the table with a matching key, or
        ``NULL`` if no such node exists.

    Notes
    -----
    This method does not increment the reference counter of the node.  This
    is always handled by the :class:`ListNode` objects themselves.
    """
    # hash Python object
    cdef Py_hash_t hash_val = PyObject_Hash(key)
    if hash_val == -1:  # hash() failed
        raise_exception()

    # get index in hash table
    cdef size_t index = hash_val % table.size
    cdef size_t step = table.prime - (hash_val % table.prime)  # double hashing
    cdef HashableNode* candidate = table.lookup[index]
    cdef int comp

    # find node
    while candidate is not NULL:
        if candidate is not table.tombstone:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(candidate.value, key, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # return node if equal
            elif comp == 1:
                return candidate

        # advance to next slot
        index = (index + step) % table.size
        candidate = table.lookup[index]

    raise KeyError(key)


cdef inline void remove(ListTable* table, PyObject* key):
    """Remove a node from a hash table.

    Parameters
    ----------
    table : ListTable*
        A pointer to the table to remove from.
    key : PyObject*
        A reference to a Python object to use as the key to the hash map.

    Notes
    -----
    This method does not decrement the reference counter of the node.  This
    is always handled by the :class:`ListNode` objects themselves.
    """
    # hash Python object
    cdef Py_hash_t hash_val = PyObject_Hash(key)
    if hash_val == -1:  # hash() failed
        raise_exception()

    # get index in hash table
    cdef size_t index = hash_val % table.size
    cdef size_t step = table.prime - (hash_val % table.prime)  # double hashing
    cdef HashableNode* candidate = table.lookup[index]
    cdef int comp

    # find node
    while candidate is not NULL:
        if candidate is not table.tombstone:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(candidate.value, key, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # remove node if equal
            elif comp == 1:
                # clear tombstones if necessary
                table.lookup[index] = table.tombstone  # mark slot as tombstone
                table.tombstone_count += 1
                if table.tombstone_count > MAX_TOMBSTONES * table.size:
                    clear_tombstones(table)
                return    

        # advance to next slot
        index = (index + step) % table.size
        candidate = table.lookup[index]

    raise KeyError(key)
