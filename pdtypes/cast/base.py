from __future__ import annotations
from contextlib import contextmanager
from typing import Any, Iterator

import numpy as np
import pandas as pd

from pdtypes.delegate import delegates


class SeriesWrapper:
    """Base wrapper for pd.Series objects.

    Implements a dynamic wrapper according to the Gang of Four's Decorator
    Pattern (not to be confused with python decorators).
    """

    # `setattr()` calls to the fields specified in `unwrapped` will not be
    # delegated to the wrapped series
    unwrapped = ("series", "_hasnans", "_is_na")

    def __init__(
        self,
        series: pd.Series,
        hasnans: bool = None,
        is_na: pd.Series = None
    ):
        if isinstance(series, SeriesWrapper):
            self.series = series.series
            self._hasnans = hasnans or series._hasnans
            self._is_na = is_na if is_na is not None else series._is_na
        else:
            # NOTE: pandas doesn't like converting scalar numpy arrays
            if isinstance(series, np.ndarray) and not series.shape:
                series = series[()]  # unwrap value

            self.series = pd.Series(series, copy=False)
            self._hasnans = hasnans
            self._is_na = is_na

        if self._hasnans is False and self._is_na is None:
            self._is_na = pd.Series(
                np.full(self.series.shape[0], False),
                index=self.series.index,
                copy=False
            )

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

    def dropna(self) -> pd.Series:
        """Return the wrapped series with missing values removed."""
        return self.series[self.notna()]

    ####################################
    ####    STRIP MISSING VALUES    ####
    ####################################

    @contextmanager
    def exclude_na(
        self,
        fill_value: Any,
        array_type: np.dtype = "O"
    ) -> Iterator[SeriesWrapper]:
        """Provides a context manager that strips missing values from the
        decorated series and replaces them at the end of the context block.
        Operations within the block can safely disregard missing values.
        """
        if not self.hasnans:
            yield self

        else:
            # strip nans
            self.series = self.series[self.notna()]

            # yield context manager
            try:
                yield self

            # replace nans
            finally:
                result = np.full(
                    self.isna().shape,
                    fill_value=fill_value,
                    dtype=array_type
                )
                result[self.notna()] = self.series
                self.series = pd.Series(
                    result,
                    index=self.isna().index,
                    dtype=self.dtype,
                    copy=False
                )

    ###############################
    ####    DYNAMIC WRAPPER    ####
    ###############################

    def __iter__(self):
        return self.series.__iter__()

    def __next__(self):
        return self.series.__next__()

    def __getattr__(self, name):
        return getattr(self.series, name)

    def __setattr__(self, name, value):
        if name in self.__class__.unwrapped:
            self.__dict__[name] = value
        else:
            setattr(self.series, name, value)

    def __delattr__(self, name):
        delattr(self.series, name)

    def __repr__(self):
        return repr(self.series)

    def __str__(self):
        return str(self.series)

    def __dir__(self):
        # delegates dir() lookups to the wrapped series.  Adapted from:
        # https://www.fast.ai/posts/2019-08-06-delegation.html
        return (
            dir(type(self)) +
            list(self.__dict__.keys()) +
            [attr for attr in dir(self.series) if not attr.startswith("_")]
        )

    ###########################
    ####    COMPARISONS    ####
    ###########################

    def __eq__(self, other):
        return self.series == other

    def __ne__(self, other):
        return self.series != other

    def __lt__(self, other):
        return self.series < other

    def __le__(self, other):
        return self.series <= other

    def __gt__(self, other):
        return self.series > other

    def __ge__(self, other):
        return self.series >= other

    ##########################
    ####    ARITHMETIC    ####
    ##########################

    def __add__(self, other):
        return self.series + other

    def __radd__(self, other):
        return other + self.series

    def __iadd__(self, other):
        return self.series.__iadd__(other)

    def __sub__(self, other):
        return self.series - other

    def __rsub__(self, other):
        return other - self.series

    def __isub__(self, other):
        return self.series.__isub__(other)

    def __mul__(self, other):
        return self.series * other

    def __rmul__(self, other):
        return other * self.series

    def __imul__(self, other):
        return self.series.__imul__(other)

    def __div__(self, other):
        return self.series / other

    def __rdiv__(self, other):
        return other / self.series

    def __idiv__(self, other):
        return self.series.__idiv__(other)

    def __floordiv__(self, other):
        return self.series // other

    def __rfloordiv__(self, other):
        return other // self.series

    def __ifloordiv__(self, other):
        return self.series.__ifloordiv__(other)

    def __mod__(self, other):
        return self.series % other

    def __rmod__(self, other):
        return other % self.series

    def __imod__(self, other):
        return self.series.__imod__(other)

    def __divmod__(self, other):
        return divmod(self.series, other)

    def __rdivmod__(self, other):
        return divmod(other, self.series)

    def __pow__(self, other):
        return self.series ** other

    def __rpow__(self, other):
        return other ** self.series

    def __ipow__(self, other):
        return self.series.__ipow__(other)

    def __neg__(self):
        return - self.series

    def __pos__(self):
        return + self.series

    def __abs__(self):
        return abs(self.series)

    def __invert__(self):
        return ~ self.series

    def __lshift__(self, other):
        return self.series << other

    def __ilshift__(self, other):
        return self.series.__ilshift__(other)

    def __rlshift__(self, other):
        return other << self.series

    def __rshift__(self, other):
        return self.series >> other

    def __rrshift__(self, other):
        return other >> self.series

    def __irshift__(self, other):
        return self.series.__irshift__(other)

    def __and__(self, other):
        return self.series & other

    def __rand__(self, other):
        return other & self.series

    def __iand__(self, other):
        return self.series.__iand__(other)

    def __or__(self, other):
        return self.series | other

    def __ror__(self, other):
        return other | self.series

    def __ior__(self, other):
        return self.series.__ior__(other)

    def __xor__(self, other):
        return self.series ^ other

    def __rxor__(self, other):
        return other ^ self.series

    def __ixor__(self, other):
        return self.series.__ixor__(other)

    ###################################
    ####    NUMERIC CONVERSIONS    ####
    ###################################

    def __int__(self):
        return int(self.series)

    def __float__(self):
        return float(self.series)

    def __oct__(self):
        return oct(self.series)

    def __hex__(self):
        return hex(self.series)

    ####################################
    ####    SEQUENCES & MAPPINGS    ####
    ####################################

    def __len__(self):
        return len(self.series)

    def __getitem__(self, key):
        return self.series.__getitem__(key)

    def __setitem__(self, key, val):
        self.series.__setitem__(key, val)

    def __delitem__(self, key):
        self.series.__delitem__(key)

    def __getslice__(self, i, j):
        return self.series.__getslice__(i, j)

    def __setslice__(self, i, j, val):
        self.series.__setslice__(i, j, val)

    def __delslice__(self, i, j):
        self.series.__delslice__(i, j)

    def __contains__(self, val):
        return self.series.__contains__(val)

    ###########################
    ####    DESCRIPTORS    ####
    ###########################

    def __get__(self, instance, class_):
        return self.series.__get__(instance, class_)

    def __set__(self, instance, value):
        self.series.__set__(instance, value)

    def __delete__(self, instance):
        self.series.__delete__(instance)


