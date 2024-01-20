"""Mypy stubs for pdcast/types/float.pyx"""
from pdcast.types import ScalarType, AbstractType

class FloatType(AbstractType):
    equiv_complex: ScalarType

class NumpyFloatType(AbstractType):
    equiv_complex: ScalarType

class Float16Type(AbstractType):
    equiv_complex: ScalarType

class NumpyFloat16Type(ScalarType):
    equiv_complex: ScalarType

class Float32Type(AbstractType):
    equiv_complex: ScalarType

class NumpyFloat32Type(ScalarType):
    equiv_complex: ScalarType

class Float64Type(AbstractType):
    equiv_complex: ScalarType

class NumpyFloat64Type(ScalarType):
    equiv_complex: ScalarType

class PythonFloat64Type(ScalarType):
    equiv_complex: ScalarType

class Float80Type(AbstractType):
    equiv_complex: ScalarType

class NumpyFloat80Type(ScalarType):
    equiv_complex: ScalarType
