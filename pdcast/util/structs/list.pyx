"""This module contains a basic implementation of a doubly-linked list, which
can be subclassed to add additional functionality.
"""
from typing import Any, Hashable, Iterable, Iterator

from cython cimport freelist
from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, free


cdef extern from "Python.h":
    void Py_INCREF(PyObject* obj) nogil
    void Py_DECREF(PyObject* obj) nogil



# TODO: nodes that are created from node_from_struct() have different reference
# counting semantics compared to those created through the ListNode() constructor.
# They increment the refcount of the underlying struct rather than taking ownership
# directly.  This means you can delete the ListNode without freeing the underlying
# memory, which is what we expect for interior nodes.  The difficulty comes when we
# delete the head or tail, which should directly own their structs.  In that case,
# we need to figure out a more efficient way to handle the memory management.


# In its current form, get_list() produces a list where every node except the
# tail has a refcount of 1.  This means that if we delete the tail, nothing
# happens.  However, if we delete the head, we automatically free its memory
# and orphan the other nodes.


# One way to solve this might be to set an ``owner`` attribute on each node
# which tells the struct to decrement its refcount twice when it's deallocated.




# Eventually, when we move on to implementing a HashedList, we'll need to
# figure out how to map objects to their respective structs.  One way of doing
# this is to use a std::unordered_map from python hashes to ListStructs.

# -> hash collision is handled by chaining, so we can just use a standard
# linked list to store the collisions, or a std::vector.

# -> we can hash by calling the `PyObject_Hash()` C function, and can compare
# objects using the `PyObject_RichCompareBool()` C function.  Both can raise
# errors, so we need to check its output for -1.






# _merge should return a C tuple of node structs rather than a Python tuple
# of ListNode objects.  This avoids unnecessary overhead from Python
# object interaction.




# TODO: implement class_getitem for mypy hints, just like list[].
# -> in the case of HashedList, this could check if the contained type is
# a subclass of Hashable, and if not, raise an error.


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



# DEBUG = TRUE adds print statements for memory allocation/deallocation to help
# identify memory leaks.
cdef bint DEBUG = True



def get_list(n):
    head = ListNode(1)
    curr = head
    for i in range(2, n + 1):
        curr.next = ListNode(i)
        curr = curr.next

    return head




def iter_linked(head):
    curr = head
    while curr is not None:
        curr = curr.next


def iter_python(pylist):
    for _ in pylist:
        pass


cpdef void fast_iter(ListNode head):
    cdef ListStruct* curr = head.c_struct

    while curr is not NULL:
        curr = curr.next


def benchmark():
    from timeit import timeit

    head = get_list(10**3)
    pylist = list(range(10**3))

    return {
        "linked list (python)": timeit(lambda: iter_linked(head), number=10**4),
        "python list": timeit(lambda: iter_python(pylist), number=10**4),
        "linked list (cython)": timeit(lambda: fast_iter(head), number=10**4),
    }





cdef inline ListStruct* allocate_struct(PyObject* value):
    """Allocate a new struct to hold the referenced object.

    Parameters
    ----------
    value : PyObject*
        A reference to a Python object to use as the value of the returned
        struct.  This method handles incrementing reference counters for the
        object.

    Returns
    -------
    ListStruct*
        A reference to the allocated struct.

    Raises
    ------
    MemoryError
        If ``malloc()`` fails to allocate a new block to hold the struct.  This
        only happens when the system runs out of memory.

    Notes
    -----
    Every :class:`ListStruct` is allocated with a reference count of 1,
    indicating that it owns the underlying Python object.  The struct will not
    be freed until this counter reaches 0, at which point it will decrement the
    refcount of the underlying object and allow it to be garbage collected.

    A :class:`ListStruct`'s reference counter is incremented whenever a
    :class:`ListNode` is constructed around it and decremented whenever one is
    destroyed.  This is net neutral, and will never cause the struct to be
    freed on its own.  However, if a struct is removed from a list, then its
    reference counter will be decremented manually, and it will be freed when
    the counter reaches 0.  This might not occur immediately, but as soon as
    the last :class:`ListNode` that references the struct is collected by the
    ordinary Python garbage collector, then the underlying struct will be freed
    along with any other orphaned nodes that are connected to it.  Whenever
    this occurs, the refcount of the referenced object(s) will also be
    decremented, allowing them to be garbage collected in turn.
    """
    if DEBUG:
        print(f"    -> malloc: {<object>value}")

    # allocate a new struct
    cdef ListStruct* c_struct = <ListStruct*>malloc(sizeof(ListStruct))
    if c_struct is NULL:  # malloc() failed to allocate new block
        raise MemoryError()

    # handle refcounters
    Py_INCREF(value)
    c_struct.ref_count = 1

    # initialize struct
    c_struct.value = value
    c_struct.next = NULL
    c_struct.prev = NULL

    # return reference to new struct
    return c_struct


cdef inline void incref(ListStruct* c_struct):
    """Increment a struct's reference counter, as well as that of the
    underlying Python object.

    Parameters
    ----------
    c_struct : ListStruct*
        A pointer to the struct to increment.

    Notes
    -----
    Every :class:`ListStruct` is allocated with a reference count of 1,
    indicating that it owns the underlying Python object.  The struct will not
    be freed until this counter reaches 0, at which point it will decrement the
    refcount of the underlying object and allow it to be garbage collected.

    A :class:`ListStruct`'s reference counter is incremented whenever a
    :class:`ListNode` is constructed around it and decremented whenever one is
    destroyed.  This is net neutral, and will never cause the struct to be
    freed on its own.  However, if a struct is removed from a list, then its
    reference counter will be decremented manually, and it will be freed when
    the counter reaches 0.  This might not occur immediately, but as soon as
    the last :class:`ListNode` that references the struct is collected by the
    ordinary Python garbage collector, then the underlying struct will be freed
    along with any other orphaned nodes that are connected to it.  Whenever
    this occurs, the refcount of the referenced object(s) will also be
    decremented, allowing them to be garbage collected in turn.
    """
    if DEBUG:
        print(f"incref: {<object>c_struct.value}")

    Py_INCREF(c_struct.value)  # underlying Python object refcount
    c_struct.ref_count += 1  # internal struct refcount


