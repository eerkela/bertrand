from types import MappingProxyType
from typing import Any

import numpy as np
import pandas as pd

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.detect as detect
import pdcast.detect as detect
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve
from pdcast.util.type_hints import type_specifier

from .base cimport AtomicType, AdapterType, CompositeType, ScalarType
from .base import register


# TODO: CategoricalType should be able to accept CompositeType?
# NOTE: this is enabled in pandas, but probably shouldn't be here.


@register
class CategoricalType(AdapterType):
    """Categorical adapter for :class:`AtomicType` objects.

    This adapter keeps track of categorical levels for series objects of the
    wrapped type.
    """

    name = "categorical"
    aliases = {
        pd.CategoricalDtype, "category", "Category", "categorical",
        "Categorical"
    }
    _priority = 5
    is_categorical = True

    def __init__(self, wrapped: ScalarType = None, levels: list = None):
        # do not re-wrap CategoricalTypes
        if isinstance(wrapped, CategoricalType):  # 1st order
            if levels is None:
                levels = wrapped.levels
            wrapped = wrapped.wrapped
        elif wrapped is not None:  # 2nd order
            for x in wrapped.adapters:
                if isinstance(x.wrapped, CategoricalType):
                    if levels is None:
                        levels = x.levels
                    wrapped = x.wrapped.wrapped
                    x.wrapped = self
                    break

        # call AdapterType.__init__()
        super().__init__(wrapped=wrapped, levels=levels)

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    @classmethod
    def from_dtype(
        cls,
        dtype: pd.api.extensions.ExtensionDtype
    ) -> AdapterType:
        return cls(
            wrapped=detect.detect_type(dtype.categories),
            levels=dtype.categories.tolist()
        )

    @classmethod
    def resolve(
        cls,
        wrapped: str = None,
        levels: str = None
    ) -> AdapterType:
        if wrapped is None:
            return cls()

        cdef ScalarType instance = resolve.resolve_type(wrapped)
        cdef list parsed = None

        # resolve levels
        if levels is not None:
            match = resolve.sequence.match(levels)
            if not match:
                raise TypeError(f"levels must be list-like: {levels}")
            tokens = resolve.tokenize(match.group("body"))
            parsed = convert.cast(tokens, instance).tolist()

        # insert into sorted adapter stack according to priority
        for x in instance.adapters:
            if x._priority <= cls._priority:  # initial
                break
            if getattr(x.wrapped, "_priority", -np.inf) <= cls._priority:
                x.wrapped = cls(x.wrapped, levels=parsed)
                return instance

        # add to front of stack
        return cls(instance, levels=parsed)

    @classmethod
    def slugify(
        cls,
        wrapped: ScalarType = None,
        levels: list = None
    ) -> str:
        if wrapped is None:
            return cls.name
        if levels is None:
            return f"{cls.name}[{str(wrapped)}]"
        return f"{cls.name}[{str(wrapped)}, {levels}]"

    ############################
    ####    TYPE METHODS    ####
    ############################

    def contains(self, other: type_specifier, exact: bool = False) -> bool:
        """Test whether a given type is contained within this type's subtype
        hierarchy.
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(o) for o in other)

        if not isinstance(other, type(self)):
            return False

        # check for naked specifier
        if self.wrapped is None:
            return True
        if other.wrapped is None:
            return False

        # check for equal levels
        result = self.levels is None or self.levels == other.levels

        # delegate to wrapped
        return result and self.wrapped.contains(other.wrapped, exact=exact)

    @property
    def dtype(self) -> pd.CategoricalDtype:
        if self.wrapped is None:
            return pd.CategoricalDtype()
        return pd.CategoricalDtype(
            pd.Index(
                [] if self.levels is None else self.levels,
                dtype=self.wrapped.dtype
            )
        )

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def transform(
        self,
        series: convert.SeriesWrapper
    ) -> convert.SeriesWrapper:
        """Convert an unwrapped series into a categorical representation."""
        # discover levels automatically
        series = self.atomic_type.make_categorical(series, levels=self.levels)
        self.levels = series.series.dtype.categories.tolist()
        series.element_type = self
        return series
