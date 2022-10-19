import pandas as pd


class SeriesWrapper:
    """Decorator Pattern wrapper for pandas Series objects."""

    # `setattr()` calls to the fields specified in `unwrapped` will be passed
    # through, unmodified.  Everything else will be delegated to `wrapped`
    unwrapped = ("_series", "_hasnans", "_is_na")
    wrapped = "_series"

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        is_na: pd.Series = None
    ):
        self._series = series
        self._hasnans = hasnans
        self._is_na = is_na

    #######################
    ####    GENERAL    ####
    #######################

    @property
    def series(self) -> pd.Series:
        """Return the decorated pd.Series object."""
        return self._series

    ####################################
    ####    CACHE MISSING VALUES    ####
    ####################################

    @property
    def hasnans(self) -> bool:
        """True if `hasnans=True` was supplied to the SeriesWrapper
        constructor.
        """
        # cached
        if self._hasnans is not None:
            return self._hasnans

        # uncached
        self._hasnans = self.isna().any()
        return self._hasnans

    def isna(self) -> pd.Series:
        """A cached version of the ordinary Series.isna() method."""
        # cached
        if self._is_na is not None:
            return self._is_na

        # uncached
        self._is_na = self._series.isna()
        return self._is_na

    def isnull(self) -> pd.Series:
        """An alias for SeriesWrapper.isna()."""
        return self.isna()

    def notna(self) -> pd.Series:
        """A cached version of the ordinary Series.notna() method."""
        return ~self.isna()

    def notnull(self) -> pd.Series:
        """An alias for SeriesWrapper.notna()"""
        return self.notna()

    ###############################
    ####    DYNAMIC WRAPPER    ####
    ###############################

    def __iter__(self):
        return self.__dict__[self.__class__.wrapped].__iter__()

    def __next__(self):
        return self.__dict__[self.__class__.wrapped].__next__()

    def __getattr__(self, name):
        return getattr(self.__dict__[self.__class__.wrapped], name)

    def __setattr__(self, name, value):
        if name in self.__class__.unwrapped:
            self.__dict__[name] = value
        else:
            setattr(self.__dict__[self.__class__.wrapped], name, value)

    def __delattr__(self, name):
        delattr(self.__dict__[self.__class__.wrapped], name)

    def __repr__(self):
        return repr(self.__dict__[self.__class__.wrapped])

    def __str__(self):
        return str(self.__dict__[self.__class__.wrapped])

    ###########################
    ####    COMPARISONS    ####
    ###########################

    def __eq__(self, other):
        return self.__dict__[self.__class__.wrapped] == other

    def __ne__(self, other):
        return self.__dict__[self.__class__.wrapped] != other

    def __lt__(self, other):
        return self.__dict__[self.__class__.wrapped] < other

    def __le__(self, other):
        return self.__dict__[self.__class__.wrapped] <= other

    def __gt__(self, other):
        return self.__dict__[self.__class__.wrapped] > other

    def __ge__(self, other):
        return self.__dict__[self.__class__.wrapped] >= other

    ##########################
    ####    ARITHMETIC    ####
    ##########################

    def __add__(self, other):
        return self.__dict__[self.__class__.wrapped] + other

    def __radd__(self, other):
        return other + self.__dict__[self.__class__.wrapped]

    def __iadd__(self, other):
        return self.__dict__[self.__class__.wrapped].__iadd__(other)

    def __sub__(self, other):
        return self.__dict__[self.__class__.wrapped] - other

    def __rsub__(self, other):
        return other - self.__dict__[self.__class__.wrapped]

    def __isub__(self, other):
        return self.__dict__[self.__class__.wrapped].__isub__(other)

    def __mul__(self, other):
        return self.__dict__[self.__class__.wrapped] * other

    def __rmul__(self, other):
        return other * self.__dict__[self.__class__.wrapped]

    def __imul__(self, other):
        return self.__dict__[self.__class__.wrapped].__imul__(other)

    def __div__(self, other):
        return self.__dict__[self.__class__.wrapped] / other

    def __rdiv__(self, other):
        return other / self.__dict__[self.__class__.wrapped]

    def __idiv__(self, other):
        return self.__dict__[self.__class__.wrapped].__idiv__(other)

    def __floordiv__(self, other):
        return self.__dict__[self.__class__.wrapped] // other

    def __rfloordiv__(self, other):
        return other // self.__dict__[self.__class__.wrapped]

    def __ifloordiv__(self, other):
        return self.__dict__[self.__class__.wrapped].__ifloordiv__(other)

    def __mod__(self, other):
        return self.__dict__[self.__class__.wrapped] % other

    def __rmod__(self, other):
        return other % self.__dict__[self.__class__.wrapped]

    def __imod__(self, other):
        return self.__dict__[self.__class__.wrapped].__imod__(other)

    def __divmod__(self, other):
        return divmod(self.__dict__[self.__class__.wrapped], other)

    def __rdivmod__(self, other):
        return divmod(other, self.__dict__[self.__class__.wrapped])

    def __pow__(self, other):
        return self.__dict__[self.__class__.wrapped] ** other

    def __rpow__(self, other):
        return other ** self.__dict__[self.__class__.wrapped]

    def __ipow__(self, other):
        return self.__dict__[self.__class__.wrapped].__ipow__(other)

    def __neg__(self):
        return - self.__dict__[self.__class__.wrapped]

    def __pos__(self):
        return + self.__dict__[self.__class__.wrapped]

    def __abs__(self):
        return abs(self.__dict__[self.__class__.wrapped])

    def __invert__(self):
        return ~ self.__dict__[self.__class__.wrapped]

    def __lshift__(self, other):
        return self.__dict__[self.__class__.wrapped] << other

    def __ilshift__(self, other):
        return self.__dict__[self.__class__.wrapped].__ilshift__(other)

    def __rlshift__(self, other):
        return other << self.__dict__[self.__class__.wrapped]

    def __rshift__(self, other):
        return self.__dict__[self.__class__.wrapped] >> other

    def __rrshift__(self, other):
        return other >> self.__dict__[self.__class__.wrapped]

    def __irshift__(self, other):
        return self.__dict__[self.__class__.wrapped].__irshift__(other)

    def __and__(self, other):
        return self.__dict__[self.__class__.wrapped] & other

    def __rand__(self, other):
        return other & self.__dict__[self.__class__.wrapped]

    def __iand__(self, other):
        return self.__dict__[self.__class__.wrapped].__iand__(other)

    def __or__(self, other):
        return self.__dict__[self.__class__.wrapped] | other

    def __ror__(self, other):
        return other | self.__dict__[self.__class__.wrapped]

    def __ior__(self, other):
        return self.__dict__[self.__class__.wrapped].__ior__(other)

    def __xor__(self, other):
        return self.__dict__[self.__class__.wrapped] ^ other

    def __rxor__(self, other):
        return other ^ self.__dict__[self.__class__.wrapped]

    def __ixor__(self, other):
        return self.__dict__[self.__class__.wrapped].__ixor__(other)

    ###################################
    ####    NUMERIC CONVERSIONS    ####
    ###################################

    def __int__(self):
        return int(self.__dict__[self.__class__.wrapped])

    def __float__(self):
        return float(self.__dict__[self.__class__.wrapped])

    def __oct__(self):
        return oct(self.__dict__[self.__class__.wrapped])

    def __hex__(self):
        return hex(self.__dict__[self.__class__.wrapped])

    ####################################
    ####    SEQUENCES & MAPPINGS    ####
    ####################################

    def __len__(self):
        return len(self.__dict__[self.__class__.wrapped])

    def __getitem__(self, key):
        return self.__dict__[self.__class__.wrapped].__getitem__(key)

    def __setitem__(self, key, val):
        self.__dict__[self.__class__.wrapped].__setitem__(key, val)

    def __delitem__(self, key):
        self.__dict__[self.__class__.wrapped].__delitem__(key)

    def __getslice__(self, i, j):
        return self.__dict__[self.__class__.wrapped].__getslice__(i, j)

    def __setslice__(self, i, j, val):
        self.__dict__[self.__class__.wrapped].__setslice__(i, j, val)

    def __delslice__(self, i, j):
        self.__dict__[self.__class__.wrapped].__delslice__(i, j)

    def __contains__(self, val):
        return self.__dict__[self.__class__.wrapped].__contains__(val)

    ###########################
    ####    DESCRIPTORS    ####
    ###########################

    def __get__(self, instance, class_):
        return self.__dict__[self.__class__.wrapped].__get__(instance, class_)

    def __set__(self, instance, value):
        self.__dict__[self.__class__.wrapped].__set__(instance, value)

    def __delete__(self, instance):
        self.__dict__[self.__class__.wrapped].__delete__(instance)
