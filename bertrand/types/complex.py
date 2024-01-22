"""This module contains all the prepackaged complex types for the ``pdcast``
type system.
"""
import numpy as np

from .base import Type, TypeMeta


class Complex(Type):
    """Abstract complex type."""

    aliases = {"complex", "cfloat", "complex float", "complex floating", "c"}

    @property
    def equiv_float(self) -> TypeMeta:
        """An equivalent floating point type."""
        from .float import Float
        return Float


@Complex.default
class Complex128(Complex):
    """Abstract 128-bit complex type."""

    aliases = {"complex128", "cdouble", "complex double", "complex_", "c16", "D"}
    max = 2**53
    min = -2**53

    @property
    def equiv_float(self) -> TypeMeta:
        """An equivalent floating point type."""
        from .float import Float64
        return Float64


@Complex128.default
class NumpyComplex128(Complex128, backend="numpy"):
    """Numpy 128-bit complex type."""

    aliases = {np.complex128, np.dtype(np.complex128)}
    dtype = np.dtype(np.complex128)
    missing = np.complex128("nan+nanj")

    @property
    def equiv_float(self) -> TypeMeta:
        """An equivalent floating point type."""
        from .float import NumpyFloat64
        return NumpyFloat64


class PythonComplex(Complex128, backend="python"):
    """Python complex type."""

    aliases = {complex}
    scalar = complex
    missing = np.complex128("nan+nanj")

    @property
    def equiv_float(self) -> TypeMeta:
        """An equivalent floating point type."""
        from .float import PythonFloat
        return PythonFloat


class Complex64(Complex):
    """Abstract 64-bit complex type."""

    aliases = {"complex64", "csingle", "complex single", "singlecomplex", "c8", "F"}
    max = 2**24
    min = -2**24

    @property
    def equiv_float(self) -> TypeMeta:
        """An equivalent floating point type."""
        from .float import Float32
        return Float32


@Complex64.default
class NumpyComplex64(Complex64, backend="numpy"):
    """Numpy 64-bit complex type."""

    aliases = {np.complex64, np.dtype(np.complex64)}
    dtype = np.dtype(np.complex64)
    missing = np.complex64("nan+nanj")

    @property
    def equiv_float(self) -> TypeMeta:
        """An equivalent floating point type."""
        from .float import NumpyFloat32
        return NumpyFloat32


#########################################
####    COMPLEX LONG DOUBLE (x86)    ####
#########################################


# TODO: figure out conditional types


# NOTE: long doubles are platform-specific and may not be valid depending on
# hardware configuration.


has_clongdouble: bool = np.dtype(np.clongdouble).itemsize > 16


class Complex160(Complex):
    """Abstract 160-bit float type."""

    aliases = {
        "complex160", "clongdouble", "clongfloat", "complex longdouble",
        "complex longfloat", "complex long double", "complex long float",
        "longcomplex", "long complex", "c20", "G"
    }
    max = 2**64
    min = -2**64

    @property
    def equiv_float(self) -> TypeMeta:
        """An equivalent floating point type."""
        from .float import Float80
        return Float80


@Complex160.default
class NumpyComplex160(Complex160, backend="numpy"):
    """Numpy 160-bit complex type."""

    aliases = {np.clongdouble, np.dtype(np.clongdouble)}
    dtype = np.dtype(np.clongdouble)
    missing = np.clongdouble("nan+nanj")

    @property
    def equiv_float(self) -> TypeMeta:
        """An equivalent floating point type."""
        from .float import NumpyFloat80
        return NumpyFloat80
