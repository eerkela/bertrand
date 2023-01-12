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


# TODO: add fixed-length numpy string backend?
# -> These would be a numpy-only feature if implemented


class StringType(AtomicType):
    """String supertype."""

    name = "string"
    aliases = {
        str: {},
        np.str_: {},
        # np.dtype("U") handled in resolve_typespec_dtype() special case
        pd.StringDtype("python"): {"backend": "python"},
        "string": {},
        "str": {},
        "unicode": {},
        "pystr": {"backend": "python"},
        "arrowstr": {"backend": "pyarrow"},
        "str0": {},
        "str_": {},
        "unicode_": {},
        "U": {},
    }
    _backends = ("python", "pyarrow")

    def __init__(self, backend: str = None):
        # "string"
        if backend is None:
            dtype = default_string_dtype

        # "string[python]"
        elif backend == "python":
            dtype = pd.StringDtype("python")

        # "string[pyarrow]"
        elif backend == "pyarrow":
            dtype = pd.StringDtype("pyarrow")

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        # call AtomicType constructor
        super().__init__(
            type_def=str,
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
                except (TypeError, ImportError):
                    continue

        # return as frozenset
        return frozenset(result)


#######################
####    PRIVATE    ####
#######################


cdef object default_string_dtype
cdef bint pyarrow_installed


# if pyarrow >= 1.0.0 is installed, use as default string storage backend
try:
    default_string_dtype = pd.StringDtype("pyarrow")
    pyarrow_installed = True
except ImportError:
    default_string_dtype = pd.StringDtype("python")
    pyarrow_installed = False


# if pyarrow is detected, add additional string alias/backend
if pyarrow_installed:
    # StringType._backends += ("pyarrow",)
    StringType.register_alias(
        pd.StringDtype("pyarrow"),
        defaults={"backend": "pyarrow"}
    )

