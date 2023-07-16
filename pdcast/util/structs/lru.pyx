"""This module describes a fixed-size dictionary object that implements a
Least-Recently-Used (LRU) caching strategy.

Classes
-------
LRUDict
    A dictionary subclass that evicts the least recently used key when its
    length exceeds a fixed value.
"""
from typing import Any

cimport cython


cdef object no_default = object()


# TODO: use a doubly-linked list for the order register.  This makes
# move_to_end() as fast as possible, since we only have to iterate through the
# list once.  It also makes purge() faster, since we can just chop off the
# remaining nodes after the first bad one we find.
# -> use a HashedList for this.


cdef class LRUDict(dict):
    """A fixed-size dictionary subclass implementing the Least Recently Used
    (LRU) caching strategy.

    Parameters
    ----------
    *args, **kwargs:
        Arbitrary arguments to pass to the standard :class:`dict() <python:dict>`
        constructor.
    maxsize: uint32, default 128
        The maximum number of keys to hold in cache.  If inserting a key would
        cause the dictionary to overflow past this length, then the least
        recently used keys are evicted according to their position in the
        order register.




    order : list
        A list of all the keys that are present in the underlying dictionary,
        which keeps track of the order in which they were inserted/requested.
        The least recent entries are at the start of the list, and the most
        recent ones are at the end.
    """

    def __init__(self, *args, unsigned int maxsize = 128, **kwargs):
        super().__init__(*args, **kwargs)
        self.maxsize = maxsize
        self.order = list(self)

    ##################################
    ####    DICTIONARY METHODS    ####
    ##################################

    def get(self, key: Any, default: Any | None = None) -> Any:
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

    def setdefault(self, key: Any, default: Any = None) -> Any:
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

    def update(self, other: Any = None, **kwargs: Any) -> None:
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
        # insert mapping first
        for k, v in dict(other).items():
            self[k] = v

        # kwargs go higher in order register
        for k, v in kwargs.items():
            self[k] = v

    def pop(self, key: Any, default: Any = no_default) -> Any:
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
            value = super().__getitem__(key)
            del self[key]
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
        key, value = super().popitem()
        self.order.remove(key)
        return (key, value)

    def clear(self) -> None:
        """Remove all key/value pairs from the dictionary and order register.

        Notes
        -----
        This method emulates the standard :meth:`dict.clear() <python:dict.clear>`
        method, except that it also clears entries from the order register.
        """
        super().clear()
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
        # get a list of item tuples and sort to match order register
        keyfunc = lambda item: self.order.index(item[0])
        reordered = dict(sorted(self.items(), key=keyfunc))
        return LRUDict(reordered, maxsize=self.maxsize)

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
        # get a list of item tuples and sort to match priority register
        keyfunc = lambda item: self.priority.index(item[0])
        reordered = sorted(self.items(), key=keyfunc)

        # clear original dictionary and replace items in new order
        super().clear()
        for (k, v) in reordered:
            super().__setitem__(k, v)

    #######################
    ####    PRIVATE    ####
    #######################

    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef void move_to_end(self, object key):
        """Move the specified key to the top of the order register.

        Parameters
        ----------
        key: Any
            The key to move.

        Notes
        -----
        This is a private method that is only accessible within Cython.  It is
        not exposed to Python.
        """
        cdef unsigned int size = len(self) - 1
        cdef unsigned int index

        # NOTE: as an optimization, we always count from the right side of the
        # order register (most recent) rather than the left (least recent).
        # This favors the most recent keys, which are more likely to be
        # accessed repeatedly.

        for index in range(size, -1, -1):
            if self.order[index] == key:
                break

        # if key is already at the top of the register, do nothing
        if index != size:
            self.order.append(self.order.pop(index))

    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef void purge(self):
        """Remove overflowing keys according to their position in the order
        register.

        Notes
        -----
        This is a private method that is only accessible within Cython.  It is
        not exposed to Python.
        """
        cdef unsigned int length = len(self)
        cdef unsigned int overflowing
        cdef unsigned int i
        cdef object key

        if self.maxsize is not None and length > self.maxsize:
            overflowing = length - self.maxsize
            for i in range(overflowing):
                key = self.order.pop(0)
                super(LRUDict, self).pop(key)

    ###############################
    ####    SPECIAL METHODS    ####
    ###############################

    def __getitem__(self, key: Any) -> Any:
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
        value = super().__getitem__(key)  # retrieve item from dict
        self.move_to_end(key)  # update LRU order
        return value

    def __setitem__(self, key: Any, value: Any) -> None:
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
        super().__setitem__(key, value)  # set item in dict
        if key in self.order:
            self.move_to_end(key)  # move key to highest order
        else:
            self.order.append(key)  # add key as highest order
            self.purge()  # purge low order key(s) if above maxsize

    def __delitem__(self, key: Any) -> None:
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
        self.order.remove(key)
