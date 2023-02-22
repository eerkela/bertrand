from types import MappingProxyType
from typing import Any

import numpy as np
import pandas as pd

from .base cimport AtomicType, AdapterType, ScalarType
from .base import register

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: does it even make sense to have manual categories in resolve?  They
# should only be allowed in detected types.


# when constructing categorical dtypes, the `categories` field should store
# an empty 


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

        # call AdapterType.__init__()
        super().__init__(wrapped=wrapped, levels=levels)

    ############################
    ####    TYPE METHODS    ####
    ############################

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
            match = resolve.brackets.match(levels)
            if not match:
                raise TypeError(f"levels must be list-like: {levels}")
            tokens = resolve.tokenize(match.group("content"))
            parsed = cast.cast(tokens, instance).tolist()

        # place CategoricalType beneath SparseType if it is present
        for x in instance.adapters:
            if x.name == "sparse":
                result = cls(wrapped=x.wrapped, levels=parsed)
                x.wrapped = result
                return instance

        return cls(wrapped=instance, levels=parsed)