"""This module contains a templated merge sort algorithm for sorting linked
list data structures.
"""
from cpython.ref cimport PyObject
from libc.stdlib cimport malloc, free

from .base cimport (
    DEBUG, ListNode, SingleNode, DoubleNode, HashNode, DictNode,
    Pair, raise_exception
)


######################
####    PUBLIC    ####
######################


cdef Pair* merge_sort(
    SortNode* head,
    SortNode* tail,
    size_t size,
    bint reverse = False,
):
    """Sort a list by value.

    Parameters
    ----------
    head : SortNode*
        The head of the unsorted list.  This can be any of the standard node
        types or one of their decorated equivalents.  The same algorithm is
        applied either way.
    tail : SortNode*
        The tail of the unsorted list.  This must be of the same type as
        ``head``.
    size : size_t
        The overall length of the list.
    reverse : bool, optional
        Indicates whether to sort the list in descending order.  The
        default is ``False``, which sorts in ascending order.

    Returns
    -------
    Pair
        A simple ``Pair`` struct that holds two items as ``void*`` pointers.
        ``Pair.first`` represents the head of the sorted list, while
        ``Pair.second`` represents the tail.  These must be typecast to the
        expected node type before they can be used.  Don't forget to ``free()``
        the resulting struct!

    Notes
    -----
    The only reason why this doesn't return a normal C tuple is because fused
    types don't support them.
    """
    cdef SortNode* sorted_head = NULL  # head of sorted list
    cdef SortNode* sorted_tail = NULL  # tail of sorted list
    cdef SortNode* curr = head
    cdef SortNode* sub_left     # head of left sublist
    cdef SortNode* sub_right    # head of right sublist
    cdef SortNode* sub_head     # head of merged sublist
    cdef SortNode* sub_tail     # tail of merged sublist
    cdef size_t length = 1      # length of each sublist for this iteration
    cdef Pair* pair             # temporary Pair tuple
    cdef size_t freed_nodes     # number of nodes freed in case of error
    cdef SortError sort_err     # exception to raise if error occurs

    if DEBUG:
        print("    -> malloc: temp node")

    # NOTE: allocating `temp` outside of _merge() allows us to avoid
    # creating a new one every time we merge two sublists.
    cdef SortNode* temp = <SortNode*>malloc(sizeof(SortNode))
    if temp is NULL:  # malloc() failed to allocate a new block
        if SortNode in KeyedNode:
            freed_nodes = free_decorated(sorted_head)
            if DEBUG:
                print(f"    -> cleaned up {freed_nodes} temporary nodes")
        raise MemoryError()

    # NOTE: as a refresher, the general merge sort algorithm is as follows:
    #   1) divide the list into sublists of length 1 (bottom-up)
    #   2) sort pairs of elements from left to right and merge
    #   3) double the length of the sublists and repeat step 2
    while length <= size:
        # reset head and tail of sorted list
        sorted_head = NULL
        sorted_tail = NULL

        # divide and conquer
        while curr is not NULL:
            # split the linked list into two sublists of size `length`
            sub_left = curr
            sub_right = split(sub_left, length)
            curr = split(sub_right, length)

            # merge the two sublists in sorted order
            try:
                pair = merge(sub_left, sub_right, temp, reverse)
                sub_head = <SortNode*>pair.first
                sub_tail = <SortNode*>pair.second
                free(pair)  # clean up temporary Pair
            except Exception as err:  # an error occurred during comparison
                sort_err = SortError()
                sort_err.original = err

                # if sort is keyed, just delete all decorated nodes
                if SortNode in KeyedNode:
                    freed_nodes = 1  # temp node
                    freed_nodes += free_decorated(sorted_head)
                    freed_nodes += free_decorated(sub_left)
                    freed_nodes += free_decorated(sub_right)
                    freed_nodes += free_decorated(curr)
                    sort_err.head = NULL  # no cleanup necessary
                    sort_err.tail = NULL
                    if DEBUG:
                        print(f"    -> cleaned up {freed_nodes} temporary nodes")
                else:
                    pair = recover_list(
                        sorted_head, sorted_tail, sub_left, sub_right, curr
                    )
                    sort_err.head = pair.first   # head of partially-sorted list
                    sort_err.tail = pair.second  # tail of partially-sorted list
                    free(pair)
                    if DEBUG:
                        print("    -> free: temp node")

                free(temp)
                raise sort_err  # propagate error

            # if this is our first merge, set the head of the merged list
            if sorted_tail is NULL:
                sorted_head = sub_head
            else:  # link the merged sublist to the previous one
                sorted_tail.next = sub_head
                if SortNode in HasPrev:
                    sub_head.prev = sorted_tail

            # set tail of merged list and move to next pair
            sorted_tail = sub_tail

        # update head of the list for the next iteration
        curr = sorted_head

        # double the length of the sublists and repeat
        length *= 2

    if DEBUG:
        print("    -> free: temp node")

    # clean up temporary node
    free(temp)

    # return the head and tail of the sorted list
    cdef Pair* result = <Pair*>malloc(sizeof(Pair))
    result.first = sorted_head
    result.second = sorted_tail
    return result


