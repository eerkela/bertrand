# distutils: language = c++
"""This module contains a pure Cython/C++ implementation of an ordered set that
uses a hash map of linked nodes to support fast lookups by value.
"""
from typing import Iterable, Iterator


cdef class LinkedSet:
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
        cdef LinkedSet result = LinkedSet().__new__(LinkedSet)  # bypass __init__()
        result.view = view
        return result

    ##############################
    ####    LIST INTERFACE    ####
    ##############################

    # NOTE: since LinkedSets are just variations on LinkedLists, they support
    # all of the same operations as normal Python lists.  The only difference
    # is that they require their values to be hashable and unique, and will
    # throw exceptions if this is not the case.

    def append(self, item: object, left: bool = False) -> None:
        """Add an item to the end of the set.

        Parameters
        ----------
        item : Any
            The item to add to the set.
        left : bool, optional
            If ``True``, add the item to the beginning of the set instead of
            the end.  The default is ``False``.

        Notes
        -----
        Appends are O(1) for both ends of the set.
        """
        # dispatch to append.h
        self.view.append(<PyObject*>item, <bint>left)

    def insert(self, index: int, item: object) -> None:
        """Insert an item at the specified index.

        Parameters
        ----------
        index : int
            The index at which to insert the item.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.
        item : Any
            The item to add to the set.

        Notes
        -----
        Inserts are O(n) on average.
        """
        # dispatch to insert.h
        self.view.insert(<PyObject*>index, <PyObject*>item)

    def extend(self, items: Iterable[object], left: bool = False) -> None:
        """Add multiple items to the end of the set.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of items to add to the set.
        left : bool, optional
            If ``True``, add the items to the beginning of the set instead of
            the end.  The default is ``False``.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.

        If ``left`` is ``True``, then this method is consistent with the
        standard library's :class:`collections.deque <python:collections.deque>`
        class.  Just like that class, the series of left appends results in
        reversing the order of elements in ``items``.
        """
        # dispatch to extend.h
        self.view.extend(<PyObject*>items, <bint>left)

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
        # dispatch to index.h
        return self.view.index(<PyObject*>item, start, stop)

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
        Pops are always O(1) if they occur at the head of the set.  Otherwise,
        the behavior depends on whether the set is singly- or doubly-linked.
        For singly-linked sets, popping from the tail is O(n), while for
        doubly-linked sets it is O(1).  This is because of the need to traverse
        the entire set to find the new tail.

        Otherwise, pops are O(n) for nodes in the middle of the set.
        """
        # dispatch to pop.h
        return <object>self.view.pop(index)

    def copy(self) -> LinkedSet:
        """Create a shallow copy of the set.

        Returns
        -------
        LinkedSet
            A new set containing the same items as this one.

        Notes
        -----
        Copying a :class:`LinkedSet` is O(n).
        """
        return LinkedSet.from_view(self.view.copy())

    def clear(self) -> None:
        """Remove all items from the set.

        Notes
        -----
        Clearing a set is O(n).
        """
        self.view.clear()

    def sort(self, *, key: object = None, reverse: bool = False) -> None:
        """Sort the set in-place.

        Parameters
        ----------
        key : Callable[[Any], Any], optional
            A function that takes an item from the set and returns a value to
            use for sorting.  If this is not given, then the items will be
            compared directly.
        reverse : bool, optional
            Indicates whether to sort the set in descending order.  The default
            is ``False``, which sorts in ascending order.

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
        the set will be left in a partially-sorted state.  This is consistent
        with the behavior of Python's built-in :meth:`list.sort() <python:list.sort>`
        method.  However, when a ``key`` function is provided, we actually end
        up sorting an auxiliary set of ``(key, value)`` pairs, which is then
        reflected in the original set.  This means that if a comparison throws
        an exception, the original set will not be changed.  This holds even
        if the ``key`` is a simple identity function (``lambda x: x``), which
        opens up the possibility of anticipating errors and handling them
        gracefully.
        """
        # dispatch to sort.h
        if key is None:
            self.view.sort(<PyObject*>NULL, <bint>reverse)
        else:
            self.view.sort(<PyObject*>key, <bint>reverse)

    def reverse(self) -> None:
        """Reverse the order of the set in-place.

        Notes
        -----
        Reversing a :class:`LinkedSet` is O(n).
        """
        # dispatch to reverse.h
        self.view.reverse()

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
        # dispatch to rotate.h
        self.view.rotate(<ssize_t>steps)

    def __len__(self) -> int:
        """Get the total number of items in the set.

        Returns
        -------
        int
            The number of items in the set.
        """
        return self.view.size()

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
        cdef SingleNode* curr_single
        cdef DoubleNode* curr_double
        cdef PyObject* value

        # doubly-linked
        if self.view.doubly_linked():
            curr_double = self.view.get_head_double()
            while curr_double is not NULL:
                value = curr_double.value
                Py_INCREF(value)
                yield <object>value
                curr_double = curr_double.next

        # singly-linked
        else:
            curr_single = self.view.get_head_single()
            while curr_single is not NULL:
                value = curr_single.value
                Py_INCREF(value)
                yield <object>value
                curr_single = curr_single.next

    def __reversed__(self) -> Iterator[object]:
        """Iterate through the set in reverse order.

        Yields
        ------
        Any
            The next item in the reversed set.

        Notes
        -----
        Iterating through a doubly-linked set in reverse is O(n), while for
        singly-linked sets it is O(2n).  This is because of the need to build a
        temporary stack to store each element, which forces a second iteration.
        """
        cdef SingleNode* curr_single
        cdef DoubleNode* curr_double
        cdef PyObject* value
        cdef stack[SingleNode*] reverse

        # doubly-linked
        if self.view.doubly_linked():
            curr_double = self.view.get_tail_double()
            while curr_double is not NULL:
                value = curr_double.value
                Py_INCREF(value)
                yield <object>value
                curr_double = curr_double.prev

        # singly-linked
        else:
            # build a temporary stack
            curr_single = self.view.get_head_single()
            while curr_single is not NULL:
                reverse.push(curr_single)
                curr_single = curr_single.next

            # yield from the stack to reverse the order of iteration
            while not reverse.empty():
                curr_single = reverse.top()
                reverse.pop()
                Py_INCREF(curr_single.value)
                yield <object>curr_single.value

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
        cdef Py_ssize_t start, stop, step

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size())
            return LinkedSet.from_view(self.view.get_slice(start, stop, step))

        # index directly
        return <object>self.view.get_index(<PyObject*>key)

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
        cdef Py_ssize_t start, stop, step

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size())
            self.view.set_slice(start, stop, step, <PyObject*>value)

        # index directly
        else:
            self.view.set_index(<PyObject*>key, <PyObject*>value)

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
        cdef Py_ssize_t start, stop, step

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size())
            self.view.delete_slice(start, stop, step)

        # index directly
        else:
            self.view.delete_index(<PyObject*>key)

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

    ###################################
    ####    RELATIVE OPERATIONS    ####
    ###################################

    # NOTE: one thing linked sets are especially good at is inserting, removing
    # and reordering items relative to one another.  Since the set has O(1)
    # access to each node, these operations can often be done in constant time
    # anywhere in the set, which is not possible with LinkedLists or standard
    # Python sets.

    def insertafter(self, sentinel: object, item: object, steps: int = 1) -> None:
        """Insert an item after a given sentinel value.

        Parameters
        ----------
        sentinel : Any
            The value to insert the item after.
        item : Any
            The item to add to the set.
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
        # NOTE: call the same C++ method as insertafter, but with a negative
        # offset.
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

    ###################################
    ####    TYPE SPECIALIZATION    ####
    ###################################

    @property
    def specialization(self) -> Any:
        """Return the type specialization that is being enforced by the set.

        Returns
        -------
        Any
            The type specialization of the set, or ``None`` if the set is
            generic.

        See Also
        --------
        LinkedSet.specialize :
            Specialize the set with a particular type.

        Notes
        -----
        This is equivalent to the ``spec`` argument passed to the constructor
        and/or :meth:`specialize() <LinkedSet.specialize>` method.
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
            set.

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
        error will be raised.  The set is not modified during this process.

        .. note::

            Typed sets are slightly slower at appending items due to the extra
            type check.  Otherwise, they have identical performance to their
            untyped equivalents.
        """
        raise NotImplementedError()

    def __class_getitem__(cls, key: Any) -> type:
        """Subscribe a :class:`LinkedSet` to a particular type specialization.

        Parameters
        ----------
        key : Any
            The type to enforce for elements of the set.  This can be in any
            format recognized by :func:`isinstance() <python:isinstance>`,
            including tuples and
            :func:`runtime-checkable <python:typing.runtime_checkable>`
            :class:`typing.Protocol <python:typing.Protocol>` objects.

        See Also
        --------
        LinkedSet.specialize :
            Specialize a set at runtime.

        Returns
        -------
        type
            A variant of the linked set that is permanently specialized to the
            templated type.  Constructing such a set is equivalent to calling
            the constructor with the ``spec`` argument, except that the
            specialization cannot be changed for the lifetime of the object.

        Notes
        -----
        :class:`LinkedSets <LinkedSet>` provide 3 separate mechanisms for
        enforcing type safety:

            #.  The :meth:`specialize() <LinkedSet.specialize>` method, which
                allows runtime specialization of a set with a particular type.
                If the set is not empty when this method is called, it will
                loop through the set and check whether the contents satisfy
                the specialized type, and then enforce that type for any future
                additions to the set.
            #.  The ``spec`` argument to the constructor, which allows
                specialization at the time of set creation.  This is
                equivalent to calling :meth:`specialize() <LinkedSet.specialize>`
                immediately after construction, but avoids an extra loop.
            #.  Direct subscription via the
                :meth:`__class_getitem__() <python:object.__class_getitem__>`
                syntax.  This is equivalent to using the ``spec`` argument to
                create a typed set, except that the specialization is
                permanent and cannot be changed afterwards.  This is the most
                restrictive form of type safety, but also allows users to be
                absolutely sure about the set's contents.

        In any case, a set's specialization can be checked at any time by
        accessing its :attr:`specialization` attribute, which can be used in
        :func:`isinstance() <python:isinstance>` and
        :func:`issubclass() <python:issubclass>` checks directly.

        Examples
        --------
        .. doctest::

            >>> d = DoublyLinkedSet[int]([1, 2, 3])
            TypedSet[<class 'int'>]([1, 2, 3])
            >>> d.specialization
            <class 'int'>
            >>> d.append(4)
            >>> d
            TypedSet[<class 'int'>]([1, 2, 3, 4])
            >>> d.append("foo")
            Traceback (most recent call last):
                ...
            TypeError: 'foo' is not of type <class 'int'>
            >>> d.specialize((int, str))
            Traceback (most recent call last):
                ...
            TypeError: TypedSet is already specialized to <class 'int'>

        Because type specialization is enforced through the
        :func:`isinstance() <python:isinstance>` function, it is possible to
        specialize a set with any type that implements the
        :func:`__instancecheck__() <python:object.__instancecheck__>` special
        method, including :func:`runtime-checkable <python:typing.runtime_checkable>`
        :class:`typing.Protocol <python:typing.Protocol>` objects.

        .. doctest::

            >>> from typing import Iterable

            >>> d = DoublyLinkedSet[Iterable]()
            >>> d.append([1, 2, 3])
            >>> d
            TypedSet[typing.Iterable]([[1, 2, 3]])
            >>> d.append("foo")
            >>> d
            TypedSet[typing.Iterable]([[1, 2, 3], 'foo'])
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
                doubly_linked: bool = False,
                reverse: bool = False,
                max_size: int = -1,
            ) -> None:
                """Disable the `spec` argument for TypedSets."""
                super().__init__(
                    items,
                    doubly_linked=doubly_linked,
                    reverse=reverse,
                    max_size=max_size,
                    spec=key
                )

            def specialize(self, spec: object) -> None:
                """Disable runtime specialization for TypedSets."""
                raise TypeError(f"TypedSet is already specialized to {repr(key)}")

        return TypedSet

    ####################
    ####    MISC    ####
    ####################

    def nbytes(self) -> int:
        """The total memory consumption of the set in bytes.

        Returns
        -------
        int
            The total number of bytes consumed by the set, including all its
            nodes (but not their values), as well as the internal hash table.
        """
        return self.view.nbytes()

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
