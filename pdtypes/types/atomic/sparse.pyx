from types import MappingProxyType
from typing import Any

import numpy as np
import pandas as pd

from .base cimport AtomicType, AdapterType

cimport pdtypes.types.resolve as resolve


cdef class Null:
    pass


# a null value other than None (allows fill_value to match literal Nones)
null = Null()


class SparseType(AdapterType, cache_size=64):

    name = "sparse"
    aliases = {
        "sparse": {}
    }
    is_sparse = True

    def __init__(
        self,
        atomic_type: AtomicType,
        fill_value: Any = null
    ):
        if atomic_type.is_sparse:
            raise TypeError(f"`atomic_type` must not be another SparseType")

        if fill_value is null:
            self.fill_value = atomic_type.na_value
        else:
            self.fill_value = fill_value

        # wrap dtype
        dtype = atomic_type.dtype
        if dtype is not None:
            dtype = pd.SparseDtype(dtype, self.fill_value)

        # call AtomicType.__init__()
        super(SparseType, self).__init__(
            atomic_type=atomic_type,
            type_def=atomic_type.type_def,
            dtype=dtype,
            na_value=atomic_type.na_value,
            itemsize=atomic_type.itemsize,
            slug=self.slugify(
                atomic_type=atomic_type,
                fill_value=fill_value
            )
        )

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(
        cls,
        atomic_type: AtomicType,
        fill_value: Any = null
    ) -> str:
        if fill_value is null:
            fill_value = atomic_type.na_value
        return f"{cls.name}[{str(atomic_type)}, {str(fill_value)}]"

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({
            "atomic_type": self.atomic_type,
            "fill_value": self.fill_value
        })

    ##############################
    ####    CUSTOMIZATIONS    ####
    ##############################

    @classmethod
    def resolve(cls, atomic_type: str = None, fill_value: str = None):
        cdef AtomicType instance = None
        cdef object parsed = null

        if atomic_type is not None:
            instance = resolve.resolve_typespec_string(atomic_type)
        if fill_value is not None:
            parsed = instance.parse(fill_value)

        return cls.instance(atomic_type=instance, fill_value=parsed)
