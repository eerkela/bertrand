# distutils: language = c++
"""This module contains a pure Cython/C++ implementation of an ordered set that
uses a hash map of linked nodes to support fast lookups by value.
"""
from typing import Iterable, Iterator


cdef class LinkedSet(LinkedList):
    """A pure Cython/C++ implementation of an ordered set using a linked list
    and a hash table for fast access to each node.

    This is a drop-in relacement for a standard Python :class:`set <python:set>`
    supporting all of the same operations.

    Parameters
    ----------
    items : Iterable[Hashable], optional
        An iterable of items to initialize the list.  Each value must be both
        unique and hashable.
    doubly_linked : bool, optional
        Controls how each node is linked to its neighbors.  Doubly-linked sets
        use more memory, but allow for efficient iteration in both directions,
        speeding up operations like :meth:`insert() <LinkedSet.insert>`,
        :meth:`pop() <LinkedSet.pop>`, and
        :meth:`__getitem__() <LinkedSet.__getitem__>`.  The default is
        ``True``.
    reverse : bool, optional
        If ``True``, reverse the order of ``items`` during list construction.
        This is more efficient than calling
        :meth:`reverse() <LinkedSet.reverse>` after construction.  The default
        The default is ``False``.
    max_size : int, optional
        The maximum number of items that can be stored in the set.  If this is
        set to a positive value, then the set will pre-allocate a contiguous
        block of memory to store each node, reducing memory fragmentation and
        improving performance for fixed-size sets.  These sets cannot grow
        beyond the given size, and attempting to append any further items will
        raise an exception.  The default is ``-1``, which disables this feature.
    spec : Any, optional
        A specific type to enforce for elements of the set, allowing the
        creation of type-safe containers.  This can be in any format recognized
        by :func:`isinstance() <python:isinstance>`.  The default is ``None``,
        which disables strict type checking for the set.  See the
        :meth:`specialize() <LinkedSet.specialize>` method for more details.

    Attributes
    ----------
    view : VariantSet*
        A low-level C++ wrapper around the set's contents.  This is a pointer to
        a C++ object that wraps a set of templated :class:`SetView` classes
        as a single variant type, which binds the correct implementation for
        each method statically at compile time.  There are no virtual
        functions or dynamic dispatch involved other than a single
        :func:`std::visit()` call to resolve the variant type, so this
        preserves as much performance as possible from the C++ implementation.
        This can only be accessed from Cython/C++, and it's not intended for
        general use.  Thorough inspection of the C++ header files is
        recommended before attempting to access this attribute.

    Notes
    -----
    Because of the extra hash table, this data structure offers O(1) access to
    any node in the set by its value.  In the doubly-linked variant, it also
    supports O(1) removals.

    .. warning::

        :class:`LinkedSets <LinkedSet>` are not thread-safe.  If you want to
        use them in a multithreaded context, you should use a
        :class:`threading.Lock` to synchronize access.
    """

    def __init__(
        self,
        items: Iterable[object] | None = None,
        doubly_linked: bool = True,
        reverse: bool = False,
        max_size: int = -1,
        spec: object | None = None,
    ):
        # init empty
        if items is None:
            self.view = new VariantSet(doubly_linked, max_size)
            if spec is not None:
                self.view.specialize(<PyObject*>spec)

        # unpack iterable
        elif spec is None:
            self.view = new VariantSet(
                <PyObject*>items, doubly_linked, reverse, max_size, NULL
            )
        else:
            self.view = new VariantSet(
                <PyObject*>items, doubly_linked, reverse, max_size, <PyObject*>spec
            )

    def __dealloc__(self):
        del self.view

    @staticmethod
    cdef LinkedSet from_view(VariantSet* view):
        """Create a new LinkedSet from a C++ view."""
        cdef LinkedSet result = LinkedSet.__new__(LinkedSet)  # bypass __init__()
        result.view = view
        return result

    ##############################
    ####    LIST INTERFACE    ####
    ##############################

    # NOTE: since LinkedSets are just variations on LinkedLists, they support
    # all of the same operations as normal Python lists.  The only difference
    # is that they require their values to be hashable and unique, and will
    # throw exceptions if this is not the case.  Only the methods that differ
    # from LinkedLists are documented here.

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
        If start and stop indices are not given, then counting devolves into a
        simple membership check across the whole set, which is O(1).  Otherwise,
        it is O(n) on average, where ``n`` is the number of items in the set.
        """
        # dispatch to count.h
        return self.view.count(<PyObject*>item, start, stop)

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
        If the set is doubly-linked, then removals are O(1) due to the extra
        hash table.  Otherwise, if the set is singly-linked, then they are O(n)
        on average, where ``n`` is the number of items in the set.
        """
        # delegate to remove.h
        self.view.remove(<PyObject*>item)

    def __mul__(self, repeat: int) -> LinkedSet:
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
        :class:`LinkedSets <LinkedSet>`.
        """
        if repeat == 0:
            return type(self)()
        if repeat == 1:
            return self.copy()

        raise ValueError("repetition count must be 0 or 1")

    def __imul__(self, repeat: int) -> LinkedSet:
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
        :class:`LinkedSets <LinkedSet>`.
        """
        if repeat == 0:
            self.clear()
        elif repeat != 1:
            raise ValueError("repetition count must be 0 or 1")

        return self

    ###################################
    ####    RELATIVE OPERATIONS    ####
    ###################################

    # NOTE: one thing linked sets are especially good at is inserting, removing
    # and reordering items relative to one another.  Since the set has O(1)
    # access to each node, these operations can be done in constant time
    # anywhere in the set, which is not possible with LinkedLists or unordered
    # Python sets.

    def distance(self, item1: object, item2: object) -> int:
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

    def move(self, item: object, steps: int = 1) -> None:
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

    def move_to_index(self, item: object, index: int = 0) -> None:
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

    def move_relative(self, item: object, sentinel: object, offset: int = 1) -> None:
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

    def insert_relative(self, item: object, sentinel: object, offset: int = 1) -> None:
        """Insert an item relative to a given sentinel value.

        Parameters
        ----------
        sentinel : Any
            The value to begin counting from.
        item : Any
            The item to add to the set.
        offset : int, optional
            An offset from the sentinel value.  If this is positive, this
            method will count to the right the specified number of spaces from
            the sentinel and insert the item at that index.  Negative values
            count to the left.  0 is invalid.  The default is ``1``.

        Raises
        ------
        KeyError
            If the sentinel is not contained in the set.
        TypeError
            If the value is not hashable.
        ValueError
            If the value is already contained in the set or if the offset is
            zero.

        See Also
        --------
        LinkedSet.insert :
            Insert an item at the specified index.

        Notes
        -----
        Relative inserts are O(offset) thanks to the internal hash map.

        This method is significantly faster than an :meth:`index() <LinkedSet.index>`
        lookup followed by an :meth:`insert() <LinkedSet.insert>` call, which
        would be O(2n) on average.
        """
        # dispatch to insert.h
        self.view.insert_relative(
            <PyObject*>sentinel,
            <PyObject*>item,
            <ssize_t>offset
        )

    def extend_relative(
        self,
        items: Iterable[object],
        sentinel: object,
        offset: int = 1
        reverse: bool = False
    ) -> None:
        """Insert a sequence of items relative to a given sentinel value.

        Parameters
        ----------
        sentinel : PyObject*
            The value after which to insert the new values.
        other : Iterable[Hashable]
            The values to insert into the set.

        Raises
        ------
        KeyError
            If the sentinel is not contained in the set.
        TypeError
            If any values are not hashable.
        ValueError
            If any values are already contained in the set.

        See Also
        --------
        LinkedSet.extend :
            Add a sequence of items to the end of the set.

        Notes
        -----
        Thanks to the internal hash map, relative extends are O(offset + m),
        where `m` is the length of ``other``.

        This method is significantly faster than an :meth:`index() <LinkedSet.index>`
        lookup followed by a slice assignment, which would be O(2n + m) on
        average.
        """
        # dispatch to extend.h
        self.view.extend_relative(
            <PyObject*>sentinel,
            <PyObject*>items,
            <Py_ssize_t>offset,
            <bint>reverse
        )

    def get_relative(self, sentinel: object, offset: int = 1) -> object:
        raise NotImplementedError()

    def remove_relative(self, sentinel: object, offset: int = 1) -> None:
        raise NotImplementedError()

    def discard_relative(self, sentinel: object, offset: int = 1) -> None:
        """Remove an item from the set, relative to a given sentinel value.

        Parameters
        ----------
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
        # dispatch to remove.h
        self.view.discardafter(<PyObject*>sentinel, <PyObject*>item, <ssize_t>steps)

    def pop_relative(self, sentinel: object, offset: int = 1) -> object:
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

    def clear_relative(
        self,
        sentinel: object,
        offset: int = 1,
        length: int = -1
    ) -> None:
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

    #############################
    ####    SET INTERFACE    ####
    #############################

    # NOTE: LinkedSets also conform to the standard library's set interface,
    # making them interchangeable with their built-in counterparts.

    def add(self, item: object, left: bool = False) -> None:
        """An alias for :meth:`LinkedSet.append` that is consistent with the
        standard library :class:`set <python:set>` interface.
        """
        # dispatch to append.h
        self.view.add(<PyObject*>item, <bint>left)

    def discard(self, item: object) -> None:
        """Remove an item from the set if it is present.

        Parameters
        ----------
        item : Any
            The item to remove from the set.

        Notes
        -----
        This method is consistent with the standard :class:`set <python:set>`
        interface.  It is functionally equivalent to :meth:`LinkedSet.remove`
        except that it does not raise an error if the item is not contained in
        the set.
        """
        self.view.discard(<PyObject*>item)

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
        # cdef self.view.Node* prev = NULL
        # cdef self.view.Node* curr = self.view.head
        # cdef PyObject* value

        # while curr is not NULL:
        #     value = <object>curr.value
        #     if <object>curr.value not in other:
        #         if prev is NULL:
        #             self.view.head = curr.next
        #         else:
        #             prev.next = curr.next

        #         stack.push(curr)
        #     else:
        #         prev = curr

        #     curr = curr.next

        # return self
        raise NotImplementedError()

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
        # TODO: implement this in C++ under a compare.h header
        # -> this is the opposite of __iand__(), so we can do it in one
        # iteration.
        raise NotImplementedError()

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
        return self.view.contains(<PyObject*>item)

    ####################
    ####    MISC    ####
    ####################

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
    value is both unique and hashable.  This allows it to use a hash table to
    map each value to its corresponding node, which allows for O(1) removals
    and membership checks.

    For an implementation without these constraints, see the base
    :class:`LinkedList`.
    """

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