cdef inline void decref(ListStruct* c_struct):
    """Decrement a struct's reference counter, as well as that of the
    underlying Python object.

    If the counter reaches zero, the struct is freed.

    Parameters
    ----------
    c_struct : ListStruct*
        A pointer to the struct to decrement.

    Notes
    -----
    Every :class:`ListStruct` is allocated with a reference count of 1,
    indicating that it owns the underlying Python object.  The struct will not
    be freed until this counter reaches 0, at which point it will decrement the
    refcount of the underlying object and allow it to be garbage collected.

    A :class:`ListStruct`'s reference counter is incremented whenever a
    :class:`ListNode` is constructed around it and decremented whenever one is
    destroyed.  This is net neutral, and will never cause the struct to be
    freed on its own.  However, if a struct is removed from a list, then its
    reference counter will be decremented manually, and it will be freed when
    the counter reaches 0.  This might not occur immediately, but as soon as
    the last :class:`ListNode` that references the struct is collected by the
    ordinary Python garbage collector, then the underlying struct will be freed
    along with any other orphaned nodes that are connected to it.  Whenever
    this occurs, the refcount of the referenced object(s) will also be
    decremented, allowing them to be garbage collected in turn.
    """
    if DEBUG:
        print(f"decref: {<object>c_struct.value}")

    # decrement internal reference counter
    c_struct.ref_count -= 1

    # early return if not free()-able
    if c_struct.ref_count != 0:
        return

    # free() struct and decrement Python refcount
    cdef ListStruct* forward = c_struct.next
    cdef ListStruct* backward = c_struct.prev
    cdef ListStruct* temp

    if DEBUG:
        print(f"    -> free: {<object>c_struct.value}")

    # nullify references to avoid dangling pointers
    if c_struct.next is not NULL:
        c_struct.next.prev = NULL
        c_struct.next = NULL
    if c_struct.prev is not NULL:
        c_struct.prev.next = NULL
        c_struct.prev = NULL

    Py_DECREF(c_struct.value)  # decrement Python refcount
    free(c_struct)  # free struct

    # NOTE: Whenever we remove a struct, we implicitly remove a reference to
    # both of its neighbors.  This can lead to memory leaks if we remove a node
    # from the middle of the list, since the neighbors will become inaccessible
    # and cannot be freed.  To solve this, we emit a wave that propagates
    # outwards from the removed node in both directions, searching for a node
    # with refcount > 1 in that direction.  If one is found, we preserve all
    # the nodes in that direction, as they are still reachable via the
    # referenced node.  Otherwise, we can safely free them, as they would
    # become orphaned by the removal.

    cdef bint delete_forward = True
    cdef bint delete_backward = True

    # search forward
    temp = forward
    while temp is not NULL:
        if temp.ref_count > 1:
            delete_forward = False
            break
        temp = temp.next

    # search backward
    temp = backward
    while temp is not NULL:
        if temp.ref_count > 1:
            delete_backward = False
            break
        temp = temp.prev

    # delete orphaned nodes in the forward direction
    if delete_forward:
        while forward is not NULL:
            # remember next node
            temp = forward.next

            if DEBUG:
                print(f"    -> free: {<object>forward.value}")

            # NOTE: we don't need to nullify pointers here since we're
            # deleting all the way to the end of the list.

            # free struct
            Py_DECREF(forward.value)
            free(forward)

            # advance to next node
            forward = temp

    # delete orphaned nodes in the backward direction
    if delete_backward:
        while backward is not NULL:
            # remember next node
            temp = backward.prev

            if DEBUG:
                print(f"    -> free: {<object>backward.value}")

            # NOTE: we don't need to nullify pointers here since we're
            # deleting all the way to the end of the list.

            # free struct
            Py_DECREF(backward.value)
            free(backward)

            # advance to next node
            backward = temp


cdef inline ListNode node_from_struct(ListStruct* c_struct):
    """Factory function to create a :class:`ListNode` from an existing
    ``ListStruct*`` pointer.

    Parameters
    ----------
    c_struct : ListStruct*
        A pointer to the underlying C struct.  This method will handle
        incrementing reference counters for the struct and its contents.

    Returns
    -------
    ListNode
        A new :class:`ListNode` object that wraps the specified struct.

    Notes
    -----
    This function is used to construct a :class:`ListNode` around an existing
    struct.  This is necessary because the structs themselves are implemented
    in pure C and are not normally exposed to Python.  To address this, we
    automatically construct a new wrapper every time we access a node's
    :attr:`next <ListNode.next>` and/or :attr:`prev <ListNode.prev>`
    attributes, allowing us to interact with the list normally at the Python
    level.
    
    This makes the C implementation virtually transparent to the user while
    simultaneously providing the performance benefits of a low level language
    like C.  All the ordinary :class:`LinkedList` methods are thus free to
    operate on the underlying structs directly, without sacrificing the
    flexibility and convenience of a native Python interface.
    """
    # NOTE: using __new__ + __cinit__ bypasses __init__ entirely
    cdef ListNode node = ListNode.__new__(ListNode)

    incref(c_struct)  # increment refcount of struct + underlying PyObject
    node.c_struct = c_struct  # point to existing struct
    return node


