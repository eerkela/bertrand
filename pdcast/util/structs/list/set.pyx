# distutils: language = c++
"""This module contains a pure Cython/C++ implementation of an ordered set that
uses a hash map of linked nodes to support fast lookups by value.
"""
from typing import Iterable, Iterator


# TODO: inheritance approach is good.  Just add a `(consider using LinkedSet.add()
# to avoid errors)` note to the error message.


# TODO: relative operations can be hidden behind a proxy object that handles the
# sentinel lookup + offset calculation.  This avoids cluttering the base class's
# interface and centralizes some of the concerns.  We would then invoke these
# methods as follows:

# l = LinkedSet("abc")
# l.relative("b").insert("x")  # insert "x" after "b"
# l.relative("c", -1).insert("y")  # insert "y" before "c"
# l == LinkedSet("abxyc")

# This necessitates the creation of a RelativeSet proxy in C++



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
        cdef PyObject* c_spec

        # make specialization C-compatible
        if spec is None:
            c_spec = NULL
        else:
            c_spec = <PyObject*>spec  # borrowed reference

        # init empty
        if items is None:
            self.view = new VariantSet(doubly_linked, max_size, c_spec)

        # unpack iterable
        else:
            self.view = new VariantSet(
                <PyObject*>items, doubly_linked, reverse, max_size, c_spec
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
    # either silently ignore or throw exceptions if this is not the case.

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
        return (<VariantSet*>self.view).count(<PyObject*>item, start, stop)

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
        (<VariantSet*>self.view).remove(<PyObject*>item)

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

    #############################
    ####    SET INTERFACE    ####
    #############################

    # NOTE: LinkedSets also conform to the standard library's set interface,
    # making them interchangeable with their built-in counterparts.

    def add(self, item: object, left: bool = False) -> None:
        """An alias for :meth:`LinkedSet.append` that is consistent with the
        standard library :class:`set <python:set>` interface.
        """
        # dispatch to add.h
        (<VariantSet*>self.view).add(<PyObject*>item, <bint>left)

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
        # dispatch to discard.h
        (<VariantSet*>self.view).discard(<PyObject*>item)

    def union(self, *others: Iterable[object], left: bool = False) -> LinkedSet:
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
        cdef VariantSet* result = <VariantSet*>self.view.copy()

        for items in others:
            result.update(<PyObject*>items, <bint>left)

        return LinkedSet.from_view(result)

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
        cdef VariantSet* result = <VariantSet*>self.view.copy()

        for items in others:
            result.intersection_update(<PyObject*>items)

        return LinkedSet.from_view(result)

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
        cdef VariantSet* result = <VariantSet*>self.view.copy()

        for items in others:
            result.difference_update(<PyObject*>items)

        return LinkedSet.from_view(result)

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
        return LinkedSet.from_view(
            (<VariantSet*>self.view).symmetric_difference(<PyObject*>other)
        )

    def update(self, *others: Iterable[object], left: bool = False) -> None:
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
            (<VariantSet*>self.view).update(<PyObject*>items, <bint>left)

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
            (<VariantSet*>self.view).intersection_update(<PyObject*>other)

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
            (<VariantSet*>self.view).difference_update(<PyObject*>other)

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
        (<VariantSet*>self.view).symmetric_difference_update(<PyObject*>other)

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
        return (<VariantSet*>self.view).isdisjoint(<PyObject*>other)

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
        return (<VariantSet*>self.view).issubset(<PyObject*>other, <bint>False)

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
        return (<VariantSet*>self.view).issuperset(<PyObject*>other, <bint>False)

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

        return LinkedSet.from_view(
            (<VariantSet*>self.view).union_(<PyObject*>other, <bint>False)
        )

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

        (<VariantSet*>self.view).update(<PyObject*>other, <bint>False)
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

        return LinkedSet.from_view(
            (<VariantSet*>self.view).intersection(<PyObject*>other)
        )

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
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        (<VariantSet*>self.view).intersection_update(<PyObject*>other)
        return self

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

        return LinkedSet.from_view(
            (<VariantSet*>self.view).difference(<PyObject*>other)
        )

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
        if not isinstance(other, (set, LinkedSet)):
            return NotImplemented

        (<VariantSet*>self.view).difference_update(<PyObject*>other)
        return self

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

        return LinkedSet.from_view(
            (<VariantSet*>self.view).symmetric_difference(<PyObject*>other)
        )

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

        (<VariantSet*>self.view).symmetric_difference_update(<PyObject*>other)
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

        # setting `strict` to `True` makes this a proper subset check
        return bool((<VariantSet*>self.view).issubset(<PyObject*>other, <bint>True))

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

        # setting `strict` to `False` allows for equality between sets
        return bool((<VariantSet*>self.view).issubset(<PyObject*>other, <bint>False))

    # TODO: implement equals on the View itself.

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

        # setting `strict` to `False` allows for equality between sets
        return bool((<VariantSet*>self.view).issuperset(<PyObject*>other, <bint>False))

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

        # setting `strict` to `True` makes this a proper superset check
        return bool((<VariantSet*>self.view).issuperset(<PyObject*>other, <bint>True))

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
        return (<VariantSet*>self.view).contains(<PyObject*>item)

    ###################################
    ####    RELATIVE OPERATIONS    ####
    ###################################

    # NOTE: one thing linked sets are especially good at is inserting, removing
    # and reordering items relative to one another.  Since the set has O(1)
    # access to each node, these operations can be done in constant time
    # anywhere in the set, which is not possible with LinkedLists or unordered
    # Python sets.

    def distance(self, item1: object, item2: object) -> int:
        """Get the linear distance between two items in the set.

        Parameters
        ----------
        item1 : Any
            The first item to measure.
        item2 : Any
            The second item to measure.

        Returns
        -------
        int
            The difference between the indices of the two items.  Positive values
            indicate that ``item1`` is to the left of ``item2``, while negative
            values indicate the opposite.  If the items are the same, this will
            be 0.

        Raises
        ------
        KeyError
            If either item is not contained in the set.

        Notes
        -----
        Calculating the distance between two items is O(n) on average.

        This method is equivalent to ``self.index(item2) - self.index(item1)``,
        except that it gathers both indices in a single iteration.
        """
        # dispatch to index.h
        return (<VariantSet*>self.view).distance(<PyObject*>item1, <PyObject*>item2)

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
        Swaps are O(1) if the set is doubly-linked, and O(n) otherwise.
        """
        # dispatch to move.h
        (<VariantSet*>self.view).swap(<PyObject*>item1, <PyObject*>item2)

    def move(self, item: object, steps: int = 1) -> None:
        """Move an item within the set by the specified number of steps.

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
        # dispatch to move.h
        (<VariantSet*>self.view).move(<PyObject*>item, <Py_ssize_t>steps)

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
        # dispatch to move.h
        (<VariantSet*>self.view).move_to_index(<PyObject*>item, <Py_ssize_t>index)

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
        # dispatch to move.h
        (<VariantSet*>self.view).move_relative(
            <PyObject*>item,
            <PyObject*>sentinel,
            <Py_ssize_t>offset
        )

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
        (<VariantSet*>self.view).insert_relative(
            <PyObject*>sentinel,
            <PyObject*>item,
            <ssize_t>offset
        )

    def extend_relative(
        self,
        items: Iterable[object],
        sentinel: object,
        offset: int = 1,
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
        (<VariantSet*>self.view).extend_relative(
            <PyObject*>sentinel,
            <PyObject*>items,
            <Py_ssize_t>offset,
            <bint>reverse
        )

    def get_relative(self, sentinel: object, offset: int = 1) -> object:
        raise NotImplementedError()

    def remove_relative(self, sentinel: object, offset: int = 1) -> None:
        """Remove an item from the set relative to a given sentinel value.

        Parameters
        ----------
        sentinel : Any
            The value to remove relative to.
        offset : int, optional
            An offset from the sentinel value.  If this is positive, then this
            method will count to the right by the specified number of spaces
            from the sentinel and remove the item at that index.  Negative
            values count to the left instead.  The default is ``1``.

        Raises
        ------
        KeyError
            If the sentinel is not contained in the set.

        Notes
        -----
        """
        # dispatch to remove.h
        (<VariantSet*>self.view).remove_relative(
            <PyObject*>sentinel, <Py_ssize_t>offset
        )

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
        # dispatch to discard.h
        (<VariantSet*>self.view).discard_relative(
            <PyObject*>sentinel, <Py_ssize_t>offset
        )

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
        # dispatch to pop.h
        return <object>(<VariantSet*>self.view).pop_relative(
            <PyObject*>sentinel, <Py_ssize_t>offset
        )

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
        # dispatch to clear.h
        (<VariantSet*>self.view).clear_relative(
            <PyObject*>sentinel,
            <Py_ssize_t>offset,
            <Py_ssize_t>length
        )

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
        specialization = self.specialization
        if specialization is not None:
            prefix += f"[{', '.join(repr(s) for s in specialization)}]"

        # abbreviate in order to avoid spamming the console
        if len(self) > 64:
            contents = ", ".join(repr(item) for item in self[:32])
            contents += ", ..., "
            contents += ", ".join(repr(item) for item in self[-32:])
        else:
            contents = ", ".join(repr(item) for item in self)

        return f"{prefix}({{{contents}}})"
