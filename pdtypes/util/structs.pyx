"""Rudimentary data structures used in `pdtypes`.

Classes
-------
LRUDict(dict)
    A fixed-size dictionary subclass implementing the Least Recently Used
    (LRU) caching strategy.
"""
cimport cython


cdef class LRUDict(dict):
    """A fixed-size dictionary subclass implementing the Least Recently Used
    (LRU) caching strategy.

    Attributes
    ----------
    maxsize : unsigned short, default 128
        The maximum number of keys held in the cache dictionary.  If inserting
        a key would cause the dictionary to overflow past this length, then the
        least recently used keys are evicted according to their position in
        the priority register.
    priority : list
        A list of all the keys that are present in the underlying dictionary,
        which keeps track of the order in which they were inserted/requested.
        The least recent entries are at the start of the list, and the most
        recent ones are at the end.
    *args, **kwargs
        Additional arguments passed to the standard `dict()` constructor.

    Methods
    -------
    clear()
        Remove all key/value pairs from the dictionary and priority register.
    copy()
        Return another `LRUDict` with the same contents as the original,
        including priorities.
    copy()
        Return another `LRUDict` with the same contents as the original,
        with the same priority register.
    get(key[, default])
        Return the value for `key` and move it to the top of the priority if
        `key` is in the dictionary register.  Else, return `default`.  If
        `default` is not given, it defaults to `None`, so that this method
        never raises a KeyError.
    move_to_end(key)
        Move the specified key to the top of the priority register.  Cdef-only.
    pop(key[, default])
        If `key` is in the dictionary, remove it from both the dictionary and
        priority register and return its value.  Else, return `default`.  If
        `default` is not given, and `key` is not in the dictionary, a KeyError
        is raised.
    popitem()
        Remove and return a `(key, value)` pair from the dictionary and
        priority register.  Pairs are returned in LIFO order.
    purge()
        Remove overflowing keys according to their priority.  Cdef-only.
    setdefault(key[, default])
        If `key` is in the dictionary, return its value and update its position
        in the priority register.  Else, insert `key` with a value of `default`
        and return `default`.  `default` defaults to `None`.
    update([other])
        Update the dictionary with the key/value pairs from `other`,
        overwriting existing keys.  Priorities are assigned to each new
        key/value pair in order.
    """

    def __init__(
        self,
        *args,
        unsigned short maxsize=128,
        **kwargs
    ):
        super().__init__(*args, **kwargs)
        self.maxsize = maxsize
        self.priority = list(self)

    def clear(self):
        """Extends dict.clear() to also clear priority register."""
        super().__clear__()
        self.priority.clear()

    def copy(self):
        """Copies a full LRUDict instance, including the underlying priority
        register.
        """
        # get a list of item tuples and sort to match priority register
        keyfunc = lambda item: self.priority.index(item[0])
        reordered = dict(sorted(self.items(), key=keyfunc))
        return LRUDict(reordered, maxsize=self.maxsize)

    def get(self, key, default=None):
        """Patches __getitem__() logic into LRUDict.get()."""
        try:
            return self[key]
        except KeyError:
            return default

    cdef void move_to_end(self, key):
        """Move the specified key to the top of the priority register."""
        cdef unsigned short index = self.priority.index(key)
        self.priority.append(self.priority.pop(index))

    def pop(self, key, *args, **kwargs):
        """Extends dict.pop() to also remove keys from priority register."""
        try:
            value = super().__getitem__(key)
            del self[key]
            return value
        except KeyError as err:
            if args:
                return args[0]
            elif "default" in kwargs:
                return kwargs["default"]
            raise err

    def popitem(self):
        """Extends dict.popitem() to also remove keys from priority register.
        """
        key, value = super().popitem()
        self.priority.remove(key)
        return (key, value)

    @cython.boundscheck(False)
    @cython.wraparound(False)
    cdef void purge(self):
        """Remove overflowing keys according to their priority."""
        cdef unsigned short length = len(self)
        cdef unsigned short overflowing
        cdef unsigned short i
        cdef object key

        if self.maxsize is not None and length > self.maxsize:
            overflowing = length - self.maxsize
            for i in range(overflowing):
                key = self.priority.pop(0)
                super(LRUDict, self).pop(key)

    def setdefault(self, key, default=None):
        """Patches __getitem__()/__setitem__() logic into LRUDict.setdefault().
        """
        try:
            return self[key]
        except KeyError:
            self[key] = default
            return default

    def sort(self):
        """Sort dictionary entries according to their position in the priority
        register.
        """
        # get a list of item tuples and sort to match priority register
        keyfunc = lambda item: self.priority.index(item[0])
        reordered = sorted(self.items(), key=keyfunc)

        # clear original dictionary and replace items in new order
        super().clear()
        for (k, v) in reordered:
            super().__setitem__(k, v)

    def update(self, other):
        """Patches __setitem__ logic into LRUDict.update()."""
        for k, v in dict(other).items():
            self[k] = v

    def __delitem__(self, key):
        """Emulates standard dictionary key deletion, but propagates changes
        to the underlying priority register.
        """
        super().__delitem__(key)
        self.priority.remove(key)

    def __getitem__(self, key):
        value = super().__getitem__(key)  # retrieve item from dict
        self.move_to_end(key)  # update LRU priority
        return value

    def __setitem__(self, key, value):
        super().__setitem__(key, value)  # set item in dict
        if key in self.priority:
            self.move_to_end(key)  # move key to highest priority
        else:
            self.priority.append(key)  # add key as highest priority
            self.purge()  # purge low priority key(s) if above maxsize