@freelist(256)
cdef class ListNode:
    """A node containing an individual element of a LinkedList.

    Parameters
    ----------
    value : object
        The object to store in the node.

    Attributes
    ----------
    struct : ListStruct*
        A pointer to the underlying C struct.  This is not exposed to Python,
        and can only be accessed from Cython.

    Notes
    -----
    In order to optimize performance as much as possible, :class:`LinkedLists`
    don't actually store full Python objects in their nodes, even if they are
    implemented in Cython.  Instead, each node is implemented as a pure C
    struct that is packed in memory as a contiguous block.  These structs
    contain all the information necessary to form the list, including pointers
    to the next and previous nodes, as well as a ``PyObject*`` pointer to the
    actual value being stored.  This makes the list extremely efficient, but
    also means the nodes themselves are inaccessible to Python code.

    This class solves that problem by creating a thin wrapper around a struct,
    which temporarily exposes its attributes to Python.  This allows us to
    manipulate the list just like normal, even though the nodes themselves are
    pure C.  The wrapper also handles reference counting for the struct and its
    contents, ensuring that memory is freed whenever a struct is orphaned or
    destroyed.
    """

    def __init__(self, object value):
        # NOTE: node_from_struct() uses a combination of `__cinit__()` and
        # `__new__()` to bypass this method and speed up instantiation.
        if DEBUG:
            print(f"construct: ListNode({<object>value})")

        self.c_struct = allocate_struct(<PyObject*>value)

    def __dealloc__(self) -> None:
        if DEBUG:
            print(f"destroy: ListNode({self.value})")

        if self.c_struct is not NULL:
            decref(self.c_struct)

    ################################
    ####    STRUCT INTERFACE    ####
    ################################

    @property
    def value(self) -> Any:
        """The object being stored in the node."""
        return <object>self.c_struct.value  # Python handles refcount for us

    @property
    def next(self) -> "ListNode":
        """The next node in the list."""
        if self.c_struct.next is NULL:
            return None

        # NOTE: we automatically wrap the next struct in a ListNode object
        # during attribute access.  This allows us to iterate through the list
        # just like normal, even though the nodes themselves are pure C.
        return node_from_struct(self.c_struct.next)

    @next.setter
    def next(self, ListNode node) -> None:
        """Set the next node in the list."""
        # prevent self-referential nodes
        if node is self:
            raise ValueError("cannot assign node to itself")

        cdef ListStruct* existing = self.c_struct.next

        # early return if new node is identical to existing
        if node.c_struct is existing:
            return

        # assign new node
        if node is None:
            self.c_struct.next = NULL
        else:
            incref(node.c_struct)
            self.c_struct.next = node.c_struct
            node.c_struct.prev = self.c_struct

        # manage memory if we're replacing an existing node
        if existing is not NULL:
            existing.prev = NULL  # nullify pointer
            decref(existing)

    @next.deleter
    def next(self) -> None:
        """Delete the next node in the list."""
        cdef ListStruct* existing = self.c_struct.next

        # nullify pointer
        self.c_struct.next = NULL

        # manage memory if we're replacing an existing node
        if existing is not NULL:
            existing.prev = NULL  # nullify pointer
            decref(existing)

    @property
    def prev(self) -> "ListNode":
        """The previous node in the list."""
        if self.c_struct.prev is NULL:
            return None

        # NOTE: we automatically wrap the previous struct in a ListNode object
        # during attribute access.  This allows us to iterate through the list
        # just like normal, even though the nodes themselves are pure C.
        return node_from_struct(self.c_struct.prev)

    @prev.setter
    def prev(self, ListNode node) -> None:
        """Set the previous node in the list."""
        # prevent self-referential nodes
        if node is self:
            raise ValueError("cannot assign node to itself")

        cdef ListStruct* existing = self.c_struct.prev

        # early return if new node is identical to existing
        if node.c_struct is existing:
            return

        # assign new node
        if node is None:
            self.c_struct.prev = NULL
        else:
            incref(node.c_struct)
            self.c_struct.prev = node.c_struct
            node.c_struct.next = self.c_struct

        # manage memory if we're replacing an existing node
        if existing is not NULL:
            existing.next = NULL  # nullify pointer
            decref(existing)

    @prev.deleter
    def prev(self) -> None:
        """Delete the previous node in the list."""
        cdef ListStruct* existing = self.c_struct.prev

        # nullify pointer
        self.c_struct.prev = NULL

        # manage memory if we're replacing an existing node
        if existing is not NULL:
            existing.next = NULL  # nullify pointer
            decref(existing)

    ##################################
    ####    REFERENCE COUNTING    ####
    ##################################

    @property
    def owner(self) -> bool:
        """Indicates whether this :class:`ListNode` owns the underlying struct.

        Returns
        -------
        bool
            ``True`` if the struct will be freed when the node is destroyed,
            ``False`` otherwise.

        Notes
        -----
        This is mostly for testing and debugging purposes.
        """
        return self.c_struct.ref_count == 1


# TODO: LinkedList should operate on ListStructs directly, rather than
# creating full ListNodes.  Only head and tail are stored as ListNodes.


