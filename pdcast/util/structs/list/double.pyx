# distutils: language = c++
"""This module contains a pure C/Cython implementation of a doubly-linked list.
"""
from typing import Any, Iterable, Iterator


####################
####    LIST    ####
####################


cdef class DoublyLinkedList(LinkedList):
    """A pure Cython implementation of a doubly-linked list data structure.

    This is a drop-in replacement for a standard Python
    :class:`list <python:list>` or :class:`deque <python:collections.deque>`,
    with comparable performance.

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
        before attempting to access this attribute.

    Notes
    -----
    This data structure behaves similarly to a
    :class:`collections.deque <python:collections.deque>` object, but is
    implemented as a standard doubly-linked list instead of a list of arrays.

    The list is implemented in pure C++ to maximize performance, which is
    generally on par with the standard library.  This implementation retains
    all the usual tradeoffs of linked lists vs arrays (e.g. O(n) indexing vs
    O(1) appends), but is highly optimized and reduces compromises as much as
    possible.

    .. warning::

        The list is not thread-safe.  If you need to use it in a multithreaded
        context, you should use a :class:`threading.Lock` to synchronize
        access.
    """

    def __cinit__(self):
        self.view = new ListView[DoubleNode]()

    def __dealloc__(self):
        del self.view

    ########################
    ####    CONCRETE    ####
    ########################

    cdef void _append(self, PyObject* item):
        """Add an item to the end of the list.

        Parameters
        ----------
        item : PyObject*
            The item to add to the list.

        Notes
        -----
        Appends are O(1) for both ends of the list.
        """
        append(self.view, item)  # from append.h

    cdef void _appendleft(self, PyObject* item):
        """Add an item to the beginning of the list.

        Parameters
        ----------
        item : PyObject*
            The item to add to the list.

        Notes
        -----
        Appends are O(1) for both ends of the list.
        
        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.
        """
        appendleft(self.view, item)  # from append.h

    cdef void _insert(self, PyObject* item, long index):
        """Insert an item at the specified index.

        Parameters
        ----------
        item : PyObject*
            The item to add to the list.
        index : long int
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
        # allow negative indexing + check bounds
        cdef size_t norm_index = normalize_index(index, self.view.size)

        # allocate new node
        cdef DoubleNode* node = self.view.allocate(item)
        cdef DoubleNode* curr
        cdef size_t i

        # insert node at specified index, starting from nearest end
        if norm_index <= self.view.size // 2:
            # iterate forwards from head
            curr = self.view.head
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

    cdef void _extend(self, PyObject* items):
        """Add multiple items to the end of the list.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of hashable items to add to the list.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.
        """
        extend(self.view, items)  # from extend.h

    cdef void _extendleft(self, PyObject* items):
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
        extendleft(self.view, items)  # from extend.h

    cdef size_t _index(self, PyObject* item, long start = 0, long stop = -1):
        """Get the index of an item within the list.

        Parameters
        ----------
        item : PyObject*
            The item to search for.

        Returns
        -------
        size_t
            The index of the item within the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.

        Notes
        -----
        Indexing is O(n) on average.
        """
        cdef DoubleNode* node = self.head
        cdef size_t index = 0
        cdef int comp

        # normalize start/stop indices
        cdef size_t norm_start = normalize_index(start, self.size)
        cdef size_t norm_stop = normalize_index(stop, self.size)

        # skip to `start`
        for index in range(norm_start):
            if node is NULL:  # hit end of list
                raise ValueError(f"{repr(<object>item)} is not in list")
            node = node.next

        # iterate until `stop`
        while node is not NULL and index < norm_stop:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(node.value, item, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()
    
            # return index if equal
            if comp == 1:
                return index

            # advance to next node
            node = node.next
            index += 1

        raise ValueError(f"{repr(<object>item)} is not in list")

    cdef size_t _count(
        self,
        PyObject* item,
        long long start = 0,
        long long stop = -1
    ):
        """Count the number of occurrences of an item in the list.

        Parameters
        ----------
        item : PyObject*
            The item to count.

        Returns
        -------
        size_t
            The number of occurrences of the item in the list.

        Notes
        -----
        Counting is O(n).
        """
        return count(self.view, item, start, stop)  # from count.h

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
        Removals are O(n) on average.
        """
        remove(self.view, item)  # from remove.h

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
        # allow negative indexing + check bounds
        cdef size_t norm_index = normalize_index(index, self.size)

        # get node at index
        cdef DoubleNode* node = node_at_index(
            norm_index,
            self.head,
            self.tail,
            self.size
        )
        cdef PyObject* value = node.value

        # we have to increment the reference counter of the popped object to
        # ensure it isn't garbage collected when we free the node.
        Py_INCREF(value)

        # drop node and return contents
        self._unlink_node(node)
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
        cdef DoubleNode* node = self.head
        cdef PyObject* value = node.value

        # we have to increment the reference counter of the popped object to
        # ensure it isn't garbage collected when we free the node.
        Py_INCREF(value)

        # drop node and return contents
        self._unlink_node(node)
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
        cdef DoubleNode* node = self.tail
        cdef PyObject* value = node.value

        # we have to increment the reference counter of the popped object to
        # ensure it isn't garbage collected when we free the node.
        Py_INCREF(value)

        # drop node and return contents
        self._unlink_node(node)
        free_node(node)
        return value

    cdef void _clear(self):
        """Remove all items from the list.

        Notes
        -----
        Clearing a list is O(n).
        """
        self.view.clear()  # from view.h

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
        sort(self.view, key, reverse)  # from sort.h

    cdef void _reverse(self):
        """Reverse the order of the list in-place.

        Notes
        -----
        Reversing a :class:`LinkedList` is O(n).
        """
        reverse(self.view)  # from reverse.h

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
        rotate(self.view, steps)  # from rotate.h

    cdef size_t _nbytes(self):
        """Get the total number of bytes used by the list."""
        import sys
        cdef size_t total = self.view.nbytes()  # from view.h
        total += sys.getsizeof(self)  # add size of Python wrapper
        return total

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
        cdef DoubleNode* curr = self.view.head

        while curr is not NULL:
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python
            curr = <DoubleNode*>curr.next

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
        cdef DoubleNode* curr = self.view.tail

        while curr is not NULL:
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python
            curr = <DoubleNode*>curr.prev

    def __getitem__(self, key: int | slice) -> Any:
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
        cdef LinkedList result
        cdef DoubleNode* curr
        cdef object start, stop, step  # kept at Python level
        cdef size_t index, end_index, abs_step, i
        cdef bint reverse

        # support slicing
        if isinstance(key, slice):
            # create a new LinkedList to hold the slice
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
                    # TODO: just copy the node and link it manually.  This
                    # avoids rehashing nodes in the case of HashNodes.
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
                    # TODO: just copy the node and link it manually.  This
                    # avoids rehashing nodes in the case of HashNodes.
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
        cdef DoubleNode* node
        cdef DoubleNode* curr
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
                    curr = allocate_double_node(<PyObject*>val)
                    self._link_node(NULL, curr, self.head)
                elif start == self.size:  # assignment at end of list
                    val = next(value_iter)
                    curr = allocate_double_node(<PyObject*>val)
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
                    node = allocate_double_node(<PyObject*>val)
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
                    # node = allocate_double_node(<PyObject*>val)
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
                    # node = allocate_double_node(<PyObject*>val)
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
        # node = allocate_double_node(<PyObject*>value)
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
        cdef DoubleNode* curr
        cdef object start, stop, step  # kept at Python level
        cdef size_t abs_step, small_step, index, end_index, i
        cdef DoubleNode* temp  # temporary node for deletion

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
            free_node(curr)

    def __contains__(self, item: Any) -> bool:
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
