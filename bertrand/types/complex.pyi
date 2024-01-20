"""Mypy stubs for pdcast/types/complex.pyx"""
from pdcast.types import ScalarType, AbstractType

class ComplexType(AbstractType):
    equiv_float: ScalarType

class NumpyComplexType(AbstractType):
    equiv_float: ScalarType

class Complex64Type(AbstractType):
    equiv_float: ScalarType

class NumpyComplex64Type(ScalarType):
    equiv_float: ScalarType

class Complex128Type(AbstractType):
    equiv_float: ScalarType

class NumpyComplex128Type(ScalarType):
    equiv_float: ScalarType

class PythonComplex128Type(ScalarType):
    equiv_float: ScalarType

class Complex160Type(AbstractType):
    equiv_float: ScalarType

class NumpyComplex160Type(ScalarType):
    equiv_float: ScalarType
