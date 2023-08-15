# distutils: language = c++
"""This module contains a pure Cython/C++ implementation of both singly-linked
and doubly-linked list data structures.
"""
from typing import Iterable, Iterator


# TODO: list.pyx, set.pyx, and dict.pyx should be lifted to structs/.  Header
# files can go in structs/algorithms/.


# structs/
#    algorithms/
#        append.h
#        contains.h
#        count.h
#        etc.
#    nodes/
#       base.h  (Constants, HashTable, SingleNode, DoubleNode, Allocater, traits)
#       list.h  (ListView)
#       set.h  (SetView)
#       dict.h  (DictView)
#       sorted.h  (Sorted<>)
#    list.pyx  (SinglyLinkedList, LinkedList)
#    set.pyx  (SinglyLinkedSet, DoublyLinkedSet)
#    dict.pyx  (SinglyLinkedDict, DoublyLinkedDict)
#    LRU.pyx  (LRUDict)
#    MFU.pyx  (MFUDict)



# TODO: __getitem__() sometimes throws unexpected ValueErrors and occasionally
# causes a segfault.


# TODO: in order to rigorously test these, we should copy over the tests from
# the standard library.
# https://github.com/python/cpython/blob/3.11/Lib/test/list_tests.py


####################
####    BASE    ####
####################


