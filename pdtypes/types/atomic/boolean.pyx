import decimal
from types import MappingProxyType
from typing import Union

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: change defaults for backend=None


# TODO: consider backend="python" in to_boolean


# TODO: add datetime_like `since` hint to to_datetime, to_timedelta



class BooleanType(AtomicType):
    """Boolean supertype."""

    name = "bool"
    aliases = {
        # type
        bool: {},
        np.bool_: {"backend": "numpy"},

        # dtype
        np.dtype(np.bool_): {"backend": "numpy"},
        pd.BooleanDtype(): {"backend": "pandas"},

        # string
        "bool": {},
        "boolean": {},
        "bool_": {},
        "bool8": {},
        "b1": {},
        "?": {},
        "Boolean": {"backend": "pandas"},
    }
    _backends = ("python", "numpy", "pandas")

    def __init__(self, backend: str = None):
        # "bool"
        if backend is None:
            type_def = bool
            dtype = np.dtype(bool)

        # "bool[python]"
        elif backend == "python":
            type_def = bool
            dtype = np.dtype("O")

        # "bool[numpy]"
        elif backend == "numpy":
            type_def = np.bool_
            dtype = np.dtype(bool)

        # "bool[pandas]"
        elif backend == "pandas":
            type_def = np.bool_
            dtype = pd.BooleanDtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        # call AtomicType constructor
        super(BooleanType, self).__init__(
            type_def=type_def,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=1,
            slug=self.slugify(backend=backend)
        )

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(cls, backend: str = None) -> str:
        slug = cls.name
        if backend is not None:
            slug += f"[{backend}]"
        return slug

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({
            "backend": self.backend
        })

    ##############################
    ####    CUSTOMIZATIONS    ####
    ##############################

    def _generate_subtypes(self, types: set) -> frozenset:
        # treat backend=None as wildcard
        kwargs = [self.kwargs]
        if self.backend is None:
            kwargs.extend([
                {**kw, **{"backend": b}}
                for kw in kwargs
                for b in self._backends
            ])

        # build result, skipping invalid kwargs
        result = set()
        for t in types:
            for kw in kwargs:
                try:
                    result.add(t.instance(**kw))
                except TypeError:
                    continue

        # return as frozenset
        return frozenset(result)

    def parse(self, input_str: str):
        if input_str in resolve.na_strings:
            return resolve.na_strings[input_str]
        if input_str not in ("True", "False"):
            raise TypeError(
                f"could not interpret boolean string: {input_str}"
            )
        return self.type_def(input_str == "True")

    ###########################
    ####    CONVERSIONS    ####
    ###########################

    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> pd.Series:
        """Convert boolean data to another boolean data type."""
        # python bool special case
        if dtype.backend == "python":
            with series.exclude_na(dtype.na_value):
                series.series = series.astype("O")
            return series.series

        # if missing values are detected, ensure dtype is nullable
        elif dtype.backend in (None, "numpy") and series.hasnans:
            dtype = dtype.replace(backend="pandas")

        # convert
        return series.astype(dtype.dtype)

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        downcast: bool = False,
        **unused
    ) -> pd.Series:
        """Convert boolean data to an integer data type."""
        # downcast integer dtype if directed
        if downcast:
            dtype = dtype.downcast(min=0, max=1)

        # python integer special case
        if dtype.backend == "python":
            with series.exclude_na(dtype.na_value):
                series.series = np.frompyfunc(int, 1, 1)(series)
            return series.series

        # if missing values are detected, ensure dtype is nullable
        if dtype.backend in (None, "numpy") and series.hasnans:
            dtype = dtype.replace(backend="pandas")

        # convert
        if dtype.backend == "pandas" and pd.api.types.is_object_dtype(series):
            series.series = self.to_boolean(series, self)
        return series.astype(dtype.dtype)

    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        downcast: bool = False,
        **unused
    ) -> pd.Series:
        """Convert boolean data to a floating point data type."""
        # downcast float dtype if directed
        if downcast:
            dtype = dtype.downcast(min=0, max=1)

        # astype(float) fails when given pd.NA in an object series
        if pd.api.types.is_object_dtype(series):
            series.series = self.to_boolean(series, self)

        return series.astype(dtype.dtype)

    def to_complex(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        downcast: bool = False,
        **unused
    ) -> pd.Series:
        """Convert boolean data to a complex data type."""
        if downcast:
            dtype = dtype.downcast(min=0, max=1)

        # astype(complex) complains on missing values
        with series.exclude_na(dtype.na_value):
            series.series = series.astype(dtype.dtype)

        return series.series

    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> pd.Series:
        """Convert boolean data to a decimal data type."""
        with series.exclude_na(dtype.na_value):
            series.series += decimal.Decimal(0)
        return series.series

    def to_datetime(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str = "ns",
        since: str = "UTC"
    ):
        """Convert boolean data to a datetime data type."""
        pass

