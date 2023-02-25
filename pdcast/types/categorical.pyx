from types import MappingProxyType
from typing import Any

import numpy as np
import pandas as pd

cimport pdcast.cast as cast
import pdcast.cast as cast
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve
from pdcast.util.type_hints import type_specifier

from .base cimport AtomicType, AdapterType, CompositeType, ScalarType
from .base import register


@register
class CategoricalType(AdapterType):

    name = "categorical"
    aliases = {"categorical", "Categorical"}

    def __init__(self, wrapped: ScalarType, levels: list = None):
        # do not re-wrap CategoricalTypes
        if isinstance(wrapped, CategoricalType):  # 1st order
            if levels is None:
                levels = wrapped.levels
            wrapped = wrapped.wrapped
        else:  # 2nd order
            for x in wrapped.adapters:
                if isinstance(x.wrapped, CategoricalType):
                    if levels is None:
                        levels = x.levels
                    wrapped = x.wrapped.wrapped
                    x.wrapped = self
                    break

        # wrap dtype
        self.dtype = pd.CategoricalDtype(
            pd.Index(
                [] if levels is None else levels,
                dtype=wrapped.dtype
            )
        )

        # call AdapterType.__init__()
        super().__init__(wrapped=wrapped, levels=levels)

    ############################
    ####    TYPE METHODS    ####
    ############################

    def contains(self, other: type_specifier) -> bool:
        """Test whether a given type is contained within this type's subtype
        hierarchy.
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(o) for o in other)

        return (
            isinstance(other, type(self)) and
            (self.levels is None or self.levels == other.levels) and
            self.wrapped.contains(other.wrapped)
        )

    @classmethod
    def slugify(
        cls,
        wrapped: ScalarType,
        levels: list = None
    ) -> str:
        if levels is None:
            return f"{cls.name}[{str(wrapped)}]"
        return f"{cls.name}[{str(wrapped)}, {levels}]"

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def apply_adapters(
        self,
        series: cast.SeriesWrapper
    ) -> cast.SeriesWrapper:
        """Convert an unwrapped series into a categorical representation."""
        # evaluate adapters from the inside out
        series = super().apply_adapters(series)

        # discover levels automatically
        series = self.atomic_type.make_categorical(series, self.levels)
        self.levels = series.series.dtype.categories.tolist()
        series.element_type = self
        return series

    @classmethod
    def resolve(cls, wrapped: str, levels: str = None) -> AdapterType:
        cdef ScalarType instance = resolve.resolve_type(wrapped)
        cdef list parsed = None

        # resolve levels
        if levels is not None:
            match = resolve.sequence.match(levels)
            if not match:
                raise TypeError(f"levels must be list-like: {levels}")
            tokens = resolve.tokenize(match.group("body"))
            parsed = cast.cast(tokens, instance).tolist()

        # place CategoricalType beneath SparseType if it is present
        for x in instance.adapters:
            if x.name == "sparse":
                x.wrapped = cls(wrapped=x.wrapped, levels=parsed)
                return instance

        return cls(wrapped=instance, levels=parsed)
