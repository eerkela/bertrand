
from typing import Hashable, Iterable

from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, calloc, free
from libcpp.unordered_set cimport unordered_set
from libcpp.vector cimport vector

from .base cimport DEBUG, raise_exception

cdef extern from "Python.h":
    int Py_EQ
    Py_hash_t PyObject_Hash(PyObject* obj)
    int PyObject_RichCompareBool(PyObject* obj1, PyObject* obj2, int opid)
    int Py_INCREF(PyObject* obj)
    int Py_DECREF(PyObject* obj)
    PyObject* PyErr_Occurred()


# HashMap could be spun off into its own C++ class to support templating.  This
# would have to go in a separate header file though.

# cdef extern from "hashmap.h":
#     cdef cppclass HashMap[T]:
#         pass  # declare all methods here
# 
#     # declare specific instantiations of the generic class
#     cdef cppclass IntMap "HashMap<int>":
#         pass
#     cdef cppclass DoubleMap "HashMap<double>":
#         pass

# we can then use the HashMap class like so:
#     cdef IntMap map = new IntMap()
#     cdef DoubleMap map = new DoubleMap()


# the actual C++ file would contain something along the lines of:

#     template <typename T>
#     class HashMap {
#         private:
#             T** table;
#             T* tombstone;
#             size_t size;
#             size_t occupied;
#             size_t tombstones;
#             size_t exponent;
#             size_t prime;
#         public:
#             HashMap() {
#                 table = <T**>calloc(INITIAL_TABLE_SIZE, sizeof(T*));
#                 ...
#             }
#             ~HashMap() { ... }  # destructor
#             void insert(T* value) { ... }
#             void remove(T* value) { ... }
#             T* search(T value) { ... }
#             ...