cdef class LinkedList:
    """A pure Cython/C++ implementation of a linked list data structure.

    These come in 2 forms: :class:`SinglyLinkedList` and :class:`LinkedList`,
    both of which are drop-in replacements for a standard Python
    :class:`list <python:list>` or :class:`deque <python:collections.deque>`
    data structure.

    Parameters
    ----------
    items : Iterable[Any], optional
        An iterable of items to initialize the list.
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
    view : ListView*
        A low-level view into the list.  This is a pointer to a C++ object that
        tracks the list's head and tail pointers and manages memory for each of
        its nodes.  It is not intended to be accessed by the user, and
        manipulating it directly it can result in memory leaks and/or undefined
        behavior.  Thorough inspection of the C++ header files is recommended
        before attempting to access this attribute, which can only be done in
        Cython.

    Notes
    -----
    This implementation retains all the usual tradeoffs of linked lists vs
    arrays (e.g. O(n) indexing vs O(1) appends), but attempts to minimize
    compromises wherever possible.

    .. warning::

        These lists are not thread-safe.  If you need to use them in a
        multithreaded context, you should use a :class:`threading.Lock` to
        synchronize access.
    """

    def __init__(
        self,
        items: Iterable[object] | None = None,
        doubly_linked: bool = True,
        reverse: bool = False,
        spec: object | None = None,
        max_size: int = -1,
    ):
        # init empty
        if items is None:
            self.view = new VariantList(doubly_linked, max_size)
            if spec is not None:
                self.view.specialize(<PyObject*>spec)

        # unpack iterable
        elif spec is None:
            self.view = new VariantList(
                <PyObject*>items, doubly_linked, reverse, NULL, max_size
            )
        else:
            self.view = new VariantList(
                <PyObject*>items, doubly_linked, reverse, <PyObject*>spec, max_size
            )

    def __dealloc__(self):
        """Free underlying ListView when the list is garbage collected."""
        del self.view

    @staticmethod
    cdef LinkedList from_view(VariantList* view):
        """Create a new LinkedList from a C++ view."""
        cdef LinkedList result = LinkedList().__new__(LinkedList)  # bypass __init__()
        result.view = view
        return result

    ##############################
    ####    LIST INTERFACE    ####
    ##############################

    def append(self, item: object, left: bool = False) -> None:
        """Add an item to the end of the list.

        Parameters
        ----------
        item : Any
            The item to add to the list.
        left : bool, optional
            If ``True``, add the item to the beginning of the list instead of
            the end.  The default is ``False``.

        Notes
        -----
        Appends are O(1) for both ends of the list.
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
            The item to add to the list.

        Raises
        ------
        IndexError
            If the index is out of bounds.

        Notes
        -----
        Inserts are O(n) on average.
        """
        # allow Python-style negative indexing + boundschecking
        cdef size_t norm_index = normalize_index(
            <PyObject*>index,
            self.view.size(),
            truncate=True
        )

        # dispatch to insert.h
        self.view.insert(norm_index, <PyObject*>item)

    def extend(self, items: Iterable[object], left: bool = False) -> None:
        """Add multiple items to the end of the list.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of items to add to the list.
        left : bool, optional
            If ``True``, add the items to the beginning of the list instead of
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
        cdef pair[size_t, size_t] bounds = normalize_bounds(
            start, stop, size=self.view.size(), truncate=True
        )

        # dispatch to index.h
        return self.view.index(<PyObject*>item, bounds.first, bounds.second)

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
        cdef pair[size_t, size_t] bounds = normalize_bounds(
            start, stop, size=self.view.size(), truncate=True
        )

        # dispatch to count.h
        return self.view.count(<PyObject*>item, bounds.first, bounds.second)

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
        # delegate to remove.h
        self.view.remove(<PyObject*>item)

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
        # allow Python-style negative indexing + bounds checking
        cdef size_t norm_index = normalize_index(
            <PyObject*>index,
            self.view.size(),
            truncate=False
        )

        # dispatch to pop.h
        return <object>self.view.pop(norm_index)

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
        cdef LinkedList result = LinkedList().__new__(LinkedList)  # bypass __init__()
        result.view = self.view.copy()
        return result

    def clear(self) -> None:
        """Remove all items from the list.

        Notes
        -----
        Clearing a list is O(n).
        """
        self.view.clear()

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
        # dispatch to sort.h
        if key is None:
            self.view.sort(<PyObject*>NULL, <bint>reverse)
        else:
            self.view.sort(<PyObject*>key, <bint>reverse)

    def reverse(self) -> None:
        """Reverse the order of the list in-place.

        Notes
        -----
        Reversing a :class:`LinkedList` is O(n).
        """
        # dispatch to reverse.h
        self.view.reverse()

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
        # dispatch to rotate.h
        self.view.rotate(<ssize_t>steps)

    def __len__(self) -> int:
        """Get the total number of items in the list.

        Returns
        -------
        int
            The number of items in the list.
        """
        return self.view.size()

    def __iter__(self) -> Iterator[object]:
        """Iterate through the list items in order.

        Yields
        ------
        Any
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedList` is O(n).
        """
        # cdef SingleNode* curr = self.view.head

        # while curr is not NULL:
        #     Py_INCREF(curr.value)
        #     yield <object>curr.value  # this returns ownership to Python
        #     curr = <SingleNode*>curr.next
        raise NotImplementedError()

    def __reversed__(self) -> Iterator[object]:
        """Iterate through the list in reverse order.

        Yields
        ------
        Any
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedList` is O(n).
        """
        # cdef SingleNode* curr = self.view.head
        # cdef stack[SingleNode*] nodes

        # # push each node to a stack
        # while curr is not NULL:
        #     nodes.push(curr)
        #     curr = <SingleNode*>curr.next

        # # pop from the stack to reverse the order of iteration
        # while not nodes.empty():
        #     curr = nodes.top()
        #     nodes.pop()
        #     Py_INCREF(curr.value)
        #     yield <object>curr.value  # this returns ownership to Python
        raise NotImplementedError()

    def __getitem__(self, key: int | slice) -> object | LinkedList:
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
        a slice boundary, and to never backtrack.  It collects all values in a
        single iteration and stops as soon as the slice is complete.
        """
        cdef size_t index
        cdef PyObject* new_ref
        cdef Py_ssize_t start, stop, step

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size())
            return LinkedList.from_view(self.view.get_slice(start, stop, step))

        # index directly
        index = normalize_index(<PyObject*>key, self.view.size(), truncate=False)
        new_ref = self.view.get_index(index)
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
        cdef size_t index
        cdef Py_ssize_t start, stop, step

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size())
            self.view.set_slice(start, stop, step, <PyObject*>value)

        # index directly
        else:
            index = normalize_index(<PyObject*>key, self.view.size(), truncate=False)
            self.view.set_index(index, <PyObject*>value)

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
        cdef size_t index
        cdef Py_ssize_t start, stop, step

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size())
            self.view.delete_slice(start, stop, step)

        # index directly
        else:
            index = normalize_index(<PyObject*>key, self.view.size(), truncate=False)
            self.view.delete_index(index)

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
        return self.view.contains(<PyObject*>item)

    def __add__(self, other: Iterable[object]) -> LinkedList:
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
        result = self.copy()
        result.extend(other)
        return result

    def __iadd__(self, other: Iterable[object]) -> LinkedList:
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
        self.extend(other)
        return self

    def __mul__(self, repeat: int) -> LinkedList:
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
        cdef Py_ssize_t i

        result = self.copy()
        for i in range(<Py_ssize_t>repeat):
            result.extend(self.copy())
        return result

    def __imul__(self, repeat: int) -> LinkedList:
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
        cdef Py_ssize_t i

        original = self.copy()
        for i in range(<Py_ssize_t>repeat):
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

    ###################################
    ####    TYPE SPECIALIZATION    ####
    ###################################

    @property
    def specialization(self) -> Any:
        """Return the type specialization that is being enforced by the list.

        Returns
        -------
        Any
            The type specialization of the list, or ``None`` if the list is
            generic.

        See Also
        --------
        LinkedList.specialize :
            Specialize the list with a particular type.

        Notes
        -----
        This is equivalent to the ``spec`` argument passed to the constructor
        and/or :meth:`specialize() <LinkedList.specialize>` method.
        """
        return <object>self.view.get_specialization()

    def specialize(self, spec: object) -> None:
        """Specialize the list with a particular type.

        Parameters
        ----------
        spec : Any
            The type to enforce for elements of the list.  This can be in any
            format recognized by :func:`isinstance() <python:isinstance>`.  If
            it is set to ``None``, then type checking will be disabled for the
            list.

        Raises
        ------
        TypeError
            If the list contains elements that do not match the specified type.

        Notes
        -----
        Specializing a list is O(n).

        The way type specialization works is by adding an extra
        :func:`isinstance() <python:isinstance>` check during node allocation.
        If the type of the new item does not match the specialized type, then
        an exception will be raised and the type will not be added to the list.
        This ensures that the list is type-safe at all times.

        If the list is not empty when this method is called, then the type of
        each existing item will be checked against the new type.  If any of
        them do not match, then the specialization will be aborted and an
        error will be raised.  The list is not modified by this process.

        .. note::

            Typed lists are slightly slower at appending items due to the extra
            type check.  Otherwise, they have identical performance to their
            untyped equivalents.
        """
        self.view.specialize(<PyObject*>spec)

    def __class_getitem__(cls, key: Any) -> type:
        """Subscribe a linked list class to a particular type specialization.

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
        LinkedList.specialize :
            Specialize a list at runtime.

        Returns
        -------
        type
            A variant of the linked list that is permanently specialized to the
            templated type.  Constructing such a list is equivalent to calling
            the constructor with the ``spec`` argument, except that the
            specialization cannot be changed for the lifetime of the object.

        Notes
        -----
        :class:`LinkedLists <LinkedList>` provide 3 separate mechanisms for
        enforcing type safety:

            #.  The :meth:`specialize() <LinkedList.specialize>` method, which
                allows runtime specialization of a list with a particular type.
                If the list is not empty when this method is called, it will
                loop through the list and check whether the contents satisfy
                the specialized type, and then enforce that type for any future
                additions to the list.
            #.  The ``spec`` argument to the constructor, which allows
                specialization at the time of list creation.  This is
                equivalent to calling :meth:`specialize() <LinkedList.specialize>`
                immediately after construction, but avoids an extra loop.
            #.  Direct subscription via the
                :meth:`__class_getitem__() <python:object.__class_getitem__>`
                syntax.  This is equivalent to using the ``spec`` argument to
                create a typed list, except that the specialization is
                permanent and cannot be changed afterwards.  This is the most
                restrictive form of type safety, but also allows users to be
                absolutely sure about the list's contents.

        In any case, a list's specialization can be checked at any time by
        accessing its :attr:`specialization` attribute, which can be used in
        :func:`isinstance() <python:isinstance>` and
        :func:`issubclass() <python:issubclass>` checks directly.

        Examples
        --------
        .. doctest::

            >>> d = LinkedList[int]([1, 2, 3])
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

            >>> d = LinkedList[Iterable]()
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
            can significantly slow down list appends and inserts.  Other operations
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
        class TypedList(cls):
            """Implement `TypedList` for the given specialization."""

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

    ####################
    ####    MISC    ####
    ####################

    def nbytes(self) -> int:
        """The total memory consumption of the list in bytes.

        Returns
        -------
        int
            The total number of bytes consumed by the list, including all its
            nodes (but not their values).
        """
        return self.view.nbytes()

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

        return f"{prefix}([{contents}])"


#####################
####    TESTS    ####
#####################


# TODO: migrate these to pytest.  In general, testing consists of comparing
# the output from this data structure to the built-in list.


def test_doubly_linked_list_getitem_slice():
    l = list(range(20))
    d = LinkedList(l)

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
    d = LinkedList(l)

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