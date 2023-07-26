"""This module contains pure C structs, constants, and type definitions for use
in linked list-based data structures.
"""

#########################
####    CONSTANTS    ####
#########################


# DEBUG = TRUE adds print statements for memory allocation/deallocation to help
# identify memory leaks.
cdef const bint DEBUG = False


#######################
####    CLASSES    ####
#######################


# TODO: implement class_getitem for mypy hints, just like list[].
# -> in the case of HashedList, this could check if the contained type is
# a subclass of Hashable, and if not, raise an error.


cdef class LinkedList:
    """Base class for all linked list data structures.

    Each linked list is a drop-in replacement for a standard Python
    :class:`list <python:list>` or :class:`deque <python:collections.deque>`,
    and is implemented in pure Cython to maximize performance.

    Parameters
    ----------
    items : Iterable[Any], optional
        An iterable of items to initialize the list.

    Attributes
    ----------
    head : node
        A reference to the first node in the list.  This is a C struct and is
        not normally accessible from Python.
    tail : node
        The last node in the list.  This is a C struct and is not normally
        accessible from Python.

    Notes
    -----
    This is an abstract class that defines the Cython interface for all its
    subclasses.  It is not intended to be instantiated directly, and none of
    its attributes are visible to Python code (besides magic methods).
    """

    def __init__(self, items: Iterable[Any] | None = None):
        cdef PyObject* other_list

        if items is not None:
            other_list = <PyObject*>items
            self._extend(other_list)

    def __cinit__(self):
        self.size = 0

    ########################
    ####    ABSTRACT    ####
    ########################

    # TODO: distribute comments to subclasses

    cdef void _append(self, PyObject* item):
        """Add an item to the end of the list.

        Parameters
        ----------
        item : PyObject*
            A borrowed reference to the item to add to the list.
        """
        raise NotImplementedError()

    cdef void _appendleft(self, PyObject* item):
        """Add an item to the beginning of the list.

        Parameters
        ----------
        item : PyObject*
            A borrowed reference to the item to add to the list.
        """
        raise NotImplementedError()

    cdef void _insert(self, PyObject* item, long index):
        """Insert an item at the specified index.

        Parameters
        ----------
        item : PyObject*
            A borrowed reference to the item to add to the list.
        index : long int
            The index at which to insert the item.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.

        Raises
        ------
        IndexError
            If the index is out of bounds.
        """
        raise NotImplementedError()

    cdef void _extend(self, PyObject* items):
        """Add a sequence of items to the end of the list.

        Parameters
        ----------
        items : PyObject*
            A borrowed reference to a sequence of items to add to the list.
        """
        raise NotImplementedError()

    cdef void _extendleft(self, PyObject* items):
        """Add a sequence of items to the beginning of the list.

        Parameters
        ----------
        items : PyObject*
            A borrowed reference to a sequence of items to add to the list.
        """
        raise NotImplementedError()

    cdef size_t _index(self, PyObject* item, long start = 0, long stop = -1):
        """Get the index of an item within the list.

        Parameters
        ----------
        item : PyObject*
            A borrowed reference to the item to search for.
        start : long int, optional
            The index at which to begin searching.  If this is negative, it
            will be translated to a positive index by counting backwards from
            the end of the list.  The default is ``0``, which references the
            start of the list.
        stop : long int, optional
            The index at which to stop searching.  If this is negative, it will
            be translated to a positive index by counting backwards from the
            end of the list.  The default is ``-1``, which references the end
            of the list.

        Returns
        -------
        size_t
            The index of the item within the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.
        """
        raise NotImplementedError()

    cdef size_t _count(self, PyObject* item):
        """Count the number of occurrences of an item in the list.

        Parameters
        ----------
        item : PyObject*
            A borrowed reference to the item to count.

        Returns
        -------
        size_t
            The number of occurrences of the item in the list.
        """
        raise NotImplementedError()

    cdef void _remove(self, PyObject* item):
        """Remove an item from the list.

        Parameters
        ----------
        item : PyObject*
            A borrowed reference to the item to remove from the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.
        """
        raise NotImplementedError()

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
            A borrowed reference to the item that was removed.

        Raises
        ------
        IndexError
            If the index is out of bounds.
        """
        raise NotImplementedError()

    cdef PyObject* _popleft(self):
        """Remove and return the first item in the list.

        Returns
        -------
        PyObject*
            A borrowed reference to the item that was removed.

        Raises
        ------
        IndexError
            If the list is empty.
        """
        raise NotImplementedError()

    cdef PyObject* _popright(self):
        """Remove and return the last item in the list.

        Returns
        -------
        PyObject*
            A borrowed reference to the item that was removed.

        Raises
        ------
        IndexError
            If the list is empty.
        """
        raise NotImplementedError()

    cdef void _clear(self):
        """Remove all items from the list in-place."""
        raise NotImplementedError()

    cdef void _sort(self, PyObject* key = NULL, bint reverse = False):
        """Sort the list in-place.

        Parameters
        ----------
        key : Callable[[Any], Any], optional
            A function that takes an item from the list and returns a value to
            use for sorting.  If this is ``None``, then the items will be
            compared directly.  The default is ``None``.
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
        with the behavior of Python's built-in
        :meth:`list.sort() <python:list.sort>` method.  However, when a ``key``
        function is provided, we actually end up sorting an auxiliary list of
        ``(key, value)`` pairs, which is then reflected in the original list.
        This means that if a comparison throws an exception, the original list
        will not be changed.  This holds even if the ``key`` is a simple
        identity function (``lambda x: x``), which opens up the possibility of
        anticipating errors and handling them gracefully.
        """
        raise NotImplementedError()

    cdef void _reverse(self):
        """Reverse the order of the list in-place."""
        raise NotImplementedError()

    cdef size_t _nbytes(self):
        """Get the total number of bytes used by the list."""
        raise NotImplementedError()

    def __iter__(self) -> Iterator[Any]:
        """Iterate through the list items in order.

        Yields
        ------
        Any
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedList` is O(n).
        """
        raise NotImplementedError()

    def __reversed__(self) -> Iterator[Any]:
        """Iterate through the list in reverse order.

        Yields
        ------
        Any
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedList` is O(n).
        """
        raise NotImplementedError()

    def __getitem__(self, key: int | slice) -> Any:
        """Index the list for a particular item or slice.

        Parameters
        ----------
        key : int or slice
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
        raise NotImplementedError()

    def __setitem__(self, key: int | slice, value: object) -> None:
        """Set the value of an item or slice in the list.

        Parameters
        ----------
        key : int or slice
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
        ValueError
            If the length of ``value`` does not match the length of the slice.

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
        raise NotImplementedError()

    def __delitem__(self, key: int | slice) -> None:
        """Delete an item or slice from the list.

        Parameters
        ----------
        key : int or slice
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
        raise NotImplementedError()

    def __contains__(self, item: object) -> bool:
        """Check if the item is contained in the list.

        Parameters
        ----------
        item : Any
            The item to search for.

        Returns
        -------
        bool
            Indicates whether the item is contained in the list.

        Notes
        -----
        Membership checks are O(n) on average.
        """
        raise NotImplementedError()

    #########################
    ####    INHERITED    ####
    #########################

    cdef LinkedList _copy(self):
        """Create a shallow copy of the list.

        Returns
        -------
        LinkedList
            A new list containing the same items as this one.

        Notes
        -----
        Copying a :class:`LinkedList` is O(n).
        """
        return type(self)(self)

    cdef void _rotate(self, ssize_t steps = 1):
        """Rotate the list to the right by the specified number of steps.

        Parameters
        ----------
        steps : ssize_t, optional
            The number of steps to rotate the list.  If this is positive, the
            list will be rotated to the right.  If this is negative, the list
            will be rotated to the left.  The default is ``1``.

        Notes
        -----
        Rotations are O(steps).

        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.
        """
        cdef bint shift_right = steps > 0
        cdef ssize_t i

        # avoid inconsistencies related to sign
        steps = abs(steps)

        # rotate right
        if shift_right:
            for i in range(steps):
                self._appendleft(self._popright())

        # rotate left
        else:
            for i in range(steps):
                self._append(self._popleft())

    cdef size_t _normalize_index(self, long index):
        """Allow negative indexing and check if the result is within bounds.

        Parameters
        ----------
        index : long int
            The index to normalize.  If this is negative, it will be translated
            to a positive index by counting backwards from the end of the list.

        Returns
        -------
        size_t
            The normalized index.

        Raises
        ------
        IndexError
            If the index is out of bounds.
        """
        # allow negative indexing
        if index < 0:
            index += self.size

        cdef long size = <long>self.size

        # check bounds
        if not 0 <= index < size:
            raise IndexError("list index out of range")

        return index

    def __add__(self, other: Iterable[Any]) -> "LinkedList":
        """Concatenate two lists.

        Parameters
        ----------
        other : Iterable[Any]
            The list to concatenate with this one.

        Returns
        -------
        LinkedList
            A new list containing the items from both lists.

        Notes
        -----
        Concatenation is O(n), where `n` is the length of the other list.
        """
        cdef LinkedList result = self._copy()
        cdef PyObject* other_list = <PyObject*>other

        result._extend(other_list)
        return result

    def __iadd__(self, other: Iterable[Any]) -> "LinkedList":
        """Concatenate two lists in-place.

        Parameters
        ----------
        other : Iterable[Any]
            The list to concatenate with this one.

        Returns
        -------
        LinkedList
            This list, with the items from the other list appended.

        Notes
        -----
        Concatenation is O(m), where `m` is the length of the ``other`` list.
        """
        cdef PyObject* other_list = <PyObject*>other

        self._extend(other_list)
        return self

    def __mul__(self, repeat: int) -> "LinkedList":
        """Repeat the list a specified number of times.

        Parameters
        ----------
        repeat : int
            The number of times to repeat the list.

        Returns
        -------
        LinkedList
            A new list containing successive copies of this list, repeated
            the given number of times.

        Notes
        -----
        Repetition is O(n * repeat).
        """
        cdef LinkedList result = self._copy()
        cdef LinkedList temp
        cdef size_t i

        for i in range(<size_t>repeat):
            temp = self._copy()
            result._extend(<PyObject*>temp)
        return result

    def __imul__(self, repeat: int) -> "LinkedList":
        """Repeat the list a specified number of times in-place.

        Parameters
        ----------
        repeat : int
            The number of times to repeat the list.

        Returns
        -------
        LinkedList
            This list, repeated the given number of times.

        Notes
        -----
        Repetition is O(n * repeat).
        """
        cdef LinkedList original = self._copy()
        cdef size_t i

        for i in range(<size_t>repeat):
            self._extend(<PyObject*>original)
        return self

    def __lt__(self, other: object) -> bool:
        """Check if this list is lexographically less than another list.

        Parameters
        ----------
        other : Any
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

        # compare elements in order
        for a, b in zip(self, other):
            if a < b:
                return True
            elif a > b:
                return False

        # if all elements are equal, the shorter list is smaller
        return self.size < other.size

    def __le__(self, other: object) -> bool:
        """Check if this list is lexographically less than or equal to another
        list.

        Parameters
        ----------
        other : Any
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

        # compare elements in order
        for a, b in zip(self, other):
            if a < b:
                return True
            elif a > b:
                return False

        # if all elements are equal, the shorter list is smaller
        return self.size <= other.size

    def __eq__(self, other: object) -> bool:
        """Compare two lists for equality.

        Parameters
        ----------
        other : Any
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

        if self.size != <size_t>len(other):
            return False

        return all(a == b for a, b in zip(self, other))

    def __gt__(self, other: object) -> bool:
        """Check if this list is lexographically greater than another list.

        Parameters
        ----------
        other : Any
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

        # compare elements in order
        for a, b in zip(self, other):
            if a > b:
                return True
            elif a < b:
                return False

        # if all elements are equal, the longer list is greater
        return self.size > other.size

    def __ge__(self, other: object) -> bool:
        """Check if this list is lexographically greater than or equal to
        another list.

        Parameters
        ----------
        other : Any
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

        # compare elements in order
        for a, b in zip(self, other):
            if a > b:
                return True
            elif a < b:
                return False

        # if all elements are equal, the longer list is greater
        return self.size >= other.size

    def __len__(self) -> int:
        """Get the total number of items in the list.

        Returns
        -------
        int
            The number of items in the list.
        """
        return self.size

    def __bool__(self) -> bool:
        """Treat empty lists as Falsy in boolean logic.

        Returns
        -------
        bool
            Indicates whether the list is empty.
        """
        return bool(self.size)

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
        return f"[{', '.join(str(item) for item in self)}]"

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
        return f"{type(self).__name__}([{', '.join(repr(item) for item in self)}])"

    ###############################
    ####    PYTHON WRAPPERS    ####
    ###############################

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
        self._append(<PyObject*>item)

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
        self._appendleft(<PyObject*>item)

    def insert(self, item: object, index: int) -> None:
        """Insert an item at the specified index.

        Parameters
        ----------
        item : Any
            The item to add to the list.
        index : int
            The index at which to insert the item.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        Notes
        -----
        Inserts are O(n) on average.
        """
        self._insert(<PyObject*>item, index)

    def extend(self, items: Iterable[object]) -> None:
        """Add multiple items to the end of the list.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of hashable items to add to the list.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.
        """
        self._extend(<PyObject*>items)

    def extendleft(self, items: Iterable[object]) -> None:
        """Add multiple items to the beginning of the list.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of hashable items to add to the list.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.

        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.  Just like
        that class, the series of left appends results in reversing the order
        of elements in ``items``.
        """
        self._extendleft(<PyObject*>items)

    def index(self, item: object, start: int = 0, stop: int = -1) -> int:
        """Get the index of an item within the list.

        Parameters
        ----------
        item : Any
            The item to search for.

        Returns
        -------
        int
            The index of the item within the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.

        Notes
        -----
        Indexing is O(n) on average.
        """
        return self._index(<PyObject*>item, start, stop)

    def count(self, item: object) -> int:
        """Count the number of occurrences of an item in the list.

        Parameters
        ----------
        item : Any
            The item to count.

        Returns
        -------
        int
            The number of occurrences of the item in the list.

        Notes
        -----
        Counting is O(n).
        """
        return self._count(<PyObject*>item)

    def remove(self, item: object) -> None:
        """Remove an item from the list.

        Parameters
        ----------
        item : Any
            The item to remove from the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.

        Notes
        -----
        Removals are O(n) on average.
        """
        self._remove(<PyObject*>item)

    def pop(self, index: int = -1) -> object:
        """Remove and return the item at the specified index.

        Parameters
        ----------
        index : int, optional
            The index of the item to remove.  If this is negative, it will be
            translated to a positive index by counting backwards from the end
            of the list.  The default is ``-1``, which removes the last item.

        Returns
        -------
        Any
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
        return <object>self._pop(index)  # this returns ownership to Python

    def popleft(self) -> object:
        """Remove and return the first item in the list.

        Returns
        -------
        Any
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
        return <object>self._popleft()  # this returns ownership to Python

    def popright(self) -> object:
        """Remove and return the last item in the list.

        Returns
        -------
        Any
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
        return <object>self._popright()  # this returns ownership to Python

    def clear(self) -> None:
        """Remove all items from the list.

        Notes
        -----
        Clearing a list is O(1).
        
        Due to the way Python's garbage collector works, we don't actually need
        to iterate over the list to free it.  The gc can automatically detect
        reference cycles and free them if the referenced objects cannot be
        reached from anywhere else in the program.
        """
        self._clear()

    def sort(self, *, key: object = None, reverse: bool = False) -> None:
        """Sort the list in-place.

        Parameters
        ----------
        key : Callable[[Any], Any], optional
            A function that takes an item from the list and returns a value to
            use for sorting.  If this is ``None``, then the items will be
            compared directly.  The default is ``None``.
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

        One quirk of this implementation is in how it handles errors.  By
        default, if a comparison throws an exception, then the sort will be
        aborted and the list will be left in a partially-sorted state.  This is
        consistent with the behavior of Python's built-in
        :meth:`list.sort() <python:list.sort>` method.  However, when a ``key``
        function is provided, we actually end up sorting an auxiliary list of
        ``(key, value)`` pairs, which is then reflected in the original list.
        This means that if a comparison throws an exception, the original list
        will not be changed.  This is true even if the ``key`` is a simple
        identity function (``lambda x: x``), which opens up the possibility of
        anticipating errors and handling them gracefully.
        """
        if key is None:
            self._sort(NULL, reverse)
        else:
            self._sort(<PyObject*>key, reverse)

    def reverse(self) -> None:
        """Reverse the order of the list in-place.

        Notes
        -----
        Reversing a :class:`LinkedList` is O(n).
        """
        self._reverse()


    def copy(self) -> LinkedList:
        """Create a shallow copy of the list.

        Returns
        -------
        LinkedList
            A new list containing the same items as this one.

        Notes
        -----
        Copying a :class:`LinkedList` is O(n).
        """
        return self._copy()

    def rotate(self, steps: int = 1) -> None:
        """Rotate the list to the right by the specified number of steps.

        Parameters
        ----------
        steps : int, optional
            The number of steps to rotate the list.  If this is positive, the
            list will be rotated to the right.  If this is negative, the list
            will be rotated to the left.  The default is ``1``.

        Notes
        -----
        Rotations are O(steps).

        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.
        """
        self._rotate(steps)

    @property
    def nbytes(self) -> int:
        """The total memory consumption of the list in bytes.

        Returns
        -------
        int
            The total number of bytes consumed by the list, including all its
            nodes (but not their values).
        """
        return self._nbytes()


####################
####    MISC    ####
####################


cdef void raise_exception() except *:
    """If the python interpreter is currently storing an exception, raise it.

    Notes
    -----
    Interacting with the Python C API can sometimes result in errors that are
    encoded in the function's output and checked by the `PyErr_Occurred()`
    interpreter flag.  This function can be called whenever this occurs in
    order to force that error to be raised as normal.
    """
    # Since we're using the except * Cython syntax, the error handler will be
    # invoked every time this function is called.  This means we don't have to
    # do anything here, just return void and let the built-in machinery do
    # all the work
    return
