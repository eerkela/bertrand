from types import MappingProxyType
from typing import Any

import numpy as np
import pandas as pd

from .base cimport AtomicType, AdapterType, CompositeType, ScalarType
from .base import register

cimport pdtypes.cast as cast
import pdtypes.cast as cast
cimport pdtypes.resolve as resolve
import pdtypes.resolve as resolve

from pdtypes.util.type_hints import type_specifier


@register
class SparseType(AdapterType):

    name = "sparse"
    aliases = {"sparse", "Sparse"}

    def __init__(self, wrapped: ScalarType, fill_value: Any = None):
        # do not re-wrap SparseTypes
        if isinstance(wrapped, SparseType):  # 1st order
            if fill_value is None:
                fill_value = wrapped.fill_value
            wrapped = wrapped.wrapped
        else:  # 2nd order
            for x in wrapped.adapters:
                if isinstance(x.wrapped, SparseType):
                    if fill_value is None:
                        fill_value = x.fill_value
                    wrapped = x.wrapped.wrapped
                    x.wrapped = self
                    break

        # wrap dtype
        if fill_value is None:
            fill_value = wrapped.na_value
        self.dtype = pd.SparseDtype(wrapped.dtype, fill_value)

        # call AdapterType.__init__()
        super().__init__(wrapped=wrapped, fill_value=fill_value)

    ############################
    ####    TYPE METHODS    ####
    ############################

    def contains(self, other: type_specifier) -> bool:
        """Check whether the given type is contained within this type's
        subtype hierarchy.
        """
        other = resolve.resolve_type(other)
        if isinstance(other, CompositeType):
            return all(self.contains(o) for o in other)

        if isinstance(other, type(self)):
            na_1 = pd.isna(self.fill_value)
            na_2 = pd.isna(other.fill_value)
            if na_1 or na_2:
                result = na_1 & na_2
            else:
                result = self.fill_value == other.fill_value
            return result and self.wrapped.contains(other.wrapped)
        return False

    @classmethod
    def slugify(
        cls,
        wrapped: ScalarType,
        fill_value: Any = None
    ) -> str:
        if fill_value is None:
            return f"{cls.name}[{str(wrapped)}]"
        return f"{cls.name}[{str(wrapped)}, {fill_value}]"

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    def apply_adapters(
        self,
        series: cast.SeriesWrapper
    ) -> cast.SeriesWrapper:
        """Convert a series into a sparse format with the given fill_value."""
        # evaluate adapters from the inside out
        series = super().apply_adapters(series)

        # apply custom logic for each AtomicType
        series = self.atomic_type.make_sparse(
            series,
            fill_value=self.fill_value
        )
        series.element_type = self
        return series

    @classmethod
    def resolve(cls, wrapped: str, fill_value: str = None):
        cdef ScalarType instance = resolve.resolve_type(wrapped)
        cdef object parsed = None

        # resolve fill_value
        if fill_value is not None:
            if fill_value in resolve.na_strings:
                parsed = resolve.na_strings[fill_value]
            else:
                parsed = cast.cast(fill_value, instance)[0]

        return cls(wrapped=instance, fill_value=parsed)
