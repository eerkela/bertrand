import decimal
import numbers
import random
import sys

import numpy as np
import pandas as pd
from pandas.api.extensions import register_extension_dtype
from pandas.core.arrays import ExtensionArray, ExtensionScalarOpsMixin
from pandas.core.dtypes.base import ExtensionDtype


# minimal working example: https://github.com/tomharvey/pandas-extension-dtype
# taken from: https://github.com/pandas-dev/pandas/blob/e246c3b05924ac1fe083565a765ce847fcad3d91/pandas/tests/extension/decimal/array.py


@register_extension_dtype
class DecimalDtype(ExtensionDtype):

    type = decimal.Decimal
    name = "decimal[python]"
    na_value = decimal.Decimal("NaN")

    def __repr__(self) -> str:
        return "DecimalDtype()"

    @classmethod
    def construct_array_type(cls):
        """Return the array type associated with this dtype

        Returns
        -------
        type
        """
        return DecimalArray

    @classmethod
    def construct_from_string(cls, string):
        if string == cls.name:
            return cls()
        else:
            raise TypeError(f"Cannot construct a '{cls}' from '{string}'")

    @property
    def context(self) -> decimal.Context:
        """Get the current context for the active thread."""
        return decimal.getcontext()

    @property
    def _is_numeric(self):
        return True


class DecimalArray(ExtensionArray, ExtensionScalarOpsMixin):
    __array_priority__ = 1000

    def __init__(self, values, dtype=None, copy=False):
        for val in values:
            if not isinstance(val, decimal.Decimal):
                raise TypeError(
                    f"All values must be of type {str(decimal.Decimal)}"
                )
        values = np.asarray(values, dtype=object)

        self._data = values

        # Some aliases for common attribute names to ensure pandas supports
        # these
        self._items = self.data = self._data

        # those aliases are currently not working due to assumptions
        # in internal code (GH-20735)
        # self._values = self.values = self.data
        self._dtype = DecimalDtype()

    @property
    def dtype(self):
        return self._dtype

    @classmethod
    def _from_sequence(cls, scalars, dtype=None, copy=False):
        return cls(scalars)

    @classmethod
    def _from_sequence_of_strings(cls, strings, dtype=None, copy=False):
        return cls._from_sequence(
            [decimal.Decimal(x) for x in strings],
            dtype,
            copy
        )

    @classmethod
    def _from_factorized(cls, values, original):
        return cls(values)

    _HANDLED_TYPES = (decimal.Decimal, numbers.Number, np.ndarray)

    def __array_ufunc__(self, ufunc, method, *inputs, **kwargs):
        handled = self._HANDLED_TYPES + (DecimalArray,)
        if not all(isinstance(t, handled) for t in inputs):
            return NotImplemented

        inputs = tuple(
            x._data if isinstance(x, DecimalArray) else x
            for x in inputs
        )
        result = getattr(ufunc, method)(*inputs, **kwargs)

        def reconstruct(x):
            if isinstance(x, (decimal.Decimal, numbers.Number)):
                return x
            else:
                return DecimalArray._from_sequence(x)

        if isinstance(result, tuple):
            return tuple(reconstruct(x) for x in result)
        else:
            return reconstruct(result)

    def __getitem__(self, item):
        if isinstance(item, numbers.Integral):
            return self._data[item]
        else:
            return type(self)(self._data[item])

    def take(self, indexer, allow_fill=False, fill_value=None):
        from pandas.api.extensions import take

        data = self._data
        if allow_fill and fill_value is None:
            fill_value = self.dtype.na_value

        result = take(
            data,
            indexer,
            fill_value=fill_value,
            allow_fill=allow_fill
        )
        return self._from_sequence(result)

    def copy(self):
        return type(self)(self._data.copy())

    def astype(self, dtype, copy=True):
        if isinstance(dtype, type(self.dtype)):
            return type(self)(self._data)
        return np.asarray(self, dtype=dtype)

    def __setitem__(self, key, value):
        if pd.api.types.is_list_like(value):
            if pd.api.types.is_scalar(key):
                raise ValueError("setting an array element with a sequence.")
            value = [decimal.Decimal(v) for v in value]
        else:
            value = decimal.Decimal(value)
        self._data[key] = value

    def __len__(self) -> int:
        return len(self._data)

    @property
    def nbytes(self) -> int:
        n = len(self)
        if n:
            return n * sys.getsizeof(self[0])
        return 0

    def isna(self):
        return np.array([x.is_nan() for x in self._data], dtype=bool)

    @property
    def _na_value(self):
        return decimal.Decimal("NaN")

    @classmethod
    def _concat_same_type(cls, to_concat):
        return cls(np.concatenate([x._data for x in to_concat]))

    def _reduce(self, name, skipna=True, **kwargs):
        if skipna:
            # If we don't have any NAs, we can ignore skipna
            if self.isna().any():
                other = self[~self.isna()]
                return other._reduce(name, **kwargs)

        if name == "sum" and len(self) == 0:
            # GH#29630 avoid returning int 0 or np.bool_(False) on old numpy
            return decimal.Decimal(0)

        try:
            op = getattr(self.data, name)
        except AttributeError:
            raise NotImplementedError(
                "decimal does not support the {} operation".format(name)
            )
        return op(axis=0)


DecimalArray._add_arithmetic_ops()
DecimalArray._add_comparison_ops()
