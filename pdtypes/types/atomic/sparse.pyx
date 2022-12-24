from typing import Any

import numpy as np
import pandas as pd

from .base import AtomicType, CompositeType

from ..resolve.string import resolve_atomic_type


# TODO: these should be regular .py files


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
        # add sparse-specific fields
        self.atomic_type = atomic_type
        if fill_value is None:
            self.fill_value = self.atomic_type.na_value
        else:
            self.fill_value = fill_value

        # wrap dtype
        dtype = self.atomic_type.dtype
        if dtype is not None:
            dtype = pd.SparseDtype(self.atomic_type.dtype, fill_value)

        # call AtomicType.__init__()
        super(SparseType, self).__init__(
            backend=self.atomic_type.backend,
            object_type=self.atomic_type.object_type,
            dtype=dtype,
            na_value=self.atomic_type.na_value,
            itemsize=self.atomic_type.itemsize,
            slug=self.generate_slug(
                atomic_type=self.atomic_type,
                fill_value=fill_value
            )
        )

        # override AtomicType.is_sparse field
        self.is_sparse = True

    @classmethod
    def generate_slug(
        cls,
        atomic_type: AtomicType,
        fill_value: Any = None
    ) -> str:
        args = [str(atomic_type)]
        if fill_value is not None:
            args.append(str(fill_value))
        return f"{cls.name}[{', '.join(args)}]"

    @classmethod
    def from_typespec(cls, *args: str):
        atomic_types = set()
        for a in args:
            a = resolve_atomic_type(a)
            if isinstance(a, CompositeType):
                atomic_types.update(a.types)
            else:
                atomic_types.add(a)

        if not atomic_types:  # no types
            return cls.instance()
        if len(atomic_types) > 1:  # multiple types
            return CompositeType(cls.instance(a) for a in atomic_types)
        return cls.instance(atomic_types.pop())  # single type

    @classmethod
    def register_supertype(cls, supertype: type) -> None:
        raise TypeError(f"SparseType cannot have supertypes")

    @classmethod
    def register_subtype(cls, subtype: type) -> None:
        raise TypeError(f"SparseType cannot have subtypes")

    @property
    def root(self) -> AtomicType:
        # TODO: these are broken due to the lack of generate_slug()
        if self.atomic_type.supertype is None:
            return self
        return self.instance(self.atomic_type.root, fill_value=self.fill_value)

    @property
    def subtypes(self) -> frozenset:
        # TODO: these are broken due to the lack of generate_slug()
        return frozenset(
            self.instance(t, fill_value=self.fill_value)
            for t in self.atomic_type.subtypes
        )

    @property
    def supertype(self) -> AtomicType:
        # TODO: these are broken due to the lack of generate_slug()
        result = self.atomic_type.supertype
        if result is None:
            return None
        return self.instance(result, fill_value=self.fill_value)

    def __eq__(self, other: AtomicType) -> bool:
        # TODO: account for default fill_value, which is a wildcard
        return isinstance(other, type(self))

    def __getattr__(self, name: str) -> Any:
        return getattr(self.atomic_type, name)

    def __repr__(self) -> str:
        return (
            f"{type(self).__name__}("
            f"{repr(self.atomic_type)}, "
            f"fill_value={self.fill_value}"
            f")"
        )