# I can maybe also implement a C struct to hold a key to the table.  This would
# have two fields, a `PyObject*` and a `Py_hash_t`, which could be assigned
# before looking up the value in the table.  This would allow us to pre-compute
# hashes for values that are already in the table, which would save us a call
# to `PyObject_Hash()`.  This could maybe be a C++ class that overrides the
# hash and equality operators, which could make lookups easier and potentially
# make it compatible with a `std::unordered_map`-based approach.


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
    head : HashNode
        The first node in the list.
    tail : HashNode
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
            print(f"    -> malloc: ListTable({INITIAL_TABLE_SIZE})")

        # allocate table struct
        cdef ListTable* table = <ListTable*>malloc(sizeof(ListTable))
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
        self.table = table

        # set head/tail
        self.head = NULL
        self.tail = NULL

    def __dealloc__(self):
        if DEBUG:
            print(f"    -> free: ListTable({self.table.size})")

        self._clear()  # free all nodes
        free(self.table.map)  # free hash map
        free(self.table.tombstone)  # free tombstone value
        free(self.table)  # free table struct

    ########################
    ####    CONCRETE    ####
    ########################

    cdef void _append(self, PyObject* item):
        """Add an item to the end of the list.

        Parameters
        ----------
        item : PyObject*
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
        cdef HashNode* node = self._allocate_node(item)

        # add node to hash table
        try:
            self._remember_node(node)
        except ValueError:  # node is not unique
            self._free_node(node)
            raise

        # append to end of list
        self._link_node(self.tail, node, NULL)

    cdef void _appendleft(self, PyObject* item):
        """Add an item to the beginning of the list.

        Parameters
        ----------
        item : PyObject*
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
        cdef HashNode* node = self._allocate_node(item)

        # add node to hash table
        try:
            self._remember_node(node)
        except ValueError:  # node is not unique
            self._free_node(node)
            raise

        # append to beginning of list
        self._link_node(NULL, node, self.head)

    cdef void _insert(self, PyObject* item, long index):
        """Insert an item at the specified index.

        Parameters
        ----------
        item : PyObject*
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
        # allow negative indexing + check bounds
        cdef size_t norm_index = self._normalize_index(index)

        # allocate new node
        cdef HashNode* node = self._allocate_node(item)
        cdef HashNode* curr
        cdef size_t i

        # add node to hash table
        try:
            self._remember_node(node)
        except ValueError:  # node is not unique
            self._free_node(node)
            raise

        # insert node at specified index, starting from nearest end
        if norm_index <= self.size // 2:
            # iterate forwards from head
            curr = self.head
            for i in range(norm_index):
                curr = curr.next

            # insert before current node
            self._link_node(curr.prev, node, curr)

        else:
            # iterate backwards from tail
            curr = self.tail
            for i in range(self.size - norm_index - 1):
                curr = curr.prev

            # insert after current node
            self._link_node(curr, node, curr.next)

    cdef void _insertafter(self, PyObject* sentinel, PyObject* item):
        """Insert an item immediately after the specified sentinel.

        Parameters
        ----------
        sentinel : PyObject*
            The value after which to insert the new value.
        item : PyObject*
            The value to insert into the list.

        Raises
        ------
        KeyError
            If the sentinel is not contained in the list.
        TypeError
            If the value is not hashable.
        ValueError
            If the value is already contained in the list.
        """
        # look up sentinel node
        cdef HashNode* curr = self._search(sentinel)
        if curr is NULL:
            raise KeyError(f"{repr(sentinel)} is not contained in the list")

        # allocate new node
        cdef HashNode* node = self._allocate_node(item)

        # add node to hash table
        try:
            self._remember_node(node)
        except ValueError:  # node is not unique
            self._free_node(node)
            raise

        # insert node after sentinel
        self._link_node(curr, node, curr.next)

    cdef void _insertbefore(self, PyObject* sentinel, PyObject* item):
        """Insert an item immediately before the specified sentinel.

        Parameters
        ----------
        sentinel : PyObject*
            The value before which to insert the new value.
        item : PyObject*
            The value to insert into the list.

        Raises
        ------
        KeyError
            If the sentinel is not contained in the list.
        TypeError
            If the value is not hashable.
        ValueError
            If the value is already contained in the list.
        """
        # look up sentinel node
        cdef HashNode* curr = self._search(sentinel)
        if curr is NULL:
            raise KeyError(f"{repr(sentinel)} is not contained in the list")

        # allocate new node
        cdef HashNode* node = self._allocate_node(item)

        # add node to hash table
        try:
            self._remember_node(node)
        except ValueError:  # node is not unique
            self._free_node(node)
            raise

        # insert node before sentinel
        self._link_node(curr.prev, node, curr)

    cdef void _extend(self, PyObject* items):
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
        cdef HashNode* staged_head
        cdef HashNode* staged_tail
        cdef size_t count

        # NOTE: we stage the items in a temporary list to ensure we don't
        # modify the original if we encounter any errors
        staged_head, staged_tail, count = self._stage_nodes(items, False)  # TODO: handle override?
        if staged_head is NULL:
            return

        # append staged to end of list
        self.size += count
        if self.tail is NULL:
            self.head = staged_head
            self.tail = staged_tail
        else:
            self.tail.next = staged_head
            staged_head.prev = self.tail
            self.tail = staged_tail

        # add staged nodes to hash table
        while True:
            self._remember_node(staged_head)
            if staged_head is staged_tail:
                break
            staged_head = staged_head.next

    cdef void _extendleft(self, PyObject* items):
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
        cdef HashNode* staged_head
        cdef HashNode* staged_tail
        cdef size_t count

        # NOTE: we stage the items in a temporary list to ensure we don't
        # modify the original if we encounter any errors
        staged_head, staged_tail, count = self._stage_nodes(items, True)  # TODO: handle override?
        if staged_head is NULL:
            return

        # append staged to beginning of list
        self.size += count
        if self.head is NULL:
            self.head = staged_head
            self.tail = staged_tail
        else:
            self.head.prev = staged_tail
            staged_tail.next = self.head
            self.head = staged_head

        # add staged nodes to hash table
        while True:
            self._remember_node(staged_head)
            if staged_head is staged_tail:
                break
            staged_head = staged_head.next

    cdef void _extendafter(self, PyObject* sentinel, PyObject* other):
        """Insert a sequence of items immediately after the specified sentinel.

        Parameters
        ----------
        sentinel : PyObject*
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
        """
        # look up sentinel node
        cdef HashNode* curr = self._search_node(sentinel)
        if curr is NULL:
            raise KeyError(f"{repr(sentinel)} is not contained in the list")

        cdef HashNode* staged_head
        cdef HashNode* staged_tail
        cdef size_t count

        # NOTE: we stage the items in a temporary list to ensure we don't
        # modify the original if we encounter any errors
        staged_head, staged_tail, count = self._stage_nodes(items, False)  # TODO: handle override?
        if staged_head is NULL:
            return

        # insert staged immediately after sentinel
        self.count += count
        if curr.next is NULL:
            curr.next = staged_head
            staged_head.prev = curr
            self.tail = staged_tail
        else:
            curr.next.prev = staged_tail
            staged_tail.next = curr.next
            curr.next = staged_head
            staged_head.prev = curr

        # add staged nodes to hash table
        while True:
            self._remember_node(staged_head)
            if staged_head is staged_tail:
                break
            staged_head = staged_head.next

    cdef void _extendbefore(self, PyObject* sentinel, PyObject* other):
        """Insert a sequence of items immediately before the specified sentinel.

        Parameters
        ----------
        sentinel : PyObject*
            The value before which to insert the new values.
        other : Iterable[Hashable]
            The values to insert into the list.  Note that this method
            implicitly reverses the order of the elements.

        Raises
        ------
        KeyError
            If the sentinel is not contained in the list.
        TypeError
            If any values are not hashable.
        ValueError
            If any values are already contained in the list.
        """
        # look up sentinel node
        cdef HashNode* curr = self._search_node(sentinel)
        if curr is NULL:
            raise KeyError(f"{repr(sentinel)} is not contained in the list")

        cdef HashNode* staged_head
        cdef HashNode* staged_tail
        cdef size_t count

        # NOTE: we stage the items in a temporary list to ensure we don't
        # modify the original if we encounter any errors
        staged_head, staged_tail, count = self._stage_nodes(items, True)  # TODO: handle override?
        if staged_head is NULL:
            return

        # insert staged immediately before sentinel
        self.count += count
        if curr.prev is NULL:
            curr.prev = staged_tail
            staged_tail.next = curr
            self.head = staged_head
        else:
            curr.prev.next = staged_head
            staged_head.prev = curr.prev
            curr.prev = staged_tail
            staged_tail.next = curr

        # add staged nodes to hash table
        while True:
            self._remember_node(staged_head)
            if staged_head is staged_tail:
                break
            staged_head = staged_head.next

    cdef size_t index(self, PyObject* item, long start = 0, long stop = -1):
        """Get the index of an item within the list.

        Parameters
        ----------
        item : PyObject*
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
        # look up item in hash map
        cdef HashNode* node = self._search(item)
        if node is NULL:
            raise ValueError(f"{repr(item)} is not contained in the list")

        # normalize start/stop indices
        cdef size_t norm_start = self._normalize_index(start)
        cdef size_t norm_stop = self._normalize_index(stop)
        cdef size_t index = 0

        # count backwards to the start of the list
        while node is not NULL and index < norm_stop:
            node = node.prev
            index += 1

        # check if item was found in the specified range
        if not norm_start <= index < norm_stop:
            raise ValueError(f"{repr(item)} is not contained in the list")

        return index

    cdef size_t count(self, PyObject* item):
        """Count the number of occurrences of an item in the list.

        Parameters
        ----------
        item : PyObject*
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
        return self._search(item) is not NULL

    cdef void _remove(self, PyObject* item):
        """Remove an item from the list.

        Parameters
        ----------
        item : PyObject*
            The item to remove from the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.

        Notes
        -----
        Removals are O(1) due to the presence of the hash map.
        """
        cdef HashNode* node = self._search(item)

        if node is NULL:
            raise ValueError(f"{repr(item)} is not contained in the list")

        self._unlink_node(node)
        self._forget_node(node)
        self._free_node(node)

    cdef PyObject* _pop(self, long index = -1):
        """Remove and return the item at the specified index.

        Parameters
        ----------
        index : long int, optional
            The index of the item to remove.  If this is negative, it will be
            translated to a positive index by counting backwards from the end
            of the list.  The default is ``-1``, which removes the last item.

        Returns
        -------
        PyObject*
            The item that was removed from the list.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        Notes
        -----
        Pops are O(1) if ``index`` points to either of the list's ends, and
        O(n) otherwise.
        """
        # normalize index and get corresponding node
        cdef size_t norm_index = self._normalize_index(index)
        cdef HashNode* node = self._node_at_index(norm_index)
        cdef PyObject* value = node.value

        # we have to increment the reference counter of the popped object to
        # ensure it isn't garbage collected when we free the node.
        Py_INCREF(value)

        # drop node and return contents
        self._unlink_node(node)
        self._forget_node(node)
        self._free_node(node)
        return value

    cdef PyObject* _popleft(self):
        """Remove and return the first item in the list.

        Returns
        -------
        PyObject*
            The item that was removed from the list.

        Raises
        ------
        IndexError
            If the list is empty.

        Notes
        -----
        This is equivalent to :meth:`LinkedList.pop` with ``index=0``, but it
        avoids the overhead of handling indices and is thus more efficient in
        the specific case of removing the first item.
        """
        if self.head is NULL:
            raise IndexError("pop from empty list")

        # no need to handle indices, just skip straight to head
        cdef HashNode* node = self.head
        cdef PyObject* value = node.value

        # we have to increment the reference counter of the popped object to
        # ensure it isn't garbage collected when we free the node.
        Py_INCREF(value)

        # drop node and return contents
        self._unlink_node(node)
        self._forget_node(node)
        self._free_node(node)
        return value

    cdef PyObject* _popright(self):
        """Remove and return the last item in the list.

        Returns
        -------
        PyObject*
            The item that was removed from the list.

        Raises
        ------
        IndexError
            If the list is empty.

        Notes
        -----
        This is equivalent to :meth:`LinkedList.pop` with ``index=-1``, but it
        avoids the overhead of handling indices and is thus more efficient in
        the specific case of removing the last item.
        """
        if self.tail is NULL:
            raise IndexError("pop from empty list")

        # no need to handle indices, just skip straight to tail
        cdef HashNode* node = self.tail
        cdef PyObject* value = node.value

        # we have to increment the reference counter of the popped object to
        # ensure it isn't garbage collected when we free the node.
        Py_INCREF(value)

        # drop node and return contents
        self._unlink_node(node)
        self._forget_node(node)
        self._free_node(node)
        return value

    cdef void _clear(self):
        """Remove all items from the list.

        Notes
        -----
        This method is O(1).
        """
        cdef HashNode* node = self.head
        cdef HashNode* temp

        # free all nodes
        while node is not NULL:
            temp = node
            node = node.next
            self._forget_node(temp)
            self._free_node(temp)

        # TODO: maybe inline the tombstone thing.  Rather than iterating
        # through the list, we could just iterate through the hash map and
        # free the nodes as we go.

        # TODO: resize the hash map back to its initial size

        # take the opportunity to clear out tombstones
        self._clear_tombstones()

        # avoid dangling pointers
        self.head = NULL
        self.tail = NULL
        self.size = 0

    cdef void _sort(self):
        """Sort the list in-place.

        Notes
        -----
        Sorting is O(n log n) on average.
        
        This method uses an iterative merge sort algorithm that avoids the
        extra memory overhead required to handle recursive stack frames.
        """
        # trivial case: empty list
        if self.head is NULL:
            return

        # NOTE: as a refresher, the general merge sort algorithm is as follows:
        #   1) divide the list into sublists of length 1 (bottom-up)
        #   2) sort pairs of elements from left to right and merge
        #   3) double the length of the sublists and repeat step 2

        # NOTE: allocating `temp` outside of _merge() allows us to avoid
        # creating a new head every time we merge two sublists.
        cdef HashNode* curr
        cdef HashNode* tail
        cdef HashNode* left
        cdef HashNode* right
        cdef HashNode* sub_head
        cdef HashNode* sub_tail
        cdef size_t length = 1

        # allocate a temporary node to anchor sublists
        cdef HashNode* temp = <HashNode*>malloc(sizeof(HashNode))
        if temp is NULL:  # malloc() failed to allocate a new block
            raise MemoryError()

        # merge pairs of sublists of increasing size, starting at length 1
        while length < self.size:
            curr = self.head  # left to right

            # divide and conquer
            while curr:
                # split the linked list into two sublists of size `length`
                left = curr
                right = self._split(left, length)
                curr = self._split(right, length)

                # merge the two sublists, maintaining sorted order
                sub_head, sub_tail = self._merge(left, right, temp)

                # if this is our first merge, set the head of the new list
                if tail is NULL:
                    self.head = sub_head
                else:
                    # link the merged sublist to the previous one
                    tail.next = sub_head
                    sub_head.prev = tail

                # set tail of new list
                tail = sub_tail

            # double the length of the sublists for the next iteration
            length *= 2

        # clean up temporary node
        free(temp)

    cdef void _reverse(self):
        """Reverse the order of the list in-place.

        Notes
        -----
        Reversing a :class:`LinkedList` is O(n).
        """
        cdef HashNode* node = self.head

        # swap all prev and next pointers
        while node is not NULL:
            node.prev, node.next = node.next, node.prev
            node = node.prev  # next is now prev

        # swap head and tail
        self.head, self.tail = self.tail, self.head

    cdef size_t _nbytes(self):
        """Get the total number of bytes used by the list."""
        cdef size_t consumed = sizeof(self)
        consumed += self.size * sizeof(HashNode)
        consumed += sizeof(self.table.map)
        consumed += sizeof(self.table.tombstone)
        consumed += sizeof(self.table)
        return consumed

    def __iter__(self) -> Iterator[Any]:
        """Iterate through the list items in order.

        Yields
        ------
        Any
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedList` is O(n) on average.
        """
        cdef HashNode* curr = self.head

        while curr is not NULL:
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python
            curr = curr.next

    def __reversed__(self) -> Iterator[Any]:
        """Iterate through the list in reverse order.

        Yields
        ------
        Any
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedList` is O(n) on average.
        """
        cdef HashNode* curr = self.tail

        while curr is not NULL:
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python
            curr = curr.prev

    def __getitem__(self, key: int | slice) -> "HashedList":
        """Index the list for a particular item or slice.

        Parameters
        ----------
        key : long int or slice
            The index or slice to retrieve from the list.  If this is a slice,
            the result will be a new :class:`LinkedList` containing the
            specified items.  This can be negative, following the same
            convention as Python's standard :class:`list <python:list>`.

        Returns
        -------
        scalar or LinkedList
            The item or list of items corresponding to the specified index or
            slice.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        See Also
        --------
        LinkedList.__setitem__ :
            Set the value of an item or slice in the list.
        LinkedList.__delitem__ :
            Delete an item or slice from the list.

        Notes
        -----
        Integer-based indexing is O(n) on average.

        Slicing is optimized to always begin iterating from the end nearest to
        a slice boundary, and to never backtrack.  It collects all values in
        a single iteration and stops as soon as the slice is complete.
        """
        cdef HashedList result
        cdef HashNode* curr
        cdef object start, stop, step  # kept at Python level
        cdef size_t index, end_inex, abs_step, i
        cdef bint reverse

        # support slicing
        if isinstance(key, slice):
            # create a new HashedList to hold the slice
            result = type(self)()

            # NOTE: Python slices are normally half-open.  This complicates our
            # optimization strategy because we can't treat the slices symmetrically
            # in both directions.  To account for this, we convert the slice into
            # a closed interval so we're free to iterate in either direction.
            start, stop, step = key.indices(self.size)
            stop -= (stop - start) % step or step  # make stop inclusive
            if (step > 0 and stop < start) or (step < 0 and start < stop):
                return result  # Python returns an empty list in these cases

            # determine direction of traversal to avoid backtracking
            index, end_index = self._get_slice_direction(start, stop, step)
            reverse = step < 0  # append to slice in reverse order
            abs_step = abs(step)

            # get first node in slice, counting from nearest end
            curr = self._node_at_index(index)

            # forward traversal
            if index <= end_index:
                while curr is not NULL and index <= end_index:
                    if reverse:
                        result._appendleft(curr.value)  # appendleft
                    else:
                        result._append(curr.value)  # append

                    # jump according to step size
                    index += abs_step  # increment index
                    for i in range(abs_step):
                        curr = curr.next
                        if curr is NULL:
                            break

            # backward traversal
            else:
                while curr is not NULL and index >= end_index:
                    if reverse:
                        result._append(curr.value)  # append
                    else:
                        result._appendleft(curr.value)  # appendleft

                    # jump according to step size
                    index -= abs_step  # decrement index
                    for i in range(abs_step):
                        curr = curr.prev
                        if curr is NULL:
                            break

            return result

        # index directly
        key = self._normalize_index(key)
        curr = self._node_at_index(key)
        Py_INCREF(curr.value)
        return <object>curr.value  # this returns ownership to Python

    def __setitem__(self, key: int | slice, value: Hashable) -> None:
        """Set the value of an item or slice in the list.

        Parameters
        ----------
        key : int64 or slice
            The index or slice to set in the list.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.
        value : PyObject*
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
        cdef HashNode* curr
        cdef size_t start, stop, slice_size, index, end_index, i
        cdef ssize_t step
        cdef unordered_set[PyObject*] replaced_items
        cdef vector[Pair] staged
        cdef Pair p  # for iterating over `staged`
        cdef object val, old_item

        # TODO: update

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
                replace_value(<HashNode*>p.first, <PyObject*>p.second)
                self.nodes[<PyObject*>p.second] = <HashNode*>p.first

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

    def __delitem__(self, key: int | slice) -> None:
        """Delete an item or slice from the list.

        Parameters
        ----------
        key : long int or slice
            The index or slice to delete from the list.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        See Also
        --------
        LinkedList.__getitem__ :
            Index the list for a particular item or slice.
        LinkedList.__setitem__ :
            Set the value of an item or slice in the list.

        Notes
        -----
        Integer-based deletion is O(n) on average.

        Slice deletion is optimized to always begin iterating from the end
        nearest to a slice boundary, and to never backtrack.  It deletes all
        values in a single iteration and stops as soon as the slice is
        complete.
        """
        cdef HashNode* curr
        cdef object start, stop, step  # kept at Python level
        cdef size_t abs_step, small_step, index, end_index, i
        cdef HashNode* temp  # temporary node for deletion

        # support slicing
        if isinstance(key, slice):
            # NOTE: Python slices are normally half-open.  This complicates our
            # optimization strategy because we can't treat the slices symmetrically
            # in both directions.  To account for this, we convert the slice into
            # a closed interval so we're free to iterate in either direction.
            start, stop, step = key.indices(self.size)
            stop -= (stop - start) % step or step  # make stop inclusive
            if (start > stop and step > 0) or (start < stop and step < 0):
                return  # Python does nothing in this case

            # determine direction of traversal to avoid backtracking
            index, end_index = self._get_slice_direction(start, stop, step)
            abs_step = abs(step)
            small_step = abs_step - 1  # we implicitly advance by one at each step

            # get first node in slice, counting from nearest end
            curr = self._node_at_index(index)

            # forward traversal
            if index <= end_index:
                while curr is not NULL and index <= end_index:
                    temp = curr
                    curr = curr.next
                    self._unlink_node(temp)
                    self._forget_node(temp)
                    self._free_node(temp)

                    # jump according to step size
                    index += abs_step  # tracks with end_index to maintain condition
                    for i in range(small_step):
                        curr = curr.next
                        if curr is NULL:
                            break

            # backward traversal
            else:
                while curr is not NULL and index >= end_index:
                    temp = curr
                    curr = curr.prev
                    self._unlink_node(temp)
                    self._forget_node(temp)
                    self._free_node(temp)

                    # jump according to step size
                    index -= abs_step  # tracks with end_index to maintain condition
                    for i in range(small_step):
                        curr = curr.prev
                        if curr is NULL:
                            break

        # index directly
        else:
            key = self._normalize_index(key)
            curr = self._node_at_index(key)
            self._unlink_node(curr)
            self._forget_node(curr)
            self._free_node(curr)

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

    ##################################
    ####    ADDITIONAL METHODS    ####
    ##################################

    # removebefore/removeafter?
    # clearbefore/clearafter?
    # popbefore/popafter?
    # move/moveup/movedown/movebefore/moveafter?

    def insertafter(self, sentinel: Any, item: Any) -> None:
        """Insert an item immediately after the specified sentinel.

        Parameters
        ----------
        sentinel : Any
            The value after which to insert the new value.
        item : Any
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
        Value-based inserts are O(1) thanks to the internal item map.

        This method is significantly faster than an :meth:`index() <HashedList.index>`
        lookup followed by an :meth:`insert() <HashedList.insert>` call, which
        would be O(2n) on average.
        """
        self._insertafter(<PyObject*>sentinel, <PyObject*>item)

    def insertbefore(self, sentinel: Any, item: Any) -> None:
        """Insert an item immediately before the specified sentinel.

        Parameters
        ----------
        sentinel : PyObject*
            The value before which to insert the new value.
        item : PyObject*
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
        Value-based inserts are O(1) thanks to the internal item map.

        This method is significantly faster than an :meth:`index() <HashedList.index>`
        lookup followed by an :meth:`insert() <HashedList.insert>` call, which
        would be O(2n) on average.
        """
        self._insertbefore(<PyObject*>sentinel, <PyObject*>item)

    def extendafter(self, sentinel: Any, other: Iterable[Hashable]) -> None:
        """Insert a sequence of items immediately after the specified sentinel.

        Parameters
        ----------
        sentinel : PyObject*
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
        Thanks to the internal item map, value-based inserts are O(m), where
        `m` is the length of ``other``.

        This method is significantly faster than an :meth:`index() <HashedList.index>`
        lookup followed by a slice assignment, which would be O(2n + m) on
        average.
        """
        self._extendafter(<PyObject*>sentinel, <PyObject*>other)

    def extendbefore(self, sentinel: Any, item: Any):
        """Insert a sequence of items immediately before the specified sentinel.

        Parameters
        ----------
        sentinel : PyObject*
            The value before which to insert the new values.
        other : Iterable[Hashable]
            The values to insert into the list.  Note that this method
            implicitly reverses the order of the elements.

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
        Thanks to the internal item map, value-based inserts are O(m), where
        `m` is the length of ``other``.

        This method is significantly faster than an :meth:`index() <HashedList.index>`
        lookup followed by a slice assignment, which would be O(2n + m) on
        average.
        """
        self._extendbefore(self, <PyObject*>sentinel, <PyObject*>other)

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

    #######################
    ####    PRIVATE    ####
    #######################

    # Now that allocate_node and remember_node are distinct, we can allocate a
    # staged list of nodes, ensure they're hashable, and then search them in
    # the hash table.

    # If we're doing a slice assignment, we can extract out all the current
    # nodes, add their values to an unordered_set, and then when we check the
    # uniqueness of the new values, we can just check if they're in the set.
    # If so, we replace the existing value.

    # TODO: what if we also maintain an unordered_set() of keys?  Then the
    # __setitem__ check just removes the extracted keys and checks all the new
    # keys against the set.

    cdef HashNode* _allocate_node(self, PyObject* value):
        """Allocate a new node and set its value.

        Parameters
        ----------
        value : PyObject*
            The value to set for the new node.  This must be hashable and
            unique.

        Returns
        -------
        HashNode*
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
        It should always be followed up with a call to :meth:`_link_node()` to
        add the node to the list.
        """
        cdef ListTable* table = self.table

        # C API equivalent of the hash() function
        cdef Py_hash_t hash_val = PyObject_Hash(value)
        if hash_val == -1:  # hash() failed
            raise_exception()

        if DEBUG:
            print(f"    -> malloc: {<object>value}")

        # allocate new node
        cdef HashNode* node = <HashNode*>malloc(sizeof(HashNode))
        if node is NULL:
            raise MemoryError()

        # increment reference count of underlying Python object
        Py_INCREF(value)

        # initialize node
        node.value = value
        node.hash = hash_val
        node.next = NULL
        node.prev = NULL
        return node

    cdef void _free_node(self, HashNode* node):
        """Free a node and decrement the reference count of its value.

        Parameters
        ----------
        node : HashNode*
            The node to free.

        Notes
        -----
        This method also removes the node from the hash map.

        The node must be unlinked from the list before calling this method.
        Any remaining references to it will become dangling pointers.
        """
        if DEBUG:
            print(f"    -> free: {<object>node.value}")

        # nullify pointers
        node.next = NULL
        node.prev = NULL

        # deallocate
        Py_DECREF(node.value)
        free(node)

    cdef void _link_node(self, HashNode* prev, HashNode* curr, HashNode* next):
        """Add a node to the list.

        Parameters
        ----------
        prev : HashNode*
            The node that should precede the new node in the list.
        curr : HashNode*
            The node to add to the list.
        next : HashNode*
            The node that should follow the new node in the list.

        Notes
        -----
        This is a helper method for doing the pointer arithmetic of adding a
        node to the list, since it's used in multiple places.
        """
        # prev <-> curr
        curr.prev = prev
        if prev is NULL:
            self.head = curr
        else:
            prev.next = curr

        # curr <-> next
        curr.next = next
        if next is NULL:
            self.tail = curr
        else:
            next.prev = curr

        # increment size
        self.size += 1

    cdef void _unlink_node(self, HashNode* curr):
        """Remove a node from the list.

        Parameters
        ----------
        curr : HashNode*
            The node to remove from the list.

        Notes
        -----
        This is a helper method for doing the pointer arithmetic of removing a
        node, as well as handling reference counts and freeing the underlying
        memory.
        """
        # prev <-> next
        if curr.prev is NULL:
            self.head = curr.next
        else:
            curr.prev.next = curr.next

        # prev <-> next
        if curr.next is NULL:
            self.tail = curr.prev
        else:
            curr.next.prev = curr.prev

        # decrement size
        self.size -= 1

    # TODO: include a `set override` argument.  If an item is not unique, but
    # does 

    cdef (HashNode*, HashNode*, size_t) _stage_nodes(
        self, PyObject* items, bint reverse, set override = None
    ):
        """Stage a sequence of nodes for insertion into the list.

        Parameters
        ----------
        items : PyObject*
            An iterable of items to insert into the list.
        reverse : bool
            Indicates whether to reverse the order of the items during staging.
        override : set, optional
            A set of values to allow collisions for.

        Returns
        -------
        head : HashNode*
            The head of the staged list (or NULL if no values are staged).
        tail : HashNode*
            The tail of the staged list (or NULL if no values are staged).
        count : size_t
            The number of nodes in the staged list.
        """
        # TODO: have to account for duplicates within the list itself.  We
        # also can't duplicate

        # C API equivalent of iter(items)
        cdef PyObject* iterator = PyObject_GetIter(items)
        if iterator is NULL:
            raise_exception()

        # NOTE: we stage the items in a temporary list to ensure we don't
        # modify the original if we encounter any errors
        cdef HashNode* staged_head = NULL
        cdef HashNode* staged_tail = NULL
        cdef HashNode* node
        cdef PyObject* item
        cdef size_t count = 0

        # iterate over items (equivalent to `for item in iterator`)
        while True:
            item = PyIter_Next(iterator)  # generates a reference
            if item is NULL:  # end of iterator or error
                if PyErr_Occurred():
                    Py_DECREF(item)
                    Py_DECREF(iterator)
                    while staged_head is not NULL:  # clean up staged items
                        node = staged_head
                        staged_head = staged_head.next
                        Py_DECREF(node.value)
                        free(node)
                    raise_exception()  # propagate error
                break

            # allocate new node
            node = self._allocate_node(item)

            # check if node is already present in hash table
            if self._search_node(node) is not NULL:
                Py_DECREF(item)
                Py_DECREF(iterator)
                while staged_head is not NULL:  # clean up staged items
                    node = staged_head
                    staged_head = staged_head.next
                    Py_DECREF(node.value)
                    free(node)
                raise ValueError(f"list elements must be unique: {repr(item)}")

            # link to staged list
            if reverse:  # insert at front
                if staged_tail is NULL:
                    staged_tail = node
                else:
                    staged_head.prev = node
                    node.next = staged_head
                staged_head = node
            else:  # insert at end
                if staged_head is NULL:
                    staged_head = node
                else:
                    staged_tail.next = node
                    node.prev = staged_tail
                staged_tail = node

            # increment count
            count += 1

            # release reference on item
            Py_DECREF(item)

        # release reference on iterator
        Py_DECREF(iterator)

        return (staged_head, staged_tail, count)

    #############################
    ####    INDEX HELPERS    ####
    #############################

    cdef HashNode* _node_at_index(self, size_t index):
        """Get the node at the specified index.

        Parameters
        ----------
        index : size_t
            The index of the node to retrieve.  This should always be passed
            through :meth:`LinkedList._normalize_index` first.

        Returns
        -------
        HashNode*
            The node at the specified index.

        Notes
        -----
        This method is O(n) on average.  As an optimization, it always iterates
        from the nearest end of the list.
        """
        cdef HashNode* curr
        cdef size_t i

        # count forwards from head
        if index <= self.size // 2:
            curr = self.head
            for i in range(index):
                curr = curr.next

        # count backwards from tail
        else:
            curr = self.tail
            for i in range(self.size - index - 1):
                curr = curr.prev

        return curr

    cdef (size_t, size_t) _get_slice_direction(
        self,
        size_t start,
        size_t stop,
        ssize_t step,
    ):
        """Determine the direction in which to traverse a slice so as to
        minimize total iterations.

        Parameters
        ----------
        start : size_t
            The start index of the slice.
        stop : size_t
            The stop index of the slice.
        step : size_t
            The step size of the slice.

        Returns
        -------
        index : size_t
            The index at which to begin iterating.
        end_index : size_t
            The index at which to stop iterating.

        Notes
        -----
        Slicing is optimized to always begin iterating from the end nearest to
        a slice boundary, and to never backtrack.  This is done by checking
        whether the slice is ascending (step > 0) or descending, and whether
        the start or stop index is closer to its respective end.  This gives
        the following cases:

            1) slice is ascending, `start` closer to head than `stop` is to tail
                -> iterate forwards from head to `stop`
            2) slice is ascending, `stop` closer to tail than `start` is to head
                -> iterate backwards from tail to `start`
            3) slice is descending, `start` closer to tail than `stop` is to head
                -> iterate backwards from tail to `stop`
            4) slice is descending, `stop` closer to head than `start` is to tail
                -> iterate forwards from head to `start`

        The final direction of traversal is determined by comparing the
        indices returned by this method.  If ``end_index >= index``, then the
        slice should be traversed in the forward direction, starting from
        ``index``.  Otherwise, it should be iterated over in reverse to avoid
        backtracking, again starting from ``index``.
        """
        cdef size_t index, end_index

        # determine direction of traversal
        if (
            step > 0 and start <= self.size - stop or   # 1)
            step < 0 and self.size - start <= stop      # 4)
        ):
            index = start
            end_index = stop
        else:
            if step > 0:                                # 2)
                index = stop - 1
                end_index = start - 1
            else:                                       # 3)
                index = stop + 1
                end_index = start + 1

        # return as C tuple
        return (index, end_index)

    ############################
    ####    SORT HELPERS    ####
    ############################

    cdef HashNode* _split(self, HashNode* head, size_t length):
        """Split a linked list into sublists of the specified length.

        Parameters
        ----------
        head : HashNode*
            The head of the list to split.
        length : size_t
            The maximum length of each split.  This method will walk forward
            from ``head`` by this many nodes and then split the list.

        Returns
        -------
        HashNode*
            The head of the next sublist.

        Notes
        -----
        This method is O(length).  It just iterates forward ``length`` times
        and then splits the list at that point.
        """
        cdef HashNode* split
        cdef size_t i

        # walk `length` nodes forward from `head`
        for i in range(length - 1):
            if head is NULL:
                break
            head = head.next

        # if we've reached the end of the list, there's nothing to split
        if head is NULL:
            return NULL

        # otherwise, split the list
        split = head.next
        head.next = NULL
        if split is not NULL:
            split.prev = NULL
        return split

    cdef (HashNode*, HashNode*) _merge(
        self,
        HashNode* left,
        HashNode* right,
        HashNode* temp
    ):
        """Merge two sorted linked lists into a single sorted list.

        Parameters
        ----------
        left : HashNode*
            The head of the first sorted list.
        right : HashNode*
            The head of the second sorted list.
        temp : HashNode*
            A temporary node to use as the head of the merged list.  As an
            optimization, this is allocated once and then passed as a parameter
            rather than creating a new one every time this method is called.

        Returns
        -------
        head : HashNode*
            The head of the merged list.
        tail : HashNode*
            The tail of the merged list.

        Notes
        -----
        This is a standard implementation of the divide-and-conquer merge
        algorithm.  It is O(l) where `l` is the length of the longer list.
        """
        cdef HashNode* curr = temp
        cdef HashNode* tail

        # iterate through sublists until one is empty
        while left and right:
            # only append the smaller of the two nodes
            if left.value < right.value:
                curr.next = left
                left.prev = curr
                left = left.next
            else:
                curr.next = right
                right.prev = curr
                right = right.next

            # advance to next node
            curr = curr.next

        # append the remaining nodes
        tail = left if right is NULL else right
        curr.next = tail
        tail.prev = curr

        # advance tail to end of merged list
        while tail.next is not NULL:
            tail = tail.next

        # unlink temporary head
        curr = temp.next
        curr.prev = NULL
        temp.next = NULL

        # return the proper head and tail of the merged list
        return (curr, tail)  # remove temporary head

    ##############################
    ####    LOOKUP HELPERS    ####
    ##############################

    cdef void _remember_node(self, HashNode* node):
        """Add a node to the list's hash map for direct access.
        """
        cdef ListTable* table = self.table

        # check if value is already present
        cdef size_t index = node.hash % table.size
        cdef size_t step = table.prime - (node.hash % table.prime)
        cdef HashNode* curr = table.map[index]
        cdef int comp

        # search table
        while curr is not NULL:
            if candidate is not table.tombstone:  # skip over tombstones
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

        # insert
        table[index] = node

    cdef void _forget_node(self, HashNode* node):
        """Remove a node from the list's hash map.
        """
        cdef ListTable* table = self.table

        # get index in hash map
        cdef size_t index = node.hash % table.size
        cdef size_t step = table.prime - (node.hash % table.prime)  # double hash
        cdef HashNode* curr = table.map[index]
        cdef int comp

        # find node
        while curr is not NULL:
            if curr is not table.tombstone:  # skip over tombstones
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

    cdef HashNode* _search(self, PyObject* key):
        """Search the hash table for a node with the given value.

        Parameters
        ----------
        key : PyObject*
            The value to search for.

        Returns
        -------
        HashNode*
            A reference to the node with the given value, or ``NULL`` if it is
            not present.

        Notes
        -----
        This method is used to look up nodes by their value.  It is almost
        identical to `_search_node()`, except that it computes the value's
        hash for cases where a pre-existing node is not available.
        """
        cdef ListTable* table = self.table

        # C API equivalent of the hash() function
        cdef Py_hash_t hash_val = PyObject_Hash(key)
        if hash_val == -1:  # hash() failed
            raise_exception()

        # get index in hash table
        cdef size_t index = hash_val % table.size
        cdef size_t step = table.prime - (hash_val % table.prime)  # double hash
        cdef HashNode* curr = table.map[index]
        cdef int comp

        # find node
        while candidate is not NULL:
            if candidate is not table.tombstone:  # skip over tombstones
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

    cdef HashNode* _search_node(self, HashNode* node):
        """Check whether a node is contained in the list.

        Parameters
        ----------
        node : HashNode*
            The node to search for.  The node's ``hash`` and ``value`` fields
            will be used to search the hash table.

        Returns
        -------
        HashNode*
            A reference to a node with a matching value, or ``NULL`` if it is
            not present.

        Notes
        -----
        This method is called to check whether a node can be safely added to
        the list.  It is almost identical to `_search()`, except that it
        re-uses the node's pre-computed hash for efficiency.
        """
        cdef ListTable* table = self.table

        # get index in hash table
        cdef size_t index = node.hash % table.size
        cdef size_t step = table.prime - (node.hash % table.prime)  # double hash
        cdef HashNode* curr = table.map[index]
        cdef int comp

        # find node
        while candidate is not NULL:
            if candidate is not table.tombstone:  # skip over tombstones
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

    cdef void _resize_table(self):
        """Resize the hash table and rehash its contents.

        Notes
        -----
        This method is called automatically whenever the hash table exceeds the
        maximum load factor.  It is O(n), and simultaneously clears all
        tombstones it encounters.
        """
        cdef ListTable* table = self.table
        cdef HashNode** old_map = table.map
        cdef size_t old_size = table.size

        # update table parameters
        table.size = old_size * 2
        table.exponent += 1
        table.prime = PRIMES[table.exponent]

        if DEBUG:
            print(f"    -> malloc: ListTable({table.size})")

        # allocate new hash map
        table.map = <HashNode**>calloc(table.size, sizeof(HashNode*))
        if table.map is NULL:  # calloc() failed to allocate a new block
            raise MemoryError()

        cdef size_t index, new_index, step
        cdef HashNode* curr

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
        cdef HashNode** old_map = table.map

        if DEBUG:
            print(f"    -> malloc: ListTable({table.size})    <- tombstones")

        # allocate new hash map
        table.map = <HashNode**>calloc(table.size, sizeof(HashNode*))
        if table.map is NULL:  # calloc() failed to allocate a new block
            raise MemoryError()

        cdef size_t index, new_index, step
        cdef HashNode* curr

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
