"""This module contains a basic implementation of a doubly-linked list, which
can be subclassed to add additional functionality.
"""
from typing import Any, Iterable, Iterator

from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, free

from .base cimport DEBUG, raise_exception

cdef extern from "Python.h":
    int Py_EQ
    int PyObject_RichCompareBool(PyObject* obj1, PyObject* obj2, int opid)
    int Py_INCREF(PyObject* obj)
    int Py_DECREF(PyObject* obj)
    PyObject* PyObject_GetIter(PyObject* obj)
    PyObject* PyIter_Next(PyObject* obj)


# TODO: __getitem__ and __delitem__ are now correct for slices, but
# __setitem__ is not.
# -> impossible slices are handled differently for this method.

# If you assign an iterable into a slice of length 0, it will insert the
# values into the list at the specified index, extending it.

# p = list(range(10))
# p[5:5] = [15, 15, 15]
# print(p)  # [0, 1, 2, 3, 4, 15, 15, 15, 5, 6, 7, 8, 9]



# TODO: testing in general consists of comparing the output of this class's
# methods to the built-in `list` object.


# TODO: slice tester:

# r = lambda: random.randrange(-20, 20)

# def test():
#     start, stop, step = (r(), r(), r())
#     s1 = l[start:stop:step]
#     s2 = p[start:stop:step]
#     assert list(s1) == s2, f"{repr(s1)} != {s2}   <- {':'.join([str(x) for x in (start, stop, step)])}"

# there seems to be a bug with negative step sizes that are larger than the
# length of the list.



# TODO: rename ListNode -> DoubleNode to distinguish from future SingleNode


#######################
####    CLASSES    ####
#######################


