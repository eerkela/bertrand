# distutils: language = c++
"""This module contains a pure C/Cython implementation of a doubly-linked list
that uses a hash map to support fast lookups by value.
"""
from typing import Hashable, Iterable


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
        # set head/tail
        self.head = NULL
        self.tail = NULL

        # allocate C++ templated hash table
        self.table = new ListTable[HashNode]()  # can throw a MemoryError

    def __dealloc__(self):
        cdef HashNode* node = self.head
        cdef HashNode* temp

        # free all nodes
        while node is not NULL:
            temp = node
            node = node.next
            free_node(temp)

        # avoid dangling pointers
        self.head = NULL
        self.tail = NULL
        self.size = 0

        # deallocate C++ hash table
        del self.table

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
        cdef HashNode* node = allocate_hash_node(item)

        # add node to hash table
        try:
            self.table.remember(node)
        except ValueError:  # node is not unique
            free_node(node)
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
        cdef HashNode* node = allocate_hash_node(item)

        # add node to hash table
        try:
            self.table.remember(node)
        except ValueError:  # node is not unique
            free_node(node)
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
        cdef size_t norm_index = normalize_index(index, self.size)

        # allocate new node
        cdef HashNode* node = allocate_hash_node(item)
        cdef HashNode* curr
        cdef size_t i

        # add node to hash table
        try:
            self.table.remember(node)
        except ValueError:  # node is not unique
            free_node(node)
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
        cdef HashNode* curr = self.table.search(sentinel)
        if curr is NULL:
            raise KeyError(
                f"{repr(<object>sentinel)} is not contained in the list"
            )

        # allocate new node
        cdef HashNode* node = allocate_hash_node(item)

        # add node to hash table
        try:
            self.table.remember(node)
        except ValueError:  # node is not unique
            free_node(node)
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
        cdef HashNode* curr = self.table.search(sentinel)
        if curr is NULL:
            raise KeyError(
                f"{repr(<object>sentinel)} is not contained in the list"
            )

        # allocate new node
        cdef HashNode* node = allocate_hash_node(item)

        # add node to hash table
        try:
            self.table.remember(node)
        except ValueError:  # node is not unique
            free_node(node)
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
        staged_head, staged_tail, count = self._stage_nodes(items, False)
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
            self.table.remember(staged_head)
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
        staged_head, staged_tail, count = self._stage_nodes(items, True)
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
            self.table.remember(staged_head)
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
        cdef HashNode* curr = self.table.search(sentinel)
        if curr is NULL:
            raise KeyError(
                f"{repr(<object>sentinel)} is not contained in the list"
            )

        cdef HashNode* staged_head
        cdef HashNode* staged_tail
        cdef size_t count

        # NOTE: we stage the items in a temporary list to ensure we don't
        # modify the original if we encounter any errors
        staged_head, staged_tail, count = self._stage_nodes(other, False)
        if staged_head is NULL:
            return

        # insert staged immediately after sentinel
        self.size += count
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
            self.table.remember(staged_head)
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
        cdef HashNode* curr = self.table.search(sentinel)
        if curr is NULL:
            raise KeyError(
                f"{repr(<object>sentinel)} is not contained in the list"
            )

        cdef HashNode* staged_head
        cdef HashNode* staged_tail
        cdef size_t count

        # NOTE: we stage the items in a temporary list to ensure we don't
        # modify the original if we encounter any errors
        staged_head, staged_tail, count = self._stage_nodes(other, True)
        if staged_head is NULL:
            return

        # insert staged immediately before sentinel
        self.size += count
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
            self.table.remember(staged_head)
            if staged_head is staged_tail:
                break
            staged_head = staged_head.next

    cdef size_t _index(self, PyObject* item, long start = 0, long stop = -1):
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
        cdef HashNode* node = self.table.search(item)
        if node is NULL:
            raise ValueError(
                f"{repr(<object>item)} is not contained in the list"
            )

        # normalize start/stop indices
        cdef size_t norm_start = normalize_index(start, self.size)
        cdef size_t norm_stop = normalize_index(stop, self.size)
        cdef size_t index = 0

        # count backwards to the start of the list
        while node is not NULL and index < norm_stop:
            node = node.prev
            index += 1

        # check if item was found in the specified range
        if not norm_start <= index < norm_stop:
            raise ValueError(
                f"{repr(<object>item)} is not contained in the list"
            )

        return index

    cdef void _move(self, PyObject* item, long index):
        """Move a specified value to a particular index.

        Parameters
        ----------
        item : PyObject*
            The value to move.
        index : long int
            The index to move the value to.  This can be negative, following
            the same convention as Python's standard :class:`list <python:list>`.

        Raises
        ------
        KeyError
            If the item is not contained in the list.
        IndexError
            If the index is out of bounds.
        """        
        # normalize index
        cdef size_t norm_index = normalize_index(index, self.size)

        # look up item in hash map
        cdef HashNode* node = self.table.search(item)
        if node is NULL:
            raise KeyError(
                f"{repr(<object>item)} is not contained in the list"
            )

        # fastpaths for move to beginning/end of list
        if norm_index == 0:
            self._unlink_node(node)
            self._link_node(NULL, node, self.head)
            return
        elif norm_index == self.size - 1:
            self._unlink_node(node)
            self._link_node(self.tail, node, NULL)
            return

        cdef HashNode* curr = node
        cdef size_t curr_index = 0

        # get current index
        while curr is not NULL:
            curr = curr.prev
            curr_index += 1

        # move forwards
        if curr_index < norm_index:
            while curr_index < norm_index:
                curr = curr.next
                curr_index += 1
            self._unlink_node(node)
            self._link_node(curr, node, curr.next)

        # move backwards
        elif curr_index > norm_index:
            while curr_index > norm_index:
                curr = curr.prev
                curr_index -= 1
            self._unlink_node(node)
            self._link_node(curr.prev, node, curr)

    cdef void _moveleft(self, PyObject* item, size_t steps = 1):
        """Move a specified value a number of steps to the left (toward the
        front of the list).

        Parameters
        ----------
        item : PyObject*
            The value to move.
        steps : unsigned int, optional
            The number of steps to move the value.  The default is ``1``.

        Raises
        ------
        KeyError
            If the item is not contained in the list.
        """
        # look up item in hash map
        cdef HashNode* node = self.table.search(item)
        if node is NULL:
            raise KeyError(
                f"{repr(<object>item)} is not contained in the list"
            )

        # no-op if moving zero steps
        if steps == 0:
            return

        cdef HashNode* curr = node
        cdef size_t i

        # get node at new index
        for i in range(steps):
            if curr.prev is NULL:
                break
            curr = curr.prev

        # move node
        self._unlink_node(node)
        self._link_node(curr.prev, node, curr)

    cdef void _moveright(self, PyObject* item, size_t steps = 1):
        """Move a specified value a number of steps to the right (toward the
        back of the list).

        Parameters
        ----------
        item : PyObject*
            The value to move.
        steps : unsigned int, optional
            The number of steps to move the value.  The default is ``1``.

        Raises
        ------
        KeyError
            If the item is not contained in the list.
        """
        # look up item in hash map
        cdef HashNode* node = self.table.search(item)
        if node is NULL:
            raise KeyError(
                f"{repr(<object>item)} is not contained in the list"
            )

        # no-op if moving zero steps
        if steps == 0:
            return

        cdef HashNode* curr = node
        cdef size_t i

        # get node at new index
        for i in range(steps):
            if curr.next is NULL:
                break
            curr = curr.next

        # move node
        self._unlink_node(node)
        self._link_node(curr, node, curr.next)

    cdef void _moveafter(self, PyObject* sentinel, PyObject* item):
        """Move an item immediately after the specified sentinel.

        Parameters
        ----------
        item : PyObject*
            The value to move.
        sentinel : PyObject*
            The value after which to move the item.

        Raises
        ------
        KeyError
            If the item or sentinel is not contained in the list.
        """
        # look up item in hash map
        cdef HashNode* node = self.table.search(item)
        if node is NULL:
            raise KeyError(
                f"{repr(<object>item)} is not contained in the list"
            )

        # look up sentinel node
        cdef HashNode* curr = self.table.search(sentinel)
        if curr is NULL:
            raise KeyError(
                f"{repr(<object>sentinel)} is not contained in the list"
            )

        # move node after sentinel
        self._unlink_node(node)
        self._link_node(curr, node, curr.next)

    cdef void _movebefore(self, PyObject* sentinel, PyObject* item):
        """Move an item immediately before the specified sentinel.

        Parameters
        ----------
        item : PyObject*
            The value to move.
        sentinel : PyObject*
            The value before which to move the item.

        Raises
        ------
        KeyError
            If the item or sentinel is not contained in the list.
        """
        # look up item in hash map
        cdef HashNode* node = self.table.search(item)
        if node is NULL:
            raise KeyError(
                f"{repr(<object>item)} is not contained in the list"
            )

        # look up sentinel node
        cdef HashNode* curr = self.table.search(sentinel)
        if curr is NULL:
            raise KeyError(
                f"{repr(<object>sentinel)} is not contained in the list"
            )

        # move node before sentinel
        self._unlink_node(node)
        self._link_node(curr.prev, node, curr)

    cdef size_t _count(self, PyObject* item):
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
        return self.table.search(item) is not NULL

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
        cdef HashNode* node = self.table.search(item)
        if node is NULL:
            raise ValueError(
                f"{repr(<object>item)} is not contained in the list"
            )

        self._unlink_node(node)
        self.table.forget(node)
        free_node(node)

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
        cdef size_t norm_index = normalize_index(index, self.size)
        cdef HashNode* node = node_at_index(
            norm_index, self.head, self.tail, self.size
        )
        cdef PyObject* value = node.value

        # we have to increment the reference counter of the popped object to
        # ensure it isn't garbage collected when we free the node.
        Py_INCREF(value)

        # drop node and return contents
        self._unlink_node(node)
        self.table.forget(node)
        free_node(node)
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
        self.table.forget(node)
        free_node(node)
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
        self.table.forget(node)
        free_node(node)
        return value

    cdef void _clear(self):
        """Remove all items from the list.

        Notes
        -----
        This method is O(n).
        """
        # clear C++ hash table
        self.table.clear()  # can throw a MemoryError

        cdef HashNode* node = self.head
        cdef HashNode* temp

        # free all nodes
        while node is not NULL:
            temp = node
            node = node.next
            free_node(temp)

        # avoid dangling pointers
        self.head = NULL
        self.tail = NULL
        self.size = 0

    cdef void _sort(self, PyObject* key = NULL, bint reverse = False):
        """Sort the list in-place.

        Parameters
        ----------
        key : Callable[[Any], Any], optional
            A function that takes an item from the list and returns a value to
            use for sorting.  If this is not given, then the items will be
            compared directly.
        reverse : bool, optional
            Indicates whether to sort the list in descending order.  The
            default is ``False``, which sorts in ascending order.

        Notes
        -----
        Sorting is O(n log n) on average, using an iterative merge sort
        algorithm that avoids recursion.  The sort is stable, meaning that the
        relative order of elements that compare equal will not change, and it
        is performed in-place for minimal memory overhead.

        If a ``key`` function is provided, then the keys will be computed once
        and reused for all iterations of the sorting algorithm.  Otherwise,
        each element will be compared directly using the ``<`` operator.  If
        ``reverse=True``, then the value of the comparison will be inverted
        (i.e. ``not a < b``).

        One quirk of this implementation is how it handles errors.  By default,
        if a comparison throws an exception, then the sort will be aborted and
        the list will be left in a partially-sorted state.  This is consistent
        with the behavior of Python's built-in :meth:`list.sort() <python:list.sort>`
        method.  However, when a ``key`` function is provided, we actually end
        up sorting an auxiliary list of ``(key, value)`` pairs, which is then
        reflected in the original list.  This means that if a comparison throws
        an exception, the original list will not be changed.  This holds even
        if the ``key`` is a simple identity function (``lambda x: x``), which
        opens up the possibility of anticipating errors and handling them
        gracefully.
        """
        # trivial case: empty list
        if self.head is NULL:
            return

        cdef KeyedHashNode* decorated_head
        cdef KeyedHashNode* decorated_tail
        cdef Pair* pair
        cdef SortError sort_err

        # if a key func is given, decorate the list and sort by key
        if key is not NULL:
            decorated_head, decorated_tail = decorate_hash(
                self.head, self.tail, key
            )
            try:
                pair = merge_sort(decorated_head, decorated_tail, self.size, reverse)
                self.head, self.tail = undecorate_hash(<KeyedHashNode*>pair.first)
                free(pair)
            except SortError as err:
                # NOTE: no cleanup necessary for decorated sort
                sort_err = <SortError>err
                raise sort_err.original
            return

        # otherwise, sort the list directly
        try:
            pair = merge_sort(self.head, self.tail, self.size, reverse)
            self.head = <HashNode*>pair.first
            self.tail = <HashNode*>pair.second
            free(pair)
        except SortError as err:
            # NOTE: we have to manually reassign the head and tail of the list
            # to avoid memory leaks.
            sort_err = <SortError>err
            self.head = <HashNode*>sort_err.head
            self.tail = <HashNode*>sort_err.tail
            raise sort_err.original  # raise original exception

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
        return sizeof(self) + self.size * sizeof(HashNode) + self.table.nbytes()

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
            index, end_index = get_slice_direction(
                <size_t>start,
                <size_t>stop,
                <ssize_t>step,
                self.head,
                self.tail,
                self.size,
            )
            reverse = step < 0  # append to slice in reverse order
            abs_step = abs(step)

            # get first node in slice, counting from nearest end
            curr = node_at_index(index, self.head, self.tail, self.size)

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
        index = normalize_index(key, self.size)
        curr = node_at_index(index, self.head, self.tail, self.size)
        Py_INCREF(curr.value)
        return <object>curr.value  # this returns ownership to Python

    def __setitem__(self, key: int | slice, value: Any) -> None:
        """Set the value of an item or slice in the list.

        Parameters
        ----------
        key : long int or slice
            The index or slice to set in the list.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.
        value : Any
            The value or values to set at the specified index or slice.  If
            ``key`` is a slice, then ``value`` must be an iterable of the same
            length.

        Raises
        ------
        IndexError
            If the index is out of bounds.
        TypeError
            If any of the values in ``value`` are not hashable.
        ValueError
            If the length of ``value`` does not match the length of the slice,
            or if any of its values violate the uniqueness of the final list.

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
        nearest to a slice boundary, and to never backtrack.  It assigns all
        values in a single iteration and stops as soon as the slice is
        complete.
        """
        cdef HashNode* node
        cdef HashNode* curr
        cdef object start, stop, step, expected_size  # kept at Python level
        cdef size_t abs_step, index, end_index, i
        cdef object value_iter, val

        # support slicing
        if isinstance(key, slice):
            value_iter = iter(value)  # check input is iterable

            # NOTE: Python slices are normally half-open.  This complicates our
            # optimization strategy because we can't treat the slices symmetrically
            # in both directions.  To account for this, we convert the slice into
            # a closed interval so we're free to iterate in either direction.
            start, stop, step = key.indices(self.size)

            # Python allows assignment to empty/improper slices iff step == 1
            if step == 1:
                self.__delitem__(key)  # delete previous values

                # handle edge cases
                if start == 0:  # assignment at beginning of list
                    val = next(value_iter)
                    curr = allocate_hash_node(<PyObject*>val)
                    self._link_node(NULL, curr, self.head)
                elif start == self.size:  # assignment at end of list
                    val = next(value_iter)
                    curr = allocate_hash_node(<PyObject*>val)
                    self._link_node(self.tail, curr, NULL)
                else:  # assignment in middle of list
                    curr = node_at_index(
                        <size_t>(start - 1),
                        self.head,
                        self.tail,
                        self.size
                    )

                # insert all values at current index
                for val in value_iter:
                    node = allocate_hash_node(<PyObject*>val)
                    self._link_node(curr, node, curr.next)
                    curr = node

                return  # early return

            # proceed as normal
            abs_step = abs(step)
            stop -= (stop - start) % step or step  # make stop inclusive
            expected_size = 1 + abs(stop - start) // abs_step
            if (
                (step > 0 and stop < start) or
                (step < 0 and start < stop) or
                len(value) != expected_size
            ):
                raise IndexError(
                    f"attempt to assign sequence of size {len(value)} to "
                    f"extended slice of size {expected_size}"
                )

            # determine direction of traversal to avoid backtracking
            index, end_index = get_slice_direction(
                <size_t>start,
                <size_t>stop,
                <ssize_t>step,
                self.head,
                self.tail,
                self.size,
            )

            # get first node in slice, counting from nearest end
            curr = node_at_index(index, self.head, self.tail, self.size)

            # forward traversal
            if index <= end_index:
                if step < 0:
                    value_iter = reversed(value)
                while curr is not NULL and index <= end_index:
                    val = next(value_iter)
                    Py_INCREF(<PyObject*>val)
                    Py_DECREF(curr.value)
                    curr.value = <PyObject*>val
                    # node = allocate_hash_node(<PyObject*>val)
                    # self._link_node(curr.prev, node, curr.next)
                    # free_node(curr)
                    # curr = node

                    # jump according to step size
                    index += abs_step  # increment index
                    for i in range(abs_step):
                        curr = curr.next
                        if curr is NULL:
                            break

            # backward traversal
            else:
                if step > 0:
                    value_iter = reversed(value)
                while curr is not NULL and index >= end_index:
                    val = next(value_iter)
                    Py_INCREF(<PyObject*>val)
                    Py_DECREF(curr.value)
                    curr.value = <PyObject*>val
                    # node = allocate_hash_node(<PyObject*>val)
                    # self._link_node(curr.prev, node, curr.next)
                    # free_node(curr)
                    # curr = node

                    # jump according to step size
                    index -= abs_step  # decrement index
                    for i in range(abs_step):
                        curr = curr.prev
                        if curr is NULL:
                            break

            return

        # index directly
        index = normalize_index(key, self.size)
        curr = node_at_index(index, self.head, self.tail, self.size)
        Py_INCREF(<PyObject*>value)
        Py_DECREF(curr.value)
        curr.value = <PyObject*>value
        # node = allocate_hash_node(<PyObject*>value)
        # self._link_node(curr.prev, node, curr.next)
        # free_node(curr)

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
            index, end_index = get_slice_direction(
                <size_t>start,
                <size_t>stop,
                <ssize_t>step,
                self.head,
                self.tail,
                self.size,
            )
            abs_step = abs(step)
            small_step = abs_step - 1  # we implicitly advance by one at each step

            # get first node in slice, counting from nearest end
            curr = node_at_index(index, self.head, self.tail, self.size)

            # forward traversal
            if index <= end_index:
                while curr is not NULL and index <= end_index:
                    temp = curr
                    curr = curr.next
                    self._unlink_node(temp)
                    self.table.forget(temp)
                    free_node(temp)

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
                    self.table.forget(temp)
                    free_node(temp)

                    # jump according to step size
                    index -= abs_step  # tracks with end_index to maintain condition
                    for i in range(small_step):
                        curr = curr.prev
                        if curr is NULL:
                            break

        # index directly
        else:
            index = normalize_index(key, self.size)
            curr = node_at_index(index, self.head, self.tail, self.size)
            self._unlink_node(curr)
            self.table.forget(curr)
            free_node(curr)

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
        return self.table.search(<PyObject*>item) is not NULL

    ##################################
    ####    ADDITIONAL METHODS    ####
    ##################################

    # removebefore/removeafter?
    # clearbefore/clearafter?
    # popbefore/popafter?

    def insertafter(self, sentinel: object, item: object) -> None:
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
        Value-based inserts are O(1) thanks to the internal item map.

        This method is significantly faster than an :meth:`index() <HashedList.index>`
        lookup followed by an :meth:`insert() <HashedList.insert>` call, which
        would be O(2n) on average.
        """
        self._insertafter(<PyObject*>sentinel, <PyObject*>item)

    def insertbefore(self, sentinel: object, item: object) -> None:
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

    def extendafter(self, sentinel: object, other: Iterable[Hashable]) -> None:
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

    def extendbefore(self, sentinel: object, item: object):
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
        self._extendbefore(<PyObject*>sentinel, <PyObject*>item)

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
        # C API equivalent of iter(items)
        cdef PyObject* iterator = PyObject_GetIter(items)
        if iterator is NULL:
            raise_exception()

        cdef HashNode* staged_head = NULL
        cdef HashNode* staged_tail = NULL
        cdef HashNode* node
        cdef PyObject* item
        cdef size_t count = 0
        cdef set observed = set()

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
            node = allocate_hash_node(item)

            # check if node is already present in hash table
            if (
                (self.table.search_node(node) is not NULL) or
                (<object>item in observed)
            ):
                Py_DECREF(item)
                Py_DECREF(iterator)
                while staged_head is not NULL:  # clean up staged items
                    node = staged_head
                    staged_head = staged_head.next
                    Py_DECREF(node.value)
                    free(node)
                raise ValueError(
                    f"list elements must be unique: {repr(<object>item)}"
                )

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

            # prep for next iteration
            count += 1
            Py_DECREF(item)  # release reference on item
            observed.add(<object>item)  # mark item as observed

        Py_DECREF(iterator)  # release reference on iterator
        return (staged_head, staged_tail, count)