cdef (KeyedSingleNode*, KeyedSingleNode*) decorate_single(
    SingleNode* head,
    SingleNode* tail,
    PyObject* key,
):
    """Decorate a list of ``SingleNodes`` using the specified key function.

    Parameters
    ----------
    head : SingleNode*
        The head of the undecorated list.
    tail : SingleNode*
        The tail of the undecorated list.
    key : PyObject*
        The key function to compute for each node.

    Returns
    -------
    head : KeyedSingleNode*
        The head of the decorated list.
    tail : KeyedSingleNode*
        The tail of the decorated list.
    """
    cdef SingleNode* undecorated = head       # current undecorated node
    cdef KeyedSingleNode* decorated               # current decorated node
    cdef KeyedSingleNode* decorated_head = NULL   # head of decorated list
    cdef KeyedSingleNode* decorated_tail = NULL   # tail of decorated list
    cdef PyObject* key_value                # key(undecorated.value)
    cdef size_t freed_nodes                 # number of nodes freed if error

    # NOTE: we iterate over the undecorated list, decorating each node and
    # building a parallel list.
    while undecorated is not NULL:
        # C API equivalent of `key(undecorated.value)`
        key_value = PyObject_CallFunctionObjArgs(key, undecorated.value, NULL)
        if key_value is NULL:  # key() raised an error
            try:
                raise_exception()  # propagate error
            except:  # clean up previous nodes
                freed_nodes = free_decorated(decorated_head)
                if DEBUG:
                    print(f"    -> cleaned up {freed_nodes} temporary nodes")
                raise  # re-raise error

        if DEBUG:
            print(f"    -> malloc: {<object>key_value}")

        # allocate a new KeyedSingleNode
        decorated = <KeyedSingleNode*>malloc(sizeof(KeyedSingleNode))
        if decorated is NULL:  # malloc() failed
            freed_nodes = free_decorated(decorated_head)  # clean up
            if DEBUG:
                print(f"    -> cleaned up {freed_nodes} temporary nodes")
            raise MemoryError()

        # initalize node
        decorated.node = undecorated
        decorated.key = key_value

        # update links
        decorated.next = NULL  # we set this in the next iteration
        if decorated_head is NULL:
            decorated_head = decorated
        else:
            decorated_tail.next = decorated

        # advance to next node
        decorated_tail = decorated
        undecorated = undecorated.next

    return (decorated_head, decorated_tail)


cdef (KeyedDoubleNode*, KeyedDoubleNode*) decorate_double(
    DoubleNode* head,
    DoubleNode* tail,
    PyObject* key,
):
    """Decorate a list of ``DoubleNodes`` using the specified key function.

    Parameters
    ----------
    head : DoubleNode*
        The head of the undecorated list.
    tail : DoubleNode*
        The tail of the undecorated list.
    key : PyObject*
        The key function to compute for each node.

    Returns
    -------
    head : KeyedDoubleNode*
        The head of the decorated list.
    tail : KeyedDoubleNode*
        The tail of the decorated list.
    """
    cdef DoubleNode* undecorated = head       # current undecorated node
    cdef KeyedDoubleNode* decorated               # current decorated node
    cdef KeyedDoubleNode* decorated_head = NULL   # head of decorated list
    cdef KeyedDoubleNode* decorated_tail = NULL   # tail of decorated list
    cdef PyObject* key_value                # key(undecorated.value)
    cdef size_t freed_nodes                 # number of nodes freed if error

    # NOTE: we iterate over the undecorated list, decorating each node and
    # building a parallel list.
    while undecorated is not NULL:
        # C API equivalent of `key(undecorated.value)`
        key_value = PyObject_CallFunctionObjArgs(key, undecorated.value, NULL)
        if key_value is NULL:  # key() raised an error
            try:
                raise_exception()  # propagate error
            except:  # clean up previous nodes
                freed_nodes = free_decorated(decorated_head)
                if DEBUG:
                    print(f"    -> cleaned up {freed_nodes} temporary nodes")
                raise  # re-raise error

        if DEBUG:
            print(f"    -> malloc: {<object>key_value}")

        # allocate a new KeyedDoubleNode
        decorated = <KeyedDoubleNode*>malloc(sizeof(KeyedDoubleNode))
        if decorated is NULL:  # malloc() failed
            freed_nodes = free_decorated(decorated_head)  # clean up
            if DEBUG:
                print(f"    -> cleaned up {freed_nodes} temporary nodes")
            raise MemoryError()

        # initalize node
        decorated.node = undecorated
        decorated.key = key_value

        # update links
        decorated.next = NULL  # we set this in the next iteration
        decorated.prev = decorated_tail  # prev <-> decorated
        if decorated_head is NULL:
            decorated_head = decorated
        else:
            decorated_tail.next = decorated

        # advance to next node
        decorated_tail = decorated
        undecorated = undecorated.next

    return (decorated_head, decorated_tail)


