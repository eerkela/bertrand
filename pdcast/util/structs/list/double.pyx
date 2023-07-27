"""This module contains a pure C/Cython implementation of a doubly-linked list.
"""
from typing import Any, Iterable, Iterator

from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, free

from .base cimport (
    DEBUG, DoubleNode, Pair, raise_exception, Py_INCREF, Py_DECREF, PyErr_Occurred,
    Py_EQ, PyObject_RichCompareBool, PyObject_GetIter, PyIter_Next
)
from .sort cimport (
    KeyedDoubleNode, SortError, merge_sort, decorate_double, undecorate_double
)


# TODO: free_node, link_node, unlink_node, stage_nodes can all be templated
# using fused types


# TODO: index helpers should be templated using fused types.
# -> index.pyx

# extract_slice(head, tail, slice)
# set_slice(head, tail, slice, value)
# delete_slice(head, tail, slice)
# get_slice_direction(start, stop, step)
# normalize_index(index, size)
# node_at_index(index, head, tail, size)


#######################
####    CLASSES    ####
#######################


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
    head : DoubleNode
        The first node in the list.  This is a pure C struct and is not
        normally accessible from Python.
    tail : DoubleNode
        The last node in the list, with the same restrictions as ``head``.

    Notes
    -----
    This structure behaves similarly to a
    :class:`collections.deque <python:collections.deque>` object, but is
    implemented as a standard list instead of a list of arrays.

    It is implemented in pure Cython to maximize performance, and generally
    performs on par with the built-in :class:`list <python:list>` type.
    """

    def __cinit__(self):
        self.head = NULL
        self.tail = NULL

    def __dealloc__(self):
        self._clear()  # free all nodes and avoid dangling pointers

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
        cdef DoubleNode* node = self._allocate_node(item)

        # append to end of list
        self._link_node(self.tail, node, NULL)

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
        cdef DoubleNode* node = self._allocate_node(item)

        # append to beginning of list
        self._link_node(NULL, node, self.head)

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
        cdef size_t norm_index = self._normalize_index(index)

        # allocate new node
        cdef DoubleNode* node = self._allocate_node(item)
        cdef DoubleNode* curr
        cdef size_t i

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
        cdef DoubleNode* staged_head
        cdef DoubleNode* staged_tail
        cdef size_t count

        # NOTE: we stage the items in a temporary list to ensure we don't
        # modify the original if we encounter any errors
        staged_head, staged_tail, count = self._stage_nodes(items, False)
        if staged_head is NULL:
            return

        # append staged items to end of list
        self.size += count
        if self.tail is NULL:
            self.head = staged_head
            self.tail = staged_tail
        else:
            self.tail.next = staged_head
            staged_head.prev = self.tail
            self.tail = staged_tail

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
        cdef DoubleNode* staged_head
        cdef DoubleNode* staged_tail
        cdef size_t count

        # NOTE: we stage the items in a temporary list to ensure we don't
        # modify the original if we encounter any errors
        staged_head, staged_tail, count = self._stage_nodes(items, True)
        if staged_head is NULL:
            return

        # append staged items to beginning of list
        self.size += count
        if self.head is NULL:
            self.head = staged_head
            self.tail = staged_tail
        else:
            self.head.prev = staged_tail
            staged_tail.next = self.head
            self.head = staged_head

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
        cdef size_t norm_start = self._normalize_index(start)
        cdef size_t norm_stop = self._normalize_index(stop)

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

    cdef size_t _count(self, PyObject* item):
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
        cdef DoubleNode* node = self.head
        cdef size_t count = 0
        cdef int comp

        # we iterate entirely at the C level for maximum performance
        while node is not NULL:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(node.value, item, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # increment count if equal
            if comp == 1:
                count += 1

            # advance to next node
            node = node.next

        return count

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
        cdef DoubleNode* node = self.head
        cdef int comp

        # remove first node that matches item
        while node is not NULL:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(node.value, item, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # remove if equal
            if comp == 1:
                self._unlink_node(node)
                self._free_node(node)
                return

            # advance to next node
            node = node.next

        raise ValueError(f"{repr(<object>item)} is not in list")

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
        cdef size_t norm_index = self._normalize_index(index)

        # get node at index
        cdef DoubleNode* node = self._node_at_index(norm_index)
        cdef PyObject* value = node.value

        # we have to increment the reference counter of the popped object to
        # ensure it isn't garbage collected when we free the node.
        Py_INCREF(value)

        # drop node and return contents
        self._unlink_node(node)
        self._free_node(node)
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
        self._free_node(node)
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
        self._free_node(node)
        return value

    cdef void _clear(self):
        """Remove all items from the list.

        Notes
        -----
        Clearing a list is O(n).
        """
        cdef DoubleNode* node = self.head
        cdef DoubleNode* temp

        # free all nodes
        while node is not NULL:
            temp = node
            node = node.next
            self._free_node(temp)

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

        cdef KeyedDoubleNode* decorated_head
        cdef KeyedDoubleNode* decorated_tail
        cdef Pair* pair
        cdef SortError sort_err

        # if a key func is given, decorate the list and sort by key
        if key is not NULL:
            decorated_head, decorated_tail = decorate_double(
                self.head, self.tail, key
            )
            try:
                pair = merge_sort(decorated_head, decorated_tail, self.size, reverse)
                self.head, self.tail = undecorate_double(<KeyedDoubleNode*>pair.first)
                free(pair)
            except SortError as err:
                # NOTE: no cleanup necessary for decorated sort
                sort_err = <SortError>err
                raise sort_err.original
            return

        # otherwise, sort the list directly
        try:
            pair = merge_sort(self.head, self.tail, self.size, reverse)
            self.head = <DoubleNode*>pair.first
            self.tail = <DoubleNode*>pair.second
            free(pair)
        except SortError as err:
            # NOTE: we have to manually reassign the head and tail of the list
            # to avoid memory leaks.
            sort_err = <SortError>err
            self.head = <DoubleNode*>sort_err.head
            self.tail = <DoubleNode*>sort_err.tail
            raise sort_err.original  # raise original exception

    cdef void _reverse(self):
        """Reverse the order of the list in-place.

        Notes
        -----
        Reversing a :class:`LinkedList` is O(n).
        """
        cdef DoubleNode* node = self.head

        # swap all prev and next pointers
        while node is not NULL:
            node.prev, node.next = node.next, node.prev
            node = node.prev  # next is now prev

        # swap head and tail
        self.head, self.tail = self.tail, self.head

    cdef size_t _nbytes(self):
        """Get the total number of bytes used by the list."""
        return sizeof(self) + self.size * sizeof(DoubleNode)

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
        cdef DoubleNode* curr = self.head

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
        cdef DoubleNode* curr = self.tail

        while curr is not NULL:
            Py_INCREF(curr.value)
            yield <object>curr.value  # this returns ownership to Python
            curr = curr.prev

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
            index, end_index = self._get_slice_direction(start, stop, step)
            reverse = step < 0  # append to slice in reverse order
            abs_step = abs(step)

            # get first node in slice, counting from nearest end
            curr = self._node_at_index(index)

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
        key = self._normalize_index(key)
        curr = self._node_at_index(key)
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
                    curr = self._allocate_node(<PyObject*>val)
                    self._link_node(NULL, curr, self.head)
                elif start == self.size:  # assignment at end of list
                    val = next(value_iter)
                    curr = self._allocate_node(<PyObject*>val)
                    self._link_node(self.tail, curr, NULL)
                else:  # assignment in middle of list
                    curr = self._node_at_index(start - 1)

                # insert all values at current index
                for val in value_iter:
                    node = self._allocate_node(<PyObject*>val)
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
            index, end_index = self._get_slice_direction(start, stop, step)

            # get first node in slice, counting from nearest end
            curr = self._node_at_index(index)

            # forward traversal
            if index <= end_index:
                if step < 0:
                    value_iter = reversed(value)
                while curr is not NULL and index <= end_index:
                    val = next(value_iter)
                    Py_INCREF(<PyObject*>val)
                    Py_DECREF(curr.value)
                    curr.value = <PyObject*>val
                    # node = self._allocate_node(<PyObject*>val)
                    # self._link_node(curr.prev, node, curr.next)
                    # self._free_node(curr)
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
                    # node = self._allocate_node(<PyObject*>val)
                    # self._link_node(curr.prev, node, curr.next)
                    # self._free_node(curr)
                    # curr = node

                    # jump according to step size
                    index -= abs_step  # decrement index
                    for i in range(abs_step):
                        curr = curr.prev
                        if curr is NULL:
                            break

            return

        # index directly
        key = self._normalize_index(key)
        curr = self._node_at_index(key)
        Py_INCREF(<PyObject*>value)
        Py_DECREF(curr.value)
        curr.value = <PyObject*>value
        # node = self._allocate_node(<PyObject*>value)
        # self._link_node(curr.prev, node, curr.next)
        # self._free_node(curr)

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
            index, end_index = self._get_slice_direction(start, stop, step)
            abs_step = abs(step)
            small_step = abs_step - 1  # we implicitly advance by one at each step

            # get first node in slice, counting from nearest end
            curr = self._node_at_index(index)

            # forward traversal
            if index <= end_index:
                while curr is not NULL and index <= end_index:
                    temp = curr
                    curr = curr.next
                    self._unlink_node(temp)
                    self._free_node(temp)

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
                    self._free_node(temp)

                    # jump according to step size
                    index -= abs_step  # tracks with end_index to maintain condition
                    for i in range(small_step):
                        curr = curr.prev
                        if curr is NULL:
                            break

        # index directly
        else:
            key = self._normalize_index(key)
            curr = self._node_at_index(key)
            self._unlink_node(curr)
            self._free_node(curr)

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
        cdef DoubleNode* curr = self.head
        cdef PyObject* borrowed = <PyObject*>item  # borrowed reference

        # we iterate entirely at the C level for maximum performance
        while curr is not NULL:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(curr.value, borrowed, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # remove node if equal
            if comp == 1:
                return True

            # advance to next node
            curr = curr.next

        return False

    #######################
    ####    PRIVATE    ####
    #######################

    cdef DoubleNode* _allocate_node(self, PyObject* value):
        """Allocate a new node and set its value.

        Parameters
        ----------
        value : PyObject*
            The value to set for the node.

        Returns
        -------
        DoubleNode*
            The newly allocated node.

        Notes
        -----
        This method handles the memory allocation and reference counting for
        each node, which can be tricky.  It should always be followed up with a
        call to :meth:`_link_node()` to add the node to the list.
        """
        if DEBUG:
            print(f"    -> malloc: {<object>value}")

        # allocate node
        cdef DoubleNode* node = <DoubleNode*>malloc(sizeof(DoubleNode))
        if node is NULL:  # malloc() failed to allocate a new block
            raise MemoryError()

        # increment reference count of underlying Python object
        Py_INCREF(value)

        # initialize
        node.value = value
        node.next = NULL
        node.prev = NULL
        return node

    cdef void _free_node(self, DoubleNode* node):
        """Free a node and decrement the reference count of its value.

        Parameters
        ----------
        node : DoubleNode*
            The node to free.

        Notes
        -----
        The node must be unlinked from the list before calling this method.
        Any remaining references to it will become dangling pointers.
        """
        if DEBUG:
            print(f"    -> free: {<object>node.value}")

        # nullify pointers
        node.next = NULL
        node.prev = NULL

        Py_DECREF(node.value)  # decrement refcount of underlying Python object
        free(node)  # free node

    cdef void _link_node(self, DoubleNode* prev, DoubleNode* curr, DoubleNode* next):
        """Add a node to the list.

        Parameters
        ----------
        prev : DoubleNode*
            The node that should precede the new node in the list.
        curr : DoubleNode*
            The node to add to the list.
        next : DoubleNode*
            The node that should follow the new node in the list.

        Notes
        -----
        This is a helper method for doing the pointer manipulations of adding a
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

    cdef void _unlink_node(self, DoubleNode* curr):
        """Remove a node from the list.

        Parameters
        ----------
        curr : DoubleNode*
            The node to remove from the list.

        Notes
        -----
        This is a helper method for doing the pointer manipulations of removing
        a node, as well as handling reference counts and freeing the underlying
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

    cdef (DoubleNode*, DoubleNode*, size_t) _stage_nodes(
        self, PyObject* items, bint reverse
    ):
        """Stage a sequence of nodes for insertion into the list.

        Parameters
        ----------
        items : PyObject*
            An iterable of items to insert into the list.
        reverse : bool
            Indicates whether to reverse the order of the items during staging.

        Returns
        -------
        head : DoubleNode*
            The head of the staged list (or NULL if no values are staged).
        tail : DoubleNode*
            The tail of the staged list (or NULL if no values are staged).
        count : size_t
            The number of nodes in the staged list.
        """
        # C API equivalent of iter(items)
        cdef PyObject* iterator = PyObject_GetIter(items)  # generates a reference
        if iterator is NULL:
            raise_exception()

        # NOTE: we stage the items in a temporary list to ensure we don't
        # modify the original if we encounter any errors
        cdef DoubleNode* staged_head = NULL
        cdef DoubleNode* staged_tail = NULL
        cdef DoubleNode* node
        cdef PyObject* item
        cdef size_t count = 0

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
            node = self._allocate_node(item)

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

            count += 1  # increment count
            Py_DECREF(item)  # release reference on item

        Py_DECREF(iterator)  # release reference on iterator
        return (staged_head, staged_tail, count)

    #############################
    ####    INDEX HELPERS    ####
    #############################

    cdef DoubleNode* _node_at_index(self, size_t index):
        """Get the node at the specified index.

        Parameters
        ----------
        index : size_t
            The index of the node to retrieve.  This should always be passed
            through :meth:`LinkedList._normalize_index` first.

        Returns
        -------
        DoubleNode*
            The node at the specified index.

        Notes
        -----
        This method is O(n) on average.  As an optimization, it always iterates
        from the nearest end of the list.
        """
        cdef DoubleNode* curr
        cdef size_t i

        # count forwards from head
        if index <= self.size // 2:
            curr = self.head
            for i in range(index):
                curr = curr.next

        # count backwards from tail
        else:
            curr = self.tail
            for i in range(self.size - index - 1):
                curr = curr.prev

        return curr

    cdef (size_t, size_t) _get_slice_direction(
        self,
        size_t start,
        size_t stop,
        ssize_t step,
    ):
        """Determine the direction in which to traverse a slice so as to
        minimize total iterations.

        Parameters
        ----------
        start : size_t
            The start index of the slice (inclusive).
        stop : size_t
            The stop index of the slice (inclusive).
        step : ssize_t
            The step size of the slice.

        Returns
        -------
        index : size_t
            The index at which to begin iterating (inclusive).
        end_index : size_t
            The index at which to stop iterating (inclusive).

        Notes
        -----
        Slicing is optimized to always begin iterating from the end nearest to
        a slice boundary, and to never backtrack.  This is done by checking
        whether the slice is ascending (step > 0) or descending, and whether
        the start or stop index is closer to its respective end.  This gives
        the following cases:

            1) slice is ascending, `start` closer to head than `stop` is to tail
                -> iterate forwards from head to `stop`
            2) slice is ascending, `stop` closer to tail than `start` is to head
                -> iterate backwards from tail to `start`
            3) slice is descending, `start` closer to tail than `stop` is to head
                -> iterate backwards from tail to `stop`
            4) slice is descending, `stop` closer to head than `start` is to tail
                -> iterate forwards from head to `start`

        The final direction of traversal is determined by comparing the
        indices returned by this method.  If ``end_index >= index``, then the
        slice should be traversed in the forward direction, starting from
        ``index``.  Otherwise, it should be iterated over in reverse to avoid
        backtracking, again starting from ``index``.
        """
        cdef size_t distance_from_head, distance_from_tail, index, end_index

        # determine direction of traversal
        if step > 0:
            distance_from_head = start
            distance_from_tail = self.size - stop
            if distance_from_head <= distance_from_tail:    # 1) start from head
                index = start
                end_index = stop
            else:                                           # 2) start from tail
                index = stop
                end_index = start
        else:
            distance_from_head = stop
            distance_from_tail = self.size - start
            if distance_from_tail <= distance_from_head:    # 3) start from tail 
                index = start
                end_index = stop
            else:                                           # 4) start from head
                index = stop
                end_index = start

        # return as C tuple
        return (index, end_index)


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
