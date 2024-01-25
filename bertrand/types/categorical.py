"""This module describes a ``CategoricalType`` object, which can be used to
dynamically wrap other types.
"""
from __future__ import annotations

from typing import Any, Iterable

import pandas as pd

from .base import EMPTY, Decorator, DecoratorMeta, Empty, TypeMeta, UnionMeta, detect
from .base.meta import TOKEN


# TODO: Categorical should be able to accept Unions?
# NOTE: this is enabled in pandas, but maybe shouldn't be here.
# -> If CategoricalType becomes a Composite Pattern decorator, then this
# might be handled automatically.


class Categorical(Decorator, cache_size=256):
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
            raise NotImplementedError("TODO")  # TODO: should never occur

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

    @classmethod
    def from_string(cls, wrapped: str, levels: str | Empty = EMPTY) -> DecoratorMeta:
        """Resolve a categorical specifier in the type specification
        mini-language.
        """
        if levels is EMPTY:
            return cls[wrapped, levels]

        if levels.startswith("[") and levels.endswith("]"):
            stripped = levels[1:-1].strip()
        elif levels.startswith("(") and levels.endswith(")"):
            stripped = levels[1:-1].strip()
        else:
            raise TypeError(f"invalid levels: {repr(levels)}")

        return cls[wrapped, list(s.group() for s in TOKEN.finditer(stripped))]

    @classmethod
    def from_dtype(cls, dtype: DTYPE) -> DecoratorMeta | UnionMeta:
        """Convert a pandas CategoricalDtype into a
        :class:`CategoricalType <pdcast.CategoricalType>` object.
        """
        if dtype.categories is None:
            if hasattr(dtype, "_bertrand_wrapped_type"):
                return dtype._bertrand_wrapped_type
            return cls

        wrapped = detect(dtype.categories)
        if isinstance(wrapped, UnionMeta):
            # TODO: produce a correct index for the original array.
            # -> this can maybe be a special case in detect()
            raise NotImplementedError(
                "Mixed-type categoricals are not currently supported"
            )

        return cls[wrapped, dtype.categories]

    @classmethod
    def transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Convert a series into a categorical representation."""
        # TODO: this should use a @dispatch function to allow for custom behavior
        # -> Ideally, this would just use a generalized overload of cast()

        if cls.dtype.categories is None:
            dtype = pd.CategoricalDtype(cls.wrapped(series.unique()))
        else:
            dtype = cls.dtype
        return series.astype(dtype, copy=False)