cdef (KeyedHashNode*, KeyedHashNode*) decorate_hash(
    HashNode* head,
    HashNode* tail,
    PyObject* key,
):
    """Decorate a list of ``HashNodes`` using the specified key function.

    Parameters
    ----------
    head : HashNode*
        The head of the undecorated list.
    tail : HashNode*
        The tail of the undecorated list.
    key : PyObject*
        The key function to compute for each node.

    Returns
    -------
    head : KeyedHashNode*
        The head of the decorated list.
    tail : KeyedHashNode*
        The tail of the decorated list.
    """
    cdef HashNode* undecorated = head       # current undecorated node
    cdef KeyedHashNode* decorated               # current decorated node
    cdef KeyedHashNode* decorated_head = NULL   # head of decorated list
    cdef KeyedHashNode* decorated_tail = NULL   # tail of decorated list
    cdef PyObject* key_value                # key(undecorated.value)
    cdef size_t freed_nodes                 # number of nodes freed if error

    # NOTE: we iterate over the undecorated list, decorating each node and
    # building a parallel list.
    while undecorated is not NULL:
        # C API equivalent of `key(undecorated.value)`
        key_value = PyObject_CallFunctionObjArgs(key, undecorated.value, NULL)
        if key_value is NULL:  # key() raised an error
            try:
                raise_exception()  # propagate error
            except:  # clean up previous nodes
                freed_nodes = free_decorated(decorated_head)
                if DEBUG:
                    print(f"    -> cleaned up {freed_nodes} temporary nodes")
                raise  # re-raise error

        if DEBUG:
            print(f"    -> malloc: {<object>key_value}")

        # allocate a new KeyedHashNode
        decorated = <KeyedHashNode*>malloc(sizeof(KeyedHashNode))
        if decorated is NULL:  # malloc() failed
            freed_nodes = free_decorated(decorated_head)  # clean up
            if DEBUG:
                print(f"    -> cleaned up {freed_nodes} temporary nodes")
            raise MemoryError()

        # initalize node
        decorated.node = undecorated
        decorated.key = key_value

        # update links
        decorated.next = NULL  # we set this in the next iteration
        decorated.prev = decorated_tail  # prev <-> decorated
        if decorated_head is NULL:
            decorated_head = decorated
        else:
            decorated_tail.next = decorated

        # advance to next node
        decorated_tail = decorated
        undecorated = undecorated.next

    return (decorated_head, decorated_tail)


