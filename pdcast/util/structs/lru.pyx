"""This module describes a fixed-size dictionary object that implements a
Least-Recently-Used (LRU) caching strategy.

Classes
-------
LRUDict
    A dictionary subclass that evicts the least recently used key when its
    length exceeds a fixed value.
"""
from typing import Any, Hashable

cimport cython

from .list cimport HashedList


cdef object no_default = object()


cdef class LRUDict(dict):
    """A fixed-size dictionary implementing a Least Recently Used (LRU) caching
    strategy.

    Parameters
    ----------
    *args, **kwargs:
        Arbitrary arguments to pass to the standard :class:`dict() <python:dict>`
        constructor.
    maxsize: int64, default 128
        The maximum number of keys to hold in cache.  If inserting a key would
        cause the dictionary to overflow past this length, then the least
        recently used keys are evicted according to their position in the
        order register.  If this is negative, then the dictionary has no fixed
        size and will never purge its elements.

    Notes
    -----
    This is a subclass of the standard :class:`dict() <python:dict>` to make it
    as compatible as possible with existing code.
    
    Since dictionaries are generally not ordered, these objects maintain a
    separate register that keeps track of the order in which keys have been
    accessed.  This is implemented as a doubly-linked list that is backed with
    a hash table for fast lookups.  This allows constant-time access to each
    of the keys within the register, as well as constant-time insertion and
    deletion of keys from either end.  Performance is thus comparable to the
    standard :class:`dict() <python:dict>`, with negligible overhead in most
    cases.
    """

    def __init__(
        self,
        *args,
        long long maxsize = 128,
        **kwargs
    ):
        super().__init__(*args, **kwargs)
        self.maxsize = maxsize
        self.order = HashedList(self)

    ##################################
    ####    DICTIONARY METHODS    ####
    ##################################

    def get(self, key: Hashable, default: Any | None = None) -> Any:
        """Get a value from the dictionary and update its position in the order
        register if it is present, else return ``default``.

        Parameters
        ----------
        key: Any
            The key to retrieve from the dictionary.
        default: Any, optional
            The value to return if ``key`` is not in the dictionary.  Defaults
            to :data:`None <python:None>`.

        Returns
        -------
        Any
            The value for ``key`` if it is in the dictionary, else ``default``.

        Notes
        -----
        This method emulates the standard :meth:`dict.get() <python:dict.get>`
        method, except that it also updates the order register if the key is
        already present.
        """
        try:
            return self[key]
        except KeyError:
            return default

    def setdefault(self, key: Hashable, default: Any = None) -> Any:
        """Get a value from the dictionary and update its position in the order
        register if it is present, else insert it with the default value.

        Parameters
        ----------
        key: Any
            The key to retrieve from the dictionary.
        default: Any, optional
            The value to insert if ``key`` is not in the dictionary.  Defaults
            to :data:`None <python:None>`.

        Returns
        -------
        Any
            The value for ``key`` if it is in the dictionary, else ``default``.

        Notes
        -----
        This method emulates the standard
        :meth:`dict.setdefault() <python:dict.setdefault>`, except that it also
        updates the order register if the key is already present, and purges
        overflowing keys if it is not.
        """
        try:
            return self[key]
        except KeyError:
            self[key] = default
            return default

    def update(self, other: Any = None, /, **kwargs: Any) -> None:
        """Insert the key/value pairs from ``other`` into the dictionary and
        update their positions in the order register.

        Parameters
        ----------
        other: Mapping[Any, Any] | Iterable[tuple[Any, Any]] | None, optional
            A mapping object or iterable of key/value pairs to insert into the
            dictionary.  Priorities are assigned to each new key/value pair in
            order.
        **kwargs: Any
            Additional key/value pairs to insert into the dictionary.  These
            are added after the items from ``other``, meaning they will end up
            higher in the order register if both arguments are supplied at
            once.

        Notes
        -----
        This method emulates the standard :meth:`dict.update() <python:dict.update>`
        method, except that it also updates the order register and purges
        overflowing keys if necessary.

        If ``other`` is :data:`None <python:None>` and no ``kwargs`` are
        supplied, then this method does nothing.
        """
        cdef object key, val

        # insert mapping first
        for key, val in dict(other).items():
            self[key] = val

        # kwargs go higher in order register
        for key, val in kwargs.items():
            self[key] = val

    def pop(self, key: Hashable, default: Any = no_default) -> Any:
        """Remove a key from the dictionary if it is present and return its
        value, else return ``default``.

        Parameters
        ----------
        key: Any
            The key to remove from the dictionary.
        default: Any, optional
            The value to return if ``key`` is not in the dictionary.  A
            ``KeyError`` will be raised if this is not given and ``key`` is
            not in the dictionary.

        Returns
        -------
        Any
            The value for ``key`` if it is in the dictionary, else ``default``.

        Raises
        ------
        KeyError
            If ``key`` is not in the dictionary and no ``default`` is given.

        Notes
        -----
        This method emulates the standard :meth:`dict.pop() <python:dict.pop>`
        method, except that it also removes the key from the order register.
        """
        try:
            value = super(type(self), self).pop(key)  # pop val from dictionary
            self.order.remove(key)  # remove from order register - O(1)
            return value

        except KeyError as err:
            if default is no_default:
                raise err
            return default

    def popitem(self) -> tuple:
        """Remove and return an arbitrary `(key, value)` pair from the
        dictionary.

        Returns
        -------
        tuple[Any, Any]
            A ``(key, value)`` pair from the dictionary.  The pair is always
            removed from the end of the order register in LIFO order.

        Notes
        -----
        This method emulates the standard :meth:`dict.popitem() <python:dict.popitem>`
        method, except that it also removes the key from the order register.
        """
        key, value = super(type(self), self).popitem()  # pop item from dict
        self.order.remove(key)  # remove from order register - O(1)
        return (key, value)

    def clear(self) -> None:
        """Remove all key/value pairs from the dictionary and order register.

        Notes
        -----
        This method emulates the standard :meth:`dict.clear() <python:dict.clear>`
        method, except that it also clears entries from the order register.
        """
        super(type(self), self).clear()
        self.order.clear()

    def copy(self) -> LRUDict:
        """Return a copy of this :class:`LRUDict`.

        Returns
        -------
        LRUDict
            A new :class:`LRUDict` with the same contents as this one, in the
            same order.

        Notes
        -----
        This method emulates the standard :meth:`dict.copy() <python:dict.copy>`
        method, except that it also copies the order register.
        """
        cdef list ordered = []
        cdef object key
        cdef tuple item

        # NOTE: we always extract the new dictionary in order so that the new
        # register matches the original 

        for key in self.order:
            item = (key, super(type(self), self).__getitem__(key))
            ordered.append(item)

        return LRUDict(ordered, maxsize=self.maxsize)

    ###########################
    ####    NEW METHODS    ####
    ###########################

    def sort(self) -> None:
        """Sort entries according to their position in the order register.

        Notes
        -----
        For performance reasons, the underlying dictionary is not actually
        stored in sorted order.  This optimizes key lookups, but also means
        that the order of the dictionary is not guaranteed to be synchronized
        with the order in which elements have been accessed.

        If the order of the dictionary is important to you, then this method
        will manually sort it to match the order register.
        """
        cdef list ordered = []
        cdef object key, val
        cdef tuple item

        # NOTE: what we're doing here is extracting each key from the
        # dictionary according to its position in the order register, then
        # clearing the dictionary and re-inserting each item.  This doesn't
        # modify the order register in any way, just the underlying dictionary.

        # extract items in order
        for key in self.order:
            item = (key, super(type(self), self).__getitem__(key))
            ordered.append(item)

        # clear original dictionary
        super(type(self), self).clear()

        # re-insert items in order
        for key, val in ordered:
            super().__setitem__(key, val)

    #######################
    ####    PRIVATE    ####
    #######################

    cdef void purge(self):
        """Remove overflowing keys according to their position in the order
        register.

        Notes
        -----
        This is a private method that is only accessible from within Cython.
        It is not exposed to Python.
        """
        cdef long long length = len(self)
        cdef long long to_purge, i
        cdef object key

        # NOTE: the order register is stored from most to least recent, so all
        # we need to do to is pop from the rightmost end until we're no longer
        # overflowing.

        # only purge if maxsize is positive and we're overflowing
        if self.maxsize >= 0 and length > self.maxsize:
            to_purge = length - self.maxsize
            for i in range(to_purge):
                key = self.order.popright()  # O(1) due to linked list
                dict.__delitem__(self, key)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __getitem__(self, key: Hashable) -> Any:
        """Get a value from the dictionary and update its position in the order
        register.

        Parameters
        ----------
        key: Any
            The key to retrieve from the dictionary.

        Returns
        -------
        Any
            The value corresponding to ``key``.

        Raises
        ------
        KeyError
            If ``key`` is not in the dictionary.

        Notes
        -----
        This method emulates the standard
        :meth:`dict.__getitem__() <python:dict.__getitem__>` method, except
        that it also updates the order register.
        """
        # retrieve item from dict
        cdef object value = super(type(self), self).__getitem__(key)

        # only update order register if key is not already at the top
        if key != self.order.head.value:
            self.order.remove(key)  # O(1) due to hash table
            self.order.appendleft(key)  # O(1) due to linked list

        return value

    def __setitem__(self, key: Hashable, value: Any) -> None:
        """Insert a key/value pair into the dictionary and update its position
        in the order register.

        Parameters
        ----------
        key: Any
            The key to insert into the dictionary.
        value: Any
            The value to assign to ``key``.

        Notes
        -----
        This method emulates the standard
        :meth:`dict.__setitem__() <python:dict.__setitem__>` method, except
        that it also updates the order register and purges overflowing keys if
        necessary.
        """
        # set item in dict
        super().__setitem__(key, value)

        # update order register
        if key not in self.order:  # O(1) due to hash table
            self.order.appendleft(key)  # O(1) due to linked list
            self.purge()

        # only update order register if key is not already at the top
        elif key != self.order.head.value:
            self.order.remove(key)  # O(1) due to hash table
            self.order.appendleft(key)  # O(1) due to linked list

    def __delitem__(self, key: Hashable) -> None:
        """Remove a key from the dictionary and order register.

        Parameters
        ----------
        key: Any
            The key to remove from the dictionary.

        Raises
        ------
        KeyError
            If ``key`` is not in the dictionary.

        Notes
        -----
        This method emulates the standard
        :meth:`dict.__delitem__() <python:dict.__delitem__>` method, except
        that it also removes the key from the order register.
        """
        super().__delitem__(key)
        self.order.remove(key)  # O(1) due to hash table
