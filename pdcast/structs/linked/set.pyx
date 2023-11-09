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
    variant : Slot[CyLinkedSet]
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
        max_size: int = None,
        spec: object | None = None,
        reverse: bool = False,
        singly_linked: bool = False,
    ):
        cdef optional[size_t] c_max_size
        cdef PyObject* c_spec

        # make max_size C-compatible
        if max_size is None:
            c_max_size = nullopt
        else:
            c_max_size = <size_t>max_size

        # make specialization C-compatible
        if spec is None:
            c_spec = NULL
        else:
            c_spec = <PyObject*>spec  # borrowed reference

        # init variant
        if items is None:
            self.variant.construct(max_size, c_spec, <bint>singly_linked)
        else:
            self.variant.construct(
                <PyObject*>items, max_size, c_spec, <bint>reverse, <bint>singly_linked
            )

    @staticmethod
    cdef LinkedSet from_variant(CyLinkedSet* variant):
        """Create a new LinkedSet from a C++ variant."""
        cdef LinkedSet result = LinkedSet.__new__(LinkedSet)  # bypass __init__()
        result.variant.move_ptr(variant)  # Cython-compatible move assignment
        return result

    ##############################
    ####    LIST INTERFACE    ####
    ##############################

    def insert(self, index: int, item: object) -> None:
        """TODO
        """
        self.variant.ptr().insert(<PyObject*>index, <PyObject*>item)

    def index(self, item: object, start: int = 0, stop: int = -1) -> int:
        """TODO
        """
        return self.variant.ptr().index(<PyObject*>item, start, stop)

    def count(self, item: object, start: int = 0, stop: int = -1) -> int:
        """TODO
        """
        return self.variant.ptr().count(<PyObject*>item, start, stop)

    def pop(self, index: int = -1) -> object:
        """TODO
        """
        return <object>self.variant.ptr().pop(index)

    def copy(self) -> LinkedSet:
        """TODO
        """
        return LinkedList.from_variant(self.variant.ptr().copy().ptr())

    def clear(self) -> None:
        """TODO
        """
        self.variant.ptr().clear()

    def sort(self, *, key: object = None, reverse: bool = False) -> None:
        """TODO
        """
        if key is None:
            self.variant.ptr().sort(<PyObject*>NULL, <bint>reverse)
        else:
            self.variant.ptr().sort(<PyObject*>key, <bint>reverse)

    def reverse(self) -> None:
        """TODO
        """
        self.variant.ptr().reverse()

    def rotate(self, steps: int = 1) -> None:
        """TODO
        """
        self.variant.ptr().rotate(<long long>steps)

    def __bool__(self) -> bool:
        """Treat empty lists as Falsy in boolean logic.

        Returns
        -------
        bool
            Indicates whether the list is empty.
        """
        return not bool(self.variant.ptr().empty())

    def __len__(self) -> int:
        """Get the total number of items in the list.

        Returns
        -------
        int
            The number of items in the list.
        """
        return self.variant.ptr().size()

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
        return <object>(self.variant.ptr().iter())

    def __reversed__(self) -> Iterator[object]:
        """Iterate through the list in reverse order.

        Yields
        ------
        Any
            The next item in the reversed list.

        Notes
        -----
        Iterating through a doubly-linked list in reverse is O(n), while for
        singly-linked lists it is O(2n).  This is because of the need to build a
        temporary stack to store each element, which forces a second iteration.
        """
        return <object>(self.variant.ptr().riter())

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
        if isinstance(key, slice):
            return LinkedList.from_variant(
                self.variant.ptr().slice(<PyObject*>key).get().ptr()
            )

        return <object>(deref(self.variant.ptr())[<PyObject*>key].get())

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
        if isinstance(key, slice):
            self.variant.ptr().slice(<PyObject*>key).set(<PyObject*>value)
        else:
            deref(self.variant.ptr())[<PyObject*>key].set(<PyObject*>value)

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
        if isinstance(key, slice):
            self.variant.ptr().slice(<PyObject*>key).delete()
        else:
            deref(self.variant.ptr())[<PyObject*>key].delete()

    #############################
    ####    SET INTERFACE    ####
    #############################

    def add(self, item: object, left: bool = False) -> None:
        """An alias for :meth:`LinkedSet.append` that is consistent with the
        standard library :class:`set <python:set>` interface.
        """
        self.variant.ptr().add(<PyObject*>item, <bint>left)

    def remove(self, item: object) -> None:
        """TODO
        """
        self.variant.ptr().remove(<PyObject*>item)

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
        self.variant.ptr().discard(<PyObject*>item)

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
        return LinkedSet.from_variant(
            self.variant.ptr().set_union(<PyObject*>others, <bint>left)
        )

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
        return LinkedSet.from_variant(
            self.variant.ptr().set_intersection(<PyObject*>others)
        )

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
        return LinkedSet.from_variant(
            self.variant.ptr().set_difference(<PyObject*>others)
        )

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
        return LinkedSet.from_variant(
            self.variant.ptr().symmetric_difference(<PyObject*>other)
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
        self.variant.ptr().update(<PyObject*>others, <bint>left)

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
        self.variant.ptr().intersection_update(<PyObject*>others)

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
        self.variant.ptr().difference_update(<PyObject*>others)

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
        self.variant.ptr().symmetric_difference_update(<PyObject*>other)

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
        return self.variant.ptr().isdisjoint(<PyObject*>other)

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
        return self.variant.ptr().issubset(<PyObject*>other)

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
        return self.variant.ptr().issuperset(<PyObject*>other)

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
        return LinkedSet.from_variant(
            self.variant.ptr().set_union(<PyObject*>other, <bint>False)
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
        self.variant.ptr().update(<PyObject*>other, <bint>False)
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
        return LinkedSet.from_variant(
            self.variant.ptr().intersection(<PyObject*>other)
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
        self.variant.ptr().intersection_update(<PyObject*>other)
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
        return LinkedSet.from_variant(
            self.variant.ptr().difference(<PyObject*>other)
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
        self.variant.ptr().difference_update(<PyObject*>other)
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
        return LinkedSet.from_variant(
            self.variant.ptr().symmetric_difference(<PyObject*>other)
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
        self.variant.ptr().symmetric_difference_update(<PyObject*>other)
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
        return bool(self.variant.ptr().issubset(<PyObject*>other))

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
        return bool(self.variant.ptr().issubset(<PyObject*>other))

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
        return bool(self.variant.ptr().equals(<PyObject*>other))

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
        return bool(self.variant.ptr().issuperset(<PyObject*>other))

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
        return bool(self.variant.ptr().issuperset(<PyObject*>other))

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
        return self.variant.ptr().contains(<PyObject*>item)

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
        return f"{{{', '.join(str(item) for item in self)}}}"

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
        specialization = self.specialization
        if specialization is not None:
            prefix += f"{{{repr(specialization)}}}"

        # abbreviate in order to avoid spamming the console
        if len(self) > 64:
            contents = ", ".join(repr(item) for item in self[:32])
            contents += ", ..., "
            contents += ", ".join(repr(item) for item in self[-32:])
        else:
            contents = ", ".join(repr(item) for item in self)

        return f"{prefix}({{{contents}}})"

    __hash__ = None  # mutable containers are not hashable

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
        return self.variant.ptr().distance(<PyObject*>item1, <PyObject*>item2)

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
        self.variant.ptr().swap(<PyObject*>item1, <PyObject*>item2)

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
        self.variant.ptr().move(<PyObject*>item, <Py_ssize_t>steps)

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
        self.variant.ptr().move_to_index(<PyObject*>item, <Py_ssize_t>index)

    #############################
    ####    EXTRA METHODS    ####
    #############################

    @property
    def dynamic(self) -> bool:
        """Indicates whether the list's allocator supports dynamic resizing (True), or
        is fixed to a particular size (False).

        Returns
        -------
        bool
            True if the list can dynamically grow and shrink, or False if it has a
            fixed size.

        Notes
        -----
        This is always false if ``max_size`` was given to the constructor, which
        prevents the list from growing beyond a fixed size.
        """
        return self.variant.ptr().dynamic()

    @property
    def frozen(self) -> bool:
        """Indicates whether the list's allocator is temporarily frozen for memory
        stability.

        This is only intended for debugging purposes.

        Returns
        -------
        bool
            ``True`` if the memory addresses of the list's nodes are guaranteed to
            remain stable within the current context, or ``False`` if they may be
            reallocated.

        Notes
        -----
        Due to the nature of linked data structures, traversal over the list requires
        knowing the exact memory address of each node.  However, some allocation
        strategies can result in the movement of nodes during resize/defragment calls,
        which invalidates any existing pointers/iterators over the list.  This presents
        a problem for any code that needs to modify a list while iterating over it.

        Thankfully, this can be avoided by temporarily freezing the allocator, which
        can be done through the :meth:`reserve() <LinkedList.reserve>` method and
        associated context manager.  This will prevent any reallocation from occurring
        until the context is exited, which guarantees that the memory addresses of the
        list's nodes will remain stable for the duration.  The
        :attr:`frozen <LinkedList.frozen>` property indicates whether this is the case
        within the current context.

        Generally speaking, users should never need to worry about this.  The built-in
        algorithms are designed to be safe at all times, and will automatically reserve
        memory in the optimal way for each operation.  However, if you are writing
        custom algorithms that need to keep track of raw node pointers, then you may
        need to use this flag to ensure that your pointers remain valid.
        """
        return self.variant.ptr().frozen()

    @property
    def nbytes(self) -> int:
        """The total memory consumption of the list in bytes.

        Returns
        -------
        int
            The total number of bytes consumed by the list, including all its
            nodes (but not their values).
        """
        return self.variant.ptr().nbytes()

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
        cdef PyObject* spec = self.variant.ptr().specialization()
        if spec is NULL:
            return None

        return <object>spec

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
        error will be raised.  The list is not modified during this process.

        .. note::

            Typed lists are slightly slower at appending items due to the extra
            type check.  Otherwise, they have identical performance to their
            untyped equivalents.
        """
        if spec is None:
            self.variant.ptr().specialize(<PyObject*>NULL)
        else:
            self.variant.ptr().specialize(<PyObject*>spec)

    def __class_getitem__(cls, key: object) -> type:
        """Subscribe a :class:`LinkedList` to a particular type specialization.

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

        def __init__(
            self,
            items: Iterable[object] | None = None,
            max_size: int = None,
            reverse: bool = False,
            singly_linked: bool = False,
        ) -> None:
            """Disable the `spec` argument for strictly-typed lists."""
            cls.__init__(
                self,
                items,
                max_size=max_size,
                reverse=reverse,
                spec=key,
                singly_linked=singly_linked,
            )

        def specialize(self, spec: object) -> None:
            """Disable runtime specialization for strictly-typed lists."""
            raise TypeError(f"{cls.__name__} is already specialized to {repr(key)}")

        # dynamically create the new class
        return type(
            cls.__name__,
            (cls,),
            {"__init__": __init__, "specialize": specialize}
        )

    def lock(self) -> object:
        """Generate a context manager that temporarily locks a list for use in a
        multithreaded environment.

        Returns
        -------
        ThreadGuard
            A Python-style context manager that acquires an internal mutex upon entering
            a context block and releases it after exiting.  Any operations within the
            context block are guaranteed to be atomic.

        Notes
        -----
        By default, :class:`LinkedList`-based data structures are not considered to be
        thread-safe.  Instead, they are optimized for maximum single-threaded
        performance, and do not introduce any more overhead than is necessary.

        Examples
        --------
        This method allows users to choose when and where to enforce thread-safety for
        their specific use case.

        .. doctest::

            >>> l = LinkedList("abcdef")
            >>> with l.lock():
            >>>     # anything inside the context block is guaranteed to be atomic
            >>>     l.append("x")
            >>> 
            >>> # anything outside the context block is not
            >>> l.append("y")
        """
        return <object>(self.variant.ptr().lock())


###################################
####    RELATIVE OPERATIONS    ####
###################################


cdef class RelativeProxy:


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
        self.variant.ptr().move_relative(
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
        self.variant.ptr().insert_relative(
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
        self.variant.ptr().extend_relative(
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
        self.variant.ptr().remove_relative(
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
        self.variant.ptr().discard_relative(
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
        return <object>self.variant.ptr().pop_relative(
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
        self.variant.ptr().clear_relative(
            <PyObject*>sentinel,
            <Py_ssize_t>offset,
            <Py_ssize_t>length
        )