cdef (KeyedDictNode*, KeyedDictNode*) decorate_dict(
    DictNode* head,
    DictNode* tail,
    PyObject* key,
):
    """Decorate a list of ``DictNodes`` using the specified key function.

    Parameters
    ----------
    head : DictNode*
        The head of the undecorated list.
    tail : DictNode*
        The tail of the undecorated list.
    key : PyObject*
        The key function to compute for each node.

    Returns
    -------
    head : KeyedDictNode*
        The head of the decorated list.
    tail : KeyedDictNode*
        The tail of the decorated list.
    """
    cdef DictNode* undecorated = head       # current undecorated node
    cdef KeyedDictNode* decorated               # current decorated node
    cdef KeyedDictNode* decorated_head = NULL   # head of decorated list
    cdef KeyedDictNode* decorated_tail = NULL   # tail of decorated list
    cdef PyObject* key_value                # key(undecorated.value)
    cdef size_t freed_nodes                 # number of nodes freed if error

    # NOTE: we iterate over the undecorated list, decorating each node and
    # building a parallel list.
    while undecorated is not NULL:
        # C API equivalent of `key(undecorated.value)`
        key_value = PyObject_CallFunctionObjArgs(key, undecorated.value, NULL)
        if key_value is NULL:  # key() raised an error
            try:
                raise_exception()  # propagate error
            except:  # clean up previous nodes
                freed_nodes = free_decorated(decorated_head)
                if DEBUG:
                    print(f"    -> cleaned up {freed_nodes} temporary nodes")
                raise  # re-raise error

        if DEBUG:
            print(f"    -> malloc: {<object>key_value}")

        # allocate a new KeyedDictNode
        decorated = <KeyedDictNode*>malloc(sizeof(KeyedDictNode))
        if decorated is NULL:  # malloc() failed
            freed_nodes = free_decorated(decorated_head)  # clean up
            if DEBUG:
                print(f"    -> cleaned up {freed_nodes} temporary nodes")
            raise MemoryError()

        # initalize node
        decorated.node = undecorated
        decorated.key = key_value

        # update links
        decorated.next = NULL  # we set this in the next iteration
        decorated.prev = decorated_tail  # prev <-> decorated
        if decorated_head is NULL:
            decorated_head = decorated
        else:
            decorated_tail.next = decorated

        # advance to next node
        decorated_tail = decorated
        undecorated = undecorated.next

    return (decorated_head, decorated_tail)


cdef (SingleNode*, SingleNode*) undecorate_single(KeyedSingleNode* head):
    """Rearrange all nodes in the list to match their positions in the
    decorated list and remove each decorator

    Parameters
    ----------
    head : KeyedSingleNode*
        The head of the decorated list.

    Returns
    -------
    head : SingleNode*
        The head of the undecorated list.
    tail : SingleNode*
        The tail of the undecorated list.
    """
    cdef KeyedSingleNode* next_decorated
    cdef SingleNode* undecorated
    cdef SingleNode* sorted_head = NULL
    cdef SingleNode* sorted_tail = NULL

    # NOTE: we free decorators as we go in order to avoid iterating over
    # the list twice.
    while head is not NULL:
        undecorated = head.node
        next_decorated = head.next

        # sorted_tail -> undecorated
        if sorted_tail is not NULL:
            sorted_tail.next = undecorated

        # undecorated -> next
        if next_decorated is NULL:
            undecorated.next = NULL
        else:
            undecorated.next = next_decorated.node

        # update sorted_head
        if sorted_head is NULL:
            sorted_head = undecorated

        if DEBUG:
            print(f"    -> free: {<object>head.key}")

        # release reference on precomputed key
        Py_DECREF(head.key)

        # free decorator
        free(head)

        # advance to next node
        sorted_tail = undecorated
        head = next_decorated

    # return head and tail of sorted list
    return (sorted_head, sorted_tail)


cdef (DoubleNode*, DoubleNode*) undecorate_double(KeyedDoubleNode* head):
    """Rearrange all nodes in the list to match their positions in the
    decorated list and remove each decorator

    Parameters
    ----------
    head : KeyedDoubleNode*
        The head of the decorated list.

    Returns
    -------
    head : DoubleNode*
        The head of the undecorated list.
    tail : DoubleNode*
        The tail of the undecorated list.
    """
    cdef KeyedDoubleNode* next_decorated
    cdef DoubleNode* undecorated
    cdef DoubleNode* sorted_head = NULL
    cdef DoubleNode* sorted_tail = NULL

    # NOTE: we free decorators as we go in order to avoid iterating over
    # the list twice.
    while head is not NULL:
        undecorated = head.node
        next_decorated = head.next

        # sorted_tail <-> undecorated
        undecorated.prev = sorted_tail
        if sorted_tail is not NULL:
            sorted_tail.next = undecorated

        # undecorated <-> next
        if next_decorated is NULL:
            undecorated.next = NULL
        else:
            undecorated.next = next_decorated.node

        # update sorted_head
        if sorted_head is NULL:
            sorted_head = undecorated

        if DEBUG:
            print(f"    -> free: {<object>head.key}")

        # release reference on precomputed key
        Py_DECREF(head.key)

        # free decorator
        free(head)

        # advance to next node
        sorted_tail = undecorated
        head = next_decorated

    # return head and tail of sorted list
    return (sorted_head, sorted_tail)


