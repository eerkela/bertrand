"""This module describes a ``CategoricalType`` object, which can be used to
dynamically wrap other types.
"""
import numpy as np
import pandas as pd

from pdcast.resolve cimport sequence, tokenize
from pdcast.resolve import resolve_type
from pdcast.detect import detect_type
from pdcast.util.type_hints import array_like, dtype_like, type_specifier

from .base cimport DecoratorType, CompositeType, VectorType
from .base import register


# TODO: CategoricalType should be able to accept CompositeType?
# NOTE: this is enabled in pandas, but maybe shouldn't be here.


@register
class CategoricalType(DecoratorType):
    """Categorical decorator for :class:`ScalarType` objects.

    This decorator keeps track of categorical levels for series objects of the
    wrapped type.
    """

    name = "categorical"
    aliases = {
        pd.CategoricalDtype, "category", "Category", "categorical",
        "Categorical"
    }

    def __init__(self, wrapped: VectorType = None, levels: list = None):
        super(type(self), self).__init__(wrapped=wrapped, levels=levels)

    ############################
    ####    CONSTRUCTORS    ####
    ############################

    def from_string(
        self,
        wrapped: str = None,
        levels: str = None
    ) -> DecoratorType:
        """Resolve a categorical specifier in the type specification
        mini-language.
        """
        if wrapped is None:
            return self

        cdef VectorType instance = resolve_type(wrapped)
        cdef list parsed = None

        # resolve levels
        if levels is not None:
            from pdcast.convert import cast

            match = sequence.match(levels)
            if not match:
                raise TypeError(f"levels must be list-like: {levels}")

            tokens = tokenize(match.group("body"))
            parsed = cast(tokens, instance).tolist()

        return self(instance, levels=parsed)

    def from_dtype(
        self,
        dtype: dtype_like,
        array: array_like | None = None
    ) -> Type:
        """Convert a pandas CategoricalDtype into a
        :class:`CategoricalType <pdcast.CategoricalType>` object.
        """
        # detect type of categories
        categories = dtype.categories
        levels = categories.tolist()
        wrapped = detect_type(categories)

        # if categories are composite, broadcast across non-homogenous array
        if isinstance(wrapped, CompositeType):
            if array is None:  # no index
                return CompositeType(
                    {self(typ, levels=levels) for typ in wrapped}
                )

            # generate an index from the full array
            wrapped = detect_type(array.astype(dtype.categories.dtype))
            index = wrapped._index  # run-length encoded version of .index
            index["value"] = np.array(
                [self(typ, levels=levels) for typ in wrapped]
            )
            return CompositeType(
                {self(typ, levels=levels) for typ in wrapped},
                index=index
            )

        return self(wrapped=wrapped, levels=levels)

    ##################################
    ####    DECORATOR-SPECIFIC    ####
    ##################################

    def transform(self, series: pd.Series) -> pd.Series:
        """Convert a series into a categorical representation."""
        from pdcast.convert import categorize

        # NOTE: categorize() is a @dispatch function, so implementations can be
        # added to it as necessary.

        return categorize(series, levels=self.levels)

    def inverse_transform(self, series: pd.Series) -> pd.Series:
        """Convert a categorical series into a non-categorical representation.
        """
        from pdcast.convert import decategorize

        # NOTE: decategorize() is a @dispatch function, so implementations can
        # be added to it as necessary.

        return decategorize(series)

    #############################
    ####    CONFIGURATION    ####
    #############################

    @property
    def dtype(self) -> pd.CategoricalDtype:
        """Render an equivalent CategoricalDtype to use for arrays of this
        type.
        """
        if self.wrapped is None:
            return pd.CategoricalDtype()

        return pd.CategoricalDtype(
            pd.Index(
                [] if self.levels is None else self.levels,
                dtype=self.wrapped.dtype
            )
        )

    def contains(self, other: type_specifier) -> bool:
        """Check whether the given type is contained within the wrapped type's
        hierarchy.
        """
        other = resolve_type(other)

        # if target is composite, test each element individually
        if isinstance(other, CompositeType):
            return all(self.contains(typ) for typ in other)

        # assert other is categorical
        if not isinstance(other, type(self)):
            return False

        # check for naked specifier
        if self.wrapped is None:
            return True
        if other.wrapped is None:
            return False

        # check for unequal levels
        if self.levels is not None and self.levels != other.levels:
            return False

        # delegate to wrapped
        return self.wrapped.contains(other.wrapped)
