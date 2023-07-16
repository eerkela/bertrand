"""This module contains a basic implementation of a doubly-linked list, which
can be subclassed to add additional functionality.
"""
from typing import Any, Hashable, Iterable, Iterator


# TODO: implement class_getitem for mypy hints, just like list[].
# -> in the case of HashedList, this could check if the contained type is
# a subclass of Hashable, and if not, raise an error.


# TODO: test cases: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
#   [1:4:1] -> [1, 2, 3]
#   [6:9:1] -> [6, 7, 8]
#   [8:5:-1] -> [8, 7, 6]  # error
#   [3:0:-1] -> [3, 2, 1]  # error


# TODO: index() should support optional start and stop arguments for
# compatibility with list.index()

# TODO: sort() should be implemented using a recursionless merge sort
# algorithm, which is O(n log n) on average.



# TODO: implement a separate class for lists that can contain
# non-unique/hashable items.
#   LinkedList (non-unique)
#   HashedList (unique)

# both inherit from a single List class that implements all the common
# functionality.  the only difference is that HashedList uses a dict to map
# items to their corresponding nodes.


# A PriorityList is a HashedList with extra move() methods



cdef class LinkedList:
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
    This structure behaves similarly to a
    :class:`collections.deque <python:collections.deque>` object, but is
    implemented as a doubly-linked list instead of a ring buffer.  Additionally,
    it uses a dictionary to map items to their corresponding nodes, which
    allows for O(1) removals and membership checks, but also forces all items
    to be hashable and unique.  For an implementation without these constraints,
    see :class:`LinkedList`.

    This class is implemented in pure Cython for performance reasons.  It is
    not intended to be used directly from Python, and None of its attributes or
    methods (besides the constructor and special methods) are accessible from
    a non-Cython context.  If you want to use it from Python, you should first
    write a Cython wrapper that exposes the desired functionality.
    """

    def __init__(self, items: Iterable[Hashable] | None = None):
        self.head = None
        self.tail = None
        self.items = {}

        # add items from initializer
        if items is not None:
            for item in items:
                self.append(item)

    ######################
    ####    APPEND    ####
    ######################

    cdef LinkedList copy(self):
        """Create a shallow copy of the list.

        Returns
        -------
        LinkedList
            A new list containing the same items as this one.

        Notes
        -----
        Copying a :class:`LinkedList` is O(n).
        """
        return LinkedList(self)

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
        cdef ListNode node

        # check if item is already present
        if item in self.items:
            raise ValueError(f"list elements must be unique: {repr(item)}")

        # generate new node and add to item map
        node = ListNode(item)
        self.items[item] = node

        # append to end of list
        if self.head is None:
            self.head = node
            self.tail = node
        else:
            self.tail.next = node
            node.prev = self.tail
            self.tail = node

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
        cdef ListNode node

        # check if item is already present
        if item in self.items:
            raise ValueError(f"list elements must be unique: {repr(item)}")

        # generate new node and add to item map
        node = ListNode(item)
        self.items[item] = node

        # append to beginning of list
        if self.head is None:
            self.head = node
            self.tail = node
        else:
            self.head.prev = node
            node.next = self.head
            self.head = node

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
        TypeError
            If the item is not hashable.
        ValueError
            If the item is already contained in the list.
        IndexError
            If the index is out of bounds.

        Notes
        -----
        Inserts are O(n) on average.
        """
        cdef ListNode node, curr
        cdef long long i

        # check if item is already present
        if item in self.items:
            raise ValueError(f"list elements must be unique: {repr(item)}")

        # allow negative indexing + check bounds
        index = self._normalize_index(index)

        # generate new node and add to item map
        node = ListNode(item)
        self.items[item] = node

        # insert at specified index, starting from nearest end
        if index <= len(self) // 2:
            # iterate forwards from head
            curr = self.head
            for i in range(index):
                curr = curr.next

            # insert before current node
            node.next = curr
            node.prev = curr.prev
            curr.prev = node
            if node.prev is None:
                self.head = node
            else:
                node.prev.next = node

        else:
            # iterate backwards from tail
            curr = self.tail
            for i in range(len(self) - index - 1):
                curr = curr.prev

            # insert after current node
            node.prev = curr
            node.next = curr.next
            curr.next = node
            if node.next is None:
                self.tail = node
            else:
                node.next.prev = node

    cdef void extend(self, object items):
        """Add multiple items to the end of the list.

        Parameters
        ----------
        items : Iterable[Hashable]
            An iterable of hashable items to add to the list.

        Raises
        ------
        TypeError
            If any of the items are not hashable.
        ValueError
            If any of the items are already contained in the list.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.
        """
        cdef object item

        for item in items:
            self.append(item)

    cdef void extendleft(self, object items):
        """Add multiple items to the beginning of the list.

        Parameters
        ----------
        items : Iterable[Hashable]
            An iterable of hashable items to add to the list.

        Raises
        ------
        TypeError
            If any of the items are not hashable.
        ValueError
            If any of the items are already contained in the list.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.

        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.  Just like
        that class, the series of left appends results in reversing the order
        of elements in ``items``.
        """
        cdef object item

        for item in items:
            self.appendleft(item)

    def __add__(self, object other: Iterable[Hashable]) -> "LinkedList":
        """Concatenate two lists.

        Parameters
        ----------
        other : Iterable[Hashable]
            The list to concatenate with this one.

        Returns
        -------
        LinkedList
            A new list containing the items from both lists.

        Raises
        ------
        TypeError
            If the other list is not iterable.
        ValueError
            If any of the items in the other list are already contained in this
            list.

        Notes
        -----
        Concatenation is O(n), where `n` is the length of the other list.
        """
        cdef LinkedList result = LinkedList(self)

        result.extend(other)
        return result

    def __iadd__(self, object other: Iterable[Hashable]) -> "LinkedList":
        """Concatenate two lists in-place.

        Parameters
        ----------
        other : Iterable[Hashable]
            The list to concatenate with this one.

        Returns
        -------
        LinkedList
            This list, with the items from the other list appended.

        Raises
        ------
        TypeError
            If the other list is not iterable.
        ValueError
            If any of the items in the other list are already contained in this
            list.

        Notes
        -----
        Concatenation is O(m), where `m` is the length of the ``other`` list.
        """
        self.extend(other)
        return self

    def __mul__(self, n: int) -> "LinkedList":
        """Repeat the list a specified number of times.

        Parameters
        ----------
        n : int
            The number of times to repeat the list.

        Returns
        -------
        LinkedList
            A new list containing the items from this list repeated ``n``
            times.  Due to the uniqueness constraint, this will always be
            either an empty list or a copy of this list.

        Raises
        ------
        ValueError
            If `n` is not 0 or 1.

        Notes
        -----
        Due to the uniqueness constraint, repetition is always O(1).
        """
        if n == 0:
            return LinkedList()
        if n == 1:
            return LinkedList(self)

        raise ValueError("repetition count must be 0 or 1")

    def __imul__(self, n: int) -> "LinkedList":
        """Repeat the list a specified number of times in-place.

        Parameters
        ----------
        n : int
            The number of times to repeat the list.

        Returns
        -------
        LinkedList
            This list, with the items repeated ``n`` times.  Due to the
            uniqueness constraint, this will always be either an empty list or
            the list itself.

        Raises
        ------
        ValueError
            If `n` is not 0 or 1.

        Notes
        -----
        Due to the uniqueness constraint, repetition is always O(1).
        """
        if n == 0:
            self.clear()
        elif n == 1:
            pass
        else:
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
        return <long long> item in self

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
        cdef ListNode node = self.head
        cdef long long index = 0

        while node is not None:
            if node.item == item:
                return index
            node = node.next
            index += 1

        raise ValueError(f"{repr(item)} is not contained in the list")

    cdef void sort(self):
        """Sort the list in-place.

        Notes
        -----
        Sorting is O(n log n) on average.
        """
        raise NotImplementedError()

    cdef void rotate(self, long long steps = 1):
        """Rotate the list to the right by the specified number of steps.

        Parameters
        ----------
        steps : int64, optional
            The number of steps to rotate the list.  If this is positive, the
            list will be rotated to the right.  If this is negative, the list
            will be rotated to the left.  The default is ``1``.

        Notes
        -----
        Rotations are O(steps).

        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.
        """
        cdef long long i

        # rotate right
        if steps > 0:
            for i in range(steps):
                self.appendleft(self.popright())

        # rotate left
        else:
            for i in range(steps):
                self.append(self.popleft())

    cdef void reverse(self):
        """Reverse the order of the list in-place.

        Notes
        -----
        Reversing a :class:`LinkedList` is O(n).
        """
        cdef ListNode node = self.head

        # swap all prev and next pointers
        while node is not None:
            node.prev, node.next = node.next, node.prev
            node = node.prev  # prev is now next

        # swap head and tail
        self.head, self.tail = self.tail, self.head

    def __getitem__(self, key: int | slice) -> Hashable:
        """Index the list for a particular item or slice.

        Parameters
        ----------
        key : int64 or slice
            The index or slice to retrieve from the list.  If this is a slice,
            the result will be a new :class:`LinkedList` containing the
            specified items.  This can be negative, following the same
            convention as Python's standard :class:`list <python:list>`.

        Returns
        -------
        object or LinkedList
            The item or list of items corresponding to the specified index or
            slice.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        See Also
        --------
        LinkedList.items :
            A dictionary mapping items to their corresponding nodes for fast
            access.
        LinkedList.__setitem__ :
            Set the value of an item or slice in the list.
        LinkedList.__delitem__ :
            Delete an item or slice from the list.

        Notes
        -----
        Integer-based indexing is O(n) on average.

        Slicing is optimized to always begin iterating from the end nearest to
        a slice boundary, and to never backtrack.  This is done by checking
        whether the slice is ascending (step > 0) or descending, and whether
        the start or stop index is closer to its respective end.  This gives
        the following cases:

            1) ascending, start closer to head than stop is to tail
                -> forwards from head
            2) ascending, stop closer to tail than start is to head
                -> backwards from tail
            3) descending, start closer to tail than stop is to head
                -> backwards from tail
            4) descending, stop closer to head than start is to tail
                -> forwards from head
        """
        cdef ListNode node
        cdef LinkedList result
        cdef long long start, stop, step, i
        cdef long long index, end_index

        # support slicing
        if isinstance(key, slice):
            # create a new LinkedList to hold the slice
            result = LinkedList()

            # determine direction of traversal to avoid backtracking
            start, stop, step = key.indices(len(self))
            index, end_index = self.get_slice_direction(start, stop, step)

            # get first node in slice, counting from nearest end
            node = self._node_at_index(index)

            # forward traversal
            if end_index >= index:
                while node is not None and index != end_index:
                    result.append(node.item)
                    for i in range(step):  # jump according to step size
                        if node is None:
                            break
                        node = node.next
                    index += step  # increment index

            # backward traversal
            else:
                while node is not None and index != end_index:
                    result.appendleft(node.item)
                    for i in range(step):  # jump according to step size
                        if node is None:
                            break
                        node = node.prev
                    index -= step  # decrement index

            return result

        # index directly
        key = self._normalize_index(key)
        node = self._node_at_index(key)
        return node.item

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
            If the value is not hashable.
        ValueError
            If any values are already contained in the list, or if the length
            of ``value`` does not match the length of the slice.
        IndexError
            If the index is out of bounds.

        See Also
        --------
        LinkedList.items :
            A dictionary mapping items to their corresponding nodes for fast
            access.
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
        cdef ListNode node
        cdef long long slice_size
        cdef long long start, stop, step, i
        cdef long long index, end_index
        cdef set replaced_items
        cdef list staged
        cdef object val, old_item, new_item

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
            index, end_index = self.get_slice_direction(start, stop, step)

            # get first node in slice, counting from nearest end
            node = self._node_at_index(index)

            # NOTE: due to the uniqueness constraint, we can't just blindly
            # overwrite values in the slice, as some of them might be present
            # elsewhere in the list.  We also don't care if a value is in the
            # masked items, since they will be overwritten anyway.  To address
            # this, we record the observed values and stage our changes to
            # avoid modifying values until we are sure they are valid.
            replaced_items = set()
            staged = list()

            # forward traversal
            values_iter = iter(value)
            if end_index >= index:
                for val in values_iter:
                    if node is None or index == end_index:
                        break

                    # check for uniqueness and stage the change
                    replaced_items.add(node.item)
                    if val in self.items and val not in replaced_items:
                        raise ValueError(
                            f"list elements must be unique: {repr(val)}"
                        )
                    staged.append((node, val))

                    # jump according to step size
                    for i in range(step):
                        if node is None:
                            break
                        node = node.next

                    # increment index
                    index += step

            # backward traversal
            else:
                for val in reversed(list(values_iter)):
                    if node is None or index == end_index:
                        break

                    # check for uniqueness and stage the change
                    replaced_items.add(node.item)
                    if val in self.items and val not in replaced_items:
                        raise ValueError(
                            f"list elements must be unique: {repr(val)}"
                        )
                    staged.append((node, val))

                    # jump according to step size
                    for i in range(step):
                        if node is None:
                            break
                        node = node.prev

                    # decrement index
                    index -= step

            # everything's good: update the list
            for old_item in replaced_items:
                del self.items[old_item]
            for node, new_item in staged:
                node.item = new_item
                self.items[new_item] = node

        # index directly
        else:
            key = self._normalize_index(key)
            node = self._node_at_index(key)

            # check for uniqueness
            if value in self.items and value != node.item:
                raise ValueError(f"list elements must be unique: {repr(value)}")

            # update the node's item and the items map
            del self.items[node.item]
            node.item = value
            self.items[value] = node

    def __delitem__(self, key: int | slice) -> None:
        """Delete an item or slice from the list.

        Parameters
        ----------
        key : int64 or slice
            The index or slice to delete from the list.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        See Also
        --------
        LinkedList.items :
            A dictionary mapping items to their corresponding nodes for fast
            access.
        LinkedList.__getitem__ :
            Index the list for a particular item or slice.
        LinkedList.__setitem__ :
            Set the value of an item or slice in the list.

        Notes
        -----
        Integer-based deletion is O(n) on average.

        Slice deletion is optimized to always begin iterating from the end
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
        cdef ListNode node
        cdef long long start, stop, step, i
        cdef long long index, end_index
        cdef list staged

        # support slicing
        if isinstance(key, slice):
            # determine direction of traversal to avoid backtracking
            start, stop, step = key.indices(len(self))
            index, end_index = self.get_slice_direction(start, stop, step)

            # get first node in slice, counting from nearest end
            node = self._node_at_index(index)

            # NOTE: we shouldn't delete items as we iterate.  Instead, we stage
            # the deletions and then perform them all at once at the end.
            staged = list()

            # forward traversal
            if end_index >= index:
                while node is not None and index != end_index:
                    staged.append(node)
                    for i in range(step):  # jump according to step size
                        if node is None:
                            break
                        node = node.next
                    index += step  # increment index

            # backward traversal
            else:
                while node is not None and index != end_index:
                    staged.append(node)
                    for i in range(step):  # jump according to step size
                        if node is None:
                            break
                        node = node.prev
                    index -= step

            # delete all staged nodes
            for node in staged:
                self._drop_node(node)

        # index directly
        else:
            key = self._normalize_index(key)
            node = self._node_at_index(key)
            self._drop_node(node)

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
        Removals are O(1) due to the presence of the item map.
        """
        cdef ListNode node

        # check if item is present
        try:
            node = self.items[item]
        except KeyError:
            raise ValueError(f"{repr(item)} is not contained in the list")

        # handle pointer arithmetic
        self._drop_node(node)

    cdef void clear(self):
        """Remove all items from the list.

        Notes
        -----
        This method is O(1).
        """
        self.head = None
        self.tail = None
        self.items.clear()

    cdef object pop(self, long long index = -1):
        """Remove and return the item at the specified index.

        Parameters
        ----------
        index : int64, optional
            The index of the item to remove.  If this is negative, it will be
            translated to a positive index by counting backwards from the end
            of the list.  The default is ``-1``, which removes the last item.

        Returns
        -------
        object
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
        cdef ListNode node

        # allow negative indexing + check bounds
        index = self._normalize_index(index)

        # get node at index
        node = self._node_at_index(index)

        # prev -> next
        if node.prev is None:
            self.head = node.next
        else:
            node.prev.next = node.prev

        # prev <- next
        if node.next is None:
            self.tail = node.prev
        else:
            node.next.prev = node.next

        # remove from item map
        del self.items[node.item]

        return node.item

    cdef object popleft(self):
        """Remove and return the first item in the list.

        Returns
        -------
        object
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
        cdef ListNode node

        if self.head is None:
            raise IndexError("pop from empty list")

        # remove from item map
        del self.items[self.head.item]

        # remove from list
        node = self.head
        self.head = node.next
        if self.head is None:
            self.tail = None
        else:
            self.head.prev = None

        return node.item

    cdef object popright(self):
        """Remove and return the last item in the list.

        Returns
        -------
        object
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
        cdef ListNode node

        if self.tail is None:
            raise IndexError("pop from empty list")

        # remove from item map
        del self.items[self.tail.item]

        # remove from list
        node = self.tail
        self.tail = node.prev
        if self.tail is None:
            self.head = None
        else:
            self.tail.next = None

        return node.item

    ###########################
    ####    COMPARISONS    ####
    ###########################

    def __lt__(self, other: Any) -> bool:
        """Check if this list is lexographically less than another list.

        Parameters
        ----------
        other : object
            The object to compare to this list.

        Returns
        -------
        bool
            Indicates whether the elements of this list are less than the
            elements of the other list.  This is determined lexicographically,
            meaning that the first pair of unequal elements determines the
            result.  If all elements are equal, then the shorter list is
            considered less than the longer list.

        Notes
        -----
        Comparisons are O(n).
        """
        if not isinstance(other, type(self)):
            return NotImplemented

        # compare elements at each index
        for a, b in zip(self, other):
            if a == b:
                continue
            return a < b

        # if all elements are equal, the shorter list is less than the longer
        return len(self) < len(other)

    def __le__(self, other: Any) -> bool:
        """Check if this list is lexographically less than or equal to another
        list.

        Parameters
        ----------
        other : object
            The object to compare to this list.

        Returns
        -------
        bool
            Indicates whether the elements of this list are less than or equal
            to the elements of the other list.  This is determined
            lexicographically, meaning that the first pair of unequal elements
            determines the result.  If all elements are equal, then the shorter
            list is considered less than or equal to the longer list.

        Notes
        -----
        Comparisons are O(n).
        """
        if not isinstance(other, type(self)):
            return NotImplemented

        # compare elements at each index
        for a, b in zip(self, other):
            if a == b:
                continue
            return a < b

        # if all elements are equal, the shorter list is less than or equal to
        # the longer
        return len(self) <= len(other)

    def __eq__(self, other: Any) -> bool:
        """Compare two lists for equality.

        Parameters
        ----------
        other : object
            The object to compare to this list.

        Returns
        -------
        bool
            Indicates whether the two lists are of compatible types and contain
            equal items at every index.

        Notes
        -----
        Comparisons are O(n).
        """
        if not isinstance(other, type(self)):
            return NotImplemented

        if len(self) != len(other):
            return False
        return all(a == b for a, b in zip(self, other))

    def __gt__(self, other: Any) -> bool:
        """Check if this list is lexographically greater than another list.

        Parameters
        ----------
        other : object
            The object to compare to this list.

        Returns
        -------
        bool
            Indicates whether the elements of this list are greater than the
            elements of the other list.  This is determined lexicographically,
            meaning that the first pair of unequal elements determines the
            result.  If all elements are equal, then the longer list is
            considered greater than the shorter list.

        Notes
        -----
        Comparisons are O(n).
        """
        if not isinstance(other, type(self)):
            return NotImplemented

        # compare elements at each index
        for a, b in zip(self, other):
            if a == b:
                continue
            return a > b

        # if all elements are equal, the longer list is greater than the
        # shorter
        return len(self) > len(other)

    def __ge__(self, other: Any) -> bool:
        """Check if this list is lexographically greater than or equal to
        another list.

        Parameters
        ----------
        other : object
            The object to compare to this list.

        Returns
        -------
        bool
            Indicates whether the elements of this list are greater than or
            equal to the elements of the other list.  This is determined
            lexicographically, meaning that the first pair of unequal elements
            determines the result.  If all elements are equal, then the longer
            list is considered greater than or equal to the shorter list.

        Notes
        -----
        Comparisons are O(n).
        """
        if not isinstance(other, type(self)):
            return NotImplemented

        # compare elements at each index
        for a, b in zip(self, other):
            if a == b:
                continue
            return a > b

        # if all elements are equal, the longer list is greater than or equal
        # to the shorter
        return len(self) >= len(other)

    #######################
    ####    PRIVATE    ####
    #######################

    cdef ListNode _node_at_index(self, long long index):
        """Get the node at the specified index.

        Parameters
        ----------
        index : int64
            The index of the node to retrieve.  This should always be passed
            through :meth:`LinkedList._normalize_index` first.

        Returns
        -------
        ListNode
            The node at the specified index.

        Notes
        -----
        This method is O(n) on average.  As an optimization, it always iterates
        from the nearest end of the list.
        """
        cdef ListNode node
        cdef long long size = len(self)
        cdef long long i

        # count forwards from head
        if index <= size // 2:
            node = self.head
            for i in range(index):
                node = node.next

        # count backwards from tail
        else:
            node = self.tail
            for i in range(size - index - 1):
                node = node.prev

        return node

    cdef long long _normalize_index(self, long long index):
        """Allow negative indexing and check if the result is within bounds.

        Parameters
        ----------
        index : int64
            The index to normalize.  If this is negative, it will be translated
            to a positive index by counting backwards from the end of the list.

        Returns
        -------
        int64
            The normalized index.

        Raises
        ------
        IndexError
            If the index is out of bounds.
        """
        cdef long long size = len(self)

        # allow negative indexing
        if index < 0:
            index = index + size

        # check bounds
        if not 0 <= index < size:
            raise IndexError("list index out of range")

        return index

    cdef (long long, long long) get_slice_direction(
        self,
        long long start,
        long long stop,
        long long step,
    ):
        """Determine the direction in which to traverse a slice so as to
        minimize total iterations.

        Parameters
        ----------
        start : int64
            The start index of the slice.
        stop : int64
            The stop index of the slice.
        step : int64
            The step size of the slice.

        Returns
        -------
        index : long long
            The index at which to start iterating.
        end_index : long long
            The index at which to stop iterating.

        Notes
        -----
        The direction of traversal is determined by comparing the indices
        returned by this method.  If ``end_index >= index``, then the slice
        should be traversed in the forward direction.  Otherwise, it should be
        iterated over backwards in order to avoid backtracking.
        """
        cdef long long index, end_index
        cdef long long size = len(self)

        # determine direction of traversal
        if (
            step > 0 and start <= size - stop or   # 1)
            step < 0 and size - start <= stop      # 4)
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

    cdef void _drop_node(self, ListNode node):
        """Remove a node from the list.

        Parameters
        ----------
        node : ListNode
            The node to remove from the list.

        Notes
        -----
        This is a simple helper method for doing the pointer arithmetic of
        removing a node, since it's used in multiple places.
        """
        # prev -> next
        if node.prev is None:
            self.head = node.next
        else:
            node.prev.next = node.next

        # prev <- next
        if node.next is None:
            self.tail = node.prev
        else:
            node.next.prev = node.prev

        # remove from item map
        del self.items[node.item]

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __len__(self) -> int:
        """Get the total number of items in the list.

        Returns
        -------
        int
            The number of items in the list.
        """
        return len(self.items)

    def __iter__(self) -> Iterator[Hashable]:
        """Iterate through the list items in order.

        Yields
        ------
        object
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedList` is O(n) on average.
        """
        node = self.head
        while node is not None:
            yield node.item
            node = node.next

    def __reversed__(self) -> Iterator[Hashable]:
        """Iterate through the list in reverse order.

        Yields
        ------
        object
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedList` is O(n) on average.
        """
        node = self.tail
        while node is not None:
            yield node.item
            node = node.prev

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
        return item in self.items

    def __bool__(self) -> bool:
        """Treat empty lists as Falsy in boolean logic.

        Returns
        -------
        bool
            Indicates whether the list is empty.
        """
        return bool(self.items)

    def __str__(self):
        """Return a standard string representation of the list.

        Returns
        -------
        str
            A string representation of the list.

        Notes
        -----
        Collecting the items for this method is O(n).
        """
        return str(list(self))

    def __repr__(self):
        """Return an annotated string representation of the list.

        Returns
        -------
        str
            An annotated string representation of the list.

        Notes
        -----
        Collecting the items for this method is O(n).
        """
        return f"{type(self).__name__}({list(self)})"


cdef class ListNode:
    """A node containing an individual element of a LinkedList.

    Parameters
    ----------
    item : object
        The item to store in the node.

    Attributes
    ----------
    item : object
        The item stored in the node.
    next : ListNode
        The next node in the list.
    prev : ListNode
        The previous node in the list.

    Notes
    -----
    The only reason why this isn't a ``cdef packed struct`` is because it
    contains a generic Python object, which is not allowed in structs.  If a
    subclass of LinkedList is created that only stores a specific
    (C-compatible) type of item, this can be changed to a struct for a slight
    performance boost.

    Note that if this is converted to a struct, memory would need to be handled
    manually, just like in C.
    """

    def __init__(self, object item):
        self.item = item
        self.next = None
        self.prev = None
