# distutils: language = c++
"""This module contains a pure C/Cython implementation of a doubly-linked list
that uses a hash map to support fast lookups by value.
"""
from typing import Hashable, Iterable


####################
####    BASE    ####
####################


cdef class LinkedSet:
    """A pure Cython/C++ implementation of an ordered set using a linked list
    and a hash table for fast access to each node.

    Parameters
    ----------
    items : Iterable[Any], optional
        An iterable of items to initialize the list.  Each value must be both
        unique and hashable.
    reverse : bool, optional
        If ``True``, reverse the order of `items` during list construction.
        The default is ``False``.
    spec : Any, optional
        A type to enforce for elements of the list, allowing the creation of
        type-safe containers.  This can be in any format recognized by
        :func:`isinstance() <python:isinstance>`.  If it is set to ``None``,
        then type checking will be disabled for the list.  Defaults to ``None``.

    Attributes
    ----------
    view : SetView*
        A low-level view into the list.  This is a pointer to a C++ object that
        tracks the list's head and tail pointers and manages memory for each of
        its nodes.  It also encapsulates the list's hash table, which is
        automatically updated whenever a node is added or removed from the
        list.  This object is not intended to be accessed by the user, and
        manipulating it directly it can result in memory leaks and/or undefined
        behavior.  Thorough inspection of the C++ header files is recommended
        before attempting to access this attribute, which can only be done in
        Cython.

    Notes
    -----
    Because of the extra hash table, this data structure offers O(1) access to
    any node in the list by its value.  In the doubly-linked variant, it also
    supports O(1) removals.

    .. warning::

        These sets are not thread-safe.  If you need to use them in a
        multithreaded context, you should use a :class:`threading.Lock` to
        synchronize access.
    """

    def __init__(
        self,
        items: Iterable[object] | None = None,
        reverse: bool = False,
        spec: object | None = None,
    ):
        raise NotImplementedError(
            "`LinkedSet` is an abstract class and cannot be instantiated.  "
            "Use `SinglyLinkedSet` or `DoublyLinkedSet` instead."
        )

    ########################
    ####    ABSTRACT    ####
    ########################

    def append(self, item: object) -> None:
        """Add an item to the end of the list.

        Parameters
        ----------
        item : Any
            The item to add to the list.

        Notes
        -----
        Appends are O(1) for both ends of the list.
        """
        raise NotImplementedError()

    def appendleft(self, item: object) -> None:
        """Add an item to the beginning of the list.

        Parameters
        ----------
        item : Any
            The item to add to the list.

        Notes
        -----
        Appends are O(1) for both ends of the list.
        
        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.
        """
        raise NotImplementedError()

    def add(self, item: object) -> None:
        """An alias for :meth:`LinkedSet.append` that is consistent with the
        standard :class:`set <python:set>` interface.
        """
        # TODO: call the C++ method directly instead of going through Python
        raise NotImplementedError()

    def insert(self, index: int, item: object) -> None:
        """Insert an item at the specified index.

        Parameters
        ----------
        index : int
            The index at which to insert the item.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.
        item : Any
            The item to add to the list.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        Notes
        -----
        Inserts are O(n) on average.
        """
        raise NotImplementedError()

    def insertafter(self, sentinel: object, item: object, steps: int = 1) -> None:
        """Insert an item after a given sentinel value.

        Parameters
        ----------
        sentinel : Any
            The value to insert the item after.
        item : Any
            The item to add to the list.
        steps : int, optional
            An offset from the sentinel value.  If this is positive, this
            method will count to the right the specified number of spaces from
            the sentinel and insert the item at that index.  Negative values
            count to the left.  The default is ``1``.

        Raises
        ------
        ValueError
            If the sentinel is not contained in the set.

        See Also
        --------
        LinkedSet.insert :
            Insert an item at the specified index.
        LinkedSet.insertbefore :
            Insert an item relative to a given sentinel value.

        Notes
        -----
        Inserts are O(steps).

        Calling this method with negative steps is equivalent to calling
        :meth:`insertbefore() <LinkedSet.insertbefore>` with ``steps=-steps``.
        """
        raise NotImplementedError()

    def insertbefore(self, sentinel: object, item: object, steps: int = 1) -> None:
        """Insert an item before a given sentinel value.

        Parameters
        ----------
        sentinel : Any
            The value to insert the item before.
        item : Any
            The item to add to the list.
        steps : int, optional
            An offset from the sentinel value.  If this is positive, this
            method will count to the left the specified number of spaces from
            the sentinel and insert the item at that index.  Negative values
            count to the right.  The default is ``1``.

        Raises
        ------
        ValueError
            If the sentinel is not contained in the set.

        See Also
        --------
        LinkedSet.insert :
            Insert an item at the specified index.
        LinkedSet.insertafter :
            Insert an item relative to a given sentinel value.

        Notes
        -----
        Inserts are O(steps).

        Calling this method with negative steps is equivalent to calling
        :meth:`insertafter() <LinkedSet.insertafter>` with ``steps=-steps``.
        """
        raise NotImplementedError()

    def extend(self, items: Iterable[object]) -> None:
        """Add multiple items to the end of the list.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of items to add to the list.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.
        """
        raise NotImplementedError()

    def extendleft(self, items: Iterable[object]) -> None:
        """Add multiple items to the beginning of the set.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of items to add to the set.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.

        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.  Just like
        that class, the series of left appends results in reversing the order
        of elements in ``items``.
        """
        raise NotImplementedError()

    def extendafter(self, sentinel: object, items: Iterable[object]) -> None:
        """Add multiple items after a given sentinel value.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of items to add to the set.

        See Also
        --------
        LinkedSet.extend :
            Add multiple items to the end of the set.
        LinkedSet.extendleft :
            Add multiple items to the beginning of the set.
        LinkedSet.extendbefore :
            Add multiple items before a given sentinel value.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.
        """
        raise NotImplementedError()

    def extendbefore(self, sentinel: object, items: Iterable[object]) -> None:
        """Add multiple items before a given sentinel value.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of items to add to the set.

        See Also
        --------
        LinkedSet.extend :
            Add multiple items to the end of the set.
        LinkedSet.extendleft :
            Add multiple items to the beginning of the set.
        LinkedSet.extendafter :
            Add multiple items after a given sentinel value.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.

        Just like :meth:`extendleft() <LinkedSet.extendleft>`, this method
        implicitly reverses the order of elements in ``items``.
        """
        raise NotImplementedError()

    def index(self, item: object, start: int = 0, stop: int = -1) -> int:
        """Get the index of an item within the set.

        Parameters
        ----------
        item : Any
            The item to search for.

        Returns
        -------
        int
            The index of the item within the set.

        Raises
        ------
        ValueError
            If the item is not contained in the set.

        Notes
        -----
        Indexing is O(n) on average.
        """
        raise NotImplementedError()

    def count(self, item: object, start: int = 0, stop: int = -1) -> int:
        """Count the number of occurrences of an item in the set.

        Parameters
        ----------
        item : Any
            The item to count.

        Returns
        -------
        int
            The number of occurrences of the item in the set.

        Notes
        -----
        Counting is O(1).
        """
        raise NotImplementedError()

    def remove(self, item: object) -> None:
        """Remove an item from the set.

        Parameters
        ----------
        item : Any
            The item to remove from the set.

        Raises
        ------
        ValueError
            If the item is not contained in the set.

        Notes
        -----
        Removals are O(1).
        """
        raise NotImplementedError()

    def discard(self, item: object) -> None:
        """An alias for :meth:`LinkedSet.remove` that is consistent with the
        standard :class:`set <python:set>` interface.
        """
        # TODO: provide a C++ implementation that does not raise an error, and
        # therefore does not generate a stack trace.
        raise NotImplementedError()

    def discardafter(self, sentinel: object, item: object, steps: int = 1) -> None:
        """Remove an item from the set, relative to a given sentinel value.

        Parameters
        ----------
        item : Any
            The item to remove from the set.
        sentinel : Any
            The value to remove the item after.
        steps : int, optional
            An offset from the sentinel value.  If this is positive, this
            method will count to the right the specified number of spaces from
            the sentinel and remove the item at that index.  Negative values
            count to the left.  The default is ``1``.

        Raises
        ------
        ValueError
            If either the item or the sentinel is not contained in the set.

        See Also
        --------
        LinkedSet.remove :
            Remove an item from the set.
        LinkedSet.removebefore :
            Remove an item from the set relative to a given sentinel value.

        Notes
        -----
        Removals are O(steps).
        """
        raise NotImplementedError()

    def discardbefore(self, sentinel: object, item: object, steps: int = 1) -> None:
        """Remove an item from the set, relative to a given sentinel value.

        Parameters
        ----------
        item : Any
            The item to remove from the set.
        sentinel : Any
            The value to remove the item before.
        steps : int, optional
            An offset from the sentinel value.  If this is positive, this
            method will count to the left the specified number of spaces from
            the sentinel and remove the item at that index.  Negative values
            count to the right.  The default is ``1``.

        Raises
        ------
        ValueError
            If either the item or the sentinel is not contained in the set.

        See Also
        --------
        LinkedSet.remove :
            Remove an item from the set.
        LinkedSet.removeafter :
            Remove an item from the set relative to a given sentinel value.

        Notes
        -----
        Removals are O(steps).
        """
        raise NotImplementedError()

    def pop(self, index: int = -1) -> object:
        """Remove and return the item at the specified index.

        Parameters
        ----------
        index : int, optional
            The index of the item to remove.  If this is negative, it will be
            translated to a positive index by counting backwards from the end
            of the set.  The default is ``-1``, which removes the last item.

        Returns
        -------
        Any
            The item that was removed from the set.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        Notes
        -----
        Pops are O(1) if ``index`` points to either of the set's ends, and
        O(n) otherwise.
        """
        raise NotImplementedError()

    def popleft(self) -> object:
        """Remove and return the first item in the set.

        Returns
        -------
        Any
            The item that was removed from the set.

        Raises
        ------
        IndexError
            If the set is empty.

        Notes
        -----
        This is equivalent to :meth:`LinkedSet.pop` with ``index=0``, but it
        avoids the overhead of handling indices and is thus more efficient in
        the specific case of removing the first item.
        """
        raise NotImplementedError()

    def popright(self) -> object:
        """Remove and return the last item in the set.

        Returns
        -------
        Any
            The item that was removed from the set.

        Raises
        ------
        IndexError
            If the set is empty.

        Notes
        -----
        This is equivalent to :meth:`LinkedSet.pop` with ``index=-1``, but it
        avoids the overhead of handling indices and is thus more efficient in
        the specific case of removing the last item.
        """
        raise NotImplementedError()

    def popafter(self, sentinel: object, steps: int = 1) -> None:
        """Remove an item from the set relative to a given sentinel and return
        its value.

        Parameters
        ----------
        sentinel : Any
            The value to remove after.
        steps : int, optional
            An offset from the sentinel value.  If this is positive, this
            method will count to the right the specified number of spaces from
            the sentinel and pop the item at that index.  Negative values
            count to the left.  The default is ``1``.

        Raises
        ------
        ValueError
            If the sentinel is not contained in the set.

        See Also
        --------
        LinkedSet.pop :
            Remove and return the item at the specified index.
        LinkedSet.popleft :
            Remove and return the first item in the set.
        LinkedSet.popright :
            Remove and return the last item in the set.
        LinkedSet.popbefore :
            Remove an item from the set relative to a given sentinel value.

        Notes
        -----
        Pops are O(steps).
        """
        raise NotImplementedError()

    def popbefore(self, sentinel: object, steps: int = 1) -> None:
        """Remove an item from the set relative to a given sentinel and return
        its value.

        Parameters
        ----------
        sentinel : Any
            The value to remove before.
        steps : int, optional
            An offset from the sentinel value.  If this is positive, this
            method will count to the left the specified number of spaces from
            the sentinel and pop the item at that index.  Negative values
            count to the right.  The default is ``1``.

        Raises
        ------
        ValueError
            If the sentinel is not contained in the set.

        See Also
        --------
        LinkedSet.pop :
            Remove and return the item at the specified index.
        LinkedSet.popleft :
            Remove and return the first item in the set.
        LinkedSet.popright :
            Remove and return the last item in the set.
        LinkedSet.popafter :
            Remove an item from the set relative to a given sentinel value.

        Notes
        -----
        Pops are O(steps).
        """
        raise NotImplementedError()

    def copy(self) -> LinkedSet:
        """Create a shallow copy of the set.

        Returns
        -------
        DoublyLinkedList
            A new set containing the same items as this one.

        Notes
        -----
        Copying a :class:`LinkedSet` is O(n).
        """
        raise NotImplementedError()

    def clear(self) -> None:
        """Remove all items from the set.

        Notes
        -----
        Clearing a set is O(n).
        """
        raise NotImplementedError()

    def clearafter(self, sentinel: object, length: int = -1):
        """Remove multiple items after a given sentinel value.

        Parameters
        ----------
        sentinel : Any
            The value to remove items after.
        length : int, optional
            The number of items to remove.  If this is negative, then every
            value after the sentinel will be removed, making it the new tail.

        Raises
        ------
        ValueError
            If the sentinel is not contained in the set.

        See Also
        --------
        LinkedSet.clear :
            Remove all items from the set.
        LinkedSet.clearbefore :
            Remove all items before a given sentinel value.

        Notes
        -----
        Clearing is O(length).
        """
        raise NotImplementedError()

    def clearbefore(self, sentinel: object, length: int = -1):
        """Remove multiple items before a given sentinel value.

        Parameters
        ----------
        sentinel : Any
            The value to remove items before.
        length : int, optional
            The number of items to remove.  If this is negative, then every
            value before the sentinel will be removed, making it the new head.

        Raises
        ------
        ValueError
            If the sentinel is not contained in the set.

        See Also
        --------
        LinkedSet.clear :
            Remove all items from the set.
        LinkedSet.clearafter :
            Remove all items after a given sentinel value.

        Notes
        -----
        Clearing is O(length).
        """
        raise NotImplementedError()

    def sort(self, *, key: object = None, reverse: bool = False) -> None:
        """Sort the set in-place.

        Parameters
        ----------
        key : Callable[[Any], Any], optional
            A function that takes an item from the set and returns a value to
            use for sorting.  If this is not given, then the items will be
            compared directly.
        reverse : bool, optional
            Indicates whether to sort the set in descending order.  The
            default is ``False``, which sorts in ascending order.

        Notes
        -----
        Sorting is O(n log n), using an iterative merge sort algorithm that
        avoids recursion.  The sort is stable, meaning that the relative order
        of elements that compare equal will not change, and it is performed
        in-place for minimal memory overhead.

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
        raise NotImplementedError()

    def reverse(self) -> None:
        """Reverse the order of the set in-place.

        Notes
        -----
        Reversing a :class:`LinkedSet` is O(n).
        """
        raise NotImplementedError()

    def rotate(self, steps: int = 1) -> None:
        """Rotate the set to the right by the specified number of steps.

        Parameters
        ----------
        steps : int, optional
            The number of steps to rotate the set.  If this is positive, the
            set will be rotated to the right.  If this is negative, it will be
            rotated to the left.  The default is ``1``.

        Notes
        -----
        Rotations are O(steps).

        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.
        """
        raise NotImplementedError()

    def move(self, item: object, index: int = 0):
        """Move an item to a specific index in the set.

        Parameters
        ----------
        item : Any
            The item to move.
        index : int, optional
            The index to move the item to.  If this is negative, it will be
            translated to a positive index by counting backwards from the end
            of the set.  The default is ``0``, which moves the item to the
            beginning of the set.

        Raises
        ------
        KeyError
            If the item is not contained in the set.
        IndexError
            If the index is out of bounds.

        Notes
        -----
        Moves are O(1) for either end of the set, and scale up to O(n)
        towards the middle of the list.
        """
        raise NotImplementedError()

    def moveright(self, item: object, steps: int = 1):
        """Move an item to the right by the specified number of steps.

        Parameters
        ----------
        item : Any
            The item to move.
        steps : int, optional
            The number of steps to move the item.  If this is positive, the
            item will be moved to the right.  If it is negative, the item will
            be moved to the left.  The default is to move 1 index to the right.

        Raises
        ------
        KeyError
            If the item is not contained in the set.

        See Also
        --------
        LinkedSet.move :
            Move an item to a specific index in the set.
        LinkedSet.moveleft :
            Move an item to the left by the specified number of steps.
        LinkedSet.moveafter :
            Move an item relative to a given sentinel value.
        LinkedSet.movebefore :
            Move an item relative to a given sentinel value.

        Notes
        -----
        Moves are O(steps).

        Calling this method with negative steps is equivalent to calling
        :meth:`moveleft() <LinkedSet.moveleft>` with ``steps=-steps``.
        """
        raise NotImplementedError()

    def moveleft(self, item: object, steps: int = 1):
        """Move an item to the left by the specified number of steps.

        Parameters
        ----------
        item : Any
            The item to move.
        steps : int, optional
            The number of steps to move the item.  If this is positive, the
            item will be moved to the left.  If it is negative, the item will
            be moved to the right.  The default is to move 1 index to the left.

        Raises
        ------
        KeyError
            If the item is not contained in the set.

        See Also
        --------
        LinkedSet.move :
            Move an item to a specific index in the set.
        LinkedSet.moveright :
            Move an item to the right by the specified number of steps.
        LinkedSet.moveafter :
            Move an item relative to a given sentinel value.
        LinkedSet.movebefore :
            Move an item relative to a given sentinel value.

        Notes
        -----
        Moves are O(steps).

        Calling this method with negative steps is equivalent to calling
        :meth:`moveright() <LinkedSet.moveright>` with ``steps=-steps``.
        """
        raise NotImplementedError()

    def moveafter(self, item: object, sentinel: object, steps: int = 1):
        """Move an item to the right of a given sentinel value.

        Parameters
        ----------
        item : Any
            The item to move.
        sentinel : Any
            The value to move the item after.
        steps : int, optional
            The number of steps to move the item.  If this is positive, the
            item will be moved to the right.  If it is negative, the item will
            be moved to the left.  The default is to move 1 index to the right
            of the sentinel.

        Raises
        ------
        KeyError
            If either the item or the sentinel is not contained in the set.

        See Also
        --------
        LinkedSet.move :
            Move an item to a specific index in the set.
        LinkedSet.moveleft :
            Move an item to the left by the specified number of steps.
        LinkedSet.moveright :
            Move an item to the right by the specified number of steps.
        LinkedSet.movebefore :
            Move an item relative to a given sentinel value.

        Notes
        -----
        Moves are O(steps).

        Calling this method with negative steps is equivalent to calling
        :meth:`movebefore() <LinkedSet.movebefore>` with ``steps=-steps``.
        """
        raise NotImplementedError()

    def movebefore(self, item: object, sentinel: object, steps: int = 1):
        """Move an item to the left of a given sentinel value.

        Parameters
        ----------
        item : Any
            The item to move.
        sentinel : Any
            The value to move the item before.
        steps : int, optional
            The number of steps to move the item.  If this is positive, the
            item will be moved to the right.  If it is negative, the item will
            be moved to the left.  The default is to move 1 index to the left
            of the sentinel.

        Raises
        ------
        KeyError
            If either the item or the sentinel is not contained in the set.

        See Also
        --------
        LinkedSet.move :
            Move an item to a specific index in the set.
        LinkedSet.moveleft :
            Move an item to the left by the specified number of steps.
        LinkedSet.moveright :
            Move an item to the right by the specified number of steps.
        LinkedSet.moveafter :
            Move an item relative to a given sentinel value.

        Notes
        -----
        Moves are O(steps).

        Calling this method with negative steps is equivalent to calling
        :meth:`moveafter() <LinkedSet.moveafter>` with ``steps=-steps``.
        """
        raise NotImplementedError()

    def swap(self, item1: object, item2: object) -> None:
        """Swap the positions of two items in the set.

        Parameters
        ----------
        item1 : Any
            The first item to swap.
        item2 : Any
            The second item to swap.

        Raises
        ------
        KeyError
            If either item is not contained in the set.

        Notes
        -----
        Swaps are O(1).
        """
        raise NotImplementedError()

    def specialize(self, object spec) -> None:
        """Specialize the set with a particular type.

        Parameters
        ----------
        spec : Any
            The type to enforce for elements of the set.  This can be in any
            format recognized by :func:`isinstance() <python:isinstance>`.  If
            it is set to ``None``, then type checking will be disabled for the
            list.

        Raises
        ------
        TypeError
            If the set contains elements that do not match the specified type.

        Notes
        -----
        Specializing a set is O(n).

        The way type specialization works is by adding an extra
        :func:`isinstance() <python:isinstance>` check during node allocation.
        If the type of the new item does not match the specialized type, then
        an exception will be raised and the type will not be added to the set.
        This ensures that the set is type-safe at all times.

        If the set is not empty when this method is called, then the type of
        each existing item will be checked against the new type.  If any of
        them do not match, then the specialization will be aborted and an
        error will be raised.  The set is not modified by this process.

        .. note::

            Typed sets are slightly slower at appending items due to the extra
            type check.  Otherwise, they have identical performance to their
            untyped equivalents.
        """
        raise NotImplementedError()

    @property
    def specialization(self) -> Any:
        """Return the type specialization that is being enforced by the set.

        Returns
        -------
        Any
            The type specialization of the list, or ``None`` if the set is
            generic.

        See Also
        --------
        LinkedSet.specialize :
            Specialize the list with a particular type.

        Notes
        -----
        This is equivalent to the ``spec`` argument passed to the constructor
        and/or :meth:`specialize() <LinkedSet.specialize>` method.
        """
        raise NotImplementedError()

    def nbytes(self) -> int:
        """The total memory consumption of the set in bytes.

        Returns
        -------
        int
            The total number of bytes consumed by the set, including all its
            nodes (but not their values), as well as the internal hash table.
        """
        raise NotImplementedError()

    def __len__(self) -> int:
        """Get the total number of items in the set.

        Returns
        -------
        int
            The number of items in the set.
        """
        raise NotImplementedError()

    def __iter__(self) -> Iterator[object]:
        """Iterate through the set items in order.

        Yields
        ------
        Any
            The next item in the set.

        Notes
        -----
        Iterating through a :class:`LinkedSet` is O(n).
        """
        raise NotImplementedError()

    def __reversed__(self) -> Iterator[object]:
        """Iterate through the list in reverse order.

        Yields
        ------
        Any
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedSet` is O(n).
        """
        raise NotImplementedError()

    def __getitem__(self, key: int | slice) -> object | LinkedSet:
        """Index the set for a particular item or slice.

        Parameters
        ----------
        key : int or slice
            The index or slice to retrieve from the set.  If this is a slice,
            the result will be a new :class:`LinkedSet` containing the
            specified items.  This can be negative, following the same
            convention as Python's standard :class:`list <python:list>`.

        Returns
        -------
        scalar or LinkedSet
            The item or set of items corresponding to the specified index or
            slice.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        See Also
        --------
        LinkedSet.__setitem__ :
            Set the value of an item or slice in the set.
        LinkedSet.__delitem__ :
            Delete an item or slice from the set.

        Notes
        -----
        Integer-based indexing is O(n) on average.

        Slicing is optimized to always begin iterating from the end nearest to
        a slice boundary, and to never backtrack.  It collects all values in a
        single iteration and stops as soon as the slice is complete.
        """
        raise NotImplementedError()

    def __setitem__(self, key: int | slice, value: object | Iterable[object]) -> None:
        """Set the value of an item or slice in the set.

        Parameters
        ----------
        key : int or slice
            The index or slice to assign in the set.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.
        value : Any | Iterable[Any]
            The value or values to set at the specified index or slice.  If
            ``key`` is a slice, then ``value`` must be an iterable of the same
            length.

        Raises
        ------
        IndexError
            If the index is out of bounds.
        ValueError
            If the length of ``value`` does not match the length of the slice.

        See Also
        --------
        LinkedSet.__getitem__ :
            Index the set for a particular item or slice.
        LinkedSet.__delitem__ :
            Delete an item or slice from the set.

        Notes
        -----
        Integer-based assignment is O(n) on average.

        Slice assignment is optimized to always begin iterating from the end
        nearest to a slice boundary, and to never backtrack.  It assigns all
        values in a single iteration and stops as soon as the slice is
        complete.
        """
        raise NotImplementedError()

    def __delitem__(self, key: int | slice) -> None:
        """Delete an item or slice from the set.

        Parameters
        ----------
        key : int or slice
            The index or slice to delete from the set.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        See Also
        --------
        LinkedSet.__getitem__ :
            Index the set for a particular item or slice.
        LinkedSet.__setitem__ :
            Set the value of an item or slice in the set.

        Notes
        -----
        Integer-based deletion is O(n) on average.

        Slice deletion is optimized to always begin iterating from the end
        nearest to a slice boundary, and to never backtrack.  It deletes all
        values in a single iteration and stops as soon as the slice is
        complete.
        """
        raise NotImplementedError()

    def __contains__(self, item: object) -> bool:
        """Check if the item is contained in the set.

        Parameters
        ----------
        item : Any
            The item to search for.

        Returns
        -------
        bool
            Indicates whether the item is contained in the set.

        Notes
        -----
        Membership checks are O(1) due to the integrated hash table.
        """
        raise NotImplementedError()

    def __iand__(self, other: set | LinkedSet) -> LinkedSet:
        """Update this set with the intersection of itself and another iterable.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to take the intersection with.

        Returns
        -------
        LinkedSet
            A combination of this set with the elements of the other set.
        """
        # this should be implemented in C++
        cdef self.view.Node* prev = NULL
        cdef self.view.Node* curr = self.view.head
        cdef PyObject* value

        while curr is not NULL:
            value = <object>curr.value
            if <object>curr.value not in other:
                if prev is NULL:
                    self.view.head = curr.next
                else:
                    prev.next = curr.next

                stack.push(curr)
            else:
                prev = curr

            curr = curr.next

        return self

    def __isub__(self, other: set | LinkedSet) -> LinkedSet:
        """Update this set with the difference of itself and another iterable.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to take the difference with.

        Returns
        -------
        LinkedSet
            A combination of this set with the elements of the other set.
        """
        # TODO: implement this in C++ under a set_compare.h header
        # -> this is the opposite of __iand__(), so we can do it in one
        return self

    #########################
    ####    INHERITED    ####
    #########################

    def isdisjoint(self, other: Iterable[object]) -> bool:
        """Check if this set is disjoint with another iterable.

        Parameters
        ----------
        other : Iterable[Any]
            The other iterable to check for disjointness.

        Returns
        -------
        bool
            Indicates whether the set is disjoint with the other iterable.

        Notes
        -----
        This is equivalent to ``len(self.intersection(other)) == 0``.
        """
        return not (self & other)

    def issubset(self, other: Iterable[object]) -> bool:
        """Check if this set is a subset of another iterable.

        Parameters
        ----------
        other : Iterable[Any]
            The other iterable to check for subsetness.

        Returns
        -------
        bool
            Indicates whether the set is a subset of the other iterable.

        Notes
        -----
        This is equivalent to ``len(self.difference(other)) == 0``.
        """
        return self <= other

    def issuperset(self, other: Iterable[object]) -> bool:
        """Check if this set is a superset of another iterable.

        Parameters
        ----------
        other : Iterable[Any]
            The other iterable to check for supersetness.

        Returns
        -------
        bool
            Indicates whether the set is a superset of the other iterable.

        Notes
        -----
        This is equivalent to ``len(self.symmetric_difference(other)) == 0``.
        """
        return self >= other

    def union(self, *others: Iterable[object]) -> LinkedSet:
        """Return a new set with elements from this set and all others.

        Parameters
        ----------
        *others : Iterable[Any]
            Any number of iterables containing items to add to the set.

        Returns
        -------
        LinkedSet
            A new set containing the combination of this set with each of the
            inputs.

        Notes
        -----
        This translates to a series of :meth:`extend() <LinkedSet.extend>`
        calls for each iterable.
        """
        result = self.copy()
        result.update(*others)
        return result

    def intersection(self, *others: Iterable[object]) -> LinkedSet:
        """Return a new set with elements common to this set and all others.

        Parameters
        ----------
        *others : Iterable[Any]
            Any number of iterables containing items to intersect with the set.

        Returns
        -------
        LinkedSet
            A new set containing the intersection of this set with each of the
            inputs.
        """
        result = self.copy()
        result.intersection_update(*others)
        return result

    def difference(self, *others: Iterable[object]) -> LinkedSet:
        """Return a new set with elements in this set that are not in others.

        Parameters
        ----------
        *others : Iterable[Any]
            Any number of iterables containing items to subtract from the set.

        Returns
        -------
        LinkedSet
            A new set containing the difference of this set with each of the
            inputs.
        """
        result = self.copy()
        result.difference_update(*others)
        return result

    def symmetric_difference(self, other: Iterable[object]) -> LinkedSet:
        """Return a new set with elements in either this set or ``other``, but
        not both.

        Parameters
        ----------
        other : Iterable[Any]
            An iterable containing items to compare with the set.

        Returns
        -------
        LinkedSet
            A new set containing the symmetric difference of this set with the
            input.
        """
        result = self.copy()
        result.symmetric_difference_update(other)
        return result

    def update(self, *others: Iterable[object]) -> None:
        """Update the set, adding elements from all others.

        Parameters
        ----------
        *others : Iterable[Any]
            Any number of iterables containing items to add to the set.

        Notes
        -----
        This behaves just like the standard :meth:`set.update()` method.  It
        translates to a series of :meth:`extend() <LinkedSet.extend>` calls for
        each iterable.
        """
        for items in others:
            self.extend(items)

    def intersection_update(self, *others: Iterable[object]) -> None:
        """Update the set, keeping only elements found in all others.

        Parameters
        ----------
        *others : Iterable[Any]
            Any number of iterables containing items to intersect with the set.

        Notes
        -----
        This behaves just like the standard :meth:`set.intersection_update()`
        method.
        """
        for other in others:
            self &= other

    def difference_update(self, *others: Iterable[object]) -> None:
        """Update the set, removing elements found in others.

        Parameters
        ----------
        *others : Iterable[Any]
            Any number of iterables containing items to remove from the set.

        Notes
        -----
        This behaves just like the standard :meth:`set.difference_update()`
        method.
        """
        for other in others:
            self -= other

    def symmetric_difference_update(self, other: Iterable[object]) -> None:
        """Update the set, keeping only elements found in either set, but not
        in both.

        Parameters
        ----------
        other : Iterable[Any]
            An iterable containing items to compare with the set.

        Notes
        -----
        This behaves just like the standard
        :meth:`set.symmetric_difference_update()` method.
        """
        self ^= other

    __hash__ = None  # mutable containers are not hashable

    def __bool__(self) -> bool:
        """Treat empty sets as Falsy in boolean logic.

        Returns
        -------
        bool
            Indicates whether the set is empty.
        """
        return bool(len(self))

    def __str__(self):
        """Return a standard string representation of the set.

        Returns
        -------
        str
            A string representation of the set.

        Notes
        -----
        Creating a string representation of a set is O(n).
        """
        return f"{{{', '.join(str(item) for item in self)}}}"

    def __repr__(self):
        """Return an annotated string representation of the set.

        Returns
        -------
        str
            An annotated string representation of the set.

        Notes
        -----
        Creating a string representation of a set is O(n).
        """
        prefix = f"{type(self).__name__}"

        # append specialization if given
        if self.specialization is not None:
            prefix += f"[{repr(self.specialization)}]"

        # abbreviate in order to avoid spamming the console
        if len(self) > 64:
            contents = ", ".join(repr(item) for item in self[:32])
            contents += ", ..., "
            contents += ", ".join(repr(item) for item in self[-32:])
        else:
            contents = ", ".join(repr(item) for item in self)

        return f"{prefix}({{{contents}}})"

    def __class_getitem__(cls, key: Any) -> type:
        """Subscribe a linked set class to a particular type specialization.

        Parameters
        ----------
        key : Any
            The type to enforce for elements of the list.  This can be in any
            format recognized by :func:`isinstance() <python:isinstance>`,
            including tuples and
            :func:`runtime-checkable <python:typing.runtime_checkable>`
            :class:`typing.Protocol <python:typing.Protocol>` objects.

        See Also
        --------
        LinkedSet.specialize :
            Specialize a list at runtime.

        Returns
        -------
        type
            A variant of the linked set that is permanently specialized to the
            templated type.  Constructing such a list is equivalent to calling
            the constructor with the ``spec`` argument, except that the
            specialization cannot be changed for the lifetime of the object.

        Notes
        -----
        :class:`LinkedSets <LinkedSet>` provide 3 separate mechanisms for
        enforcing type safety:

            #.  The :meth:`specialize() <LinkedSet.specialize>` method, which
                allows runtime specialization of a list with a particular type.
                If the list is not empty when this method is called, it will
                loop through the set and check whether the contents satisfy
                the specialized type, and then enforce that type for any future
                additions to the set.
            #.  The ``spec`` argument to the constructor, which allows
                specialization at the time of list creation.  This is
                equivalent to calling :meth:`specialize() <LinkedSet.specialize>`
                immediately after construction, but avoids an extra loop.
            #.  Direct subscription via the
                :meth:`__class_getitem__() <python:object.__class_getitem__>`
                syntax.  This is equivalent to using the ``spec`` argument to
                create a typed list, except that the specialization is
                permanent and cannot be changed afterwards.  This is the most
                restrictive form of type safety, but also allows users to be
                absolutely sure about the list's contents.

        In any case, a set's specialization can be checked at any time by
        accessing its :attr:`specialization` attribute, which can be used in
        :func:`isinstance() <python:isinstance>` and
        :func:`issubclass() <python:issubclass>` checks directly.

        Examples
        --------
        .. doctest::

            >>> d = DoublyLinkedSet[int]([1, 2, 3])
            TypedList[<class 'int'>]([1, 2, 3])
            >>> d.specialization
            <class 'int'>
            >>> d.append(4)
            >>> d
            TypedList[<class 'int'>]([1, 2, 3, 4])
            >>> d.append("foo")
            Traceback (most recent call last):
                ...
            TypeError: 'foo' is not of type <class 'int'>
            >>> d.specialize((int, str))
            Traceback (most recent call last):
                ...
            TypeError: TypedList is already specialized to <class 'int'>

        Because type specialization is enforced through the
        :func:`isinstance() <python:isinstance>` function, it is possible to
        specialize a list with any type that implements the
        :func:`__instancecheck__() <python:object.__instancecheck__>` special
        method, including :func:`runtime-checkable <python:typing.runtime_checkable>`
        :class:`typing.Protocol <python:typing.Protocol>` objects.

        .. doctest::

            >>> from typing import Iterable

            >>> d = DoublyLinkedSet[Iterable]()
            >>> d.append([1, 2, 3])
            >>> d
            TypedList[typing.Iterable]([[1, 2, 3]])
            >>> d.append("foo")
            >>> d
            TypedList[typing.Iterable]([[1, 2, 3], 'foo'])
            >>> d.append(4)
            Traceback (most recent call last):
                ...
            TypeError: 4 is not of type typing.Iterable

        .. note::

            Type checking with
            :func:`runtime-checkable <python:typing.runtime_checkable>` protocols
            can significantly slow down set appends and inserts.  Other operations
            are unaffected, however.
        """
        if key is None:
            return cls

        # TODO: We can maybe maintain an LRU dictionary of specialized classes
        # to avoid creating a new one every time.  This would also allow
        # isinstance() checks to work properly on specialized lists, provided
        # that the class is in the LRU dictionary.

        # we return a decorated class that permanently specializes itself
        # with the given type.
        class TypedSet(cls):
            """A strictly-typed set class."""

            def __init__(
                self,
                items: Iterable[object] | None = None,
                reverse: bool = False,
            ) -> None:
                """Disable the `spec` argument for TypedLists."""
                super().__init__(items, reverse=reverse, spec=key)

            def specialize(self, spec: object) -> None:
                """Disable runtime specialization for TypedLists."""
                raise TypeError(f"TypedList is already specialized to {repr(key)}")

        return TypedList

    def __or__(self, other: set | LinkedSet) -> LinkedSet:
        """Return the union of this set and another iterable.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to take the union with.

        Returns
        -------
        LinkedSet
            A new set containing the union of this set and the other iterable.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        cdef LinkedSet result = self.copy()
        result.extend(other)
        return result

    def __ior__(self, other: set | LinkedSet) -> LinkedSet:
        """Update this set with the union of itself and another iterable.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to take the union with.

        Returns
        -------
        LinkedSet
            A combination of this set with the elements of the other set.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        self.extend(other)
        return self

    def __and__(self, other: set | LinkedSet) -> LinkedSet:
        """Return the intersection of this set and another iterable.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to take the intersection with.

        Returns
        -------
        LinkedSet
            A new set containing the intersection of this set and the other
            iterable.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        return LinkedSet(item for item in other if item in self)

    def __sub__(self, other: set | LinkedSet) -> LinkedSet:
        """Return the difference of this set and another iterable.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to take the difference with.

        Returns
        -------
        LinkedSet
            A new set containing the difference of this set and the other
            iterable.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        return LinkedSet(item for item in self if item not in other)

    def __xor__(self, other: set | LinkedSet) -> LinkedSet:
        """Return the symmetric difference of this set and another iterable.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to take the symmetric difference with.

        Returns
        -------
        LinkedSet
            A new set containing the symmetric difference of this set and the
            other iterable.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        cdef set left = set(item for item in self if item not in other)
        cdef set right = set(item for item in other if item not in self)

        return LinkedSet(left | right)

    def __ixor__(self, other: set | LinkedSet) -> LinkedSet:
        """Update this set with the symmetric difference of itself and another
        iterable.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to take the symmetric difference with.

        Returns
        -------
        LinkedSet
            A combination of this set with the symmetric difference of the
            other set.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        cdef set left = set(item for item in self if item not in other)
        cdef set right = set(item for item in other if item not in self)

        self.clear()
        self.extend(left | right)
        return self

    def __lt__(self, other: set | LinkedSet) -> bool:
        """Return whether this set is a proper subset of another.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to compare with.

        Returns
        -------
        bool
            Whether this set is a proper subset of the other set.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        return len(self) < len(other) and all(item in other for item in self)

    def __le__(self, other: set | LinkedSet) -> bool:
        """Return whether this set is a subset of another.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to compare with.

        Returns
        -------
        bool
            Whether this set is a subset of the other set.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        return len(self) <= len(other) and all(item in other for item in self)

    def __eq__(self, other: set | LinkedSet) -> bool:
        """Return whether this set is equal to another.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to compare with.

        Returns
        -------
        bool
            Whether this set is equal to the other set.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        return len(self) == len(other) and all(item in self for item in other)

    def __ge__(self, other: set | LinkedSet) -> bool:
        """Return whether this set is a superset of another.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to compare with.

        Returns
        -------
        bool
            Whether this set is a superset of the other set.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        return len(self) >= len(other) and all(item in self for item in other)

    def __gt__(self, other: set | LinkedSet) -> bool:
        """Return whether this set is a proper superset of another.

        Parameters
        ----------
        other : set or LinkedSet
            The other set to compare with.

        Returns
        -------
        bool
            Whether this set is a proper superset of the other set.
        """
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        return len(self) > len(other) and all(item in self for item in other)

#############################
####    SINGLY-LINKED    ####
#############################






#############################
####    DOUBLY-LINKED    ####
#############################


cdef class DoublyLinkedSet(LinkedSet):
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
