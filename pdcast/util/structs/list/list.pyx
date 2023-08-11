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
#    list.pyx  (SinglyLinkedList, DoublyLinkedList)
#    set.pyx  (SinglyLinkedSet, DoublyLinkedSet)
#    dict.pyx  (SinglyLinkedDict, DoublyLinkedDict)
#    LRU.pyx  (LRUDict)
#    MFU.pyx  (MFUDict)



# TODO: __getitem__() sometimes throws unexpected ValueErrors and occasionally
# causes a segfault.


# TODO: in order to rigorously test these, we should copy over the tests from
# the standard library.
# https://github.com/python/cpython/blob/3.11/Lib/test/list_tests.py


# TODO: add "a detailed comparison of the performance of these data structures
# against each other and the standard library can be found <here>".


####################
####    BASE    ####
####################


cdef class LinkedList:
    """A pure Cython/C++ implementation of a linked list data structure.

    These come in 2 forms: :class:`SinglyLinkedList` and :class:`DoublyLinkedList`,
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
        The underlying view into the list.  This is a pointer to a C++ object
        that tracks the list's head and tail pointers and manages memory for
        each of its nodes.  It is not intended to be accessed by the user, and
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
        reverse: bool = False,
        spec: object | None = None,
    ):
        raise NotImplementedError(
            "`LinkedList` is an abstract class.  Use `SinglyLinkedList` or "
            "`DoublyLinkedList` instead."
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
        raise NotImplementedError()

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
        raise NotImplementedError()

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
        raise NotImplementedError()

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
        raise NotImplementedError()

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
        raise NotImplementedError()

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
        raise NotImplementedError()

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
        raise NotImplementedError()

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
        raise NotImplementedError()

    def clear(self) -> None:
        """Remove all items from the list.

        Notes
        -----
        Clearing a list is O(n).
        """
        raise NotImplementedError()

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
        raise NotImplementedError()

    def reverse(self) -> None:
        """Reverse the order of the list in-place.

        Notes
        -----
        Reversing a :class:`DoublyLinkedList` is O(n).
        """
        raise NotImplementedError()

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
        raise NotImplementedError()

    def specialize(self, object spec) -> None:
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
            untyped brethren.
        """
        raise NotImplementedError()

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
        DoublyLinkedList.specialize :
            Specialize the list with a particular type.

        Notes
        -----
        This is equivalent to the ``spec`` argument passed to the constructor
        and/or :meth:`specialize() <DoublyLinkedList.specialize>` method.
        """
        raise NotImplementedError()

    def nbytes(self) -> int:
        """The total memory consumption of the list in bytes.

        Returns
        -------
        int
            The total number of bytes consumed by the list, including all its
            nodes (but not their values).
        """
        raise NotImplementedError()

    def __len__(self) -> int:
        """Get the total number of items in the list.

        Returns
        -------
        int
            The number of items in the list.
        """
        raise NotImplementedError()

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
        raise NotImplementedError()

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
        raise NotImplementedError()

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
        raise NotImplementedError()

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

            >>> d = DoublyLinkedList[int]([1, 2, 3])
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

            >>> d = DoublyLinkedList[Iterable]()
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
        result = self.copy()
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
        cdef Py_ssize_t i

        result = self.copy()
        for i in range(<Py_ssize_t>repeat):
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


#############################
####    SINGLY-LINKED    ####
#############################


cdef class SinglyLinkedList(LinkedList):
    """A singly-linked list implementing the :class:`LinkedList` interface.

    Notes
    -----
    Due to the asymmetric nature of the list, some operations are more
    expensive than others, particularly as it relates to reverse iteration and
    removals.  These are more efficient toward the head of the list, since we
    have to traverse the entire list to reach any prior node.  In exchange,
    these lists are the most memory-efficient of the linked list data
    structures, since they only require a single pointer per node.
    """

    def __init__(
        self,
        items: Iterable[object] | None = None,
        reverse: bool = False,
        spec: object | None = None,
    ) -> None:
        """Construct a list from an optional input iterable."""
        cdef PyObject* items_
        cdef PyObject* spec_

        if spec is None:
            spec_ = <PyObject*>NULL
        else:
            spec_ = <PyObject*>spec

        # init empty
        if items is None:
            self.view = new ListView[SingleNode]()
            if spec is not None:
                self.view.specialize(spec_)

        # init from iterable
        else:
            items_ = <PyObject*>items
            if spec is not None:
                self.view = new ListView[SingleNode](items_, reverse, spec_)
            else:
                self.view = new ListView[SingleNode](items_, reverse, spec_)

    def __dealloc__(self):
        """Free the underlying ListView when the list is garbage collected."""
        del self.view

    ##############################
    ####    LIST INTERFACE    ####
    ##############################

    @property
    def specialization(self) -> Any:
        """Implement :attr:`LinkedList.specialization` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        cdef PyObject* spec = self.view.get_specialization()  # from view.h

        return None if spec is NULL else <object>spec

    def append(self, item: object) -> None:
        """Implement :meth:`LinkedList.append` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        append(self.view, <PyObject*>item)  # from append.h

    def appendleft(self, item: object) -> None:
        """Implement :meth:`LinkedList.appendleft` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        appendleft(self.view, <PyObject*>item)  # from append.h

    def insert(self, index: int, item: object) -> None:
        """Implement :meth:`LinkedList.insert` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        cdef size_t norm_index = normalize_index(
            <PyObject*>index,
            self.view.size,
            truncate=True
        )

        insert(self.view, norm_index, <PyObject*>item)  # from insert.h

    def extend(self, items: Iterable[object]) -> None:
        """Implement :meth:`LinkedList.extend` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        extend(self.view, <PyObject*>items)  # from extend.h

    def extendleft(self, items: Iterable[object]) -> None:
        """Implement :meth:`LinkedList.extendleft` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        extendleft(self.view, <PyObject*>items)  # from extend.h

    def index(self, item: object, start: int = 0, stop: int = -1) -> int:
        """Implement :meth:`LinkedList.index` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        # allow Python-style negative indexing + bounds checking
        cdef size_t norm_start = normalize_index(
            <PyObject*>start,
            self.view.size,
            truncate=True
        )
        cdef size_t norm_stop = normalize_index(
            <PyObject*>stop,
            self.view.size,
            truncate=True
        )

        # check that start and stop indices are consistent
        if norm_start > norm_stop:
            raise ValueError("start index must be less than or equal to stop index")

        # delegate to index.h
        return index(self.view, <PyObject*>item, norm_start, norm_stop)

    def count(self, item: object, start: int = 0, stop: int = -1) -> int:
        """Implement :meth:`LinkedList.count` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        # allow Python-style negative indexing + bounds checking
        cdef size_t norm_start = normalize_index(
            <PyObject*>start,
            self.view.size,
            truncate=True
        )
        cdef size_t norm_stop = normalize_index(
            <PyObject*>stop,
            self.view.size,
            truncate=True
        )

        # check that start and stop indices are consistent
        if norm_start > norm_stop:
            raise ValueError("start index must be less than or equal to stop index")

        # delegate to count.h
        return count(self.view, <PyObject*>item, norm_start, norm_stop)

    def remove(self, item: object) -> None:
        """Implement :meth:`LinkedList.remove` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        remove(self.view, <PyObject*>item)  # from remove.h

    def pop(self, index: int = -1) -> object:
        """Implement :meth:`LinkedList.pop` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        cdef size_t norm_index = normalize_index(
            <PyObject*>index,
            self.view.size,
            truncate=True
        )

        return <object>pop(self.view, norm_index)  # from pop.h

    def popleft(self) -> object:
        """Implement :meth:`LinkedList.popleft` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        return <object>popleft(self.view)  # from pop.h

    def popright(self) -> object:
        """Implement :meth:`LinkedList.popright` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        return <object>popright(self.view)  # from pop.h

    def copy(self) -> SinglyLinkedList:
        """Implement :meth:`LinkedList.copy` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        cdef SinglyLinkedList result = SinglyLinkedList.__new__(SinglyLinkedList)
        result.view = self.view.copy()  # from view.h
        return result

    def clear(self) -> None:
        """Implement :meth:`LinkedList.clear` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        self.view.clear()  # from view.h

    def sort(self, *, key: object = None, reverse: bool = False) -> None:
        """Implement :meth:`LinkedList.sort` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        if key is None:
            sort(self.view, <PyObject*>NULL, <bint>reverse)  # from sort.h
        else:
            sort(self.view, <PyObject*>key, <bint>reverse)  # from sort.h

    def reverse(self) -> None:
        """Implement :meth:`LinkedList.reverse` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        reverse(self.view)  # from reverse.h

    def rotate(self, steps: int = 1) -> None:
        """Implement :meth:`LinkedList.rotate` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        rotate(self.view, <Py_ssize_t>steps)  # from rotate.h

    def specialize(self, object spec) -> None:
        """Implement :meth:`LinkedList.specialize` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        if spec is None:
            self.view.specialize(<PyObject*>NULL)  # from view.h
        else:
            self.view.specialize(<PyObject*>spec)  # from view.h

    def nbytes(self) -> int:
        """Implement :meth:`LinkedList.nbytes` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        import sys

        cdef size_t total = self.view.nbytes()  # from view.h
        total += sys.getsizeof(self)  # add size of Python wrapper
        return total

    def __len__(self) -> int:
        """Implement :meth:`LinkedList.__len__` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        return self.view.size

    def __iter__(self) -> Iterator[object]:
        """Implement :meth:`LinkedList.__iter__` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        cdef SingleNode* curr = self.view.head

        while curr is not NULL:
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python
            curr = <SingleNode*>curr.next

    def __reversed__(self) -> Iterator[object]:
        """Implement :meth:`LinkedList.__reversed__` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        cdef SingleNode* curr = self.view.head
        cdef stack[SingleNode*] nodes

        # push each node to a stack
        while curr is not NULL:
            nodes.push(curr)
            curr = <SingleNode*>curr.next

        # pop from the stack to reverse the order of iteration
        while not nodes.empty():
            curr = nodes.top()
            nodes.pop()
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python

    def __getitem__(self, key: int | slice) -> object | SinglyLinkedList:
        """Implement :meth:`LinkedList.__getitem__` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        cdef SinglyLinkedList result
        cdef Py_ssize_t start, stop, step
        cdef size_t index
        cdef PyObject* new_ref

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size)
            result = SinglyLinkedList.__new__(SinglyLinkedList)
            result.view = get_slice(self.view, start, stop, step)
            return result

        # index directly
        index = normalize_index(<PyObject*>key, self.view.size, truncate=False)
        new_ref = get_index(self.view, index)  # from index.h
        return <object>new_ref

    def __setitem__(self, key: int | slice, value: object | Iterable[object]) -> None:
        """Implement :meth:`LinkedList.__setitem__` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        cdef Py_ssize_t start, stop, step
        cdef size_t index

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size)
            set_slice(self.view, start, stop, step, <PyObject*>value)

        # index directly
        else:
            index = normalize_index(<PyObject*>key, self.view.size, truncate=False)
            set_index(self.view, index, <PyObject*>value)

    def __delitem__(self, key: int | slice) -> None:
        """Implement :meth:`LinkedList.__delitem__` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        cdef Py_ssize_t start, stop, step
        cdef size_t index

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size)
            delete_slice(self.view, start, stop, step)

        # index directly
        else:
            index = normalize_index(<PyObject*>key, self.view.size, truncate=False)
            delete_index(self.view, index)

    def __contains__(self, item: object) -> bool:
        """Implement :meth:`LinkedList.__contains__` for
        :class:`SinglyLinkedLists <SinglyLinkedList>`.
        """
        return bool(contains(self.view, <PyObject*>item))  # from contains.h


