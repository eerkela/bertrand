"""This module contains the base class for all bertrand decorator types.

The entire contents of this file are hosted in the online documentation, and serve as
a reference for defining and registering new decorator types with the bertrand type
system.  Users can refer to this file to learn how to build their own decorator types,
and to see how the built-in decorator types are implemented.
"""
from __future__ import annotations

from collections import deque
from typing import Any

import pandas as pd

from .meta import Base, DecoratorMeta


class DecoratorType(Base, metaclass=DecoratorMeta):
    """TODO
    """

    # pylint: disable=missing-param-doc, missing-return-doc, missing-raises-doc

    # TODO: __class_getitem__(), from_scalar(), from_dtype(), from_string()

    @classmethod
    def transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Base implementation of the transform() method, which is inherited by all
        descendant types.
        """
        return series.astype(cls.dtype, copy=False)

    @classmethod
    def inverse_transform(cls, series: pd.Series[Any]) -> pd.Series[Any]:
        """Base implementation of the inverse_transform() method, which is inherited by
        all descendant types.
        """
        return series.astype(cls.wrapped.dtype, copy=False)

    @classmethod
    def replace(cls, *args: Any, **kwargs: Any) -> DecoratorMeta:
        """Copied from TypeMeta."""
        forwarded = dict(cls.params)
        positional = deque(args)
        n  = len(args)
        i = 0
        for k in forwarded:
            if i < n:
                i += 1
                forwarded[k] = positional.popleft()
            elif k in kwargs:
                forwarded[k] = kwargs.pop(k)

        if positional:
            raise TypeError(
                f"replace() takes at most {len(cls.params)} positional arguments but "
                f"{n} were given"
            )
        if kwargs:
            singular = len(kwargs) == 1
            raise TypeError(
                f"replace() got {'an ' if singular else ''}unexpected keyword argument"
                f"{'' if singular else 's'} [{', '.join(repr(k) for k in kwargs)}]"
            )

        return cls.base_type[*forwarded.values()]