cdef class LinkedList:
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
        The first node in the list.
    tail : ListNode
        The last node in the list.

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

    def __init__(self, items: Iterable[Any] | None = None):
        self.head = None
        self.tail = None
        self.size = 0

        # add items from initializer
        if items is not None:
            for item in items:
                self.append(item)

    ######################
    ####    APPEND    ####
    ######################

    cdef LinkedList copy(self):
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

    cdef void append(self, object item):
        """Add an item to the end of the list.

        Parameters
        ----------
        item : object
            The item to add to the list.

        Notes
        -----
        Appends are O(1) for both ends of the list.
        """
        cdef ListNode node = ListNode(item)

        # append to end of list
        self._add_node(node, self.tail, None)

    cdef void appendleft(self, object item):
        """Add an item to the beginning of the list.

        Parameters
        ----------
        item : object
            The item to add to the list.

        Notes
        -----
        Appends are O(1) for both ends of the list.
        
        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.
        """
        cdef ListNode node = ListNode(item)

        # append to beginning of list
        self._add_node(node, None, self.head)

    cdef void insert(self, object item, long long index):
        """Insert an item at the specified index.

        Parameters
        ----------
        item : object
            The item to add to the list.
        index : int64
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
        cdef ListNode node, curr
        cdef long long i

        # allow negative indexing + check bounds
        index = self._normalize_index(index)

        # generate new node
        node = ListNode(item)

        # insert node at specified index, starting from nearest end
        if index <= len(self) // 2:
            # iterate forwards from head
            curr = self.head
            for i in range(index):
                curr = curr.next

            # insert before current node
            self._add_node(node, curr.prev, curr)

        else:
            # iterate backwards from tail
            curr = self.tail
            for i in range(len(self) - index - 1):
                curr = curr.prev

            # insert after current node
            self._add_node(node, curr, curr.next)

    cdef void extend(self, object items):
        """Add multiple items to the end of the list.

        Parameters
        ----------
        items : Iterable[Any]
            An iterable of hashable items to add to the list.

        Notes
        -----
        Extends are O(m), where `m` is the length of ``items``.
        """
        cdef object item

        for item in items:
            self.append(item)

    cdef void extendleft(self, object items):
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
        cdef object item

        for item in items:
            self.appendleft(item)

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
        cdef LinkedList result = type(self)(self)

        result.extend(other)
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
        self.extend(other)
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
        cdef LinkedList result = self.copy()
        cdef long long i

        for i in range(repeat):
            result.extend(self.copy())
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
        cdef LinkedList original = self.copy()
        cdef long long i

        for i in range(repeat):
            self.extend(original)
        return self

    #####################
    ####    INDEX    ####
    #####################

    cdef long long count(self, object item):
        """Count the number of occurrences of an item in the list.

        Parameters
        ----------
        item : object
            The item to count.

        Returns
        -------
        int64
            The number of occurrences of the item in the list.

        Notes
        -----
        Counting is O(n).
        """
        cdef long long count = 0

        for value in self:
            if value == item:
                count += 1

        return count

    cdef long long index(
        self,
        object item,
        long long start = 0,
        long long stop = -1
    ):
        """Get the index of an item within the list.

        Parameters
        ----------
        item : object
            The item to search for.

        Returns
        -------
        int64
            The index of the item within the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.

        Notes
        -----
        Indexing is O(n) on average.
        """
        cdef ListNode node = self.head
        cdef long long index = 0

        # normalize start/stop indices
        start = self._normalize_index(start)
        stop = self._normalize_index(stop)

        # skip to start
        for i in range(start):
            if node is None:
                raise ValueError(f"{repr(item)} is not contained in the list")
            node = node.next

        while node is not None and index < stop:
            if node.value == item:
                return index
            node = node.next
            index += 1

        raise ValueError(f"{repr(item)} is not contained in the list")

    cdef void sort(self):
        """Sort the list in-place.

        Notes
        -----
        Sorting is O(n log n) on average.
        
        This method uses an iterative merge sort algorithm that avoids the
        extra memory overhead required to handle recursive stack frames.
        """
        cdef long long length
        cdef ListNode temp

        # trivial case: empty list
        if not self.head:
            return

        # NOTE: as a refresher, the general merge sort algorithm is as follows:
        #   1) divide the list into sublists of length 1 (bottom-up)
        #   2) sort pairs of elements from left to right and merge
        #   3) double the length of the sublists and repeat step 2

        # NOTE: allocating `temp` outside of _merge() allows us to avoid
        # creating a new head every time we merge two sublists.
        temp = ListNode(None)

        # merge pairs of sublists of increasing size, starting at length 1
        length = 1
        while length < self.size:
            curr = self.head  # left to right
            tail = None

            # divide and conquer
            while curr:
                # split the linked list into two sublists of size `length`
                left = curr
                right = self._split(left, length)
                curr = self._split(right, length)

                # merge the two sublists, maintaining sorted order
                sub_head, sub_tail = self._merge(left, right, temp)

                # if this is our first merge, set the head of the new list
                if tail is None:
                    self.head = sub_head
                else:
                    # link the merged sublist to the previous one
                    tail.next = sub_head
                    sub_head.prev = tail

                # set tail of new list
                tail = sub_tail

            # double the length of the sublists for the next iteration
            length *= 2

    cdef void rotate(self, long long steps = 1):
        """Rotate the list to the right by the specified number of steps.

        Parameters
        ----------
        steps : int64, optional
            The number of steps to rotate the list.  If this is positive, the
            list will be rotated to the right.  If this is negative, the list
            will be rotated to the left.  The default is ``1``.

        Notes
        -----
        Rotations are O(steps).

        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.
        """
        cdef long long i

        # rotate right
        if steps > 0:
            for i in range(steps):
                self.appendleft(self.popright())

        # rotate left
        else:
            for i in range(steps):
                self.append(self.popleft())

    cdef void reverse(self):
        """Reverse the order of the list in-place.

        Notes
        -----
        Reversing a :class:`LinkedList` is O(n).
        """
        cdef ListNode node = self.head

        # swap all prev and next pointers
        while node is not None:
            node.prev, node.next = node.next, node.prev
            node = node.prev  # prev is now next

        # swap head and tail
        self.head, self.tail = self.tail, self.head

    def __getitem__(self, key: int | slice) -> Any:
        """Index the list for a particular item or slice.

        Parameters
        ----------
        key : int64 or slice
            The index or slice to retrieve from the list.  If this is a slice,
            the result will be a new :class:`LinkedList` containing the
            specified items.  This can be negative, following the same
            convention as Python's standard :class:`list <python:list>`.

        Returns
        -------
        object or LinkedList
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
        a slice boundary, and to never backtrack.  This is done by checking
        whether the slice is ascending (step > 0) or descending, and whether
        the start or stop index is closer to its respective end.  This gives
        the following cases:

            1) ascending, start closer to head than stop is to tail
                -> forwards from head to stop
            2) ascending, stop closer to tail than start is to head
                -> backwards from tail to start
            3) descending, start closer to tail than stop is to head
                -> backwards from tail to stop
            4) descending, stop closer to head than start is to tail
                -> forwards from head to start
        """
        cdef ListNode node
        cdef LinkedList result
        cdef long long start, stop, step, i
        cdef long long index, end_index
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

            # get first node in slice, counting from nearest end
            node = self._node_at_index(index)

            # determine whether to reverse the slice due to sign of step
            reverse = step < 0
            step = abs(step)  # drop sign

            # forward traversal
            if end_index >= index:
                while node is not None and index < end_index:
                    if reverse:
                        result.appendleft(node.value)
                    else:
                        result.append(node.value)

                    # jump according to step size
                    for i in range(step):
                        if node is None:
                            break
                        node = node.next

                    # increment index
                    index += step

            # backward traversal
            else:
                while node is not None and index > end_index:
                    if reverse:
                        result.append(node.value)
                    else:
                        result.appendleft(node.value)

                    # jump according to step size
                    for i in range(step):
                        if node is None:
                            break
                        node = node.prev

                    # decrement index
                    index -= step

            return result

        # index directly
        key = self._normalize_index(key)
        node = self._node_at_index(key)
        return node.value

    def __setitem__(self, key: int | slice, value: Any) -> None:
        """Set the value of an item or slice in the list.

        Parameters
        ----------
        key : int64 or slice
            The index or slice to set in the list.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.
        value : object
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
        nearest to a slice boundary, and to never backtrack.  This is done by
        checking whether the slice is ascending (step > 0) or descending, and
        whether the start or stop index is closer to its respective end.  This
        gives the following cases:

            1) ascending, start closer to head than stop is to tail
                -> forwards from head to stop
            2) ascending, stop closer to tail than start is to head
                -> backwards from tail to start
            3) descending, start closer to tail than stop is to head
                -> backwards from tail to stop
            4) descending, stop closer to head than start is to tail
                -> forwards from head to start
        """
        cdef ListNode node
        cdef long long slice_size
        cdef long long start, stop, step, i
        cdef long long index, end_index
        cdef object val

        # support slicing
        if isinstance(key, slice):
            # get indices of slice
            start, stop, step = key.indices(len(self))

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
            node = self._node_at_index(index)

            # forward traversal
            values_iter = iter(value)
            if end_index >= index:
                for val in values_iter:
                    if node is None or index >= end_index:
                        break
                    node.value = val
                    for i in range(step):  # jump according to step size
                        if node is None:
                            break
                        node = node.next
                    index += step  # increment index

            # backward traversal
            else:
                for val in reversed(list(values_iter)):
                    if node is None or index == end_index:
                        break
                    node.value = val
                    for i in range(step):  # jump according to step size
                        if node is None:
                            break
                        node = node.prev
                    index -= step  # decrement index

        # index directly
        else:
            key = self._normalize_index(key)
            node = self._node_at_index(key)
            node.value = value

    def __delitem__(self, key: int | slice) -> None:
        """Delete an item or slice from the list.

        Parameters
        ----------
        key : int64 or slice
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
        nearest to a slice boundary, and to never backtrack.  This is done by
        checking whether the slice is ascending (step > 0) or descending, and
        whether the start or stop index is closer to its respective end.  This
        gives the following cases:

            1) ascending, start closer to head than stop is to tail
                -> forwards from head to stop
            2) ascending, stop closer to tail than start is to head
                -> backwards from tail to start
            3) descending, start closer to tail than stop is to head
                -> backwards from tail to stop
            4) descending, stop closer to head than start is to tail
                -> forwards from head to start
        """
        cdef ListNode node
        cdef long long start, stop, step, i
        cdef long long index, end_index
        cdef list staged

        # support slicing
        if isinstance(key, slice):
            # get bounds of slice
            start, stop, step = key.indices(len(self))
            if (start > stop and step > 0) or (start < stop and step < 0):
                return  # Python does nothing in this case

            # determine direction of traversal to avoid backtracking
            index, end_index = self._get_slice_direction(start, stop, step)

            # get first node in slice, counting from nearest end
            node = self._node_at_index(index)

            # NOTE: we shouldn't delete items as we iterate.  Instead, we stage
            # the deletions and then perform them all at once at the end.
            staged = list()

            # forward traversal
            step = abs(step)  # drop sign
            if end_index >= index:
                while node is not None and index < end_index:
                    staged.append(node)
                    for i in range(step):  # jump according to step size
                        if node is None:
                            break
                        node = node.next
                    index += step  # increment index

            # backward traversal
            else:
                while node is not None and index > end_index:
                    staged.append(node)
                    for i in range(step):  # jump according to step size
                        if node is None:
                            break
                        node = node.prev
                    index -= step

            # delete all staged nodes
            for node in staged:
                self._remove_node(node)

        # index directly
        else:
            key = self._normalize_index(key)
            node = self._node_at_index(key)
            self._remove_node(node)

    ######################
    ####    REMOVE    ####
    ######################

    cdef void remove(self, object item):
        """Remove an item from the list.

        Parameters
        ----------
        item : object
            The item to remove from the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.

        Notes
        -----
        Removals are O(n) on average.
        """
        cdef ListNode node = self.head

        while node is not None:
            if node.value == item:
                self._remove_node(node)
                break
            node = node.next

        raise ValueError(f"{repr(item)} is not contained in the list")

    cdef void clear(self):
        """Remove all items from the list.

        Notes
        -----
        Clearing a list is O(1).
        
        Due to the way Python's garbage collector works, we don't actually need
        to iterate over the list to free it.  The gc can automatically detect
        reference cycles and free them if the referenced objects cannot be
        reached from anywhere else in the program.
        """
        self.head = None
        self.tail = None
        self.size = 0

    cdef object pop(self, long long index = -1):
        """Remove and return the item at the specified index.

        Parameters
        ----------
        index : int64, optional
            The index of the item to remove.  If this is negative, it will be
            translated to a positive index by counting backwards from the end
            of the list.  The default is ``-1``, which removes the last item.

        Returns
        -------
        object
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
        index = self._normalize_index(index)

        # get node at index
        cdef ListNode node = self._node_at_index(index)

        # drop node and return its contents
        self._remove_node(node)
        return node.value

    cdef object popleft(self):
        """Remove and return the first item in the list.

        Returns
        -------
        object
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
        if self.head is None:
            raise IndexError("pop from empty list")

        # no need to handle indices, just skip straight to head
        cdef ListNode node = self.head

        # drop node and return its contents
        self._remove_node(node)
        return node.value

    cdef object popright(self):
        """Remove and return the last item in the list.

        Returns
        -------
        object
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
        if self.tail is None:
            raise IndexError("pop from empty list")

        # no need to handle indices, just skip straight to tail
        cdef ListNode node = self.tail

        # drop node and return its contents
        self._remove_node(node)
        return node.value

    ###########################
    ####    COMPARISONS    ####
    ###########################

    def __lt__(self, other: Any) -> bool:
        """Check if this list is lexographically less than another list.

        Parameters
        ----------
        other : object
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

        # compare elements at each index
        for a, b in zip(self, other):
            if a == b:
                continue
            return a < b

        # if all elements are equal, the shorter list is less than the longer
        return len(self) < len(other)

    def __le__(self, other: Any) -> bool:
        """Check if this list is lexographically less than or equal to another
        list.

        Parameters
        ----------
        other : object
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

        # compare elements at each index
        for a, b in zip(self, other):
            if a == b:
                continue
            return a < b

        # if all elements are equal, the shorter list is less than or equal to
        # the longer
        return len(self) <= len(other)

    def __eq__(self, other: Any) -> bool:
        """Compare two lists for equality.

        Parameters
        ----------
        other : object
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

    def __gt__(self, other: Any) -> bool:
        """Check if this list is lexographically greater than another list.

        Parameters
        ----------
        other : object
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

        # compare elements at each index
        for a, b in zip(self, other):
            if a == b:
                continue
            return a > b

        # if all elements are equal, the longer list is greater than the
        # shorter
        return len(self) > len(other)

    def __ge__(self, other: Any) -> bool:
        """Check if this list is lexographically greater than or equal to
        another list.

        Parameters
        ----------
        other : object
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

        # compare elements at each index
        for a, b in zip(self, other):
            if a == b:
                continue
            return a > b

        # if all elements are equal, the longer list is greater than or equal
        # to the shorter
        return len(self) >= len(other)

    #######################
    ####    PRIVATE    ####
    #######################

    cdef void _add_node(self, ListNode node, ListNode prev, ListNode next):
        """Add a node to the list.

        Parameters
        ----------
        node : ListNode
            The node to add to the list.
        prev : ListNode
            The node that should precede the new node in the list.
        next : ListNode
            The node that should follow the new node in the list.

        Notes
        -----
        This is a simple helper method for doing the pointer arithmetic of
        adding a node, since it's used in multiple places.
        """
        # prev <-> node
        node.prev = prev
        if prev is None:
            self.head = node
        else:
            prev.next = node

        # node <-> next
        node.next = next
        if next is None:
            self.tail = node
        else:
            next.prev = node

        # increment size
        self.size += 1

    cdef void _remove_node(self, ListNode node):
        """Remove a node from the list.

        Parameters
        ----------
        node : ListNode
            The node to remove from the list.

        Notes
        -----
        This is a simple helper method for doing the pointer arithmetic of
        removing a node, since it's used in multiple places.
        """
        # prev -> next
        if node.prev is None:
            self.head = node.next
        else:
            node.prev.next = node.next

        # prev <- next
        if node.next is None:
            self.tail = node.prev
        else:
            node.next.prev = node.prev

        # decrement size
        self.size -= 1

    cdef ListNode _node_at_index(self, long long index):
        """Get the node at the specified index.

        Parameters
        ----------
        index : int64
            The index of the node to retrieve.  This should always be passed
            through :meth:`LinkedList._normalize_index` first.

        Returns
        -------
        ListNode
            The node at the specified index.

        Notes
        -----
        This method is O(n) on average.  As an optimization, it always iterates
        from the nearest end of the list.
        """
        cdef ListNode node
        cdef long long i

        # count forwards from head
        if index <= self.size // 2:
            node = self.head
            for i in range(index):
                node = node.next

        # count backwards from tail
        else:
            node = self.tail
            for i in range(self.size - index - 1):
                node = node.prev

        return node

    cdef long long _normalize_index(self, long long index):
        """Allow negative indexing and check if the result is within bounds.

        Parameters
        ----------
        index : int64
            The index to normalize.  If this is negative, it will be translated
            to a positive index by counting backwards from the end of the list.

        Returns
        -------
        int64
            The normalized index.

        Raises
        ------
        IndexError
            If the index is out of bounds.
        """
        # allow negative indexing
        if index < 0:
            index = index + self.size

        # check bounds
        if not 0 <= index < self.size:
            raise IndexError("list index out of range")

        return index

    cdef (long long, long long) _get_slice_direction(
        self,
        long long start,
        long long stop,
        long long step,
    ):
        """Determine the direction in which to traverse a slice so as to
        minimize total iterations.

        Parameters
        ----------
        start : int64
            The start index of the slice.
        stop : int64
            The stop index of the slice.
        step : int64
            The step size of the slice.

        Returns
        -------
        index : long long
            The index at which to start iterating.
        end_index : long long
            The index at which to stop iterating.

        Notes
        -----
        The direction of traversal is determined by comparing the indices
        returned by this method.  If ``end_index >= index``, then the slice
        should be traversed in the forward direction.  Otherwise, it should be
        iterated over backwards in order to avoid backtracking.
        """
        cdef long long index, end_index

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

    cdef ListNode _split(self, ListNode head, long long length):
        """Split a linked list into sublists of the specified length.

        Parameters
        ----------
        head : ListNode
            The head of the list to split.
        length : int64
            The maximum length of each split.  This method will walk forward
            from ``head`` by this many nodes and then split the list.

        Returns
        -------
        ListNode
            The head of the next sublist.

        Notes
        -----
        This method is O(length).  It just iterates forward ``length`` times
        and then splits the list at that point.
        """
        cdef ListNode split
        cdef long long i

        # walk `length` nodes forward from `head`
        for i in range(length - 1):
            if head is None:
                break
            head = head.next

        # if we've reached the end of the list, there's nothing to split
        if head is None:
            return None

        # otherwise, split the list
        split = head.next
        head.next = None
        if split is not None:
            split.prev = None
        return split

    cdef tuple _merge(self, ListNode left, ListNode right, ListNode temp):
        """Merge two sorted linked lists into a single sorted list.

        Parameters
        ----------
        left : ListNode
            The head of the first sorted list.
        right : ListNode
            The head of the second sorted list.
        temp : ListNode
            A temporary node to use as the head of the merged list.  As an
            optimization, this is allocated once and then passed as a parameter
            rather than creating a new one every time this method is called.

        Returns
        -------
        head : ListNode
            The head of the merged list.
        tail : ListNode
            The tail of the merged list.

        Notes
        -----
        This is a standard implementation of the divide-and-conquer merge
        algorithm.  It is O(l) where `l` is the length of the longer list.
        """
        cdef ListNode curr = temp
        cdef ListNode tail

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
        tail = left if right is None else right
        curr.next = tail
        tail.prev = curr

        # advance tail to end of merged list
        while tail.next is not None:
            tail = tail.next

        # return the proper head and tail of the merged list
        return (temp.next, tail)  # remove temporary head

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __len__(self) -> int:
        """Get the total number of items in the list.

        Returns
        -------
        int
            The number of items in the list.
        """
        return self.size

    def __iter__(self) -> Iterator[Any]:
        """Iterate through the list items in order.

        Yields
        ------
        object
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedList` is O(n) on average.
        """
        cdef ListNode node = self.head

        while node is not None:
            yield node.value
            node = node.next

    def __reversed__(self) -> Iterator[Any]:
        """Iterate through the list in reverse order.

        Yields
        ------
        object
            The next item in the list.

        Notes
        -----
        Iterating through a :class:`LinkedList` is O(n) on average.
        """
        cdef ListNode node = self.tail

        while node is not None:
            yield node.value
            node = node.prev

    def __contains__(self, item: Any) -> bool:
        """Check if the item is contained in the list.

        Parameters
        ----------
        item : object
            The item to search for.

        Returns
        -------
        bool
            Indicates whether the item is contained in the list.

        Notes
        -----
        Membership checks are O(n) on average.
        """
        cdef ListNode node

        for node in self:
            if node.value == item:
                return True

        return False

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
        return str(list(self))

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
        return f"{type(self).__name__}({list(self)})"


cdef class HashedList(LinkedList):
    """A pure Cython implementation of a doubly-linked list where every element
    is hashable and unique.

    Parameters
    ----------
    items : Iterable[Hashable], optional
        An iterable of hashable items to initialize the list.

    Attributes
    ----------
    head : ListNode
        The first node in the list.
    tail : ListNode
        The last node in the list.
    items : dict
        A dictionary mapping items to their corresponding nodes for fast access.

    Notes
    -----
    This data structure is a special case of :class:`LinkedList` where every
    value is both unique and hashable.  This allows it to use a dictionary to
    map values to their corresponding nodes, which allows for O(1) removals and
    membership checks.

    For an implementation without these constraints, see the base
    :class:`LinkedList`.
    """

    def __init__(self, items: Iterable[Hashable] | None = None):
        self.nodes = {}
        LinkedList.__init__(self, items)

    ######################
    ####    APPEND    ####
    ######################

    cdef void append(self, object item):
        """Add an item to the end of the list.

        Parameters
        ----------
        item : object
            The item to add to the list.

        Raises
        ------
        TypeError
            If the item is not hashable.
        ValueError
            If the item is already contained in the list.

        Notes
        -----
        Appends are O(1) for both ends of the list.
        """
        cdef ListNode node

        # check if item is already present
        if item in self.nodes:
            raise ValueError(f"list elements must be unique: {repr(item)}")

        # generate new node and add to hash map
        node = ListNode(item)
        self.nodes[item] = node

        # append to end of list
        if self.head is None:
            self.head = node
            self.tail = node
        else:
            self.tail.next = node
            node.prev = self.tail
            self.tail = node

        # increment size
        self.size += 1

    cdef void appendleft(self, object item):
        """Add an item to the beginning of the list.

        Parameters
        ----------
        item : object
            The item to add to the list.

        Raises
        ------
        TypeError
            If the item is not hashable.
        ValueError
            If the item is already contained in the list.

        Notes
        -----
        Appends are O(1) for both ends of the list.
        
        This method is consistent with the standard library's
        :class:`collections.deque <python:collections.deque>` class.
        """
        cdef ListNode node

        # check if item is already present
        if item in self.nodes:
            raise ValueError(f"list elements must be unique: {repr(item)}")

        # generate new node and add to hash map
        node = ListNode(item)
        self.nodes[item] = node

        # append to beginning of list
        if self.head is None:
            self.head = node
            self.tail = node
        else:
            self.head.prev = node
            node.next = self.head
            self.head = node

        # increment size
        self.size += 1

    cdef void insert(self, object item, long long index):
        """Insert an item at the specified index.

        Parameters
        ----------
        item : object
            The item to add to the list.
        index : int64
            The index at which to insert the item.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.

        Raises
        ------
        TypeError
            If the item is not hashable.
        ValueError
            If the item is already contained in the list.
        IndexError
            If the index is out of bounds.

        Notes
        -----
        Inserts are O(n) on average.
        """
        cdef ListNode node, curr
        cdef long long i

        # check if item is already present
        if item in self.nodes:
            raise ValueError(f"list elements must be unique: {repr(item)}")

        # allow negative indexing + check bounds
        index = self._normalize_index(index)

        # generate new node and add to hash map
        node = ListNode(item)
        self.nodes[item] = node

        # insert at specified index, starting from nearest end
        if index <= len(self) // 2:
            # iterate forwards from head
            curr = self.head
            for i in range(index):
                curr = curr.next

            # insert before current node
            node.next = curr
            node.prev = curr.prev
            curr.prev = node
            if node.prev is None:
                self.head = node
            else:
                node.prev.next = node

        else:
            # iterate backwards from tail
            curr = self.tail
            for i in range(len(self) - index - 1):
                curr = curr.prev

            # insert after current node
            node.prev = curr
            node.next = curr.next
            curr.next = node
            if node.next is None:
                self.tail = node
            else:
                node.next.prev = node

        # increment size
        self.size += 1

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

    #####################
    ####    INDEX    ####
    #####################

    cdef long long count(self, object item):
        """Count the number of occurrences of an item in the list.

        Parameters
        ----------
        item : object
            The item to count.

        Returns
        -------
        int64
            The number of occurrences of the item in the list.

        Notes
        -----
        Due to the uniqueness constraint, this method is equivalent to a
        simple :meth:`LinkedList.__contains__` check.
        """
        return <long long> item in self

    cdef long long index(
        self,
        object item,
        long long start = 0,
        long long stop = -1
    ):
        """Get the index of an item within the list.

        Parameters
        ----------
        item : object
            The item to search for.

        Returns
        -------
        int64
            The index of the item within the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.

        Notes
        -----
        Indexing is O(n) on average.
        """
        # the hash map allows us to do O(1) membership checks
        if item not in self:
            raise ValueError(f"{repr(item)} is not contained in the list")

        return LinkedList.index(self, item, start, stop)

    def __setitem__(self, key: int | slice, value: Hashable) -> None:
        """Set the value of an item or slice in the list.

        Parameters
        ----------
        key : int64 or slice
            The index or slice to set in the list.  This can be negative,
            following the same convention as Python's standard
            :class:`list <python:list>`.
        value : object
            The value or values to set at the specified index or slice.  If
            ``key`` is a slice, then ``value`` must be an iterable of the same
            length.

        Raises
        ------
        TypeError
            If any values are not hashable.
        ValueError
            If any values are already contained in the list, or if the length
            of ``value`` does not match the length of the slice.
        IndexError
            If the index is out of bounds.

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
        nearest to a slice boundary, and to never backtrack.  This is done by
        checking whether the slice is ascending (step > 0) or descending, and
        whether the start or stop index is closer to its respective end.  This
        gives the following cases:

            1) ascending, start closer to head than stop is to tail
                -> forwards from head
            2) ascending, stop closer to tail than start is to head
                -> backwards from tail
            3) descending, start closer to tail than stop is to head
                -> backwards from tail
            4) descending, stop closer to head than start is to tail
                -> forwards from head
        """
        cdef ListNode node
        cdef long long slice_size
        cdef long long start, stop, step, i
        cdef long long index, end_index
        cdef set replaced_items
        cdef list staged
        cdef object val, old_item, new_item

        # support slicing
        if isinstance(key, slice):
            # get indices of slice
            start, stop, step = key.indices(len(self))

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
            node = self._node_at_index(index)

            # NOTE: due to the uniqueness constraint, we can't just blindly
            # overwrite values in the slice, as some of them might be present
            # elsewhere in the list.  We also don't care if a value is in the
            # masked items, since they will be overwritten anyway.  To address
            # this, we record the observed values and stage our changes to
            # avoid modifying values until we are sure they are valid.
            replaced_items = set()
            staged = list()

            # forward traversal
            values_iter = iter(value)
            if end_index >= index:
                for val in values_iter:
                    if node is None or index == end_index:
                        break

                    # check for uniqueness and stage the change
                    replaced_items.add(node.value)
                    if val in self.nodes and val not in replaced_items:
                        raise ValueError(
                            f"list elements must be unique: {repr(val)}"
                        )
                    staged.append((node, val))

                    # jump according to step size
                    for i in range(step):
                        if node is None:
                            break
                        node = node.next

                    # increment index
                    index += step

            # backward traversal
            else:
                for val in reversed(list(values_iter)):
                    if node is None or index == end_index:
                        break

                    # check for uniqueness and stage the change
                    replaced_items.add(node.value)
                    if val in self.nodes and val not in replaced_items:
                        raise ValueError(
                            f"list elements must be unique: {repr(val)}"
                        )
                    staged.append((node, val))

                    # jump according to step size
                    for i in range(step):
                        if node is None:
                            break
                        node = node.prev

                    # decrement index
                    index -= step

            # everything's good: update the list
            for old_item in replaced_items:
                del self.nodes[old_item]
            for node, new_item in staged:
                node.value = new_item
                self.nodes[new_item] = node

        # index directly
        else:
            key = self._normalize_index(key)
            node = self._node_at_index(key)

            # check for uniqueness
            if value in self.nodes and value != node.value:
                raise ValueError(f"list elements must be unique: {repr(value)}")

            # update the node's item and the items map
            del self.nodes[node.value]
            node.value = value
            self.nodes[value] = node

    ######################
    ####    REMOVE    ####
    ######################

    cdef void remove(self, object item):
        """Remove an item from the list.

        Parameters
        ----------
        item : object
            The item to remove from the list.

        Raises
        ------
        ValueError
            If the item is not contained in the list.

        Notes
        -----
        Removals are O(1) due to the presence of the hash map.
        """
        cdef ListNode node

        # check if item is present in hash map
        try:
            node = self.nodes[item]
        except KeyError:
            raise ValueError(f"{repr(item)} is not contained in the list")

        # handle pointer arithmetic
        self._remove_node(node)

    cdef void clear(self):
        """Remove all items from the list.

        Notes
        -----
        This method is O(1).
        """
        LinkedList.clear(self)
        self.nodes.clear()  # clear the hash map

    #######################
    ####    PRIVATE    ####
    #######################

    cdef void _remove_node(self, ListNode node):
        """Remove a node from the list.

        Parameters
        ----------
        node : ListNode
            The node to remove from the list.

        Notes
        -----
        This is a simple helper method for doing the pointer arithmetic of
        removing a node, since it's used in multiple places.
        """
        LinkedList._remove_node(self, node)

        # remove from hash map
        del self.nodes[node.value]

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __contains__(self, item: Hashable) -> bool:
        """Check if the item is contained in the list.

        Parameters
        ----------
        item : object
            The item to search for.

        Returns
        -------
        bool
            Indicates whether the item is contained in the list.

        Notes
        -----
        This method is O(1) due to the hash map of contained items.
        """
        return item in self.nodes
