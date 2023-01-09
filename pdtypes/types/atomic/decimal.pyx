import decimal
from types import MappingProxyType
from typing import Union, Sequence

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


class DecimalType(AtomicType):

    name = "decimal"
    aliases = {
        decimal.Decimal: {"backend": "python"},
        "decimal": {},
    }
    _backends = ("python",)

    def __init__(self, backend: str = None):
        # "decimal"
        if backend is None:
            type_def = decimal.Decimal

        # "decimal[python]"
        elif backend == "python":
            type_def = decimal.Decimal

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        # call AtomicType constructor
        super(DecimalType, self).__init__(
            type_def=type_def,
            dtype=np.dtype(np.object_),
            na_value=pd.NA,
            itemsize=None,
            slug=self.slugify(backend=backend)
        )

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(cls, backend: str = None) -> str:
        slug = cls.name
        if backend is not None:
            slug += f"[{backend}]"
        return slug

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({"backend": self.backend})

    ##############################
    ####    CUSTOMIZATIONS    ####
    ##############################

    def _generate_subtypes(self, types: set) -> frozenset:
        # treat backend=None as wildcard
        kwargs = [self.kwargs]
        if self.backend is None:
            kwargs.extend([
                {**kw, **{"backend": b}}
                for kw in kwargs
                for b in self._backends
            ])

        # build result, skipping invalid kwargs
        result = set()
        for t in types:
            for kw in kwargs:
                try:
                    result.add(t.instance(**kw))
                except TypeError:
                    continue

        # return as frozenset
        return frozenset(result)
