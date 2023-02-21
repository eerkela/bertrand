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


# TODO: if SparseType is called on a SparseType, just replace that type's
# na_value and return as-is.


@register
class SparseType(AdapterType):

    adapter_name = "sparse"
    aliases = {"sparse", "Sparse"}

    def __init__(self, atomic_type: AtomicType, fill_value: Any = None):
        # if atomic_type.is_sparse:
        #     raise TypeError(f"`atomic_type` must not be another SparseType")

        # wrap dtype
        if fill_value is None:
            fill_value = atomic_type.na_value
        self.dtype = pd.SparseDtype(atomic_type.dtype, fill_value)

        # call AdapterType.__init__()
        super().__init__(atomic_type=atomic_type, fill_value=fill_value)

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(
        cls,
        atomic_type: AtomicType,
        fill_value: Any = None
    ) -> str:
        if fill_value is None:
            return f"{cls.adapter_name}[{atomic_type}]"
        return f"{cls.adapter_name}[{atomic_type}, {fill_value}]"

    ##############################
    ####    CUSTOMIZATIONS    ####
    ##############################

    @classmethod
    def resolve(cls, atomic_type: str, fill_value: str = None):
        cdef ScalarType instance = resolve.resolve_type(atomic_type)
        cdef object parsed = None

        # resolve fill_value
        if fill_value is not None:
            if fill_value in resolve.na_strings:
                parsed = resolve.na_strings[fill_value]
            else:
                parsed = cast.cast(fill_value, instance)[0]

        # if instance is already sparse, just replace fill_value
        if cls.adapter_name in instance.adapters:
            if parsed is None:
                return instance
            else:
                return instance.replace(fill_value=parsed)

        return cls(atomic_type=instance, fill_value=parsed)
