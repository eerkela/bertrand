"""This module contains a doubly-linked list that can be used to represent
a precedence order during sort operations.
"""
from typing import Any, Iterable

# NOTE: this is not currently used anywhere, but it is a simple and useful data
# structure that could maybe be used in the future.


cdef class PriorityList:
    """A doubly-linked list whose elements can be rearranged to represent a
    a precedence order during sort operations.

    The list is read-only when accessed from Python.

    Examples
    --------
    .. doctest::

        >>> foo = pdcast.PriorityList([1, 2, 3])
        >>> foo
        PriorityList([1, 2, 3])
        >>> foo.index(2)
        1
        >>> foo.move_up(2)
        >>> foo
        PriorityList([2, 1, 3])
        >>> foo.move_down(2)
        >>> foo
        PriorityList([1, 2, 3])
        >>> foo.move(2, -1)
        >>> foo
        PriorityList([1, 3, 2])
    """

    def __init__(self, items: Iterable = None):
        self.head = None
        self.tail = None
        self.items = {}
        if items is not None:
            for item in items:
                self.append(item)

    cdef void append(self, object item):
        """Add an item to the list.

        This method is inaccessible from Python.
        """
        node = PriorityNode(item)
        self.items[item] = node
        if self.head is None:
            self.head = node
            self.tail = node
        else:
            self.tail.next = node
            node.prev = self.tail
            self.tail = node

    cdef void remove(self, object item):
        """Remove an item from the list.

        This method is inaccessible from Python.
        """
        node = self.items[item]

        if node.prev is None:
            self.head = node.next
        else:
            node.prev.next = node.prev

        if node.next is None:
            self.tail = node.prev
        else:
            node.next.prev = node.next

        del self.items[item]

    cdef int normalize_index(self, int index):
        """Allow negative indexing and enforcing boundschecking."""
        if index < 0:
            index = index + len(self)

        if not 0 <= index < len(self):
            raise IndexError("list index out of range")

        return index

    def index(self, item: Any) -> int:
        """Get the index of an item within the list.

        Examples
        --------
        .. doctest::

            >>> foo = pdcast.PriorityList([1, 2, 3])
            >>> foo.index(2)
            1
        """
        for idx, typ in enumerate(self):
            if item == typ:
                return idx

        raise ValueError(f"{repr(item)} is not contained in the list")

    def move_up(self, item: Any) -> None:
        """Move an item up one level in priority.

        Examples
        --------
        .. doctest::

            >>> foo = pdcast.PriorityList([1, 2, 3])
            >>> foo.move_up(2)
            >>> foo
            PriorityList([2, 1, 3])
        """
        node = self.items[item]
        prv = node.prev
        if prv is not None:
            node.prev = prv.prev

            if node.prev is None:
                self.head = node
            else:
                node.prev.next = node

            if node.next is None:
                self.tail = prv
            else:
                node.next.prev = prv

            prv.next = node.next
            node.next = prv
            prv.prev = node

    def move_down(self, item: Any) -> None:
        """Move an item down one level in priority.

        Examples
        --------
        .. doctest::

            >>> foo = pdcast.PriorityList([1, 2, 3])
            >>> foo.move_down(2)
            >>> foo
            PriorityList([1, 3, 2])
        """
        node = self.items[item]
        nxt = node.next
        if nxt is not None:
            node.next = nxt.next

            if node.next is None:
                self.tail = node
            else:
                node.next.prev = node

            if node.prev is None:
                self.head = nxt
            else:
                node.prev.next = nxt

            nxt.prev = node.prev
            node.prev = nxt
            nxt.next = node

    def move(self, item: Any, index: int) -> None:
        """Move an item to the specified index.

        Notes
        -----
        This method can accept negative indices.

        Examples
        --------
        .. doctest::

            >>> foo = pdcast.PriorityList([1, 2, 3])
            >>> foo.move(2, -1)
            >>> foo
            PriorityList([1, 3, 2])
        """
        curr_index = self.index(item)
        index = self.normalize_index(index)

        node = self.items[item]
        if index < curr_index:
            for _ in range(curr_index - index):
                self.move_up(item)
        else:
            for _ in range(index - curr_index):
                self.move_down(item)

    def __len__(self) -> int:
        """Get the total number of items in the list."""
        return len(self.items)

    def __iter__(self):
        """Iterate through the list items in order."""
        node = self.head
        while node is not None:
            yield node.item
            node = node.next

    def __reversed__(self):
        """Iterate through the list in reverse order."""
        node = self.tail
        while node is not None:
            yield node.item
            node = node.prev

    def __bool__(self) -> bool:
        """Treat empty lists as boolean False."""
        return bool(self.items)

    def __contains__(self, item: Any) -> bool:
        """Check if the item is contained in the list."""
        return item in self.items

    def __getitem__(self, key: Any):
        """Index into the list using standard syntax."""
        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(len(self))
            return PriorityList(self[i] for i in range(start, stop, step))

        key = self.normalize_index(key)

        # count from nearest end
        if key < len(self) // 2:
            node = self.head
            for _ in range(key):
                node = node.next
        else:
            node = self.tail
            for _ in range(len(self) - key - 1):
                node = node.prev

        return node.item

    def __str__(self):
        return str(list(self))

    def __repr__(self):
        return f"{type(self).__name__}({list(self)})"


cdef class PriorityNode:
    """A node containing an individual element of a PriorityList."""

    def __init__(self, object item):
        self.item = item
        self.next = None
        self.prev = None
