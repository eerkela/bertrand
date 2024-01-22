"""This module contains all the prepackaged float types for the ``pdcast``
type system.
"""
from sys import getsizeof

import numpy as np

from .base import Type, TypeMeta


class Float(Type):
    """Abstract float type."""

    aliases = {"float", "floating", "f"}

    @property
    def equiv_complex(self) -> TypeMeta:
        """An equivalent complex type."""
        from .complex import Complex
        return Complex


@Float.default
class Float64(Float):
    """Abstract 64-bit float type."""

    aliases = {"float64", "double", "float_", "f8", "d"}
    max = 2**53
    min = -2**53

    @property
    def equiv_complex(self) -> TypeMeta:
        """An equivalent complex type."""
        from .complex import Complex128
        return Complex128


@Float64.default
class NumpyFloat64(Float64, backend="numpy"):
    """Numpy 64-bit float type."""

    aliases = {np.float64, np.dtype(np.float64)}
    dtype = np.dtype(np.float64)
    missing = np.nan

    @property
    def equiv_complex(self) -> TypeMeta:
        """An equivalent complex type."""
        from .complex import NumpyComplex128
        return NumpyComplex128


class PythonFloat(Float, backend="python"):
    """Python float type."""

    aliases = {float}
    scalar = float
    dtype = np.dtype(object)  # TODO: synthesize dtype
    itemsize = getsizeof(1.0)
    missing = np.nan

    @property
    def equiv_complex(self) -> TypeMeta:
        """An equivalent complex type."""
        from .complex import PythonComplex
        return PythonComplex


class Float32(Float):
    """Abstract 32-bit float type."""

    aliases = {"float32", "single", "f4"}
    max = 2**24
    min = -2**24

    @property
    def equiv_complex(self) -> TypeMeta:
        """An equivalent complex type."""
        from .complex import Complex64
        return Complex64


@Float32.default
class NumpyFloat32(Float32, backend="numpy"):
    """Numpy 32-bit float type."""

    aliases = {np.float32, np.dtype(np.float32)}
    dtype = np.dtype(np.float32)
    missing = np.nan

    @property
    def equiv_complex(self) -> TypeMeta:
        """An equivalent complex type."""
        from .complex import NumpyComplex64
        return NumpyComplex64


class Float16(Float):
    """Abstract 16-bit float type."""

    aliases = {"float16", "half", "f2", "e"}
    max = 2**11
    min = -2**11

    @property
    def equiv_complex(self) -> TypeMeta:
        """An equivalent complex type."""
        from .complex import Complex64
        return Complex64


@Float16.default
class NumpyFloat16(Float16, backend="numpy"):
    """Numpy 16-bit float type."""

    aliases = {np.float16, np.dtype(np.float16)}
    dtype = np.dtype(np.float16)
    missing = np.nan

    @property
    def equiv_complex(self) -> TypeMeta:
        """An equivalent complex type."""
        from .complex import NumpyComplex64
        return NumpyComplex64


#################################
####    LONG DOUBLE (x86)    ####
#################################


# TODO: figure out conditional types?


# NOTE: long doubles are platform-specific and may not be valid depending on
# hardware configuration.


has_longdouble: bool = np.dtype(np.longdouble).itemsize > 8


class Float80(Float):
    """Abstract 80-bit float type."""

    aliases = {
        "float80", "longdouble", "longfloat", "long double", "long float", "f10", "g"
    }

    @property
    def equiv_complex(self) -> TypeMeta:
        """An equivalent complex type."""
        from .complex import Complex160
        return Complex160


@Float80.default
class NumpyFloat80(Float80, backend="numpy"):
    """Numpy 80-bit float type."""

    aliases = {np.longdouble, np.dtype(np.longdouble)}
    dtype = np.dtype(np.longdouble)
    missing = np.nan
    max = 2**64
    min = -2**64

    @property
    def equiv_complex(self) -> TypeMeta:
        """An equivalent complex type."""
        from .complex import NumpyComplex160
        return NumpyComplex160
