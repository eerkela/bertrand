# distutils: language = c++
"""This module contains a pure Cython/C++ implementation of both singly-linked
and doubly-linked list data structures.
"""
from typing import Iterable, Iterator


# TODO: list.pyx, set.pyx, and dict.pyx should be lifted to structs/, alongside
# structs/linked_list/


####################
####    BASE    ####
####################


# TODO: implement class_getitem for mypy hints, just like list[].
# -> in the case of HashedList, this could check if the contained type is
# a subclass of Hashable, and if not, raise an error.


cdef class LinkedList:
    """Base class for all linked list data structures.

    Each linked list is a drop-in replacement for a built-in Python data
    structure, and can be used interchangeably in most cases.  The lists
    themselves are implemented in pure C++ and are optimized for performance
    wherever possible.
    """

    __hash__ = None  # mutable containers are not hashable

    def __bool__(self) -> bool:
        """Treat empty lists as Falsy in boolean logic.

        Returns
        -------
        bool
            Indicates whether the list is empty.
        """
        return bool(len(self))

    def __str__(self):
        """Return a standard string representation of the list.

        Returns
        -------
        str
            A string representation of the list.

        Notes
        -----
        Creating a string representation of a list is O(n).
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
        Creating a string representation of a list is O(n).
        """
        return f"{type(self).__name__}([{', '.join(repr(item) for item in self)}])"


#############################
####    SINGLY-LINKED    ####
#############################


# TODO: SinglyLinkedList inherits:
# append
# appendleft
# contains
# extend
# extendleft
# popleft
# remove
# sort
# __len__
# __iter__
# math
# lexicographic comparisons

# These are not overridden in DoublyLinkedList


#############################
####    DOUBLY-LINKED    ####
#############################


# TODO: rotate() is broken
# TODO: __getitem__() sometimes throws unexpected ValueErrors and seems to have
# a problem with properly reversing slices.  It also occasionally causes a
# segfault.


# TODO: in order to rigorously test these, we should copy over the tests from
# the standard library.
# https://github.com/python/cpython/blob/3.11/Lib/test/list_tests.py


