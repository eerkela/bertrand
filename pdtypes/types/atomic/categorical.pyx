from types import MappingProxyType
from typing import Any

import numpy as np
import pandas as pd

from .base cimport AtomicType, AdapterType, ScalarType
from .base import register

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: does it even make sense to have manual categories in resolve?  They
# should only be allowed in detected types.


# when constructing categorical dtypes, the `categories` field should store
# an empty 


@register
class CategoricalType(AdapterType):

    name = "categorical"
    aliases = {"categorical", "Categorical"}

    def __init__(self, atomic_type: AtomicType, levels: list = None):
        if "sparse" in atomic_type.adapters:
            # TODO: insert categorical underneath sparse
            raise NotImplementedError()

        # call AdapterType.__init__()
        super().__init__(atomic_type=atomic_type, levels=levels)

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(
        cls,
        atomic_type: AtomicType,
        levels: list = None
    ) -> str:
        if levels is None:
            return f"{cls.name}[{atomic_type}]"
        return f"{cls.name}[{atomic_type}, {levels}]"

    ##############################
    ####    CUSTOMIZATIONS    ####
    ##############################

    @classmethod
    def resolve(cls, atomic_type: str, levels: str = None):
        cdef ScalarType instance = resolve.resolve_type(atomic_type)
        cdef list parsed = None

        # resolve levels
        if levels is not None:
            match = resolve.brackets.match(levels)
            if not match:
                raise TypeError(f"levels must be list-like: {levels}")
            tokens = resolve.tokenize(match.group("content"))
            parsed = cast.cast(tokens, instance).tolist()

        # if instance is already sparse, just replace levels
        if cls.name in instance.adapters:
            if parsed is None:
                return instance
            else:
                return instance.replace(levels=parsed)

        return cls(atomic_type=instance, levels=parsed)
