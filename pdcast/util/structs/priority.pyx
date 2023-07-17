"""This module contains a doubly-linked list that can be used to represent
a precedence order during sort operations.
"""
from typing import Any, Iterable, NoReturn

from .list cimport HashedList, ListNode

# NOTE: this is not currently used anywhere, but it is a simple and useful data
# structure that could maybe be used in the future.


cdef class PriorityList(HashedList):
    """A doubly-linked list whose elements can be rearranged to represent a
    a precedence order during sort operations.

    The list is read-only when accessed from Python.

    Examples
    --------
    .. doctest::

        >>> foo = PriorityList([1, 2, 3])
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

    def index(self, item: Any, start: int = 0, stop: int = -1) -> int:
        """Get the index of an item within the list.

        Examples
        --------
        .. doctest::

            >>> foo = PriorityList([1, 2, 3])
            >>> foo.index(2)
            1
        """
        return super().index(item, start, stop)

    def move_up(self, item: Any) -> None:
        """Move an item one index toward the front of the list.

        Examples
        --------
        .. doctest::

            >>> foo = PriorityList([1, 2, 3])
            >>> foo.move_up(2)
            >>> foo
            PriorityList([2, 1, 3])
        """
        # get node
        node = self.nodes[item]

        # do nothing if item is already at the front of the list
        curr = node.prev
        if curr is not None:
            # curr.prev <-> node
            node.prev = curr.prev
            if node.prev is None:
                self.head = node
            else:
                node.prev.next = node

            # curr <-> node.next
            curr.next = node.next
            if node.next is None:
                self.tail = curr
            else:
                node.next.prev = curr

            # node <-> curr
            node.next = curr
            curr.prev = node

    def move_down(self, item: Any) -> None:
        """Move an item one index toward the back of the list.

        Examples
        --------
        .. doctest::

            >>> foo = PriorityList([1, 2, 3])
            >>> foo.move_down(2)
            >>> foo
            PriorityList([1, 3, 2])
        """
        # get node
        node = self.nodes[item]

        # do nothing if item is already at the back of the list
        curr = node.next
        if curr is not None:
            # node <-> curr.next
            node.next = curr.next
            if node.next is None:
                self.tail = node
            else:
                node.next.prev = node

            # curr.prev <-> node
            curr.prev = node.prev
            if node.prev is None:
                self.head = curr
            else:
                node.prev.next = curr

            # curr <-> node
            node.prev = curr
            curr.next = node

    def move(self, item: Any, index: int) -> None:
        """Move an item to the specified index.

        Notes
        -----
        This method can accept negative indices.

        Examples
        --------
        .. doctest::

            >>> foo = PriorityList([1, 2, 3])
            >>> foo.move(2, -1)
            >>> foo
            PriorityList([1, 3, 2])
        """
        # NOTE: there's probably a more efficient way of doing this using the
        # _node_at_index() helper method.  This would avoid doing pointer
        # arithmetic for every index between the current and target indices,
        # and would instead do only a single substitution.  If this data
        # structure is ever used for real, this should be implemented, but it's
        # fine for now.

        curr_index = self.index(item)
        index = self._normalize_index(index)

        node = self.items[item]
        if index < curr_index:
            for _ in range(curr_index - index):
                self.move_up(item)
        else:
            for _ in range(index - curr_index):
                self.move_down(item)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    # NOTE: unless we override these methods, they will be accessible from
    # Python, meaning that the list will not be strictly read-only.

    def __setitem__(self, key: int | slice, item: Any) -> NoReturn:
        raise TypeError("cannot set items in a PriorityList")

    def __delitem__(self, key: int | slice) -> NoReturn:
        raise TypeError("cannot delete items from a PriorityList")

    def __iadd__(self, other: Iterable[Hashable]) -> NoReturn:
        raise TypeError("cannot add items to a PriorityList")

    def __imul__(self, repeat: int) -> NoReturn:
        raise TypeError("cannot add items a PriorityList")