#############################
####    DOUBLY-LINKED    ####
#############################


cdef class DoublyLinkedList(LinkedList):
    """A doubly-linked list implementing the :class:`LinkedList` interface.

    Notes
    -----
    These lists contain optimizations related to their doubly-linked nature,
    allowing them to iterate efficiently in both directions.  This particularly
    affects operations like :meth:`popright() <LinkedList.popright>`, which is
    O(1) for doubly-linked lists but O(n) for singly-linked ones.  Indexing
    operations in general are also faster for doubly-linked lists, since we
    only traverse half the list at most to reach the desired index.

    The tradeoff is that doubly-linked lists consume extra memory than their
    singly-linked counterparts, since they require an extra pointer per node.
    """

    def __init__(
        self,
        items: Iterable[object] | None = None,
        reverse: bool = False,
        spec: object | None = None,
    ) -> None:
        """Construct a list from an optional input iterable."""
        cdef PyObject* items_
        cdef PyObject* spec_

        if spec is None:
            spec_ = <PyObject*>NULL
        else:
            spec_ = <PyObject*>spec

        # init empty
        if items is None:
            self.view = new ListView[DoubleNode]()
            if spec is not None:
                self.view.specialize(spec_)

        # init from iterable
        else:
            items_ = <PyObject*>items
            if spec is not None:
                self.view = new ListView[DoubleNode](items_, reverse, spec_)
            else:
                self.view = new ListView[DoubleNode](items_, reverse, spec_)

    def __dealloc__(self):
        """Free the underlying ListView when the list is garbage collected."""
        del self.view

    ##############################
    ####    LIST INTERFACE    ####
    ##############################

    @property
    def specialization(self) -> Any:
        """Implement :attr:`LinkedList.specialization` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        cdef PyObject* spec = self.view.get_specialization()  # from view.h

        return None if spec is NULL else <object>spec

    def append(self, item: object) -> None:
        """Implement :meth:`LinkedList.append` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        append(self.view, <PyObject*>item)  # from append.h

    def appendleft(self, item: object) -> None:
        """Implement :meth:`LinkedList.appendleft` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        appendleft(self.view, <PyObject*>item)  # from append.h

    def insert(self, index: int, item: object) -> None:
        """Implement :meth:`LinkedList.insert` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        cdef size_t norm_index = normalize_index(
            <PyObject*>index,
            self.view.size,
            truncate=True
        )

        insert(self.view, norm_index, <PyObject*>item)  # from insert.h

    def extend(self, items: Iterable[object]) -> None:
        """Implement :meth:`LinkedList.extend` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        extend(self.view, <PyObject*>items)  # from extend.h

    def extendleft(self, items: Iterable[object]) -> None:
        """Implement :meth:`LinkedList.extendleft` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        extendleft(self.view, <PyObject*>items)  # from extend.h

    def index(self, item: object, start: int = 0, stop: int = -1) -> int:
        """Implement :meth:`LinkedList.index` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        # allow Python-style negative indexing + bounds checking
        cdef size_t norm_start = normalize_index(
            <PyObject*>start,
            self.view.size,
            truncate=True
        )
        cdef size_t norm_stop = normalize_index(
            <PyObject*>stop,
            self.view.size,
            truncate=True
        )

        # check that start and stop indices are consistent
        if norm_start > norm_stop:
            raise ValueError("start index must be less than or equal to stop index")

        # delegate to index.h
        return index(self.view, <PyObject*>item, norm_start, norm_stop)

    def count(self, item: object, start: int = 0, stop: int = -1) -> int:
        """Implement :meth:`LinkedList.count` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        # allow Python-style negative indexing + bounds checking
        cdef size_t norm_start = normalize_index(
            <PyObject*>start,
            self.view.size,
            truncate=True
        )
        cdef size_t norm_stop = normalize_index(
            <PyObject*>stop,
            self.view.size,
            truncate=True
        )

        # check that start and stop indices are consistent
        if norm_start > norm_stop:
            raise ValueError("start index must be less than or equal to stop index")

        # delegate to count.h
        return count(self.view, <PyObject*>item, norm_start, norm_stop)

    def remove(self, item: object) -> None:
        """Implement :meth:`LinkedList.remove` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        remove(self.view, <PyObject*>item)  # from remove.h

    def pop(self, index: int = -1) -> object:
        """Implement :meth:`LinkedList.pop` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        cdef size_t norm_index = normalize_index(
            <PyObject*>index,
            self.view.size,
            truncate=True
        )

        return <object>pop(self.view, norm_index)  # from pop.h

    def popleft(self) -> object:
        """Implement :meth:`LinkedList.popleft` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        return <object>popleft(self.view)  # from pop.h

    def popright(self) -> object:
        """Implement :meth:`LinkedList.popright` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        return <object>popright(self.view)  # from pop.h

    def copy(self) -> DoublyLinkedList:
        """Implement :meth:`LinkedList.copy` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        cdef DoublyLinkedList result = DoublyLinkedList.__new__(DoublyLinkedList)
        result.view = self.view.copy()  # from view.h
        return result

    def clear(self) -> None:
        """Implement :meth:`LinkedList.clear` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        self.view.clear()  # from view.h

    def sort(self, *, key: object = None, reverse: bool = False) -> None:
        """Implement :meth:`LinkedList.sort` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        if key is None:
            sort(self.view, <PyObject*>NULL, <bint>reverse)  # from sort.h
        else:
            sort(self.view, <PyObject*>key, <bint>reverse)  # from sort.h

    def reverse(self) -> None:
        """Implement :meth:`LinkedList.reverse` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        reverse(self.view)  # from reverse.h

    def rotate(self, steps: int = 1) -> None:
        """Implement :meth:`LinkedList.rotate` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        rotate(self.view, <Py_ssize_t>steps)  # from rotate.h

    def specialize(self, object spec) -> None:
        """Implement :meth:`LinkedList.specialize` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        if spec is None:
            self.view.specialize(<PyObject*>NULL)  # from view.h
        else:
            self.view.specialize(<PyObject*>spec)  # from view.h

    def nbytes(self) -> int:
        """Implement :meth:`LinkedList.nbytes` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        import sys

        cdef size_t total = self.view.nbytes()  # from view.h
        total += sys.getsizeof(self)  # add size of Python wrapper
        return total

    def __len__(self) -> int:
        """Implement :meth:`LinkedList.__len__` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        return self.view.size

    def __iter__(self) -> Iterator[object]:
        """Implement :meth:`LinkedList.__iter__` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        cdef DoubleNode* curr = self.view.head

        while curr is not NULL:
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python
            curr = <DoubleNode*>curr.next

    def __reversed__(self) -> Iterator[object]:
        """Implement :meth:`LinkedList.__reversed__` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        cdef DoubleNode* curr = self.view.tail

        while curr is not NULL:
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python
            curr = <DoubleNode*>curr.prev

    def __getitem__(self, key: int | slice) -> object | DoublyLinkedList:
        """Implement :meth:`LinkedList.__getitem__` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        cdef DoublyLinkedList result
        cdef Py_ssize_t start, stop, step
        cdef size_t index
        cdef PyObject* new_ref

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size)
            result = DoublyLinkedList.__new__(DoublyLinkedList)
            result.view = get_slice(self.view, start, stop, step)
            return result

        # index directly
        index = normalize_index(<PyObject*>key, self.view.size, truncate=False)
        new_ref = get_index(self.view, index)  # from index.h
        return <object>new_ref

    def __setitem__(self, key: int | slice, value: object | Iterable[object]) -> None:
        """Implement :meth:`LinkedList.__setitem__` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        cdef Py_ssize_t start, stop, step
        cdef size_t index

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size)
            set_slice(self.view, start, stop, step, <PyObject*>value)

        # index directly
        else:
            index = normalize_index(<PyObject*>key, self.view.size, truncate=False)
            set_index(self.view, index, <PyObject*>value)

    def __delitem__(self, key: int | slice) -> None:
        """Implement :meth:`LinkedList.__delitem__` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        cdef Py_ssize_t start, stop, step
        cdef size_t index

        # support slicing
        if isinstance(key, slice):
            start, stop, step = key.indices(self.view.size)
            delete_slice(self.view, start, stop, step)

        # index directly
        else:
            index = normalize_index(<PyObject*>key, self.view.size, truncate=False)
            delete_index(self.view, index)

    def __contains__(self, item: object) -> bool:
        """Implement :meth:`LinkedList.__contains__` for
        :class:`DoublyLinkedLists <DoublyLinkedList>`.
        """
        return bool(contains(self.view, <PyObject*>item))  # from contains.h


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
