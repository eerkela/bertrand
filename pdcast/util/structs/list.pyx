"""This module contains a basic implementation of a doubly-linked list, which
can be subclassed to add additional functionality.
"""
from typing import Any, Hashable, Iterable, Iterator

from cython cimport freelist
from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, free
from libcpp.unordered_set cimport unordered_set
from libcpp.vector cimport vector

from .base cimport (
    DEBUG, ListStruct, Pair, allocate_struct, decref, incref, raise_exception,
    replace_value
)

cdef extern from "Python.h":
    int Py_EQ, Py_LT, Py_GT
    int PyObject_RichCompareBool(PyObject* obj1, PyObject* obj2, int opid)



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


# TODO: If we maintain owned references to both the head and tail nodes in a
# list object, then we can still create memory leaks.  For example:
#   head = ListNode(1)
#   tail = ListNode(3)
#   head.next = ListNode(2)
#   head.next.next = tail
#   del head  # frees 1
#   del tail  # memory leak: frees 3 but not 2


# We need to be careful not to do this in our list code.  In fact, we might
# just make construction from an existing struct the default behavior in the
# ListNode constructor.  We can then move node_from_struct() directly into
# __cinit__().  It would fail whenever a ListNode is constructed from Python.

# This makes it impossible to insert a node into a list manually.  You always
# have to use the list's methods to do it.  I'm not entirely sure if this is a
# good thing.

# It might be possible to automatically wrap values in ListNodes during the
# setter, which could bypass this limitation

# head = ListNode(1)  # controlled by LinkedList
# head.next = 2
# head.next.next = 3

# If a previous node is given instead, then we use it directly.  This becomes
# a cdef helper method that uses a fused type for efficiency.

# ctypedef fused obj_or_node:
#   ListNode
#   object

# cdef inline ListNode create_node(obj_or_node value):
#   if obj_or_node is ListNode:
#       return value
# 
#   cdef ListStruct* c_struct = allocate_struct(<PyObject*>value)
#   return ListNode(c_struct)




# Probably need to use an owner attribute that double decrements the refcount
# when the node is destroyed.  This is set False whenever a node is added to
# another node's next/prev attribute.  When we encounter another reference
# to the list during deallocation, we set its owner attribute to True

# -> We should probably just disallow the creation of standalone nodes
# entirely.  Instead, we should only allow nodes to be created as part of a
# list.  This will prevent memory leaks from occurring when we wrap nodes
# in ListNodes, since the owner attribute will always be False.





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



