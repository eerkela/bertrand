from types import MappingProxyType
from typing import Any

import numpy as np
import pandas as pd

from .base cimport AtomicType, CompositeType

cimport pdtypes.types.resolve as resolve


# TODO: ensure atomic_type is not a SparseType
# -> in Categorical, ensure atomic_type is not a SparseType/CategoricalType


class SparseType(AtomicType, cache_size=64):

    name = "sparse"
    aliases = {
        "sparse": {}
    }

    def __init__(
        self,
        atomic_type: AtomicType,
        fill_value: Any = None
    ):
        self.is_sparse = True
        self.atomic_type = atomic_type
        if fill_value is None:
            self.fill_value = self.atomic_type.na_value
        else:
            self.fill_value = fill_value

        # wrap dtype
        dtype = atomic_type.dtype
        if dtype is not None:
            dtype = pd.SparseDtype(atomic_type.dtype, self.fill_value)

        # call AtomicType.__init__()
        super(SparseType, self).__init__(
            type_def=atomic_type.type_def,
            dtype=dtype,
            na_value=atomic_type.na_value,
            itemsize=atomic_type.itemsize,
            slug=self.slugify(
                atomic_type=atomic_type,
                fill_value=fill_value
            )
        )

    @classmethod
    def slugify(
        cls,
        atomic_type: AtomicType,
        fill_value: Any = None
    ) -> str:
        if fill_value is None:
            fill_value = atomic_type.na_value
        return f"{cls.name}[{str(atomic_type)}, {str(fill_value)}]"

    @classmethod
    def resolve(cls, atomic_type: str = None, fill_value: str = None):
        cdef AtomicType instance = None
        cdef object parsed = None

        if atomic_type is not None:
            instance = resolve.resolve_typespec_string(atomic_type)
        if fill_value is not None:
            parsed = instance.parse(fill_value)

        return cls.instance(atomic_type=instance, fill_value=parsed)

    @classmethod
    def register_supertype(cls, supertype: type) -> None:
        raise TypeError(f"SparseType cannot have supertypes")

    @classmethod
    def register_subtype(cls, subtype: type) -> None:
        raise TypeError(f"SparseType cannot have subtypes")

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({
            "atomic_type": self.atomic_type,
            "fill_value": self.fill_value
        })

    @property
    def root(self) -> AtomicType:
        if self.atomic_type.supertype is None:
            return self
        return self.instance(self.atomic_type.root, fill_value=self.fill_value)

    def _generate_subtypes(self, types: set) -> frozenset:
        # SparseType is an adapter type -> `types` is always an empty set
        return frozenset(
            self.instance(t, fill_value=self.fill_value)
            for t in self.atomic_type.subtypes
        )

    def _generate_supertype(self, type_def: type) -> AtomicType:
        # SparseType is an adapter type -> `type_def` is always None
        result = self.atomic_type.supertype
        if result is None:
            return None
        return self.instance(result, fill_value=self.fill_value)

    def replace(self, **kwargs) -> AtomicType:
        # extract kwargs pertaining to SparseType
        sparse_kwargs = {k: v for k, v in kwargs.items() if k in self.kwargs}
        kwargs = {k: v for k, v in kwargs.items() if k not in self.kwargs}

        # get atomic_type from sparse_kwargs or use default
        if "atomic_type" in sparse_kwargs:
            atomic_type = sparse_kwargs["atomic_type"]
            del sparse_kwargs["atomic_type"]
        else:
            atomic_type = self.atomic_type

        # pass non-sparse kwargs to atomic_type.replace()
        atomic_type = atomic_type.replace(**kwargs)

        # construct new SparseType
        return self.instance(atomic_type=atomic_type, **sparse_kwargs)

    def __getattr__(self, name: str) -> Any:
        return getattr(self.atomic_type, name)
