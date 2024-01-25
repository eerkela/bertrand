"""This module describes a ``SparseType`` object, which can be used to
dynamically wrap other types.
"""
from __future__ import annotations
from typing import Any

import pandas as pd

from .base import EMPTY, REGISTRY, Decorator, DecoratorMeta, Empty, TypeMeta, resolve


# TODO: SparseType works, but not in all cases.
# -> pd.NA disallows non-missing fill values
# -> Timestamps must be sparsified manually by converting to object and then
# to sparse
# -> Timedeltas just don't work at all.  astype() rejects pd.SparseDtype("m8")
# entirely.


# TODO:
# >>> Sparse["long[numpy] | long[pandas]"]
# Traceback (most recent call last):
#   File "<stdin>", line 1, in <module>
#   File "/home/eerkela/data/bertrand/bertrand/types/base/meta.py", line 4698, in __getitem__
#     return cls.__class_getitem__(params)
#            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#   File "/home/eerkela/data/bertrand/bertrand/types/base/meta.py", line 7118, in wrapper
#     return Union.from_types(LinkedSet(
#                             ^^^^^^^^^^
# TypeError: SparseDtype subtype must be a numpy dtype


class Sparse(Decorator, cache_size=256):
    """TODO"""

    aliases = {pd.SparseDtype, "sparse"}
    _is_empty: bool = False


    def __class_getitem__(
        cls,
        wrapped: TypeMeta | DecoratorMeta | None = None,
        fill_value: Any | Empty = EMPTY
    ) -> DecoratorMeta:
        if wrapped is None:
            raise NotImplementedError("TODO")  # TODO: should never occur

        is_empty = fill_value is EMPTY

        if isinstance(wrapped.unwrapped, DecoratorMeta):
            return cls.flyweight(wrapped, fill_value, dtype=None, _is_empty=is_empty)

        if is_empty:
            fill = wrapped.missing
        elif pd.isna(fill_value):  # type: ignore
            fill = fill_value
        else:
            fill = wrapped(fill_value)
            if len(fill) != 1:
                raise TypeError(f"fill_value must be a scalar, not {repr(fill_value)}")
            fill = fill[0]

        dtype = pd.SparseDtype(wrapped.dtype, fill)
        return cls.flyweight(wrapped, fill, dtype=dtype, _is_empty=is_empty)

    @classmethod
    def from_string(cls, wrapped: str, fill_value: str | Empty = EMPTY) -> DecoratorMeta:
        """Resolve a sparse specifier in the type specification mini-langauge.
        """
        return cls[wrapped, REGISTRY.na_strings.get(fill_value, fill_value)]  # type: ignore

    @classmethod
    def from_dtype(cls, dtype: pd.SparseDtype) -> DecoratorMeta:
        """Convert a pandas SparseDtype into a
        :class:`SparseType <pdcast.SparseType>` object.
        """
        return cls[resolve(dtype.subtype), dtype.fill_value]  # type: ignore

    @classmethod
    def replace(cls, *args: Any, **kwargs: Any) -> DecoratorMeta:
        """TODO"""
        if cls._is_empty and len(args) < 2 and "fill_value" not in kwargs:
            kwargs["fill_value"] = EMPTY
        return super().replace(*args, **kwargs)

    @classmethod
    def inverse_transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Convert a sparse series into a dense format"""
        return series.sparse.to_dense()