#######################
####    CLASSES    ####
#######################


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
        # NOTE: this is only ever called from Python.
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

    @value.setter
    def value(self, object value) -> None:
        """Set the object being stored in the node."""
        replace_value(self.c_struct, <PyObject*>value)

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
        elif self.c_struct.ref_count == 1:
            raise ValueError(
                "possible memory leak: cannot assign `next` for an owned "
                "node.  Use LinkedList() instead."
            )

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
        # prevent self-referential nodes and possible memory leaks
        if node is self:
            raise ValueError("cannot assign node to itself")
        elif self.c_struct.ref_count == 1:
            raise ValueError(
                "possible memory leak: cannot assign `prev` for an owned "
                "node.  Use LinkedList() instead."
            )


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
        if items is not None:
            self.extend(items)

    def __cinit__(self):
        self._head = NULL
        self._tail = NULL
        self.size = 0

    #############################
    ####    PYTHON ACCESS    ####
    #############################

    # NOTE: Each node in a LinkedList is implemented as a pure C struct that
    # is normally inaccessible to Python.  To allow users to access the list
    # normally, we automatically wrap each struct as we encounter it, creating
    # a thin wrapper that exposes its attributes to Python.

    @property
    def head(self) -> ListNode:
        """A Python-accessible reference to the first node in the list.

        Returns
        -------
        ListNode
            A wrapper around a :class:`ListStruct` that exposes its attributes
            to Python.

        Notes
        -----
        This is a read-only attribute.  To modify the list, use the normal
        list interface instead.
        """
        if self._head is NULL:
            return None

        return node_from_struct(self._head)

    @property
    def tail(self) -> ListNode:
        """A Python-accessible reference to the last node in the list.

        Returns
        -------
        ListNode
            A wrapper around a :class:`ListStruct` that exposes its attributes
            to Python.

        Notes
        -----
        This is a read-only attribute.  To modify the list, use the normal
        list interface instead.
        """
        if self._tail is NULL:
            return None

        return node_from_struct(self._tail)

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
        cdef ListStruct* curr = allocate_struct(<PyObject*>item)

        # append to end of list
        self._add_struct(self._tail, curr, NULL)

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
        cdef ListStruct* curr = allocate_struct(<PyObject*>item)

        # append to beginning of list
        self._add_struct(NULL, curr, self._head)

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
        # allow negative indexing + check bounds
        index = self._normalize_index(index)

        # generate new struct
        cdef ListStruct* c_struct = allocate_struct(<PyObject*>item)
        cdef ListStruct* curr
        cdef long long i

        # insert struct at specified index, starting from nearest end
        if index <= len(self) // 2:
            # iterate forwards from head
            curr = self._head
            for i in range(index):
                curr = curr.next

            # insert before current struct
            self._add_struct(curr.prev, c_struct, curr)

        else:
            # iterate backwards from tail
            curr = self._tail
            for i in range(len(self) - index - 1):
                curr = curr.prev

            # insert after current struct
            self._add_struct(curr, c_struct, curr.next)

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
        cdef PyObject* borrowed = <PyObject*>item  # borrowed reference
        cdef ListStruct* c_struct = self._head
        cdef long long count = 0
        cdef int comp

        # we iterate entirely at the C level for maximum performance
        while c_struct is not NULL:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(c_struct.value, borrowed, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # increment count if equal
            if comp == 1:
                count += 1

            # advance to next struct
            c_struct = c_struct.next

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
        cdef PyObject* borrowed = <PyObject*>item  # borrowed reference
        cdef ListStruct* c_struct = self._head
        cdef long long index = 0
        cdef int comp

        # normalize start/stop indices
        start = self._normalize_index(start)
        stop = self._normalize_index(stop)

        # skip to `start`
        for index in range(start):
            if c_struct is NULL:  # hit end of list
                raise ValueError(f"{repr(item)} is not in list")
            c_struct = c_struct.next

        # iterate until `stop`
        while c_struct is not NULL and index < stop:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(c_struct.value, borrowed, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()
    
            # return index if equal
            if comp == 1:
                return index

            # advance to next struct
            c_struct = c_struct.next
            index += 1

        raise ValueError(f"{repr(item)} is not in list")

    cdef void sort(self):
        """Sort the list in-place.

        Notes
        -----
        Sorting is O(n log n) on average.
        
        This method uses an iterative merge sort algorithm that avoids the
        extra memory overhead required to handle recursive stack frames.
        """
        # trivial case: empty list
        if self._head is NULL:
            return

        # NOTE: as a refresher, the general merge sort algorithm is as follows:
        #   1) divide the list into sublists of length 1 (bottom-up)
        #   2) sort pairs of elements from left to right and merge
        #   3) double the length of the sublists and repeat step 2

        # NOTE: allocating `temp` outside of _merge() allows us to avoid
        # creating a new head every time we merge two sublists.
        cdef ListStruct* curr
        cdef ListStruct* tail
        cdef ListStruct* left
        cdef ListStruct* right
        cdef ListStruct* sub_head
        cdef ListStruct* sub_tail
        cdef ListStruct* temp = allocate_struct(<PyObject*>None)
        cdef long long length = 1

        # merge pairs of sublists of increasing size, starting at length 1
        while length < self.size:
            curr = self._head  # left to right

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
                    self._head = sub_head
                else:
                    # link the merged sublist to the previous one
                    tail.next = sub_head
                    sub_head.prev = tail

                # set tail of new list
                tail = sub_tail

            # double the length of the sublists for the next iteration
            length *= 2

        # clean up temporary struct
        if temp.next is not NULL:
            temp.next.prev = NULL
        temp.next = NULL
        decref(temp)  # TODO: make sure we don't leave any dangling pointers

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
        cdef bint shift_right = steps > 0
        cdef long long i

        # avoid inconsistencies related to sign
        steps = abs(steps)

        # rotate right
        if shift_right:
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
        cdef ListStruct* c_struct = self._head

        # swap all prev and next pointers
        while c_struct is not NULL:
            c_struct.prev, c_struct.next = c_struct.next, c_struct.prev
            c_struct = c_struct.prev  # next is now prev

        # swap head and tail
        self._head, self._tail = self._tail, self._head

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
        cdef ListStruct* curr
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

            # get first struct in slice, counting from nearest end
            curr = self._struct_at_index(index)

            # determine whether to reverse the slice due to sign of step
            reverse = step < 0
            step = abs(step)  # drop sign

            # forward traversal
            if end_index >= index:
                while curr is not NULL and index < end_index:
                    if reverse:
                        result._add_struct(NULL, curr, self._head)  # appendleft
                    else:
                        result._add_struct(self._tail, curr, NULL)  # append

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
                        result._add_struct(self._tail, curr, NULL)  # append
                    else:
                        result._add_struct(NULL, curr, self._head)  # appendleft

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
        curr = self._struct_at_index(key)
        return <object>curr.value  # return owned reference

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
        cdef ListStruct* curr
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

            # get first struct in slice, counting from nearest end
            curr = self._struct_at_index(index)

            # forward traversal
            values_iter = iter(value)
            if end_index >= index:
                for val in values_iter:
                    if curr is NULL or index >= end_index:
                        break
                    replace_value(curr, <PyObject*>val)
                    for i in range(step):  # jump according to step size
                        if curr is NULL:
                            break
                        curr = curr.next
                    index += step  # increment index

            # backward traversal
            else:
                for val in reversed(list(values_iter)):
                    if curr is NULL or index == end_index:
                        break
                    replace_value(curr, <PyObject*>val)
                    for i in range(step):  # jump according to step size
                        if curr is NULL:
                            break
                        curr = curr.prev
                    index -= step  # decrement index

        # index directly
        else:
            key = self._normalize_index(key)
            curr = self._struct_at_index(key)
            replace_value(curr, <PyObject*>value)

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
        cdef ListStruct* curr
        cdef long long start, stop, step, i
        cdef long long index, end_index
        cdef vector[ListStruct*] staged

        # support slicing
        if isinstance(key, slice):
            # get bounds of slice
            start, stop, step = key.indices(len(self))
            if (start > stop and step > 0) or (start < stop and step < 0):
                return  # Python does nothing in this case

            # determine direction of traversal to avoid backtracking
            index, end_index = self._get_slice_direction(start, stop, step)

            # get first struct in slice, counting from nearest end
            curr = self._struct_at_index(index)

            # NOTE: we shouldn't delete items as we iterate.  Instead, we stage
            # the deletions and then perform them all at once at the end.

            # forward traversal
            step = abs(step)  # drop sign
            if end_index >= index:
                while curr is not NULL and index < end_index:
                    staged.push_back(curr)
                    for i in range(step):  # jump according to step size
                        if curr is NULL:
                            break
                        curr = curr.next
                    index += step  # increment index

            # backward traversal
            else:
                while curr is not NULL and index > end_index:
                    staged.push_back(curr)
                    for i in range(step):  # jump according to step size
                        if curr is NULL:
                            break
                        curr = curr.prev
                    index -= step

            # delete all staged structs
            for i in range(staged.size()):
                self._remove_struct(staged[i])

        # index directly
        else:
            key = self._normalize_index(key)
            curr = self._struct_at_index(key)
            self._remove_struct(curr)

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
        cdef PyObject* borrowed = <PyObject*>item  # borrowed reference
        cdef ListStruct* c_struct = self._head
        cdef int comp

        # we iterate entirely at the C level for maximum performance
        while c_struct is not NULL:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(c_struct.value, borrowed, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # remove node if equal
            if comp == 1:
                self._remove_struct(c_struct)
                return

            # advance to next struct
            c_struct = c_struct.next

        raise ValueError(f"{repr(item)} is not in list")

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
        # NOTE: we have to free the underlying memory if there are no more
        # references to the list, otherwise we'll create a memory leak.

        raise NotImplementedError("clear() currently results in memory leaks")

        # TODO: revisit this with more advanced garbage collection
        self._head = NULL
        self._tail = NULL
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

        # get struct at index
        cdef ListStruct* c_struct = self._struct_at_index(index)
        cdef object value = <object>c_struct.value  # owned reference

        # NOTE: it's important we store an owned reference to the value before
        # removing the struct, otherwise the value can be freed and we'll
        # return a dangling pointer.

        # drop struct and return contents
        self._remove_struct(c_struct)
        return value

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
        if self._head is NULL:
            raise IndexError("pop from empty list")

        # no need to handle indices, just skip straight to head
        cdef ListStruct* c_struct = self._head
        cdef object value = <object>c_struct.value  # owned reference

        # NOTE: it's important we store an owned reference to the value before
        # removing the struct, otherwise the value can be freed and we'll
        # return a dangling pointer.

        # drop struct and return contents
        self._remove_struct(c_struct)
        return value

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
        if self._tail is NULL:
            raise IndexError("pop from empty list")

        # no need to handle indices, just skip straight to tail
        cdef ListStruct* c_struct = self._tail
        cdef object value = <object>c_struct.value  # owned reference

        # NOTE: it's important we store an owned reference to the value before
        # removing the struct, otherwise the value can be freed and we'll
        # return a dangling pointer.

        # drop struct and return contents
        self._remove_struct(c_struct)
        return value

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

        cdef LinkedList other_list = <LinkedList>other  # cast to C type
        cdef ListStruct* a = self._head
        cdef ListStruct* b = other_list._head

        # compare elements at each index
        while a is not NULL and b is not NULL:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(a.value, b.value, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # return if unequal
            if comp == 0:
                # C API equivalent of the < operator
                comp = PyObject_RichCompareBool(a.value, b.value, Py_LT)
                if comp == -1:  # < failed
                    raise_exception()
                return comp

            # advance to next pair
            a = a.next
            b = b.next

        # if all elements are equal, the shorter list is less than the longer
        return self.size < other_list.size

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

        cdef LinkedList other_list = <LinkedList>other  # cast to C type
        cdef ListStruct* a = self._head
        cdef ListStruct* b = other_list._head

        # compare elements at each index
        while a is not NULL and b is not NULL:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(a.value, b.value, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # return if unequal
            if comp == 0:
                # C API equivalent of the < operator
                comp = PyObject_RichCompareBool(a.value, b.value, Py_LT)
                if comp == -1:  # < failed
                    raise_exception()
                return comp

            # advance to next pair
            a = a.next
            b = b.next

        # if all elements are equal, the shorter list is less than or equal to
        # the longer
        return self.size <= other_list.size

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

        cdef LinkedList other_list = <LinkedList>other  # cast to C type

        # check for equal size
        if self.size != other_list.size:
            return False

        cdef ListStruct* a = self._head
        cdef ListStruct* b = other_list._head

        # compare elements at each index
        while a is not NULL and b is not NULL:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(a.value, b.value, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # return if unequal
            if comp == 0:
                return False

            # advance to next pair
            a = a.next
            b = b.next

        return True

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

        cdef LinkedList other_list = <LinkedList>other  # cast to C type
        cdef ListStruct* a = self._head
        cdef ListStruct* b = other_list._head

        # compare elements at each index
        while a is not NULL and b is not NULL:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(a.value, b.value, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # return if unequal
            if comp == 0:
                # C API equivalent of the > operator
                comp = PyObject_RichCompareBool(a.value, b.value, Py_GT)
                if comp == -1:
                    raise_exception()
                return comp

            # advance to next pair
            a = a.next
            b = b.next

        # if all elements are equal, the longer list is greater than the
        # shorter
        return self.size > other_list.size

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

        cdef LinkedList other_list = <LinkedList>other  # cast to C type
        cdef ListStruct* a = self._head
        cdef ListStruct* b = other_list._head

        # compare elements at each index
        while a is not NULL and b is not NULL:
            # C API equivalent of the == operator
            comp = PyObject_RichCompareBool(a.value, b.value, Py_EQ)
            if comp == -1:  # == failed
                raise_exception()

            # return if unequal
            if comp == 0:
                # C API equivalent of the > operator
                comp = PyObject_RichCompareBool(a.value, b.value, Py_GT)
                if comp == -1:
                    raise_exception()
                return comp

            # advance to next pair
            a = a.next
            b = b.next

        # if all elements are equal, the longer list is greater than or equal
        # to the shorter
        return self.size >= other_list.size

    #######################
    ####    PRIVATE    ####
    #######################

    cdef void _add_struct(self, ListStruct* prev, ListStruct* curr, ListStruct* next):
        """Add a struct to the list.

        Parameters
        ----------
        prev : ListStruct*
            The struct that should precede the new struct in the list.
        curr : ListStruct*
            The struct to add to the list.
        next : ListStruct*
            The struct that should follow the new struct in the list.

        Notes
        -----
        This is a helper method for doing the pointer arithmetic of adding a
        node to the list, since it's used in multiple places.
        """
        # prev <-> curr
        curr.prev = prev
        if prev is NULL:
            self._head = curr
        else:
            prev.next = curr

        # curr <-> next
        curr.next = next
        if next is NULL:
            self._tail = curr
        else:
            next.prev = curr

        # increment size
        self.size += 1

    cdef void _remove_struct(self, ListStruct* curr):
        """Remove a struct from the list.

        Parameters
        ----------
        curr : ListStruct*
            The struct to remove from the list.

        Notes
        -----
        This is a helper method for doing the pointer arithmetic of removing a
        node, as well as handling reference counts and freeing the underlying
        memory.
        """
        # prev <-> next
        if curr.prev is NULL:
            self._head = curr.next
        else:
            curr.prev.next = curr.next

        # prev <-> next
        if curr.next is NULL:
            self._tail = curr.prev
        else:
            curr.next.prev = curr.prev

        # free memory
        decref(curr)

        # decrement size
        self.size -= 1

    cdef ListStruct* _struct_at_index(self, long long index):
        """Get the struct at the specified index.

        Parameters
        ----------
        index : int64
            The index of the struct to retrieve.  This should always be passed
            through :meth:`LinkedList._normalize_index` first.

        Returns
        -------
        ListStruct*
            The struct at the specified index.

        Notes
        -----
        This method is O(n) on average.  As an optimization, it always iterates
        from the nearest end of the list.
        """
        cdef ListStruct* curr
        cdef long long i

        # count forwards from head
        if index <= self.size // 2:
            curr = self._head
            for i in range(index):
                curr = curr.next

        # count backwards from tail
        else:
            curr = self._tail
            for i in range(self.size - index - 1):
                curr = curr.prev

        return curr

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

    cdef ListStruct* _split(self, ListStruct* head, long long length):
        """Split a linked list into sublists of the specified length.

        Parameters
        ----------
        head : ListStruct*
            The head of the list to split.
        length : int64
            The maximum length of each split.  This method will walk forward
            from ``head`` by this many nodes and then split the list.

        Returns
        -------
        ListStruct*
            The head of the next sublist.

        Notes
        -----
        This method is O(length).  It just iterates forward ``length`` times
        and then splits the list at that point.
        """
        cdef ListStruct* split
        cdef long long i

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

    cdef (ListStruct*, ListStruct*) _merge(
        self,
        ListStruct* left,
        ListStruct* right,
        ListStruct* temp
    ):
        """Merge two sorted linked lists into a single sorted list.

        Parameters
        ----------
        left : ListStruct*
            The head of the first sorted list.
        right : ListStruct*
            The head of the second sorted list.
        temp : ListStruct*
            A temporary node to use as the head of the merged list.  As an
            optimization, this is allocated once and then passed as a parameter
            rather than creating a new one every time this method is called.

        Returns
        -------
        head : ListStruct*
            The head of the merged list.
        tail : ListStruct*
            The tail of the merged list.

        Notes
        -----
        This is a standard implementation of the divide-and-conquer merge
        algorithm.  It is O(l) where `l` is the length of the longer list.
        """
        cdef ListStruct* curr = temp
        cdef ListStruct* tail

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

        # TODO: make sure there are no dangling pointers to temp anywhere

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
        cdef ListStruct* curr = self._head

        # NOTE: we iterate entirely at the C level for maximum performance

        while curr is not NULL:
            yield <object>curr.value  # yield owned reference
            curr = curr.next

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
        cdef ListStruct* curr = self._tail

        while curr is not NULL:
            yield <object>curr.value  # yield owned reference
            curr = curr.prev

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
        cdef ListStruct* curr
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

            # advance to next struct
            curr = curr.next

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
        return f"[{', '.join(iter(self))}]"

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
        return f"{type(self).__name__}([{', '.join(iter(self))}])"


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
        # check if item is already present
        if self.__contains__(item):
            raise ValueError(f"list elements must be unique: {repr(item)}")

        LinkedList.append(self, item)

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
        # check if item is already present
        if self.__contains__(item):
            raise ValueError(f"list elements must be unique: {repr(item)}")

        LinkedList.appendleft(self, item)

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
        # check if item is already present
        if self.__contains__(item):
            raise ValueError(f"list elements must be unique: {repr(item)}")

        LinkedList.insert(self, item, index)

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
        return <long long>(self.__contains__(item))

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
        if not self.__contains__(item):
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
        cdef ListStruct* curr
        cdef long long slice_size
        cdef long long start, stop, step, i
        cdef long long index, end_index
        cdef unordered_set[PyObject*] replaced_items
        cdef vector[Pair] staged
        cdef Pair p  # for iterating over `staged`
        cdef object val, old_item

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

            # get first struct in slice, counting from nearest end
            curr = self._struct_at_index(index)

            # NOTE: due to the uniqueness constraint, we can't just blindly
            # overwrite values in the slice, as some of them might be present
            # elsewhere in the list.  We also don't care if a value is in the
            # masked items, since they will be overwritten anyway.  To address
            # this, we record the observed values and stage our changes to
            # avoid modifying values until we are sure they are valid.

            # forward traversal
            values_iter = iter(value)
            if end_index >= index:
                for val in values_iter:
                    if curr is NULL or index == end_index:
                        break

                    # check for uniqueness and stage the change
                    replaced_items.insert(curr.value)
                    if val in self.nodes and val not in replaced_items:
                        raise ValueError(
                            f"list elements must be unique: {repr(val)}"
                        )
                    p.first = curr
                    p.second = <PyObject*>val
                    staged.push_back(p)

                    # jump according to step size
                    for i in range(step):
                        if curr is NULL:
                            break
                        curr = curr.next

                    # increment index
                    index += step

            # backward traversal
            else:
                for val in reversed(list(values_iter)):
                    if curr is NULL or index == end_index:
                        break

                    # check for uniqueness and stage the change
                    replaced_items.insert(curr.value)
                    if val in self.nodes and val not in replaced_items:
                        raise ValueError(
                            f"list elements must be unique: {repr(val)}"
                        )
                    p.first = curr
                    p.second = <PyObject*>val
                    staged.push_back(p)

                    # jump according to step size
                    for i in range(step):
                        if curr is NULL:
                            break
                        curr = curr.prev

                    # decrement index
                    index -= step

            # everything's good: update the list
            for old_item in replaced_items:
                del self.nodes[old_item]
            for p in staged:
                replace_value(<ListStruct*>p.first, <PyObject*>p.second)
                self.nodes[<PyObject*>p.second] = <ListStruct*>p.first

        # index directly
        else:
            key = self._normalize_index(key)
            curr = self._struct_at_index(key)

            # check for uniqueness
            if value in self.nodes and value != <object>curr.value:
                raise ValueError(f"list elements must be unique: {repr(value)}")

            # update the node's item and the items map
            del self.nodes[curr.value]
            replace_value(curr, value)
            self.nodes[value] = curr

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
        cdef ListStruct* curr

        # check if item is present in hash map
        try:
            curr = self.nodes[item]
        except KeyError:
            raise ValueError(f"{repr(item)} is not contained in the list")

        # handle pointer arithmetic
        self._remove_struct(curr)

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

    cdef void _add_struct(self, ListStruct* prev, ListStruct* curr, ListStruct* next):
        """Add a struct to the list.

        Parameters
        ----------
        prev : ListStruct*
            The struct that should precede the new struct in the list.
        curr : ListStruct*
            The struct to add to the list.
        next : ListStruct*
            The struct that should follow the new struct in the list.

        Notes
        -----
        This is a helper method for doing the pointer arithmetic of adding a
        node to the list, since it's used in multiple places.
        """
        LinkedList._add_struct(self, prev, curr, next)
        self.nodes[<object>curr.value] = curr  # add to hash map

    cdef void _remove_struct(self, ListStruct* curr):
        """Remove a struct from the list.

        Parameters
        ----------
        curr : ListStruct*
            The struct to remove from the list.

        Notes
        -----
        This is a helper method for doing the pointer arithmetic of removing a
        node, as well as handling reference counts and freeing the underlying
        memory.
        """
        LinkedList._remove_struct(self, curr)

        # remove from hash map
        del self.nodes[<object> curr.value]

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