@delegates()
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
        min_val: int = None,
        min_index: int = None,
        max_val: int = None,
        max_index: int = None,
        **kwargs
    ):
        super().__init__(series=series, **kwargs)
        if isinstance(series, NumericSeries):
            self._min = min_val or series._min
            self._idxmin = min_index or series._idxmin
            self._max = max_val or series._max
            self._idxmax = max_index or series._idxmax
        else:
            self._min = min_val
            self._idxmin = min_index
            self._max = max_val
            self._idxmax = max_index

        if self._idxmin is not None and self._min is None:
            self._min = self.series[self._idxmin]
        if self._idxmax is not None and self._max is None:
            self._max = self.series[self._idxmax]

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


@delegates()
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
        hasinfs: bool = None,
        is_inf: np.ndarray = None,
        **kwargs
    ):
        super().__init__(series=series, **kwargs)
        if isinstance(series, RealSeries):
            self._hasinfs = hasinfs or series._hasinfs
            self._infs = is_inf or series._infs
        else:
            self._hasinfs = hasinfs
            self._infs = is_inf

        if self._hasinfs is False and self._infs is None:
            self._infs = pd.Series(
                np.full(self.series.shape[0], False),
                index=self.series.index,
                copy=False
            )

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
        if self._infs is not None:  # infs is cached
            return self._infs

        # infs must be computed
        self._infs = np.isinf(self.series)
        return self._infs
