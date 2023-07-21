
from typing import Hashable, Iterable

from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, calloc, free
from libcpp.unordered_set cimport unordered_set
from libcpp.vector cimport vector

from .base cimport DEBUG, ListNode, ListTable, raise_exception

cdef extern from "Python.h":
    int Py_EQ
    Py_hash_t PyObject_Hash(PyObject* obj)
    int PyObject_RichCompareBool(PyObject* obj1, PyObject* obj2, int opid)
    int Py_INCREF(PyObject* obj)
    int Py_DECREF(PyObject* obj)
    PyObject* PyErr_Occurred()


#########################
####    CONSTANTS    ####
#########################


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


#######################
####    CLASSES    ####
#######################


cdef class HashedList(LinkedList):
    """A pure Cython implementation of a doubly-linked list where every element
    is hashable and unique.

    Parameters
    ----------
    items : Iterable[Hashable], optional
        An iterable of hashable items to initialize the list.

    Attributes
    ----------
    head : ListNode
        The first node in the list.
    tail : ListNode
        The last node in the list.
    items : dict
        A dictionary mapping items to their corresponding nodes for fast access.

    Notes
    -----
    This data structure is a special case of :class:`LinkedList` where every
    value is both unique and hashable.  This allows it to use a dictionary to
    map values to their corresponding nodes, which allows for O(1) removals and
    membership checks.

    For an implementation without these constraints, see the base
    :class:`LinkedList`.
    """

    def __cinit__(self):
        if DEBUG:
            print(f"    -> malloc: ListTable({INITIAL_TABLE_SIZE}})")

        # allocate table struct
        cdef NodeTable* table = <NodeTable*>malloc(sizeof(NodeTable))
        if table is NULL:  # malloc() failed to allocate a new block
            raise MemoryError()

        # allocate hash map
        table.map = <HashNode**>calloc(INITIAL_TABLE_SIZE, sizeof(HashNode*))
        if table.map is NULL:  # calloc() failed to allocate a new block
            raise MemoryError()

        # allocate tombstone value
        table.tombstone = <HashNode*>malloc(sizeof(HashNode))
        if table.tombstone is NULL:  # malloc() failed to allocate a new block
            raise MemoryError()

        # initialize table
        table.size = INITIAL_TABLE_SIZE
        table.occupied = 0
        table.tombstones = 0
        table.exponent = 0
        table.prime = PRIMES[0]

        # assign to self
        self.table = table

    def __dealloc__(self):
        if DEBUG:
            print(f"    -> free: NodeTable({self.table.size})")

        self._clear()  # free all nodes
        free(self.table.map)  # free hash map
        free(self.table.tombstone)  # free tombstone value
        free(self.table)  # free table struct

    ######################
    ####    APPEND    ####
    ######################

    cdef void append(self, object item):
        """Add an item to the end of the list.

        Parameters
        ----------
        item : object
            The item to add to the list.

        Raises
        ------
        TypeError
            If the item is not hashable.
        ValueError
            If the item is already contained in the list.

        Notes
        -----
        Appends are O(1) for both ends of the list.
        """
        # check if item is already present
        if self.__contains__(item):
            raise ValueError(f"list elements must be unique: {repr(item)}")

        LinkedList.append(self, item)  # calls overloaded _allocate_node()

    cdef void appendleft(self, object item):
        """Add an item to the beginning of the list.

        Parameters
        ----------
        item : object
            The item to add to the list.

        Raises
        ------
        TypeError
            If the item is not hashable.
        ValueError
            If the item is already contained in the list.

        Notes
        -----
        Appends are O(1) for both ends of the list.
        
        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.
        """
        # check if item is already present
        if self.__contains__(item):
            raise ValueError(f"list elements must be unique: {repr(item)}")

        LinkedList.appendleft(self, item)  # calls overloaded _allocate_node()

    cdef void insert(self, object item, long long index):
        """Insert an item at the specified index.

        Parameters
        ----------
        item : object
            The item to add to the list.
        index : int64
            The index at which to insert the item.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.

        Raises
        ------
        IndexError
            If the index is out of bounds.
        TypeError
            If the item is not hashable.
        ValueError
            If the item is already contained in the list.

        Notes
        -----
        Integer-based inserts are O(n) on average.
        """
        # check if item is already present
        if self.__contains__(item):
            raise ValueError(f"list elements must be unique: {repr(item)}")

        LinkedList.insert(self, item, index)  # calls overloaded _allocate_node()

    cdef void insert_after(self, object sentinel, object item):
        """Insert an item immediately after the specified sentinel.

        Parameters
        ----------
        sentinel : object
            The value after which to insert the new value.
        item : object
            The value to insert into the list.

        Raises
        ------
        KeyError
            If the sentinel is not contained in the list.
        TypeError
            If the value is not hashable.
        ValueError
            If the value is already contained in the list.

        Notes
        -----
        Value-based inserts are O(1) thanks to the hash map of contained items.

        This is significantly faster than an `index()` lookup followed by an
        `insert()` call, which would be O(2n) on average.
        """
        # look up sentinel node
        cdef HashableNode* curr = self._search_node(sentinel)

        # check sentinel exists
        if curr is NULL:
            raise KeyError(f"{repr(sentinel)} is not contained in the list")

        # allocate and link new node
        cdef HashableNode* node = self._allocate_node(<PyObject*>item)
        self._link_node(curr, node, curr.next)

    cdef void insert_before(self, object sentinel, object item):
        """Insert an item immediately before the specified sentinel.

        Parameters
        ----------
        sentinel : object
            The value before which to insert the new value.
        item : object
            The value to insert into the list.

        Raises
        ------
        KeyError
            If the sentinel is not contained in the list.
        TypeError
            If the value is not hashable.
        ValueError
            If the value is already contained in the list.

        Notes
        -----
        Value-based inserts are O(1) thanks to the hash map of contained items.

        This is significantly faster than an `index()` lookup followed by an
        `insert()` call, which would be O(2n) on average.
        """
        # look up sentinel node
        cdef HashableNode* curr = self._search_node(sentinel)

        # check sentinel exists
        if curr is NULL:
            raise KeyError(f"{repr(sentinel)} is not contained in the list")

        # allocate and link new node
        cdef HashableNode* node = self._allocate_node(<PyObject*>item)
        self._link_node(curr.prev, node, curr)

    cdef void extend(self, object items):
        """Add a sequence of items to the end of the list.

        Parameters
        ----------
        items : Iterable[Hashable]
            The values to add to the list.

        Raises
        ------
        TypeError
            If any values are not hashable.
        ValueError
            If any values are already contained in the list.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.
        """
        # NOTE: we stage the items in a temporary list first so we can
        # guarantee that they are all unique and hashable.

        cdef HashableNode* temp =  


        # TODO: probably the best strategy is to just allocate a new node for
        # each value within a try/except block and link them together into their
        # own list.  Then we just append that list to the end of the current
        # one.  This automatically checks for uniqueness/hashability for us,
        # and avoids us having to iterate through the list twice.



    cdef void extend_after(self, object sentinel, object other):
        """Insert a sequence of items immediately after the specified sentinel.

        Parameters
        ----------
        sentinel : object
            The value after which to insert the new values.
        other : Iterable[Hashable]
            The values to insert into the list.

        Raises
        ------
        KeyError
            If the sentinel is not contained in the list.
        TypeError
            If any values are not hashable.
        ValueError
            If any values are already contained in the list.

        Notes
        -----
        Thanks to the hash map of contained items, value-based inserts are
        O(1).

        This is significantly faster than an `index()` lookup followed by a
        slice injection, which would be O(2n + m) on average.
        """
        # look up sentinel node
        cdef HashableNode* curr = self._search_node(sentinel)

        # check sentinel exists
        if curr is NULL:
            raise KeyError(f"{repr(sentinel)} is not contained in the list")

        # NOTE: we stage the items in a temporary list first so we can guarantee
        # that they are all unique before inserting them into the list


    def __mul__(self, repeat: int) -> "HashedList":
        """Repeat the list a specified number of times.

        Parameters
        ----------
        repeat : int
            The number of times to repeat the list.

        Returns
        -------
        LinkedList
            A new list containing successive copies of this list, repeated the
            given number of times.  Due to the uniqueness constraint, this will
            always be either an empty list or a copy of this list.

        Raises
        ------
        ValueError
            If `repeat` is not 0 or 1.

        Notes
        -----
        Due to the uniqueness constraint, repetition is always O(1) for
        :class:`HashedLists`.
        """
        if repeat == 0:
            return type(self)()
        if repeat == 1:
            return self.copy()

        raise ValueError("repetition count must be 0 or 1")

    def __imul__(self, repeat: int) -> "HashedList":
        """Repeat the list a specified number of times in-place.

        Parameters
        ----------
        repeat : int
            The number of times to repeat the list.

        Returns
        -------
        LinkedList
            This list, repeated the given number of times.  Due to the
            uniqueness constraint, this will always be either an empty list or
            the list itself.

        Raises
        ------
        ValueError
            If `repeat` is not 0 or 1.

        Notes
        -----
        Due to the uniqueness constraint, repetition is always O(1) for
        :class:`HashedLists`.
        """
        if repeat == 0:
            self.clear()
        elif repeat != 1:
            raise ValueError("repetition count must be 0 or 1")

        return self

    #####################
    ####    INDEX    ####
    #####################

    cdef long long count(self, object item):
        """Count the number of occurrences of an item in the list.

        Parameters
        ----------
        item : object
            The item to count.

        Returns
        -------
        int64
            The number of occurrences of the item in the list.

        Notes
        -----
        Due to the uniqueness constraint, this method is equivalent to a
        simple :meth:`LinkedList.__contains__` check.
        """
        return <long long>(self.__contains__(item))

    cdef long long index(
        self,
        object item,
        long long start = 0,
        long long stop = -1
    ):
        """Get the index of an item within the list.

        Parameters
        ----------
        item : object
            The item to search for.

        Returns
        -------
        int64
            The index of the item within the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.

        Notes
        -----
        Indexing is O(n) on average.
        """
        # the hash map allows us to do O(1) membership checks
        if not self.__contains__(item):
            raise ValueError(f"{repr(item)} is not contained in the list")

        return LinkedList.index(self, item, start, stop)

    def __setitem__(self, key: int | slice, value: Hashable) -> None:
        """Set the value of an item or slice in the list.

        Parameters
        ----------
        key : int64 or slice
            The index or slice to set in the list.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.
        value : object
            The value or values to set at the specified index or slice.  If
            ``key`` is a slice, then ``value`` must be an iterable of the same
            length.

        Raises
        ------
        TypeError
            If any values are not hashable.
        ValueError
            If any values are already contained in the list, or if the length
            of ``value`` does not match the length of the slice.
        IndexError
            If the index is out of bounds.

        See Also
        --------
        LinkedList.__getitem__ :
            Index the list for a particular item or slice.
        LinkedList.__delitem__ :
            Delete an item or slice from the list.

        Notes
        -----
        Integer-based assignment is O(n) on average.

        Slice assignment is optimized to always begin iterating from the end
        nearest to a slice boundary, and to never backtrack.  This is done by
        checking whether the slice is ascending (step > 0) or descending, and
        whether the start or stop index is closer to its respective end.  This
        gives the following cases:

            1) ascending, start closer to head than stop is to tail
                -> forwards from head
            2) ascending, stop closer to tail than start is to head
                -> backwards from tail
            3) descending, start closer to tail than stop is to head
                -> backwards from tail
            4) descending, stop closer to head than start is to tail
                -> forwards from head
        """
        cdef ListNode* curr
        cdef long long slice_size
        cdef long long start, stop, step, i
        cdef long long index, end_index
        cdef unordered_set[PyObject*] replaced_items
        cdef vector[Pair] staged
        cdef Pair p  # for iterating over `staged`
        cdef object val, old_item

        # support slicing
        if isinstance(key, slice):
            # get indices of slice
            start, stop, step = key.indices(len(self))

            # check length of value matches length of slice
            slice_size = abs(stop - start) // abs(1 if step == 0 else abs(step))
            if not hasattr(value, "__iter__") or len(value) != slice_size:
                raise ValueError(
                    f"attempt to assign sequence of size {len(value)} to slice "
                    f"of size {slice_size}"
                )

            # determine direction of traversal to avoid backtracking
            index, end_index = self._get_slice_direction(start, stop, step)

            # get first struct in slice, counting from nearest end
            curr = self._struct_at_index(index)

            # NOTE: due to the uniqueness constraint, we can't just blindly
            # overwrite values in the slice, as some of them might be present
            # elsewhere in the list.  We also don't care if a value is in the
            # masked items, since they will be overwritten anyway.  To address
            # this, we record the observed values and stage our changes to
            # avoid modifying values until we are sure they are valid.

            # forward traversal
            values_iter = iter(value)
            if end_index >= index:
                for val in values_iter:
                    if curr is NULL or index == end_index:
                        break

                    # check for uniqueness and stage the change
                    replaced_items.insert(curr.value)
                    if val in self.nodes and val not in replaced_items:
                        raise ValueError(
                            f"list elements must be unique: {repr(val)}"
                        )
                    p.first = curr
                    p.second = <PyObject*>val
                    staged.push_back(p)

                    # jump according to step size
                    for i in range(step):
                        if curr is NULL:
                            break
                        curr = curr.next

                    # increment index
                    index += step

            # backward traversal
            else:
                for val in reversed(list(values_iter)):
                    if curr is NULL or index == end_index:
                        break

                    # check for uniqueness and stage the change
                    replaced_items.insert(curr.value)
                    if val in self.nodes and val not in replaced_items:
                        raise ValueError(
                            f"list elements must be unique: {repr(val)}"
                        )
                    p.first = curr
                    p.second = <PyObject*>val
                    staged.push_back(p)

                    # jump according to step size
                    for i in range(step):
                        if curr is NULL:
                            break
                        curr = curr.prev

                    # decrement index
                    index -= step

            # everything's good: update the list
            for old_item in replaced_items:
                del self.nodes[old_item]
            for p in staged:
                replace_value(<ListNode*>p.first, <PyObject*>p.second)
                self.nodes[<PyObject*>p.second] = <ListNode*>p.first

        # index directly
        else:
            key = self._normalize_index(key)
            curr = self._struct_at_index(key)

            # check for uniqueness
            if value in self.nodes and value != <object>curr.value:
                raise ValueError(f"list elements must be unique: {repr(value)}")

            # update the node's item and the items map
            del self.nodes[curr.value]
            replace_value(curr, value)
            self.nodes[value] = curr

    ######################
    ####    REMOVE    ####
    ######################

    cdef void remove(self, object item):
        """Remove an item from the list.

        Parameters
        ----------
        item : object
            The item to remove from the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.

        Notes
        -----
        Removals are O(1) due to the presence of the hash map.
        """
        cdef ListNode* curr

        # check if item is present in hash map
        try:
            curr = self.nodes[item]
        except KeyError:
            raise ValueError(f"{repr(item)} is not contained in the list")

        # handle pointer arithmetic
        self._remove_struct(curr)

    cdef void clear(self):
        """Remove all items from the list.

        Notes
        -----
        This method is O(1).
        """
        LinkedList.clear(self)
        self.nodes.clear()  # clear the hash map

    #######################
    ####    PRIVATE    ####
    #######################

    cdef ListNode* _allocate_node(PyObject* value):
        """Allocate a new node and set its value.

        Parameters
        ----------
        value : PyObject*
            The value to set for the new node.  This must be hashable and
            unique.

        Returns
        -------
        ListNode*
            The newly allocated node.

        Raises
        ------
        TypeError
            If the value is not hashable.
        ValueError
            If the value is already contained in the list.

        Notes
        -----
        This method automatically adds the new node to this list's hash map.
        It should always be followed with a call to :meth:`_link_node()` to
        add the node to the list.
        """
        cdef ListTable* table = self.table

        # C API equivalent of the hash() function
        cdef Py_hash_t hash_val = PyObject_Hash(value)
        if hash_val == -1:  # hash() failed
            raise_exception()

        # check if item is already present
        cdef size_t index = hash_val % table.size
        cdef size_t step = table.prime - (hash_val % table.prime)
        cdef HashableNode* curr = table.map[index]
        cdef int comp

        # search table
        while curr is not NULL:
            # skip over tombstones
            if candidate is not table.tombstone:
                # C API equivalent of the == operator
                comp = PyObject_RichCompareBool(curr.value, value, Py_EQ)
                if comp == -1:  # == failed
                    raise_exception()

                # raise error if equal
                if comp == 1:
                    raise ValueError(f"list elements must be unique: {repr(value)}")

            # advance to next node
            index = (index + step) % table.size
            curr = table.map[index]

        if DEBUG:
            print(f"    -> malloc: {<object>value}")

        # allocate new node
        curr = <HashNode*>malloc(sizeof(HashNode))
        if curr is NULL:
            raise MemoryError()

        # increment reference count of underlying Python object
        Py_INCREF(value)

        # initialize node
        curr.value = value
        curr.hash = hash_val
        curr.next = NULL
        curr.prev = NULL

        # insert into table
        table.map[index] = curr

        # return reference to new node
        return node

    cdef void _free_node(self, HashableNode* node):
        """Free a node and decrement the reference count of its value.

        Parameters
        ----------
        node : HashableNode*
            The node to free.

        Notes
        -----
        This method also removes the node from the hash map.

        The node must be unlinked from the list before calling this method.
        Any remaining references to it will become dangling pointers.
        """
        cdef ListTable* table = self.table

        if DEBUG:
            print(f"    -> free: {<object>node.value}")

        # get index in hash map
        cdef size_t index = node.hash % table.size
        cdef size_t step = table.prime - (node.hash % table.prime)  # double hash
        cdef HashableNode* curr = table.map[index]
        cdef int comp

        # find node
        while curr is not NULL:
            # skip over tombstones
            if curr is not table.tombstone:
                # C API equivalent of the == operator
                comp = PyObject_RichCompareBool(curr.value, node.value, Py_EQ)
                if comp == -1:  # == failed
                    raise_exception()

                # remove node if equal
                if comp == 1:
                    table.map[index] = table.tombstone  # mark as tombstone
                    table.tombstones += 1

                    # clear tombstones if necessary
                    if table.tombstones > MAX_TOMBSTONES * table.size:
                        self._clear_tombstones()
                    break

            # advance to next slot
            index = (index + step) % table.size
            curr = table.map[index]

        # nullify pointers
        node.next = NULL
        node.prev = NULL

        # deallocate
        Py_DECREF(node.value)
        free(node)

    cdef void _resize_table(self):
        """Resize the hash table and rehash its contents.

        Notes
        -----
        This method is called automatically whenever the hash table exceeds the
        maximum load factor.  It is O(n), and simultaneously clears all
        tombstones it encounters.
        """
        cdef ListTable* table = self.table

        # remember old values
        cdef HashableNode** old_map = table.map
        cdef size_t old_size = table.size

        # update table parameters
        table.size = old_size * 2
        table.exponent += 1
        table.prime = PRIMES[table.exponent]

        if DEBUG:
            print(f"    -> malloc: ListTable({table.size})")

        # allocate new hash map
        if ListTable is NodeTable:
            table.map = <HashNode**>calloc(table.size, sizeof(HashNode*))
        elif ListTable is DictTable:
            table.map = <DictNode**>calloc(table.size, sizeof(DictNode*))

        # check if calloc() failed to allocate a new block
        if table.map is NULL:
            raise MemoryError()

        cdef size_t index, new_index, step
        cdef HashableNode* curr

        # rehash values and remove tombstones
        for index in range(old_size):
            curr = old_map[index]
            if curr is not NULL and curr is not table.tombstone:
                # NOTE: we don't need to handle error codes here since we know
                # each object was valid when we first inserted it.
                new_index = curr.hash % table.size
                step = table.prime - (curr.hash % table.prime)  # double hash

                # find an empty slot
                while table.map[new_index] is not NULL:
                    new_index = (new_index + step) % table.size

                # insert into new table
                table.map[new_index] = curr

        # reset tombstone count
        table.occupied -= table.tombstones
        table.tombstones = 0

        if DEBUG:
            print(f"    -> free: ListTable({old_size})")

        # free old hash map
        free(old_map)

    cdef void _clear_tombstones(self):
        """Clear all tombstones from the hash table.

        Notes
        -----
        This method is called automatically whenever the number of tombstones
        exceeds the maximum tombstone ratio.  It is O(n).

        Tombstones are inserted into the hash table whenever a node is
        removed.  These cause subsequent lookups to skip over the slot, which
        can lead to performance degradation.  This method removes all
        tombstones from the table and rehashes its contents in-place.
        """
        cdef ListTable* table = self.table

        # remember old table
        cdef HashableNode** old_map = table.map

        if DEBUG:
            print(f"    -> malloc: ListTable({table.size})    <- tombstones")

        # allocate new hash map
        if ListTable is NodeTable:
            table.map = <HashNode**>calloc(table.size, sizeof(HashNode*))
        elif ListTable is DictTable:
            table.map = <DictNode**>calloc(table.size, sizeof(DictNode*))

        # check if calloc() failed to allocate a new block
        if table.map is NULL:
            raise MemoryError()

        cdef size_t index, new_index, step
        cdef HashableNode* curr

        # rehash values and remove tombstones
        for index in range(table.size):
            curr = old_map[index]
            if curr is not NULL and curr is not table.tombstone:
                # NOTE: we don't need to handle error codes here since we know
                # each object was valid when we first inserted it.
                new_index = curr.hash % table.size
                step = table.prime - (curr.hash % table.prime)  # double hash

                # find an empty slot
                while table.map[new_index] is not NULL:
                    new_index = (new_index + step) % table.size

                # insert into new table
                table.map[new_index] = curr

        # reset tombstone count
        table.occupied -= table.tombstones
        table.tombstones = 0

        if DEBUG:
            print(f"    -> free: ListTable({table.size})    <- tombstones")

        # free old hash map
        free(old_map)

    cdef HashableNode* _search_node(self, PyObject* key):
        """Search the hash table for a node with the given value.

        Parameters
        ----------
        key : PyObject*
            The value to search for.

        Returns
        -------
        HashableNode*
            A reference to the node with the given value, or ``NULL`` if it is
            not present.

        Notes
        -----
        This method is used to translate between Python objects and their
        associated nodes, which are implemented as pure C structs.
        """
        cdef ListTable* table = self.table

        # C API equivalent of the hash() function
        cdef Py_hash_t hash_val = PyObject_Hash(key)
        if hash_val == -1:  # hash() failed
            raise_exception()

        # get index in hash table
        cdef size_t index = hash_val % table.size
        cdef size_t step = table.prime - (hash_val % table.prime)  # double hash
        cdef HashableNode* curr = table.map[index]
        cdef int comp

        # find node
        while candidate is not NULL:
            # skip over tombstones
            if candidate is not table.tombstone:
                # C API equivalent of the == operator
                comp = PyObject_RichCompareBool(key, candidate.value, Py_EQ)
                if comp == -1:  # == failed
                    raise_exception()

                # return node if equal
                if comp == 1:
                    return candidate

            # advance to next slot
            index = (index + step) % table.size
            candidate = table.map[index]

        return NULL


    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __contains__(self, item: Hashable) -> bool:
        """Check if the item is contained in the list.

        Parameters
        ----------
        item : object
            The item to search for.

        Returns
        -------
        bool
            Indicates whether the item is contained in the list.

        Notes
        -----
        This method is O(1) due to the hash map of contained items.
        """
        return self._search_node(<PyObject*>item) is not NULL

