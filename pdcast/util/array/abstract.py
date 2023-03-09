import numbers
import sys

import numpy as np
import pandas as pd
from pandas.api.extensions import register_extension_dtype
from pandas.core.arrays import ExtensionArray, ExtensionScalarOpsMixin
from pandas.core.dtypes.base import ExtensionDtype


class AbstractDtype(ExtensionDtype):
    pass


class AbstractArray(ExtensionArray, ExtensionScalarOpsMixin):
    __array_priority__ = 1000
        
    def __init__(self, values, dtype=None, copy=False):
        self._data = np.asarray(values, dtype=object)

        # aliases for common attributes to ensure pandas support
        self._items = self.data = self._data

        if not hasattr(self, "_atomic_type"):
            raise NotImplementedError(
                f"AbstractArray must have an associated AtomicType"
            )

    @classmethod
    def _from_sequence(cls, scalars, dtype=None, copy=False):
        return cls(scalars, dtype=dtype, copy=copy)

    @classmethod
    def _from_factorized(cls, values, original):
        return cls(values, dtype=original.dtype)

    def __getitem__(self, item):
        if isinstance(item, numbers.Integral):
            return self._data[item]
        return type(self)(self._data[item])

    def __setitem__(self, key, value):
        item_type = self._atomic_type.type_def

        if pd.api.types.is_list_like(value):
            if pd.api.types.is_scalar(key):
                raise ValueError(
                    "setting an array element with a sequence."
                )
            value = [item_type(v) for v in value]
        else:
            value = item_type(value)
        self._data[key] = value

    def __len__(self) -> int:
        return len(self._data)

    @property
    def nbytes(self):
        n = len(self)
        if n:
            return n * sys.getsizeof(self[0])
        return 0

    @property
    def _dtype(self):
        return self.dtype

    @property
    def dtype(self):
        return self._atomic_type.dtype

    def astype(self, dtype, copy=True):
        if isinstance(dtype, type(self.dtype)):
            return type(self)(self._data)
        return np.asarray(self, dtype=dtype)

    def isna(self):
        # TODO: should maybe have each atomic_type implement its own
        # isna() method.
        return np.array([np.isnan(x) for x in self._data], dtype=bool)

    def take(self, indexer, allow_fill=False, fill_value=None):
        from pandas.api.extensions import take

        data = self._data
        if allow_fill and fill_value is None:
            fill_value = self._atomic_type.na_value

        result = take(
            data,
            indexer,
            fill_value=fill_value,
            allow_fill=allow_fill
        )
        return self._from_sequence(result)

    def copy(self):
        """Return a copy of the array."""
        return type(self)(self._data.copy())

    @classmethod
    def _concat_same_type(cls, to_concat):
        """Concatenate multiple arrays."""
        return cls(np.concatenate([x._data for x in to_concat]))

    def _reduce(self, name, skipna=True, **kwargs):
        if skipna:
            # If we don't have any NAs, we can ignore skipna
            if self.isna().any():
                other = self[~self.isna()]
                return other._reduce(name, **kwargs)

        if name == "sum" and len(self) == 0:
            # GH#29630 avoid returning int 0 or np.bool_(False) on old numpy
            item_type = self._atomic_type.type_def
            return item_type(0)

        try:
            op = getattr(self.data, name)
        except AttributeError:
            raise NotImplementedError(
                f"{str(self._atomic_type)} does not support the {name} "
                f"operation"
            )
        return op(axis=0)


def construct_array_type(
    atomic_type,
    add_arithmetic_ops: bool = True,
    add_comparison_ops: bool = True
) -> ExtensionArray:
    """Create a new ExtensionArray definition to store objects of the given
    type.
    """
    class_doc = (
        f"An abstract data type, automatically generated to store\n "
        f"{str(atomic_type)} objects."
    )

    class StubArrayType(AbstractArray):
        
        def __init__(self, *args, **kwargs):
            self._atomic_type = atomic_type
            super().__init__(*args, **kwargs)

    # add scalar operations from ExtensionScalarOpsMixin
    if add_arithmetic_ops:
        StubArrayType._add_arithmetic_ops()
    if add_comparison_ops:
        StubArrayType._add_comparison_ops()

    # replace docstring
    StubArrayType.__doc__ = class_doc

    return StubArrayType