cdef (HashNode*, HashNode*) undecorate_hash(KeyedHashNode* head):
    """Rearrange all nodes in the list to match their positions in the
    decorated list and remove each decorator

    Parameters
    ----------
    head : KeyedHashNode*
        The head of the decorated list.

    Returns
    -------
    head : HashNode*
        The head of the undecorated list.
    tail : HashNode*
        The tail of the undecorated list.
    """
    cdef KeyedHashNode* next_decorated
    cdef HashNode* undecorated
    cdef HashNode* sorted_head = NULL
    cdef HashNode* sorted_tail = NULL

    # NOTE: we free decorators as we go in order to avoid iterating over
    # the list twice.
    while head is not NULL:
        undecorated = head.node
        next_decorated = head.next

        # sorted_tail <-> undecorated
        undecorated.prev = sorted_tail
        if sorted_tail is not NULL:
            sorted_tail.next = undecorated

        # undecorated <-> next
        if next_decorated is NULL:
            undecorated.next = NULL
        else:
            undecorated.next = next_decorated.node

        # update sorted_head
        if sorted_head is NULL:
            sorted_head = undecorated

        if DEBUG:
            print(f"    -> free: {<object>head.key}")

        # release reference on precomputed key
        Py_DECREF(head.key)

        # free decorator
        free(head)

        # advance to next node
        sorted_tail = undecorated
        head = next_decorated

    # return head and tail of sorted list
    return (sorted_head, sorted_tail)


cdef (DictNode*, DictNode*) undecorate_dict(KeyedDictNode* head):
    """Rearrange all nodes in the list to match their positions in the
    decorated list and remove each decorator

    Parameters
    ----------
    head : KeyedDictNode*
        The head of the decorated list.

    Returns
    -------
    head : DictNode*
        The head of the undecorated list.
    tail : DictNode*
        The tail of the undecorated list.
    """
    cdef KeyedDictNode* next_decorated
    cdef DictNode* undecorated
    cdef DictNode* sorted_head = NULL
    cdef DictNode* sorted_tail = NULL

    # NOTE: we free decorators as we go in order to avoid iterating over
    # the list twice.
    while head is not NULL:
        undecorated = head.node
        next_decorated = head.next

        # sorted_tail <-> undecorated
        undecorated.prev = sorted_tail
        if sorted_tail is not NULL:
            sorted_tail.next = undecorated

        # undecorated <-> next
        if next_decorated is NULL:
            undecorated.next = NULL
        else:
            undecorated.next = next_decorated.node

        # update sorted_head
        if sorted_head is NULL:
            sorted_head = undecorated

        if DEBUG:
            print(f"    -> free: {<object>head.key}")

        # release reference on precomputed key
        Py_DECREF(head.key)

        # free decorator
        free(head)

        # advance to next node
        sorted_tail = undecorated
        head = next_decorated

    # return head and tail of sorted list
    return (sorted_head, sorted_tail)


#######################
####    HELPERS    ####
#######################


cdef size_t free_decorated(KeyedNode* head):
    """This method is called when an error occurs during a keyed ``sort()``
    operation to clean up any ``KeyedNodes`` that have already been created.

    Parameters
    ----------
    head : KeyedNode*
        The head of the decorated list.  We iterate through the list and
        free all nodes starting from here.

    Returns
    -------
    size_t
        The number of nodes that were freed.
    """
    cdef KeyedNode* carry
    cdef size_t count = 0

    # delete decorated list
    while head is not NULL:
        carry = head.next
        free(head)
        head = carry
        count += 1

    return count


cdef SortNode* split(SortNode* curr, size_t length):
    """Split a linked list into sublists of the specified length.

    Parameters
    ----------
    curr : SortNode*
        The starting node to begin counting from.
    length : size_t
        The number of nodes to extract.  This method will walk forward from
        ``curr`` by this many steps and then split the list at that
        location.

    Returns
    -------
    SortNode*
        The node that comes after the last extracted node in the split.  If
        ``length`` exceeds the number of nodes left in the list, this will be
        ``NULL``.

    Notes
    -----
    This method is O(length).  It just iterates forward ``length`` times and
    then splits the list at that point.
    """
    cdef SortNode* result
    cdef size_t i

    # walk forward `length` nodes from `curr`
    for i in range(length - 1):
        if curr is NULL:
            break
        curr = curr.next

    # if we've reached the end of the list, there's nothing left to split
    if curr is NULL:
        return NULL

    # otherwise, split the list
    result = curr.next
    curr.next = NULL
    if SortNode in HasPrev:
        if result is not NULL:
            result.prev = NULL
    return result


