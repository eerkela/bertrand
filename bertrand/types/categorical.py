"""This module describes a ``CategoricalType`` object, which can be used to
dynamically wrap other types.
"""
from __future__ import annotations

from typing import Any, Iterable

import numpy as np
import pandas as pd

from .base import EMPTY, DecoratorMeta, DecoratorType, Empty, TypeMeta


# TODO: CategoricalType should be able to accept CompositeType?
# NOTE: this is enabled in pandas, but maybe shouldn't be here.
# -> If CategoricalType becomes a Composite Pattern decorator, then this
# might be handled automatically.
# -> maybe we don't even need that.  In its current form, nulls are perfectly
# dispatchable even without needing a composite wrapper.


class Categorical(DecoratorType, cache_size=256):
    """Categorical decorator for :class:`ScalarType` objects.

    This decorator keeps track of categorical levels for series objects of the
    wrapped type.
    """

    aliases = {pd.CategoricalDtype, "Category", "category", "categorical"}

    def __class_getitem__(
        cls,
        wrapped: TypeMeta | DecoratorMeta | None = None,
        levels: Iterable[Any] | Empty = EMPTY
    ) -> DecoratorMeta:
        if wrapped is None:
            raise NotImplementedError("TODO")  # should never occur

        if levels is EMPTY:
            slug_repr = levels
            dtype = pd.CategoricalDtype()  # NOTE: auto-detects levels when used
            patch = wrapped
            while not patch.is_default:
                patch = patch.as_default
            dtype._bertrand_wrapped_type = patch  # disambiguates wrapped type
        else:
            levels = wrapped(levels)
            slug_repr = list(levels)
            dtype = pd.CategoricalDtype(levels)

        return cls.flyweight(wrapped, slug_repr, dtype=dtype, levels=levels)




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

        instance = resolve_type(wrapped)
        parsed_levels = None

        # resolve levels
        if levels is not None:
            from pdcast.convert import cast

            match = sequence.match(levels)
            if not match:
                raise TypeError(f"levels must be list-like: {levels}")

            tokens = tokenize(match.group("body"))
            parsed_levels = cast(tokens, instance).to_list()

        return self(instance, levels=parsed_levels)

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
        wrapped = detect_type(categories)
        categories = categories.to_list()

        # if categories are composite, broadcast across non-homogenous array
        if isinstance(wrapped, CompositeType):
            if array is None:  # no index
                return CompositeType(
                    {self(typ, levels=categories) for typ in wrapped}
                )

            # generate an index from the full array
            wrapped = detect_type(array.astype(dtype.categories.dtype))
            index = wrapped._index  # run-length encoded version of .index
            index["value"] = np.array(
                [self(typ, levels=categories) for typ in index["value"]]
            )
            return CompositeType(
                {self(typ, levels=categories) for typ in wrapped},
                index=index
            )

        return self(wrapped=wrapped, levels=categories)

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
        if self.levels is not None and not self.levels == other.levels:
            return False

        # delegate to wrapped
        return self.wrapped.contains(other.wrapped)

    def __repr__(self) -> str:
        # limit the number of displayed levels
        if self.levels and len(self.levels) > 6:
            levels = "["
            levels += ", ".join(repr(level) for level in self.levels[:3])
            levels += ", ..., "
            levels += ", ".join(repr(level) for level in self.levels[-3:])
            levels += "]"
        else:
            levels = self.levels

        # return in same format as VectorType
        return (
            f"{type(self).__name__}(wrapped={self.wrapped}, levels={levels})"
        )
