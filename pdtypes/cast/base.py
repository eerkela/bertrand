from __future__ import annotations
import decimal

import numpy as np
import pandas as pd


class SeriesWrapper:
    """Base wrapper for pd.Series objects.

    Implements a dynamic wrapper according to the Gang of Four's Decorator
    Pattern (not to be confused with python decorators).
    """

    # `setattr()` calls to the fields specified in `unwrapped` will be passed
    # through, unmodified.  Everything else will be delegated to `wrapped`
    unwrapped = ("series", "_hasnans", "_is_na")
    wrapped = "series"

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        is_na: pd.Series = None
    ):
        self.series = series
        self._hasnans = hasnans
        self._is_na = is_na

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
        self._is_na = self.series.isna()
        return self._is_na

    def isnull(self) -> pd.Series:
        """An alias for SeriesWrapper.isna()."""
        return self.isna()

    def notna(self) -> pd.Series:
        """A cached version of the ordinary Series.notna() method."""
        return self.isna().__invert__()

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


class NumericSeries(SeriesWrapper):
    """Base wrapper for pd.Series objects that contain numeric values.

    Implements a dynamic wrapper according to the Gang of Four's Decorator
    Pattern (not to be confused with python decorators).
    """

    unwrapped = SeriesWrapper.unwrapped + (
        "_min", "_max", "_idxmin", "_idxmax"
    )

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        is_na: pd.Series = None,
        min_val: int = None,
        min_index: int = None,
        max_val: int = None,
        max_index: int = None
    ):
        super().__init__(series=series, hasnans=hasnans, is_na=is_na)
        self._min = min_val
        self._idxmin = min_index
        self._max = max_val
        self._idxmax = max_index

    #############################
    ####    CACHE MIN/MAX    ####
    #############################

    def min(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.min()."""
        # cached
        if self._min is not None:
            return self._min

        # uncached
        self._min = self.series.min(*args, **kwargs)
        return self._min

    def argmin(self, *args, **kwargs) -> int:
        """Alias for IntegerSeries.min()."""
        return self.min(*args, **kwargs)

    def max(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.max()."""
        # cached
        if self._max is not None:
            return self._max

        # uncached
        self._max = self.series.max(*args, **kwargs)
        return self._max

    def argmax(self, *args, **kwargs) -> int:
        """Alias for IntegerSeries.max()."""
        return self.max(*args, **kwargs)

    def idxmin(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.idxmin()."""
        # cached
        if self._idxmin is not None:
            return self._idxmin

        # uncached
        self._idxmin = self.series.idxmin(*args, **kwargs)
        return self._idxmin

    def idxmax(self, *args, **kwargs) -> int:
        """A cached version of pd.Series.idxmax()."""
        # cached
        if self._idxmax is not None:
            return self._idxmax

        # uncached
        self._idxmax = self.series.idxmax(*args, **kwargs)
        return self._idxmax


class RealSeries(NumericSeries):
    """Base wrapper for SeriesWrapper objects that contain real numerics
    capable of storing infinity.

    Implements a dynamic wrapper according to the Gang of Four's Decorator
    Pattern (not to be confused with python decorators).
    """

    unwrapped = NumericSeries.unwrapped + ("_hasinfs", "_is_inf")

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        is_na: pd.Series = None,
        min_val: int | float | decimal.Decimal = None,
        min_index: int = None,
        max_val: int | float | decimal.Decimal = None,
        max_index: int = None,
        hasinfs: bool = None,
        is_inf: np.ndarray = None
    ):
        super().__init__(
            series=series,
            hasnans=hasnans,
            is_na=is_na,
            min_val=min_val,
            min_index=min_index,
            max_val=max_val,
            max_index=max_index
        )
        self._hasinfs = hasinfs
        self._is_inf = is_inf

    ##########################
    ####    CACHE INFS    ####
    ##########################

    @property
    def hasinfs(self) -> bool:
        """TODO"""
        if self._hasinfs is not None:  # hasinfs is cached
            return self._hasinfs

        # hasinfs must be computed
        self._hasinfs = self.infs.any()
        return self._hasinfs

    @property
    def infs(self) -> pd.Series:
        """TODO"""
        if self._is_inf is not None:  # infs is cached
            return self._is_inf

        # infs must be computed
        self._is_inf = np.isinf(self.series)
        return self._is_inf
