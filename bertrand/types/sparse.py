"""This module describes a ``SparseType`` object, which can be used to
dynamically wrap other types.
"""
from typing import Any

import numpy as np
import pandas as pd

from pdcast.resolve cimport na_strings
from pdcast.resolve import resolve_type
from pdcast.detect import detect_type
from pdcast.util.type_hints import array_like, dtype_like, type_specifier

from .base cimport DecoratorType, CompositeType, VectorType, Type
from .base import register


# TODO: may need to create a special case for nullable integers, booleans
# to use their numpy counterparts.  This avoids converting to object, but
# forces the fill value to be pd.NA.
# -> this can be handled in a dispatched sparsify() implementation


# TODO: SparseType works, but not in all cases.
# -> pd.NA disallows non-missing fill values
# -> Timestamps must be sparsified manually by converting to object and then
# to sparse
# -> Timedeltas just don't work at all.  astype() rejects pd.SparseDtype("m8")
# entirely.


@register
class SparseType(DecoratorType):

    name = "sparse"
    aliases = {pd.SparseDtype, "sparse", "Sparse"}

    def __init__(self, wrapped: VectorType = None, fill_value: Any = None):
        super(type(self), self).__init__(wrapped, fill_value=fill_value)

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(
        self,
        wrapped: str = None,
        fill_value: str = None
    ) -> DecoratorType:
        """Resolve a sparse specifier in the type specification mini-langauge.
        """
        if wrapped is None:
            return self

        instance = resolve_type(wrapped)
        parsed_fill_value = None

        if fill_value is not None:
            # TODO: use NullType aliases here?
            if fill_value in na_strings:
                parsed_fill_value = na_strings[fill_value]
            else:
                from pdcast.convert import cast
                parsed_fill_value = cast(fill_value, instance)[0]

        return self(instance, fill_value=parsed_fill_value)

    def from_dtype(
        self,
        dtype: dtype_like,
        array: array_like | None = None
    ) -> Type:
        """Convert a pandas SparseDtype into a
        :class:`SparseType <pdcast.SparseType>` object.
        """
        # detect type of unwrapped array
        if array is not None:
            # TODO: this may not work in all cases.  We can't call
            # inverse_transform() here because densify is an @dispatch func,
            # which calls detect_type() and causes a recursion error.
            wrapped = detect_type(array.sparse.to_dense())
        else:
            wrapped = resolve_type(dtype.subtype)

        # broadcast across non-homogenous array
        if isinstance(wrapped, CompositeType):
            fill_value = dtype.fill_value
            index = wrapped._index  # run-length encoded version of .index
            index["value"] = np.array(
                [self(typ, fill_value=fill_value) for typ in index["value"]]
            )
            return CompositeType(
                {self(typ, fill_value=fill_value) for typ in wrapped},
                index=index
            )

        return self(wrapped, fill_value=dtype.fill_value)

    ##################################
    ####    DECORATOR-SPECIFIC    ####
    ##################################

    def transform(self, series: pd.Series) -> pd.Series:
        """Convert a series into a sparse format with the given fill_value."""
        from pdcast.convert import sparsify

        return sparsify(series, fill_value=self.fill_value)

    def inverse_transform(self, series: pd.Series) -> pd.Series:
        """Convert a sparse series into a dense format"""
        from pdcast.convert import densify

        return densify(series)

    ##########################
    ####    OVERRIDDEN    ####
    ##########################

    @property
    def fill_value(self) -> Any:
        """The value to mask from the array."""
        val = self.kwargs["fill_value"]
        if val is None:
            val = getattr(self.wrapped, "na_value", None)
        return val

    @property
    def dtype(self) -> pd.SparseDtype:
        """An equivalent SparseDtype to use for arrays of this type."""
        return pd.SparseDtype(self.wrapped.dtype, fill_value=self.fill_value)

    def contains(
        self,
        other: type_specifier,
    ) -> bool:
        """Check whether the given type is contained within this type's
        subtype hierarchy.
        """
        other = resolve_type(other)

        # if target is composite, test each element individually
        if isinstance(other, CompositeType):
            return all(self.contains(typ) for typ in other)

        # assert other is sparse
        if not isinstance(other, type(self)):
            return False

        # check for naked specifier
        if self.wrapped is None:
            return True
        if other.wrapped is None:
            return False

        # check for unequal fill values
        if self.kwargs["fill_value"] is not None:
            na_1 = pd.isna(self.fill_value)
            na_2 = pd.isna(other.fill_value)
            if (
                na_1 ^ na_2 or
                na_1 == na_2 == False and self.fill_value != other.fill_value
            ):
                return False

        # delegate to wrapped
        return self.wrapped.contains(other.wrapped)
