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


@register
class SparseType(AdapterType):

    name = "sparse"
    aliases = {"sparse", "Sparse"}

    def __init__(self, wrapped: ScalarType, fill_value: Any = None):
        # wrap dtype
        if fill_value is None:
            fill_value = wrapped.na_value
        self.dtype = pd.SparseDtype(wrapped.dtype, fill_value)

        # call AdapterType.__init__()
        super().__init__(wrapped=wrapped, fill_value=fill_value)

    ########################
    ####    REQUIRED    ####
    ########################

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
    ####    CUSTOMIZATIONS    ####
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

        # if instance is already sparse, just replace fill_value
        if cls.name in instance.adapters:
            if parsed is None:
                return instance
            else:
                return instance.replace(fill_value=parsed)

        return cls(wrapped=instance, fill_value=parsed)