cdef Pair* merge(SortNode* left, SortNode* right, SortNode* temp, bint reverse):
    """Merge two sorted linked lists into a single sorted list.

    Parameters
    ----------
    left : SortNode*
        The head of the first sorted list.
    right : SortNode*
        The head of the second sorted list.
    temp : SortNode*
        A temporary node to use as the head of the merged list.  As an
        optimization, this is allocated once and then passed as a parameter
        rather than creating a new one every time this method is called.
    reverse : bool
        Indicates whether to invert the relationship between each element.

    Returns
    -------
    head : SortNode*
        The head of the merged list.
    tail : SortNode*
        The tail of the merged list.

    Notes
    -----
    This is a standard implementation of the divide-and-conquer merge
    algorithm.  It is O(l) where `l` is the length of the longer list.
    """
    cdef SortNode* curr = temp  # temporary head of merged list
    cdef SortNode* tail         # tail of merged list
    cdef int comp

    # iterate through left and right sublists until one is empty
    while left is not NULL and right is not NULL:
        # C API equivalent of the < operator
        if SortNode in KeyedNode:
            comp = PyObject_RichCompareBool(left.key, right.key, Py_LT)
        else:
            comp = PyObject_RichCompareBool(left.value, right.value, Py_LT)

        # check for exception
        if comp == -1:
            raise_exception()  # propagate error back to sort()

        # append the smaller of the two candidates to merged list
        if comp ^ reverse:  # [not] left < right
            curr.next = left
            if SortNode in HasPrev:
                left.prev = curr
            left = left.next
        else:
            curr.next = right
            if SortNode in HasPrev:
                right.prev = curr
            right = right.next

        # advance to next node
        curr = curr.next

    # append the remaining nodes
    tail = right if left is NULL else left
    curr.next = tail
    if SortNode in HasPrev:
        tail.prev = curr

    # advance tail to end of merged list
    while tail.next is not NULL:
        tail = tail.next

    # unlink temporary head
    curr = temp.next  # curr becomes new head of merged list
    if SortNode in HasPrev:
        curr.prev = NULL
    temp.next = NULL

    # return the proper head and tail of the merged list
    cdef Pair* result = <Pair*>malloc(sizeof(Pair))
    result.first = curr
    result.second = tail
    return result

cdef Pair* recover_list(
    SortNode* head,
    SortNode* tail,
    SortNode* sub_left,
    SortNode* sub_right,
    SortNode* curr,
):
    """Helper method for recovering a list if an error occurs in the middle of
    an in-place ``sort()`` operation.

    Parameters
    ----------
    head : SortNode*
        The head of the sorted portion.
    tail : SortNode*
        The tail of the sorted portion.
    sub_left : SortNode*
        The head of the next sublist to merge.
    sub_right : SortNode*
        The head of the subsequent sublist to merge.
    curr : SortNode*
        The head of the unsorted portion of the list.

    Notes
    -----
    This method basically undoes a single `_split()` operation.  Given a
    partially-sorted list, it will merge the two sublists back into the
    original list in their current order, and then set the head and tail
    pointers to the proper values.  That way, the list is at least in a
    consistent state and can be garbage collected properly.
    """
    # link tail -> left
    if tail is not NULL:
        tail.next = sub_left

    # link tail <- left
    if sub_left is not NULL:
        if SortNode in HasPrev:
            sub_left.prev = tail
        while tail.next is not NULL:  # advance tail to end of sublist
            tail = tail.next
        tail.next = sub_right  # link left -> right

    # link left <- right
    if sub_right is not NULL:
        if SortNode in HasPrev:
            sub_right.prev = tail
        while tail.next is not NULL:  # advance tail to end of sublist
            tail = tail.next
        tail.next = curr  # link right -> curr

    # link right <- curr
    if curr is not NULL:
        if SortNode in HasPrev:
            curr.prev = tail
        while tail.next is not NULL:  # advance tail to end of list
            tail = tail.next

    # update head and tail pointers
    cdef Pair* result = <Pair*>malloc(sizeof(Pair))
    result.first = head
    result.second = tail
    return result
