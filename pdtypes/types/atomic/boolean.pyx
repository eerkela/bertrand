from types import MappingProxyType

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType, na_strings

import pdtypes.types.resolve as resolve


class BooleanType(AtomicType):
    """Boolean supertype."""

    name = "bool"
    aliases = {
        # type
        bool: {},
        np.bool_: {"backend": "numpy"},

        # dtype
        np.dtype(np.bool_): {"backend": "numpy"},
        pd.BooleanDtype(): {"backend": "pandas"},

        # string
        "bool": {},
        "boolean": {},
        "bool_": {},
        "bool8": {},
        "b1": {},
        "?": {},
        "Boolean": {"backend": "pandas"},
    }
    is_sparse = False
    is_categorical = False
    _backends = ("python", "numpy", "pandas")

    def __init__(self, backend: str = None):
        # "bool"
        if backend is None:
            type_def = None
            dtype = None

        # "bool[python]"
        elif backend == "python":
            type_def = bool
            dtype = np.dtype("O")

        # "bool[numpy]"
        elif backend == "numpy":
            type_def = np.bool_
            dtype = np.dtype(bool)

        # "bool[pandas]"
        elif backend == "pandas":
            type_def = np.bool_
            dtype = pd.BooleanDtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        # call AtomicType constructor
        super(BooleanType, self).__init__(
            type_def=type_def,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=1,
            slug=self.slugify(backend=backend)
        )

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(cls, backend: str = None) -> str:
        slug = f"{cls.name}"
        if backend is not None:
            slug = f"{slug}[{backend}]"
        return slug

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({
            "backend": self.backend
        })

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

    def parse(self, input_str: str):
        lower = input_str.lower()
        if lower in na_strings:
            return na_strings[lower]
        if lower not in ("true", "false"):
            raise TypeError(
                f"could not interpret boolean string: {input_str}"
            )
        return self.type_def(lower == "true")
