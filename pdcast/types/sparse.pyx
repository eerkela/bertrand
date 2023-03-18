from types import MappingProxyType
from typing import Any

import numpy as np
import pandas as pd

cimport pdcast.convert as convert
import pdcast.convert as convert
cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from pdcast.util.type_hints import type_specifier

from .base cimport AtomicType, AdapterType, CompositeType, ScalarType
from .base import register, dispatch


@register
class SparseType(AdapterType):

    name = "sparse"
    aliases = {pd.SparseDtype, "sparse", "Sparse"}
    is_sparse = True

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
    def from_dtype(cls, dtype: pd.api.extensions.ExtensionDtype) -> AdapterType:
        return cls(
            wrapped=resolve.resolve_type(dtype.subtype),
            fill_value=dtype.fill_value
        )

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

    @classmethod
    def resolve(cls, wrapped: str, fill_value: str = None):
        cdef ScalarType instance = resolve.resolve_type(wrapped)
        cdef object parsed = None

        # resolve fill_value
        if fill_value is not None:
            if fill_value in resolve.na_strings:
                parsed = resolve.na_strings[fill_value]
            else:
                parsed = convert.cast(fill_value, instance)[0]

        return cls(wrapped=instance, fill_value=parsed)

    def unwrap(
        self,
        series: convert.SeriesWrapper
    ) -> convert.SeriesWrapper:
        """Convert a sparse series into a dense format"""
        # NOTE: this is a pending deprecation shim.  In a future version of
        # pandas, astype() from a sparse to non-sparse dtype will return a
        # non-sparse series.  Currently, it returns a sparse equivalent. When
        # this behavior changes, this method can be deleted.
        return convert.SeriesWrapper(
            series.sparse.to_dense(),
            hasnans=series._hasnans,
            element_type=self.wrapped
        )

    def wrap(
        self,
        series: convert.SeriesWrapper
    ) -> convert.SeriesWrapper:
        """Convert a series into a sparse format with the given fill_value."""
        # apply custom logic for each AtomicType
        series = self.atomic_type.make_sparse(
            series,
            fill_value=self.fill_value
        )
        series.element_type = self
        return series