cdef class DoublyLinkedList(LinkedList):
    """A pure Cython implementation of a doubly-linked list data structure.

    This is a drop-in replacement for a standard Python
    :class:`list <python:list>` or :class:`deque <python:collections.deque>`.

    Parameters
    ----------
    items : Iterable[Any], optional
        An iterable of items to initialize the list.

    Attributes
    ----------
    head : ListNode
        The first node in the list.  This is a pure C struct and is not
        normally accessible from Python.
    tail : ListNode
        The last node in the list.  This is a pure C struct and is not
        normally accessible from Python.

    Notes
    -----
    This structure behaves similarly to a
    :class:`collections.deque <python:collections.deque>` object, but is
    implemented as a doubly-linked list instead of a ring buffer.  It is
    implemented in pure Cython to maximize performance, and is not intended to
    be used directly from Python.  None of its attributes or methods (besides
    the constructor and special methods) are accessible from a non-Cython
    context.  If you want to use it from Python, you should first write a
    Cython wrapper that exposes the desired functionality.
    """

    def __cinit__(self):
        self.head = NULL
        self.tail = NULL

    def __dealloc__(self):
        self._clear()  # free all nodes

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
        cdef ListNode* node = self._allocate_node(item)

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
        cdef ListNode* node = self._allocate_node(item)

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
        cdef ListNode* node = self._allocate_node(item)
        cdef ListNode* curr
        cdef size_t i

        # insert node at specified index, starting from nearest end
        if norm_index <= self.size // 2:
            # iterate forwards from head
            curr = self.head
            for i in range(index):
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
        # C API equivalent of iter(items)
        cdef PyObject* iterator = PyObject_GetIter(items)
        if iterator is NULL:
            raise_exception()

        # NOTE: this is equivalent to
        #   for item in items:
        #       self._append(item)

        cdef PyObject* item

        # iterate over items
        while True:
            item = PyIter_Next(iterator)
            if item is NULL:  # end of iterator or error
                if PyErr_Occurred():  # release references and raise error
                    Py_DECREF(item)
                    Py_DECREF(iterator)
                    raise_exception()
                break

            self._append(item)  # append item to list
            Py_DECREF(item)  # release reference to item

        Py_DECREF(iterator)  # release reference to iterator

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
        # C API equivalent of iter(items)
        cdef PyObject* iterator = PyObject_GetIter(items)
        if iterator is NULL:
            raise_exception()

        # NOTE: this is equivalent to
        #   for item in items:
        #       self._appendleft(item)

        cdef PyObject* item

        # iterate over items
        while True:
            item = PyIter_Next(iterator)
            if item is NULL:  # end of iterator or error
                if PyErr_Occurred():  # release references and raise error
                    Py_DECREF(item)
                    Py_DECREF(iterator)
                    raise_exception()
                break

            self._appendleft(item)  # append item to list
            Py_DECREF(item)  # release reference to item

        Py_DECREF(iterator)  # release reference to iterator

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
        cdef ListNode* node = self.head
        cdef size_t index = 0
        cdef int comp

        # normalize start/stop indices
        cdef size_t norm_start = self._normalize_index(start)
        cdef size_t norm_stop = self._normalize_index(stop)

        # skip to `start`
        for index in range(norm_start):
            if node is NULL:  # hit end of list
                raise ValueError(f"{repr(item)} is not in list")
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

        raise ValueError(f"{repr(item)} is not in list")

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
        cdef ListNode* node = self.head
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
        cdef ListNode* node = self.head
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

        raise ValueError(f"{repr(item)} is not in list")

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
        cdef ListNode* node = self._node_at_index(norm_index)
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
        cdef ListNode* node = self.head
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
        cdef ListNode* node = self.tail
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
        Clearing a list is O(1).
        
        Due to the way Python's garbage collector works, we don't actually need
        to iterate over the list to free it.  The gc can automatically detect
        reference cycles and free them if the referenced objects cannot be
        reached from anywhere else in the program.
        """
        cdef ListNode* node = self.head
        cdef ListNode* temp

        # free all nodes
        while node is not NULL:
            temp = node.next
            free_node(node)
            node = temp

        # avoid dangling pointers
        self.head = NULL
        self.tail = NULL
        self.size = 0

    cdef void _sort(self):
        """Sort the list in-place.

        Notes
        -----
        Sorting is O(n log n) on average.
        
        This method uses an iterative merge sort algorithm that avoids the
        extra memory overhead required to handle recursive stack frames.
        """
        # trivial case: empty list
        if self.head is NULL:
            return

        # NOTE: as a refresher, the general merge sort algorithm is as follows:
        #   1) divide the list into sublists of length 1 (bottom-up)
        #   2) sort pairs of elements from left to right and merge
        #   3) double the length of the sublists and repeat step 2

        # NOTE: allocating `temp` outside of _merge() allows us to avoid
        # creating a new head every time we merge two sublists.
        cdef ListNode* curr
        cdef ListNode* tail
        cdef ListNode* left
        cdef ListNode* right
        cdef ListNode* sub_head
        cdef ListNode* sub_tail
        cdef size_t length = 1

        # allocate a temporary node to anchor sublists
        cdef ListNode* temp = <ListNode*>malloc(sizeof(ListNode))
        if temp is NULL:  # malloc() failed to allocate a new block
            raise MemoryError()

        # merge pairs of sublists of increasing size, starting at length 1
        while length < self.size:
            curr = self.head  # left to right

            # divide and conquer
            while curr:
                # split the linked list into two sublists of size `length`
                left = curr
                right = self._split(left, length)
                curr = self._split(right, length)

                # merge the two sublists, maintaining sorted order
                sub_head, sub_tail = self._merge(left, right, temp)

                # if this is our first merge, set the head of the new list
                if tail is NULL:
                    self.head = sub_head
                else:
                    # link the merged sublist to the previous one
                    tail.next = sub_head
                    sub_head.prev = tail

                # set tail of new list
                tail = sub_tail

            # double the length of the sublists for the next iteration
            length *= 2

        # clean up temporary node
        free(temp)

    cdef void _reverse(self):
        """Reverse the order of the list in-place.

        Notes
        -----
        Reversing a :class:`LinkedList` is O(n).
        """
        cdef ListNode* node = self.head

        # swap all prev and next pointers
        while node is not NULL:
            node.prev, node.next = node.next, node.prev
            node = node.prev  # next is now prev

        # swap head and tail
        self.head, self.tail = self.tail, self.head

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
        cdef ListNode* curr = self.head

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
        cdef ListNode* curr = self.tail

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
        cdef ListNode* curr
        cdef size_t start, stop, index, end_index, i  # for slicing
        cdef ssize_t step
        cdef LinkedList result  # to hold final slice
        cdef bint reverse

        # support slicing
        if isinstance(key, slice):
            # create a new LinkedList to hold the slice
            result = type(self)()

            # get bounds of slice
            start, stop, step = key.indices(self.size)
            if (start > stop and step > 0) or (start < stop and step < 0):
                return result  # Python returns an empty list in this case

            # determine direction of traversal to avoid backtracking
            index, end_index = self._get_slice_direction(start, stop, step)
            reverse = step < 0  # append to slice in reverse order
            step = abs(step)  # drop sign

            # get first node in slice, counting from nearest end
            curr = self._node_at_index(index)

            # forward traversal
            if end_index >= index:
                while curr is not NULL and index < end_index:
                    if reverse:
                        result.appendleft(curr.value)  # appendleft
                    else:
                        result.append(curr.value)  # append

                    # jump according to step size
                    for i in range(step):
                        if curr is NULL:
                            break
                        curr = curr.next

                    # increment index
                    index += step

            # backward traversal
            else:
                while curr is not NULL and index > end_index:
                    if reverse:
                        result.append(curr.value)  # append
                    else:
                        result.appendleft(curr.value)  # appendleft

                    # jump according to step size
                    for i in range(step):
                        if curr is NULL:
                            break
                        curr = curr.prev

                    # decrement index
                    index -= step

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
        cdef ListNode* curr
        cdef size_t start, stop, slice_size, index, end_index, i  # for slicing
        cdef ssize_t step
        cdef object value_iterator, val  # kept at Python level for simplicity

        # support slicing
        if isinstance(key, slice):
            # get indices of slice
            start, stop, step = key.indices(self.size)

            # check length of value matches length of slice
            slice_size = abs(stop - start) // abs(1 if step == 0 else abs(step))
            if not hasattr(value, "__iter__") or len(value) != slice_size:
                raise ValueError(
                    f"attempt to assign sequence of size {len(value)} to slice "
                    f"of size {slice_size}"
                )

            # determine direction of traversal to avoid backtracking
            index, end_index = self._get_slice_direction(start, stop, step)

            # get first node in slice, counting from nearest end
            curr = self._node_at_index(index)

            # forward traversal
            value_iterator = iter(value)
            if end_index >= index:
                for val in value_iterator:
                    if curr is NULL or index >= end_index:
                        break
                    Py_INCREF(<PyObject*>val)
                    Py_DECREF(curr.value)
                    curr.value = <PyObject*>val
                    for i in range(step):  # jump according to step size
                        if curr is NULL:
                            break
                        curr = curr.next
                    index += step  # increment index

            # backward traversal
            else:
                for val in reversed(list(value_iterator)):
                    if curr is NULL or index == end_index:
                        break
                    Py_INCREF(<PyObject*>val)
                    Py_DECREF(curr.value)
                    curr.value = <PyObject*>val
                    for i in range(step):  # jump according to step size
                        if curr is NULL:
                            break
                        curr = curr.prev
                    index -= step  # decrement index

        # index directly
        else:
            key = self._normalize_index(key)
            curr = self._node_at_index(key)
            Py_INCREF(<PyObject*>value)
            Py_DECREF(curr.value)
            curr.value = <PyObject*>value

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
        cdef ListNode* curr
        cdef size_t start, stop, adjusted_step, index, end_index, i  # for slicing
        cdef ssize_t step
        cdef ListNode* temp  # temporary node for deletion

        # support slicing
        if isinstance(key, slice):
            # get bounds of slice
            start, stop, step = key.indices(self.size)
            if (start > stop and step > 0) or (start < stop and step < 0):
                return  # Python does nothing in this case

            # determine direction of traversal to avoid backtracking
            index, end_index = self._get_slice_direction(start, stop, step)
            step = abs(step)  # drop sign
            adjusted_step = step - 1  # we implicitly advance by one at each step

            # get first node in slice, counting from nearest end
            curr = self._node_at_index(index)

            # forward traversal
            if end_index >= index:
                while curr is not NULL and index < end_index:
                    temp = curr
                    curr = curr.next
                    self._unlink_node(temp)
                    self._free_node(temp)
                    for i in range(adjusted_step):  # jump according to step size
                        if curr is NULL:
                            break
                        curr = curr.next
                    index += step  # index tracks with end_index to maintain condition

            # backward traversal
            else:
                while curr is not NULL and index > end_index:
                    temp = curr
                    curr = curr.prev
                    self._unlink_node(temp)
                    self._free_node(temp)
                    for i in range(adjusted_step):  # jump according to step size
                        if curr is NULL:
                            break
                        curr = curr.prev
                    index -= step  # index tracks with end_index to maintain condition

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
        cdef ListNode* curr
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

    cdef ListNode* _allocate_node(PyObject* value):
        """Allocate a new node and set its value.

        Parameters
        ----------
        value : PyObject*
            The value to set for the node.

        Returns
        -------
        ListNode*
            The newly allocated node.

        Notes
        -----
        This method handles the memory allocation and reference counting for
        each node, which can be tricky.
        """
        if DEBUG:
            print(f"    -> malloc: {<object>value}")

        # allocate node
        cdef ListNode* node = <ListNode*>malloc(sizeof(ListNode))
        if node is NULL:  # malloc() failed to allocate a new block
            raise MemoryError()

        # increment reference count of underlying Python object
        Py_INCREF(value)

        # initialize
        node.value = value
        node.next = NULL
        node.prev = NULL
        return node

    cdef void _free_node(self, ListNode* node):
        """Free a node and decrement the reference count of its value.

        Parameters
        ----------
        node : ListNode*
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

    cdef void _link_node(self, ListNode* prev, ListNode* curr, ListNode* next):
        """Add a node to the list.

        Parameters
        ----------
        prev : ListNode*
            The node that should precede the new node in the list.
        curr : ListNode*
            The node to add to the list.
        next : ListNode*
            The node that should follow the new node in the list.

        Notes
        -----
        This is a helper method for doing the pointer arithmetic of adding a
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

    cdef void _unlink_node(self, ListNode* curr):
        """Remove a node from the list.

        Parameters
        ----------
        curr : ListNode*
            The node to remove from the list.

        Notes
        -----
        This is a helper method for doing the pointer arithmetic of removing a
        node, as well as handling reference counts and freeing the underlying
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

    cdef ListNode* _node_at_index(self, size_t index):
        """Get the node at the specified index.

        Parameters
        ----------
        index : size_t
            The index of the node to retrieve.  This should always be passed
            through :meth:`LinkedList._normalize_index` first.

        Returns
        -------
        ListNode*
            The node at the specified index.

        Notes
        -----
        This method is O(n) on average.  As an optimization, it always iterates
        from the nearest end of the list.
        """
        cdef ListNode* curr
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
            The start index of the slice.
        stop : size_t
            The stop index of the slice.
        step : size_t
            The step size of the slice.

        Returns
        -------
        index : size_t
            The index at which to begin iterating.
        end_index : size_t
            The index at which to stop iterating.

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
        cdef size_t index, end_index

        # determine direction of traversal
        if (
            step > 0 and start <= self.size - stop or   # 1)
            step < 0 and self.size - start <= stop      # 4)
        ):
            index = start
            end_index = stop
        else:
            if step > 0:                                # 2)
                index = stop - 1
                end_index = start - 1
            else:                                       # 3)
                index = stop + 1
                end_index = start + 1

        # return as C tuple
        return (index, end_index)

    cdef ListNode* _split(self, ListNode* head, size_t length):
        """Split a linked list into sublists of the specified length.

        Parameters
        ----------
        head : ListNode*
            The head of the list to split.
        length : size_t
            The maximum length of each split.  This method will walk forward
            from ``head`` by this many nodes and then split the list.

        Returns
        -------
        ListNode*
            The head of the next sublist.

        Notes
        -----
        This method is O(length).  It just iterates forward ``length`` times
        and then splits the list at that point.
        """
        cdef ListNode* split
        cdef size_t i

        # walk `length` nodes forward from `head`
        for i in range(length - 1):
            if head is NULL:
                break
            head = head.next

        # if we've reached the end of the list, there's nothing to split
        if head is NULL:
            return NULL

        # otherwise, split the list
        split = head.next
        head.next = NULL
        if split is not NULL:
            split.prev = NULL
        return split

    cdef (ListNode*, ListNode*) _merge(
        self,
        ListNode* left,
        ListNode* right,
        ListNode* temp
    ):
        """Merge two sorted linked lists into a single sorted list.

        Parameters
        ----------
        left : ListNode*
            The head of the first sorted list.
        right : ListNode*
            The head of the second sorted list.
        temp : ListNode*
            A temporary node to use as the head of the merged list.  As an
            optimization, this is allocated once and then passed as a parameter
            rather than creating a new one every time this method is called.

        Returns
        -------
        head : ListNode*
            The head of the merged list.
        tail : ListNode*
            The tail of the merged list.

        Notes
        -----
        This is a standard implementation of the divide-and-conquer merge
        algorithm.  It is O(l) where `l` is the length of the longer list.
        """
        cdef ListNode* curr = temp
        cdef ListNode* tail

        # iterate through sublists until one is empty
        while left and right:
            # only append the smaller of the two nodes
            if left.value < right.value:
                curr.next = left
                left.prev = curr
                left = left.next
            else:
                curr.next = right
                right.prev = curr
                right = right.next

            # advance to next node
            curr = curr.next

        # append the remaining nodes
        tail = left if right is NULL else right
        curr.next = tail
        tail.prev = curr

        # advance tail to end of merged list
        while tail.next is not NULL:
            tail = tail.next

        # unlink temporary head
        curr = temp.next
        curr.prev = NULL
        temp.next = NULL

        # return the proper head and tail of the merged list
        return (curr, tail)  # remove temporary head