cdef class DoublyLinkedList(LinkedList):
    """A pure Cython implementation of a doubly-linked list data structure.

    This is a drop-in replacement for a standard Python
    :class:`list <python:list>` or :class:`deque <python:collections.deque>`
    object, with generally comparable performance.

    Parameters
    ----------
    items : Iterable[Any], optional
        An iterable of items to initialize the list.

    Attributes
    ----------
    view : ListView[DoubleNode]*
        The underlying view of the list.  This is a pointer to a C++ object
        that tracks the list's head and tail pointers and manages memory for
        each of its nodes.  It is not intended to be accessed by the user, and
        manipulating it directly it can result in memory leaks and/or undefined
        behavior.  Thorough inspection of the C++ header files is recommended
        before attempting to use this attribute.

    Notes
    -----
    This data structure behaves similarly to a
    :class:`collections.deque <python:collections.deque>` object, but is
    implemented as a standard doubly-linked list instead of a list of arrays.

    The list is implemented in pure C++ to maximize performance, which is
    generally on par with the standard library alternatives.  This
    implementation retains all the usual tradeoffs of linked lists vs arrays
    (e.g. O(n) indexing vs O(1) appends), but attempts to minimize compromises
    as much as possible.

    .. warning::

        The list is not thread-safe.  If you need to use it in a multithreaded
        context, you should use a :class:`threading.Lock` to synchronize
        access.
    """

    def __init__(
        self,
        items: Iterable[object] | None = None,
        reverse: bool = False,
    ) -> None:
        """Initialize the list.

        Parameters
        ----------
        items : Iterable[Any], optional
            An iterable of items to initialize the list.
        reverse : bool, optional
            If ``True``, reverse the order of `items` during list construction.
            The default is ``False``.
        """
        cdef PyObject* borrowed  # Cython requires PyObject* to be forward declared

        if items is None:
            self.view = new ListView[DoubleNode]()
        else:
            borrowed = <PyObject*>items
            self.view = new ListView[DoubleNode](borrowed, reverse)

    def __dealloc__(self):
        del self.view

    ########################
    ####    CONCRETE    ####
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
        append(self.view, <PyObject*>item)  # from append.h

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
        appendleft(self.view, <PyObject*>item)  # from append.h

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
        cdef size_t norm_index = normalize_index(<PyObject*>index, self.view.size, True)

        insert(self.view, norm_index, <PyObject*>item)  # from insert.h

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
        extend(self.view, <PyObject*>items)  # from extend.h

    def extendleft(self, items: Iterable[object]) -> None:
        """Add multiple items to the beginning of the list.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of items to add to the list.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.

        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.  Just like
        that class, the series of left appends results in reversing the order
        of elements in ``items``.
        """
        extendleft(self.view, <PyObject*>items)  # from extend.h

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
        # allow Python-style negative indexing + bounds checking
        cdef size_t norm_start = normalize_index(<PyObject*>start, self.view.size, True)
        cdef size_t norm_stop = normalize_index(<PyObject*>stop, self.view.size, True)

        # check that start and stop indices are consistent
        if norm_start > norm_stop:
            raise ValueError("start index must be less than or equal to stop index")

        # delegate to index.h
        return index(self.view, <PyObject*>item, norm_start, norm_stop)

    def count(self, item: object, start: int = 0, stop: int = -1) -> int:
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
        # allow Python-style negative indexing + bounds checking
        cdef size_t norm_start = normalize_index(<PyObject*>start, self.view.size, True)
        cdef size_t norm_stop = normalize_index(<PyObject*>stop, self.view.size, True)

        # check that start and stop indices are consistent
        if norm_start > norm_stop:
            raise ValueError("start index must be less than or equal to stop index")

        # delegate to count.h
        return count(self.view, <PyObject*>item, norm_start, norm_stop)

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
        remove(self.view, <PyObject*>item)  # from remove.h

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
        cdef size_t norm_index = normalize_index(<PyObject*>index, self.view.size, True)

        return <object>pop(self.view, norm_index)  # from pop.h

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
        This is equivalent to :meth:`DoublyLinkedList.pop` with ``index=0``, but it
        avoids the overhead of handling indices and is thus more efficient in
        the specific case of removing the first item.
        """
        return <object>popleft(self.view)  # from pop.h

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
        This is equivalent to :meth:`DoublyLinkedList.pop` with ``index=-1``, but it
        avoids the overhead of handling indices and is thus more efficient in
        the specific case of removing the last item.
        """
        return <object>popright(self.view)  # from pop.h

    def copy(self) -> DoublyLinkedList:
        """Create a shallow copy of the list.

        Returns
        -------
        DoublyLinkedList
            A new list containing the same items as this one.

        Notes
        -----
        Copying a :class:`DoublyLinkedList` is O(n).
        """
        cdef DoublyLinkedList result = DoublyLinkedList.__new__(DoublyLinkedList)
        result.view = self.view.copy()  # from view.h
        return result

    def clear(self) -> None:
        """Remove all items from the list.

        Notes
        -----
        Clearing a list is O(n).
        """
        self.view.clear()  # from view.h

    def sort(self, *, key: object = None, reverse: bool = False) -> None:
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
        if key is None:
            sort(self.view, <PyObject*>NULL, <bint>reverse)  # from sort.h
        else:
            sort(self.view, <PyObject*>key, <bint>reverse)  # from sort.h

    def reverse(self) -> None:
        """Reverse the order of the list in-place.

        Notes
        -----
        Reversing a :class:`DoublyLinkedList` is O(n).
        """
        reverse(self.view)  # from reverse.h

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
        rotate(self.view, <Py_ssize_t>steps)  # from rotate.h

    def nbytes(self) -> int:
        """The total memory consumption of the list in bytes.

        Returns
        -------
        int
            The total number of bytes consumed by the list, including all its
            nodes (but not their values).
        """
        import sys

        cdef size_t total = self.view.nbytes()  # from view.h
        total += sys.getsizeof(self)  # add size of Python wrapper
        return total

    def __len__(self) -> int:
        """Get the total number of items in the list.

        Returns
        -------
        int
            The number of items in the list.
        """
        return self.view.size

    def __iter__(self) -> Iterator[object]:
        """Iterate through the list items in order.

        Yields
        ------
        Any
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`DoublyLinkedList` is O(n) on average.
        """
        cdef DoubleNode* curr = self.view.head

        while curr is not NULL:
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python
            curr = <DoubleNode*>curr.next

    def __reversed__(self) -> Iterator[object]:
        """Iterate through the list in reverse order.

        Yields
        ------
        Any
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`DoublyLinkedList` is O(n) on average.
        """
        cdef DoubleNode* curr = self.view.tail

        while curr is not NULL:
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python
            curr = <DoubleNode*>curr.prev

    def __getitem__(self, key: int | slice) -> object | DoublyLinkedList:
        """Index the list for a particular item or slice.

        Parameters
        ----------
        key : int or slice
            The index or slice to retrieve from the list.  If this is a slice,
            the result will be a new :class:`DoublyLinkedList` containing the
            specified items.  This can be negative, following the same
            convention as Python's standard :class:`list <python:list>`.

        Returns
        -------
        scalar or DoublyLinkedList
            The item or list of items corresponding to the specified index or
            slice.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        See Also
        --------
        DoublyLinkedList.__setitem__ :
            Set the value of an item or slice in the list.
        DoublyLinkedList.__delitem__ :
            Delete an item or slice from the list.

        Notes
        -----
        Integer-based indexing is O(n) on average.

        Slicing is optimized to always begin iterating from the end nearest to
        a slice boundary, and to never backtrack.  It collects all values in a
        single iteration and stops as soon as the slice is complete.
        """
        cdef DoublyLinkedList result
        cdef Py_ssize_t start, stop, step
        cdef size_t index
        cdef PyObject* new_ref

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size)
            result = DoublyLinkedList.__new__(DoublyLinkedList)
            result.view = get_slice_double(self.view, start, stop, step)
            return result

        # index directly
        index = normalize_index(<PyObject*>key, self.view.size, False)
        new_ref = get_index_double(self.view, index)  # from index.h
        return <object>new_ref

    def __setitem__(self, key: int | slice, value: object | Iterable[object]) -> None:
        """Set the value of an item or slice in the list.

        Parameters
        ----------
        key : int or slice
            The index or slice to set in the list.  This can be negative,
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
        DoublyLinkedList.__getitem__ :
            Index the list for a particular item or slice.
        DoublyLinkedList.__delitem__ :
            Delete an item or slice from the list.

        Notes
        -----
        Integer-based assignment is O(n) on average.

        Slice assignment is optimized to always begin iterating from the end
        nearest to a slice boundary, and to never backtrack.  It assigns all
        values in a single iteration and stops as soon as the slice is
        complete.
        """
        cdef Py_ssize_t start, stop, step
        cdef size_t index

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size)
            set_slice_double(self.view, start, stop, step, <PyObject*>value)

        # index directly
        else:
            index = normalize_index(<PyObject*>key, self.view.size, False)
            set_index_double(self.view, index, <PyObject*>value)

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
        DoublyLinkedList.__getitem__ :
            Index the list for a particular item or slice.
        DoublyLinkedList.__setitem__ :
            Set the value of an item or slice in the list.

        Notes
        -----
        Integer-based deletion is O(n) on average.

        Slice deletion is optimized to always begin iterating from the end
        nearest to a slice boundary, and to never backtrack.  It deletes all
        values in a single iteration and stops as soon as the slice is
        complete.
        """
        cdef Py_ssize_t start, stop, step
        cdef size_t index

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size)
            delete_slice_double(self.view, start, stop, step)

        # index directly
        else:
            index = normalize_index(<PyObject*>key, self.view.size, truncate=False)
            delete_index_double(self.view, index)

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
        return bool(contains(self.view, <PyObject*>item))  # from contains.h

    def __add__(self, other: Iterable[object]) -> DoublyLinkedList:
        """Concatenate two lists.

        Parameters
        ----------
        other : Iterable[Any]
            The list to concatenate with this one.

        Returns
        -------
        DoublyLinkedList
            A new list containing the items from both lists.

        Notes
        -----
        Concatenation is O(n), where `n` is the length of the other list.
        """
        cdef DoublyLinkedList result = self.copy()
        result.extend(other)
        return result

    def __iadd__(self, other: Iterable[object]) -> DoublyLinkedList:
        """Concatenate two lists in-place.

        Parameters
        ----------
        other : Iterable[Any]
            The list to concatenate with this one.

        Returns
        -------
        DoublyLinkedList
            This list, with the items from the other list appended.

        Notes
        -----
        Concatenation is O(m), where `m` is the length of the ``other`` list.
        """
        self.extend(other)
        return self

    def __mul__(self, repeat: int) -> DoublyLinkedList:
        """Repeat the list a specified number of times.

        Parameters
        ----------
        repeat : int
            The number of times to repeat the list.

        Returns
        -------
        DoublyLinkedList
            A new list containing successive copies of this list, repeated
            the given number of times.

        Notes
        -----
        Repetition is O(n * repeat).
        """
        cdef DoublyLinkedList result = self.copy()
        cdef size_t i

        for i in range(<size_t>repeat):
            result.extend(self.copy())
        return result

    def __imul__(self, repeat: int) -> DoublyLinkedList:
        """Repeat the list a specified number of times in-place.

        Parameters
        ----------
        repeat : int
            The number of times to repeat the list.

        Returns
        -------
        DoublyLinkedList
            This list, repeated the given number of times.

        Notes
        -----
        Repetition is O(n * repeat).
        """
        cdef DoublyLinkedList original = self.copy()
        cdef size_t i

        for i in range(<size_t>repeat):
            self.extend(original)
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
        return len(self) < len(other)

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
        return len(self) <= len(other)

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

        if len(self) != len(other):
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
        return len(self) > len(other)

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
        return len(self) >= len(other)


#####################
####    TESTS    ####
#####################


# TODO: migrate these to pytest.  In general, testing consists of comparing
# the output from this data structure to the built-in list.


def test_doubly_linked_list_getitem_slice():
    l = list(range(20))
    d = DoublyLinkedList(l)

    import random
    r = lambda low, high: random.randrange(low, high)

    offset = len(d) - int(0.25 * len(d)) + 1
    a, b = -offset, len(d) + offset
    start, stop, step = r(a, b), r(a, b), r(-b, b)
    if not step:
        return

    s1 = d[start:stop:step]
    s2 = l[start:stop:step]

    assert list(s1) == s2, (
        f"{repr(s1)} != {s2}   <- {':'.join([str(x) for x in (start, stop, step)])}"
    )


def test_doubly_linked_list_setitem_slice():
    l = list(range(20))
    d = DoublyLinkedList(l)

    import random
    r = lambda low, high: random.randrange(low, high)

    offset = len(d) - int(0.25 * len(d)) + 1
    a, b = -offset, len(d) + offset
    start, stop, step = r(a, b), r(a, b), r(-b, b)
    if not step:
        return

    start, stop, step = slice(start, stop, step).indices(len(d))
    stop -= (stop - start) % step or step  # make stop inclusive
    expected_size = 1 + abs(stop - start) // abs(step)
    if (step > 0 and stop < start) or (step < 0 and start < stop):
        return

    assign = [r(100, 200) for _ in range(expected_size)]

    if step > 0:
        stop += 1
    else:
        stop -= 1

    d[start:stop:step] = assign
    l[start:stop:step] = assign

    assert list(d) == l, (
        f"{repr(d)} != {l}   <- {':'.join([str(x) for x in (start, stop, step)])}"
        f"    {assign}"
    )
